using System.Net;
using System.Text;
using System.Text.Json;
using System.Collections.Concurrent;

namespace EdgeView.Core;

/// <summary>
/// Local HTTP + Server-Sent-Events bridge that lets the EDGE-VIEW web app
/// (served from GitHub Pages over HTTPS) drive this machine's link to the
/// device. Browsers treat http://127.0.0.1 as a trustworthy origin, so the
/// HTTPS page is allowed to call it.
///
/// Security: only allow-listed origins may call, and every mutating request
/// must carry the pairing token shown in the Studio UI. Without that, any
/// web page you visited could reflash your hardware.
/// </summary>
public static class BridgeServer
{
    public const int Port = 8787;
    public const string Product = "IO-nity EDGE-VIEW Bridge";
    public const string ApiVersion = "1";

    private static HttpListener? _listener;
    private static CancellationTokenSource? _cts;
    private static readonly ConcurrentDictionary<Guid, StreamWriter> Clients = new();
    private static readonly ConcurrentQueue<string> ChatLog = new();
    private static readonly ConcurrentDictionary<string, string> Devices = new();

    public static event Action<string>? Log;
    public static bool IsRunning => _listener?.IsListening == true;
    public static int ClientCount => Clients.Count;

    /// <summary>Pairing token the web app must present. Regenerated on demand.</summary>
    public static string Token
    {
        get
        {
            var t = Settings.Get("bridge_token", "");
            if (string.IsNullOrEmpty(t)) { t = NewToken(); Settings.Set("bridge_token", t); }
            return t;
        }
    }

    public static string NewToken()
    {
        var t = Convert.ToHexString(Guid.NewGuid().ToByteArray())[..12].ToLowerInvariant();
        Settings.Set("bridge_token", t);
        return t;
    }

    /// <summary>Origins permitted to talk to the bridge. Empty entry = allow any.</summary>
    private static string[] AllowedOrigins => Settings
        .Get("bridge_origins",
             "https://ionity-global.github.io," +
             "https://antwerpdesignsionity.github.io," +
             "http://localhost:8080,http://127.0.0.1:8080,null")
        .Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);

    private static void Report(string m) => Log?.Invoke($"[{DateTime.Now:HH:mm:ss}] {m}");

    public static void Start()
    {
        if (IsRunning) return;
        _listener = new HttpListener();
        _listener.Prefixes.Add($"http://127.0.0.1:{Port}/");
        try
        {
            _listener.Start();
        }
        catch (HttpListenerException ex)
        {
            Report($"Bridge could not bind port {Port}: {ex.Message}");
            _listener = null;
            return;
        }

        _cts = new CancellationTokenSource();
        PicoLink.Traffic += OnTraffic;
        PicoLink.DeviceDiscovered += OnDiscovered;
        FeedService.Log += OnFeedLog;
        _ = AcceptLoopAsync(_cts.Token);
        _ = HeartbeatAsync(_cts.Token);
        Report($"Bridge listening on http://127.0.0.1:{Port} (token {Token})");
    }

    public static void Stop()
    {
        PicoLink.Traffic -= OnTraffic;
        PicoLink.DeviceDiscovered -= OnDiscovered;
        FeedService.Log -= OnFeedLog;
        _cts?.Cancel();
        _cts = null;
        try { _listener?.Stop(); } catch { }
        _listener = null;
        foreach (var c in Clients.Values) { try { c.Dispose(); } catch { } }
        Clients.Clear();
        Report("Bridge stopped");
    }

    private static void OnTraffic(string dir, string payload) => Broadcast("stream", new { dir, payload });
    private static void OnFeedLog(string line) => Broadcast("log", new { line });

    private static void OnDiscovered(string name, string ip)
    {
        Devices[ip] = name;
        Broadcast("device", new { name, ip });
    }

    // ---- SSE fan-out ----

    private static void Broadcast(string ev, object payload)
    {
        var data = JsonSerializer.Serialize(payload);
        var frame = $"event: {ev}\ndata: {data}\n\n";
        foreach (var (id, w) in Clients)
        {
            try { w.Write(frame); w.Flush(); }
            catch { Clients.TryRemove(id, out _); try { w.Dispose(); } catch { } }
        }
    }

    private static async Task HeartbeatAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            try { await Task.Delay(TimeSpan.FromSeconds(10), ct); }
            catch (OperationCanceledException) { break; }
            Broadcast("status", new
            {
                device = PicoLink.DeviceIp,
                feeds = FeedService.IsRunning,
                clients = Clients.Count,
                time = DateTimeOffset.UtcNow.ToUnixTimeSeconds(),
            });
        }
    }

    // ---- Request handling ----

    private static async Task AcceptLoopAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested && _listener != null)
        {
            HttpListenerContext ctx;
            try { ctx = await _listener.GetContextAsync(); }
            catch { break; }
            _ = Task.Run(() => HandleAsync(ctx), ct);
        }
    }

    private static bool OriginAllowed(string? origin)
    {
        if (string.IsNullOrEmpty(origin)) return true; // curl / same-process
        var allow = AllowedOrigins;
        return allow.Contains("*") || allow.Any(a => string.Equals(a, origin, StringComparison.OrdinalIgnoreCase));
    }

    private static void ApplyCors(HttpListenerContext ctx)
    {
        var origin = ctx.Request.Headers["Origin"];
        ctx.Response.AddHeader("Access-Control-Allow-Origin", string.IsNullOrEmpty(origin) ? "*" : origin);
        ctx.Response.AddHeader("Vary", "Origin");
        ctx.Response.AddHeader("Access-Control-Allow-Headers", "Content-Type, X-Edgeview-Token");
        ctx.Response.AddHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        ctx.Response.AddHeader("Access-Control-Max-Age", "600");
    }

    private static async Task HandleAsync(HttpListenerContext ctx)
    {
        var req = ctx.Request;
        var res = ctx.Response;
        var path = req.Url?.AbsolutePath ?? "/";

        try
        {
            ApplyCors(ctx);

            if (req.HttpMethod == "OPTIONS") { res.StatusCode = 204; res.Close(); return; }

            if (!OriginAllowed(req.Headers["Origin"]))
            {
                await WriteJsonAsync(res, 403, new { error = "origin not allowed", origin = req.Headers["Origin"] });
                return;
            }

            // /api/hello is public so the web app can detect the bridge without a token.
            if (path == "/api/hello")
            {
                await WriteJsonAsync(res, 200, new
                {
                    product = Product,
                    api = ApiVersion,
                    version = EdgeViewInfo.Version,
                    device = PicoLink.DeviceIp,
                    feeds = FeedService.IsRunning,
                    clients = Clients.Count,
                    devices = Devices.Select(d => new { ip = d.Key, name = d.Value }),
                    hosted = WebAssets.Any,
                    needsToken = true,
                });
                return;
            }

            // Anything outside /api/ is the console itself, served from the exe.
            if (!path.StartsWith("/api/", StringComparison.Ordinal))
            {
                var asset = WebAssets.Get(path);
                if (asset is { } a)
                {
                    res.StatusCode = 200;
                    res.ContentType = a.ContentType;
                    res.ContentLength64 = a.Bytes.Length;
                    await res.OutputStream.WriteAsync(a.Bytes);
                    res.Close();
                    return;
                }
                await WriteJsonAsync(res, 404, new { error = "not found", path });
                return;
            }

            var token = req.Headers["X-Edgeview-Token"] ?? req.QueryString["token"] ?? "";
            if (!CryptoEquals(token, Token))
            {
                await WriteJsonAsync(res, 401, new { error = "bad or missing pairing token" });
                return;
            }

            switch (path)
            {
                case "/api/events": await ServeEventsAsync(ctx); return;

                case "/api/mirror": await ServeMirrorAsync(ctx); return;

                case "/api/state":
                    await WriteJsonAsync(res, 200, new
                    {
                        device = PicoLink.DeviceIp,
                        feeds = FeedService.IsRunning,
                        chat = ChatLog.ToArray(),
                        devices = Devices.Select(d => new { ip = d.Key, name = d.Value }),
                        slots = PicoLink.Slots,
                        panels = PicoLink.Panels,
                        ai = AiSnapshot(),
                    });
                    return;

                case "/api/ai":
                {
                    if (req.HttpMethod == "GET") { await WriteJsonAsync(res, 200, AiSnapshot()); return; }
                    var b = await JsonDocument.ParseAsync(req.InputStream);
                    var root = b.RootElement;
                    if (root.TryGetProperty("autopilot", out var ap)) AiEngine.Autopilot = ap.GetBoolean();
                    if (root.TryGetProperty("sentinel", out var se)) AiEngine.Sentinel = se.GetBoolean();
                    if (root.TryGetProperty("curator", out var cu)) AiEngine.Curator = cu.GetBoolean();
                    if (root.TryGetProperty("gist", out var gi)) AiEngine.GistSource = gi.GetString() ?? "";
                    if (root.TryGetProperty("gistAuto", out var ga)) AiEngine.GistAuto = ga.GetBoolean();
                    if (root.TryGetProperty("running", out var ru))
                    {
                        if (ru.GetBoolean()) AiEngine.Start(); else AiEngine.Stop();
                    }
                    await WriteJsonAsync(res, 200, AiSnapshot());
                    return;
                }

                case "/api/ai/say":
                {
                    var b = await JsonDocument.ParseAsync(req.InputStream);
                    var (say, applied) = await AiEngine.SayAsync(b.RootElement.GetProperty("text").GetString() ?? "");
                    Broadcast("ai", new { say, applied });
                    await WriteJsonAsync(res, 200, new { ok = true, say, applied });
                    return;
                }

                case "/api/ai/scene":
                {
                    var b = await JsonDocument.ParseAsync(req.InputStream);
                    var id = b.RootElement.GetProperty("scene").GetString() ?? "";
                    var ok = await AiEngine.ApplySceneAsync(id);
                    await WriteJsonAsync(res, 200, new { ok, scene = id });
                    return;
                }

                case "/api/ai/inbound":
                {
                    try
                    {
                        var actions = await AiEngine.PullInboundAsync(AiEngine.GistSource);
                        var applied = await AiEngine.ApplyAsync(actions);
                        Broadcast("ai", new { say = $"Inbound: {actions.Count} directive(s)", applied });
                        await WriteJsonAsync(res, 200, new { ok = true, directives = actions.Count, applied });
                    }
                    catch (Exception ex)
                    {
                        await WriteJsonAsync(res, 200, new { ok = false, error = ex.Message });
                    }
                    return;
                }

                case "/api/brain":
                {
                    if (req.HttpMethod == "GET")
                    {
                        await LocalBrain.ProbeAsync(force: true);
                        await WriteJsonAsync(res, 200, ChatService.Snapshot());
                        return;
                    }
                    var b = await JsonDocument.ParseAsync(req.InputStream);
                    if (b.RootElement.TryGetProperty("model", out var mo))
                        LocalBrain.ModelOverride = mo.GetString() ?? "";
                    if (b.RootElement.TryGetProperty("clear", out var cl) && cl.ValueKind == JsonValueKind.True)
                        ChatService.Clear();
                    await LocalBrain.ProbeAsync(force: true);
                    await WriteJsonAsync(res, 200, ChatService.Snapshot());
                    return;
                }

                case "/api/brain/say":
                {
                    var b = await JsonDocument.ParseAsync(req.InputStream);
                    var who = b.RootElement.TryGetProperty("who", out var w) ? w.GetString() ?? "web" : "web";
                    var msg = b.RootElement.GetProperty("text").GetString() ?? "";

                    // Stream the answer to every console as it is generated.
                    var reply = await ChatService.SayAsync(who, msg,
                        piece => { Broadcast("token", new { t = piece }); return Task.CompletedTask; });

                    Broadcast("token", new { t = "", done = true });
                    await WriteJsonAsync(res, 200, new { ok = true, reply });
                    return;
                }

                case "/api/browse":
                {
                    var b = await JsonDocument.ParseAsync(req.InputStream);
                    try
                    {
                        var page = await WebReader.StreamToDeviceAsync(b.RootElement.GetProperty("url").GetString() ?? "");
                        await PicoLink.SetLayoutAsync("stage", "text");
                        await WriteJsonAsync(res, 200, new { ok = true, title = page.Title, headlines = page.Headlines });
                    }
                    catch (Exception ex)
                    {
                        await WriteJsonAsync(res, 200, new { ok = false, error = ex.Message });
                    }
                    return;
                }

                case "/api/command":
                {
                    var body = await ReadBodyAsync(req);
                    using var doc = JsonDocument.Parse(body);
                    var json = doc.RootElement.GetProperty("json").GetString() ?? "";
                    var reply = await PicoLink.SendAndReadAsync(json);
                    await WriteJsonAsync(res, 200, new { ok = reply != null, reply });
                    return;
                }

                case "/api/data":
                {
                    var b = await JsonDocument.ParseAsync(req.InputStream);
                    var ok = await PicoLink.SetDataAsync(
                        b.RootElement.GetProperty("key").GetString() ?? "",
                        b.RootElement.TryGetProperty("value", out var v) ? v.GetString() ?? "" : "");
                    await WriteJsonAsync(res, 200, new { ok });
                    return;
                }

                case "/api/layout":
                {
                    var b = await JsonDocument.ParseAsync(req.InputStream);
                    var ok = await PicoLink.SetLayoutAsync(
                        b.RootElement.GetProperty("slot").GetString() ?? "",
                        b.RootElement.GetProperty("panel").GetString() ?? "");
                    await WriteJsonAsync(res, 200, new { ok });
                    return;
                }

                case "/api/wifi":
                {
                    var b = await JsonDocument.ParseAsync(req.InputStream);
                    if (b.RootElement.TryGetProperty("reset", out var r) && r.ValueKind == JsonValueKind.True)
                    {
                        var cleared = await PicoLink.ResetWifiAsync();
                        Report(cleared ? "WiFi credentials cleared; device rebooting" : "WiFi reset failed");
                        await WriteJsonAsync(res, 200, new { ok = cleared, reset = true });
                        return;
                    }
                    var ok = await PicoLink.SetWifiAsync(
                        b.RootElement.GetProperty("ssid").GetString() ?? "",
                        b.RootElement.TryGetProperty("pass", out var p) ? p.GetString() ?? "" : "");
                    Report(ok ? "Remote WiFi credentials sent; device rebooting" : "Remote WiFi push failed");
                    await WriteJsonAsync(res, 200, new { ok });
                    return;
                }

                case "/api/reboot":
                {
                    var ok = await PicoLink.RebootAsync();
                    Broadcast("log", new { line = ok ? "Reboot sent" : "Reboot failed" });
                    await WriteJsonAsync(res, 200, new { ok });
                    return;
                }

                case "/api/reload":
                {
                    // Soft reload: re-push everything the panels read, no reboot.
                    await FeedService.PushHeartbeatAsync();
                    await WriteJsonAsync(res, 200, new { ok = true });
                    return;
                }

                case "/api/mode":
                {
                    var b = await JsonDocument.ParseAsync(req.InputStream);
                    var ok = await PicoLink.SetGameModeAsync(b.RootElement.GetProperty("name").GetString() ?? "pacman");
                    await WriteJsonAsync(res, 200, new { ok });
                    return;
                }

                case "/api/device":
                {
                    var b = await JsonDocument.ParseAsync(req.InputStream);
                    PicoLink.DeviceIp = b.RootElement.GetProperty("ip").GetString() ?? PicoLink.DeviceIp;
                    await WriteJsonAsync(res, 200, new { ok = true, device = PicoLink.DeviceIp });
                    return;
                }

                case "/api/feeds":
                {
                    var b = await JsonDocument.ParseAsync(req.InputStream);
                    var on = b.RootElement.GetProperty("on").GetBoolean();
                    if (on) FeedService.Start(); else FeedService.Stop();
                    await WriteJsonAsync(res, 200, new { ok = true, feeds = FeedService.IsRunning });
                    return;
                }

                case "/api/chat":
                {
                    if (req.HttpMethod == "GET")
                    {
                        var json = await PicoLink.GetMessagesJsonAsync();
                        await WriteJsonAsync(res, 200, new { ok = json != null, board = json, local = ChatLog.ToArray() });
                        return;
                    }
                    var b = await JsonDocument.ParseAsync(req.InputStream);
                    var author = b.RootElement.TryGetProperty("author", out var a) ? a.GetString() ?? "web" : "web";
                    var text = b.RootElement.GetProperty("text").GetString() ?? "";
                    var ok = await PicoLink.PostMessageAsync(author, text);
                    PushChat(author, text);
                    await WriteJsonAsync(res, 200, new { ok });
                    return;
                }

                default:
                    await WriteJsonAsync(res, 404, new { error = "no such endpoint", path });
                    return;
            }
        }
        catch (Exception ex)
        {
            try { await WriteJsonAsync(res, 500, new { error = ex.Message }); } catch { }
        }
    }

    /// <summary>Adds a line to the scrolling chat crawl and pushes it to every viewer.</summary>
    public static void PushChat(string author, string text)
    {
        var line = $"{author}: {text}";
        ChatLog.Enqueue(line);
        while (ChatLog.Count > 60) ChatLog.TryDequeue(out _);
        Broadcast("chat", new { author, text, at = DateTimeOffset.UtcNow.ToUnixTimeSeconds() });
    }

    private static object AiSnapshot()
    {
        var h = AiEngine.Assess();
        return new
        {
            running = AiEngine.IsRunning,
            autopilot = AiEngine.Autopilot,
            sentinel = AiEngine.Sentinel,
            curator = AiEngine.Curator,
            gist = AiEngine.GistSource,
            gistAuto = AiEngine.GistAuto,
            health = h.Score,
            label = h.Label,
            advice = h.Advice,
            scenes = AiEngine.Scenes.Select(s => new { id = s.Key, label = s.Value.Label, why = s.Value.Why }),
        };
    }

    /// <summary>
    /// One SSE stream per viewer carrying live frames off the glass. The relay
    /// only talks to the device while at least one of these is open.
    /// </summary>
    private static async Task ServeMirrorAsync(HttpListenerContext ctx)
    {
        var res = ctx.Response;
        res.StatusCode = 200;
        res.ContentType = "text/event-stream";
        res.AddHeader("Cache-Control", "no-cache");
        res.SendChunked = true;

        var writer = new StreamWriter(res.OutputStream, new UTF8Encoding(false)) { AutoFlush = false };
        var queue = new System.Collections.Concurrent.ConcurrentQueue<MirrorRelay.Frame>();
        var signal = new SemaphoreSlim(0);

        void OnFrame(MirrorRelay.Frame f)
        {
            // Never let a slow browser build a backlog of stale frames.
            if (queue.Count > 2) queue.TryDequeue(out _);
            queue.Enqueue(f);
            try { signal.Release(); } catch { }
        }

        MirrorRelay.FrameReceived += OnFrame;
        MirrorRelay.AddViewer();
        Report("Mirror viewer attached");

        try
        {
            await writer.WriteAsync("retry: 2000\n\n");
            await writer.FlushAsync();

            while (IsRunning)
            {
                if (!await signal.WaitAsync(TimeSpan.FromSeconds(10)))
                {
                    await writer.WriteAsync(": waiting\n\n");
                    await writer.FlushAsync();
                    continue;
                }
                while (queue.TryDequeue(out var f))
                {
                    var data = JsonSerializer.Serialize(new
                    {
                        w = f.Width,
                        h = f.Height,
                        fmt = f.Format,
                        b = Convert.ToBase64String(f.Payload),
                    });
                    await writer.WriteAsync($"event: frame\ndata: {data}\n\n");
                }
                await writer.FlushAsync();
            }
        }
        catch { /* viewer went away */ }
        finally
        {
            MirrorRelay.FrameReceived -= OnFrame;
            MirrorRelay.RemoveViewer();
            try { writer.Dispose(); } catch { }
            try { res.Close(); } catch { }
            Report("Mirror viewer left");
        }
    }

    private static async Task ServeEventsAsync(HttpListenerContext ctx)
    {        var res = ctx.Response;
        res.StatusCode = 200;
        res.ContentType = "text/event-stream";
        res.AddHeader("Cache-Control", "no-cache");
        res.SendChunked = true;

        var id = Guid.NewGuid();
        var writer = new StreamWriter(res.OutputStream, new UTF8Encoding(false)) { AutoFlush = false };
        Clients[id] = writer;
        Report($"Web client attached ({Clients.Count} total)");

        try
        {
            await writer.WriteAsync("retry: 3000\n\n");
            await writer.FlushAsync();
            var hello = JsonSerializer.Serialize(new { product = Product, device = PicoLink.DeviceIp });
            await writer.WriteAsync($"event: hello\ndata: {hello}\n\n");
            await writer.FlushAsync();
            foreach (var line in ChatLog.ToArray())
            {
                var d = JsonSerializer.Serialize(new { author = "history", text = line, at = 0 });
                await writer.WriteAsync($"event: chat\ndata: {d}\n\n");
            }
            await writer.FlushAsync();

            // Hold the socket open; Broadcast writes into it from other threads.
            while (Clients.ContainsKey(id) && IsRunning)
            {
                await Task.Delay(15000);
                try { await writer.WriteAsync(": keepalive\n\n"); await writer.FlushAsync(); }
                catch { break; }
            }
        }
        catch { }
        finally
        {
            Clients.TryRemove(id, out _);
            try { writer.Dispose(); } catch { }
            try { res.Close(); } catch { }
            Report($"Web client left ({Clients.Count} remaining)");
        }
    }

    // ---- helpers ----

    private static async Task<string> ReadBodyAsync(HttpListenerRequest req)
    {
        using var r = new StreamReader(req.InputStream, req.ContentEncoding);
        return await r.ReadToEndAsync();
    }

    private static async Task WriteJsonAsync(HttpListenerResponse res, int status, object payload)
    {
        var bytes = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(payload));
        res.StatusCode = status;
        res.ContentType = "application/json";
        res.ContentLength64 = bytes.Length;
        await res.OutputStream.WriteAsync(bytes);
        res.Close();
    }

    /// <summary>Length-independent compare so the token cannot be probed by timing.</summary>
    private static bool CryptoEquals(string a, string b)
    {
        if (a.Length != b.Length) return false;
        int diff = 0;
        for (int i = 0; i < a.Length; i++) diff |= a[i] ^ b[i];
        return diff == 0;
    }
}
