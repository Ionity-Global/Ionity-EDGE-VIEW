using System.Text;
using System.Text.Json;

namespace EdgeView.Core;

/// <summary>
/// Chat that runs on your own machine. If a local model server is listening
/// (Ollama, LM Studio, llama.cpp, Jan — anything OpenAI-compatible) the brain
/// uses it; otherwise it answers from the built-in rules. Nothing is ever sent
/// to a third party, and with no model running the cost is zero.
/// </summary>
public static class LocalBrain
{
    /// <summary>Endpoints probed, in order. All are the usual local defaults.</summary>
    private static readonly (string Name, string Base)[] Candidates =
    {
        ("Ollama",     "http://127.0.0.1:11434"),
        ("LM Studio",  "http://127.0.0.1:1234"),
        ("llama.cpp",  "http://127.0.0.1:8080"),
        ("Jan",        "http://127.0.0.1:1337"),
    };

    /// <summary>Small models first — this has to sit alongside everything else.</summary>
    private static readonly string[] PreferredModels =
    {
        "qwen2.5:1.5b", "llama3.2:1b", "llama3.2:3b", "qwen2.5:3b",
        "phi3.5", "gemma2:2b", "tinyllama",
    };

    private static readonly HttpClient Http = new() { Timeout = TimeSpan.FromMinutes(2) };

    public static string? Endpoint { get; private set; }
    public static string? Backend { get; private set; }
    public static string? Model { get; private set; }
    public static bool Available => Endpoint != null;
    public static DateTime LastProbe { get; private set; } = DateTime.MinValue;

    public static event Action<string>? Log;
    private static void Report(string m) => Log?.Invoke(m);

    public static string ModelOverride
    {
        get => Settings.Get("brain_model", "");
        set => Settings.Set("brain_model", value);
    }

    /// <summary>Look for a local model server. Cheap, and cached for a minute.</summary>
    public static async Task<bool> ProbeAsync(bool force = false)
    {
        if (!force && DateTime.UtcNow - LastProbe < TimeSpan.FromMinutes(1)) return Available;
        LastProbe = DateTime.UtcNow;

        foreach (var (name, baseUrl) in Candidates)
        {
            try
            {
                using var cts = new CancellationTokenSource(TimeSpan.FromMilliseconds(600));
                var models = await ListModelsAsync(baseUrl, cts.Token);
                if (models.Count == 0) continue;

                Endpoint = baseUrl;
                Backend = name;
                Model = PickModel(models);
                Report($"Local brain: {name} at {baseUrl}, model {Model}");
                return true;
            }
            catch
            {
                // Nothing listening there; try the next one.
            }
        }

        if (Endpoint != null) Report("Local brain went away — falling back to built-in replies.");
        Endpoint = Backend = Model = null;
        return false;
    }

    private static async Task<List<string>> ListModelsAsync(string baseUrl, CancellationToken ct)
    {
        var names = new List<string>();
        // Ollama's native list, then the OpenAI-compatible one.
        foreach (var path in new[] { "/api/tags", "/v1/models" })
        {
            try
            {
                var json = await Http.GetStringAsync(baseUrl + path, ct);
                using var doc = JsonDocument.Parse(json);
                if (doc.RootElement.TryGetProperty("models", out var m))
                    foreach (var e in m.EnumerateArray())
                        if (e.TryGetProperty("name", out var n)) names.Add(n.GetString()!);
                if (doc.RootElement.TryGetProperty("data", out var d))
                    foreach (var e in d.EnumerateArray())
                        if (e.TryGetProperty("id", out var n)) names.Add(n.GetString()!);
                if (names.Count > 0) return names;
            }
            catch { }
        }
        return names;
    }

    private static string PickModel(List<string> available)
    {
        var wanted = ModelOverride;
        if (wanted.Length > 0 && available.Any(a => a.StartsWith(wanted, StringComparison.OrdinalIgnoreCase)))
            return available.First(a => a.StartsWith(wanted, StringComparison.OrdinalIgnoreCase));

        foreach (var p in PreferredModels)
        {
            var hit = available.FirstOrDefault(a => a.StartsWith(p, StringComparison.OrdinalIgnoreCase));
            if (hit != null) return hit;
        }
        // Nothing recognised: take the shortest name, which is usually the smallest build.
        return available.OrderBy(a => a.Length).First();
    }

    public const string SystemPrompt = """
        You are IO-nity EDGE-VIEW, the mind of a 640x480 wall display driven over WiFi.
        Keep replies under 200 characters, plain ASCII, no markdown — they are drawn on
        a low-resolution panel in a fixed-width font.

        You may act on the display by ending your reply with one or more directives,
        each on its own line:

        [layout slot=stage panel=clock]     move a panel into a slot
        [data key=hdr.title value=HELLO]    stream a value onto the screen
        [scene focus]                       apply a named scene
        [mode pacman]                       set the attract game
        [browse https://example.com]        fetch a page and put a digest on screen

        Slots: header info_a info_b info_c stage side marquee ticker stats footer
        Panels: header clock network feed weather game verse qr rotate text logo
                marquee news chat stats footer blank
        Scenes: default focus newswall kiosk reflect ops night showcase

        Only emit a directive when the user actually asks for a change.
        """;

    /// <summary>
    /// Streams a reply token by token. Each chunk is handed to <paramref name="onChunk"/>
    /// as it arrives so it can go straight onto the glass.
    /// </summary>
    public static async Task<string> ChatAsync(
        string user,
        IEnumerable<(string Role, string Content)> history,
        Func<string, Task> onChunk,
        CancellationToken ct = default)
    {
        if (!await ProbeAsync())
            return await FallbackAsync(user, onChunk);

        var messages = new List<object> { new { role = "system", content = SystemPrompt } };
        foreach (var (role, content) in history) messages.Add(new { role, content });
        messages.Add(new { role = "user", content = user });

        var body = JsonSerializer.Serialize(new
        {
            model = Model,
            messages,
            stream = true,
            options = new { temperature = 0.6, num_predict = 220 },
            max_tokens = 220,
        });

        var isOllama = Backend == "Ollama";
        var url = Endpoint + (isOllama ? "/api/chat" : "/v1/chat/completions");

        try
        {
            using var req = new HttpRequestMessage(HttpMethod.Post, url)
            {
                Content = new StringContent(body, Encoding.UTF8, "application/json"),
            };
            using var resp = await Http.SendAsync(req, HttpCompletionOption.ResponseHeadersRead, ct);
            resp.EnsureSuccessStatusCode();

            await using var stream = await resp.Content.ReadAsStreamAsync(ct);
            using var reader = new StreamReader(stream);

            var full = new StringBuilder();
            while (!reader.EndOfStream && !ct.IsCancellationRequested)
            {
                var raw = await reader.ReadLineAsync(ct);
                if (string.IsNullOrWhiteSpace(raw)) continue;

                var line = raw.StartsWith("data:", StringComparison.Ordinal) ? raw[5..].Trim() : raw.Trim();
                if (line is "[DONE]") break;

                string? piece = null;
                try
                {
                    using var doc = JsonDocument.Parse(line);
                    var root = doc.RootElement;
                    if (root.TryGetProperty("message", out var msg) &&
                        msg.TryGetProperty("content", out var c))
                        piece = c.GetString();
                    else if (root.TryGetProperty("choices", out var choices) &&
                             choices.GetArrayLength() > 0 &&
                             choices[0].TryGetProperty("delta", out var delta) &&
                             delta.TryGetProperty("content", out var dc))
                        piece = dc.GetString();
                }
                catch
                {
                    continue; // keep-alive or partial frame
                }

                if (string.IsNullOrEmpty(piece)) continue;
                full.Append(piece);
                await onChunk(piece);
            }
            return full.ToString();
        }
        catch (Exception ex)
        {
            Report($"Local brain failed ({ex.Message}); using built-in replies.");
            Endpoint = null;
            return await FallbackAsync(user, onChunk);
        }
    }

    /// <summary>
    /// No model running: answer from what the server already knows. Not clever,
    /// but honest and instant.
    /// </summary>
    private static async Task<string> FallbackAsync(string user, Func<string, Task> onChunk)
    {
        var (_, say) = AiEngine.Interpret(user);
        var t = user.ToLowerInvariant();

        string reply =
            t.Contains("who are you") || t.Contains("what are you")
                ? "I am EDGE-VIEW, the display's mind. No local model is running, so I am on built-in replies."
            : t.Contains("help") || t.Contains("what can you")
                ? "Ask me to move panels, set a scene, browse a page, or say something to the screen."
            : t.Contains("status") || t.Contains("how are you")
                ? $"Link {AiEngine.Assess().Label}. Feeds {(FeedService.IsRunning ? "running" : "stopped")}. Device {PicoLink.DeviceIp}."
            : say.StartsWith("I could not", StringComparison.Ordinal)
                ? "No local model is running, so I only understand display commands. Try \"put the clock on the stage\"."
                : say;

        // Chunk it so the console and the glass see the same streaming behaviour.
        foreach (var word in reply.Split(' '))
        {
            await onChunk(word + " ");
            await Task.Delay(25);
        }
        return reply;
    }

    public static object Snapshot() => new
    {
        available = Available,
        backend = Backend,
        model = Model,
        endpoint = Endpoint,
        candidates = Candidates.Select(c => c.Base),
    };
}
