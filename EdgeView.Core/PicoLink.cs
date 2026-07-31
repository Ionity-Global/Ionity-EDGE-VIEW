using System.Net;
using System.Net.Sockets;
using System.Text;

namespace EdgeView.Core;

/// <summary>
/// Link to the EDGE-VIEW device: TCP 4242 JSON commands, HTTP 80 messages,
/// and UDP 4243 discovery-beacon listener.
/// </summary>
public static class PicoLink
{
    public const int StreamPort = 4242;
    public const int BeaconPort = 4243;

    private static readonly HttpClient Http = new() { Timeout = TimeSpan.FromSeconds(6) };
    private static UdpClient? _beaconListener;
    private static CancellationTokenSource? _beaconCts;

    /// <summary>Raised on the thread pool when a beacon is received: (name, ip).</summary>
    public static event Action<string, string>? DeviceDiscovered;

    /// <summary>Every byte that crosses the WiFi link: ("tx"|"rx"|"err", payload).</summary>
    public static event Action<string, string>? Traffic;

    private static void Trace(string dir, string payload)
    {
        try { Traffic?.Invoke(dir, payload); } catch { /* a bad listener must not break the link */ }
    }

    private static int _consecutiveFailures;

    /// <summary>True once the device has answered at least once recently.</summary>
    public static bool Reachable => _consecutiveFailures == 0;

    /// <summary>
    /// Full timeout while the device is answering; a short one once it clearly
    /// is not, so a ten-slot scene does not cost forty seconds of dead waits.
    /// </summary>
    private static int ConnectTimeoutMs => _consecutiveFailures >= 3 ? 700 : 4000;

    private static void NoteResult(bool ok)
    {
        if (ok) _consecutiveFailures = 0;
        else if (_consecutiveFailures < 100) _consecutiveFailures++;
    }

    public static string DeviceIp
    {
        get => Settings.Get("pico_ip", "192.168.1.100");
        set => Settings.Set("pico_ip", value);
    }

    /// <summary>Escape a string for embedding into a JSON command.</summary>
    public static string JsonEscape(string s)
    {
        var sb = new StringBuilder(s.Length);
        foreach (var c in s)
        {
            switch (c)
            {
                case '\\': sb.Append("\\\\"); break;
                case '"': sb.Append("\\\""); break;
                case '\n': case '\r': case '\t': sb.Append(' '); break;
                default:
                    sb.Append(c is >= (char)0x20 and < (char)0x7F ? c : '?'); // device font is ASCII
                    break;
            }
        }
        return sb.ToString();
    }

    /// <summary>Send one JSON command line over TCP 4242. Returns true on success.</summary>
    public static async Task<bool> SendCommandAsync(string json, CancellationToken ct = default)
    {
        try
        {
            using var client = new TcpClient();
            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct);
            timeout.CancelAfter(ConnectTimeoutMs);
            await client.ConnectAsync(DeviceIp, StreamPort, timeout.Token);
            var stream = client.GetStream();
            var bytes = Encoding.ASCII.GetBytes(json + "\n");
            await stream.WriteAsync(bytes, timeout.Token);
            await stream.FlushAsync(timeout.Token);
            Trace("tx", json);
            NoteResult(true);
            return true;
        }
        catch
        {
            NoteResult(false);
            return false;
        }
    }

    /// <summary>Send a command and read one reply line (used for ping/pong).</summary>
    public static async Task<string?> SendAndReadAsync(string json, CancellationToken ct = default)
    {
        try
        {
            using var client = new TcpClient();
            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct);
            timeout.CancelAfter(ConnectTimeoutMs);
            await client.ConnectAsync(DeviceIp, StreamPort, timeout.Token);
            var stream = client.GetStream();
            var bytes = Encoding.ASCII.GetBytes(json + "\n");
            await stream.WriteAsync(bytes, timeout.Token);
            var buf = new byte[512];
            using var reader = new StreamReader(stream, Encoding.ASCII, false, 512, true);
            Trace("tx", json);
            // First line is the hello banner; second is the reply.
            var line = await reader.ReadLineAsync(timeout.Token);
            if (line != null && line.Contains("\"hello\""))
                line = await reader.ReadLineAsync(timeout.Token);
            if (line != null) Trace("rx", line);
            NoteResult(true);
            return line;
        }
        catch
        {
            Trace("err", json);
            NoteResult(false);
            return null;
        }
    }

    // ---- Convenience commands ----
    public static Task<bool> SetGameModeAsync(string mode) =>
        SendCommandAsync($"{{\"type\":\"mode\",\"name\":\"{mode}\"}}");

    public static Task<bool> RebootAsync() =>
        SendCommandAsync("{\"type\":\"reboot\"}");

    public static Task<bool> SendTextAsync(string text) =>
        SendCommandAsync($"{{\"type\":\"text\",\"text\":\"{JsonEscape(text)}\"}}");

    public static Task<bool> SendVerseAsync(string reference, string text) =>
        SendCommandAsync($"{{\"type\":\"verse\",\"ref\":\"{JsonEscape(reference)}\",\"text\":\"{JsonEscape(text)}\"}}");

    public static Task<bool> SendNewsAsync(string headline) =>
        SendCommandAsync($"{{\"type\":\"news\",\"text\":\"{JsonEscape(headline)}\"}}");

    public static Task<bool> SendWeatherAsync(int tempC, int condition) =>
        SendCommandAsync($"{{\"type\":\"weather\",\"temp\":{tempC},\"cond\":{condition}}}");

    public static Task<bool> SendTimeAsync() =>
        SendCommandAsync($"{{\"type\":\"time\",\"epoch\":{DateTimeOffset.UtcNow.ToUnixTimeSeconds()}}}");

    // ---- Server-driven display ----

    /// <summary>Streams one value into the screen. Panels read it back by key.</summary>
    public static Task<bool> SetDataAsync(string key, string value) =>
        SendCommandAsync($"{{\"type\":\"data\",\"key\":\"{JsonEscape(key)}\",\"value\":\"{JsonEscape(value)}\"}}");

    /// <summary>Adds a line to the device's chat panel.</summary>
    public static Task<bool> SendChatAsync(string who, string text) =>
        SendCommandAsync($"{{\"type\":\"chat\",\"who\":\"{JsonEscape(who)}\",\"text\":\"{JsonEscape(text)}\"}}");

    /// <summary>Appends to the newest chat line, so a reply can arrive a word at a time.</summary>
    public static Task<bool> SendChatAppendAsync(string text) =>
        SendCommandAsync($"{{\"type\":\"chatadd\",\"text\":\"{JsonEscape(text)}\"}}");

    /// <summary>Binds a panel to a screen slot at runtime.</summary>
    public static Task<bool> SetLayoutAsync(string slot, string panel) =>
        SendCommandAsync($"{{\"type\":\"layout\",\"slot\":\"{JsonEscape(slot)}\",\"panel\":\"{JsonEscape(panel)}\"}}");

    /// <summary>Reads back the current slot bindings and available panels.</summary>
    public static Task<string?> QueryLayoutAsync() =>
        SendAndReadAsync("{\"type\":\"query\",\"what\":\"layout\"}");

    /// <summary>Reprovisions WiFi remotely. The device saves and reboots.</summary>
    public static Task<bool> SetWifiAsync(string ssid, string password) =>
        SendCommandAsync($"{{\"type\":\"wifi\",\"ssid\":\"{JsonEscape(ssid)}\",\"pass\":\"{JsonEscape(password)}\"}}");

    /// <summary>Forgets the saved credentials. The device reboots onto its
    /// build-time network, or into the provisioning AP if it has none.</summary>
    public static async Task<bool> ResetWifiAsync()
    {
        // The device reports whether the flash erase verified, so read the reply
        // rather than trusting that the write went out.
        var reply = await SendAndReadAsync("{\"type\":\"wifi\",\"reset\":true}");
        return reply?.Contains("\"cleared\":true") == true;
    }

    public static readonly string[] Slots =
        { "header", "info_a", "info_b", "info_c", "stage", "side", "marquee", "ticker", "stats", "footer" };

    public static readonly string[] Panels =
        { "header", "clock", "network", "feed", "weather", "game", "verse", "qr",
          "rotate", "text", "logo", "marquee", "news", "chat", "stats", "footer", "blank" };

    public static async Task<bool> PingAsync()
    {
        var reply = await SendAndReadAsync("{\"type\":\"ping\"}");
        return reply != null && reply.Contains("pong");
    }

    // ---- HTTP broadcast messages (port 80 on the device) ----
    public static async Task<bool> PostMessageAsync(string author, string text)
    {
        try
        {
            var json = $"{{\"author\":\"{JsonEscape(author)}\",\"text\":\"{JsonEscape(text)}\"}}";
            var resp = await Http.PostAsync($"http://{DeviceIp}/message",
                new StringContent(json, Encoding.UTF8, "application/json"));
            return resp.IsSuccessStatusCode || (int)resp.StatusCode == 303;
        }
        catch
        {
            return false;
        }
    }

    public static async Task<string?> GetMessagesJsonAsync()
    {
        try
        {
            return await Http.GetStringAsync($"http://{DeviceIp}/messages");
        }
        catch
        {
            return null;
        }
    }

    // ---- UDP discovery ----
    public static void StartDiscovery()
    {
        if (_beaconListener != null) return;
        try
        {
            _beaconCts = new CancellationTokenSource();
            _beaconListener = new UdpClient();
            _beaconListener.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);
            _beaconListener.Client.Bind(new IPEndPoint(IPAddress.Any, BeaconPort));
            _ = ListenLoopAsync(_beaconCts.Token);
        }
        catch
        {
            _beaconListener = null;
        }
    }

    private static async Task ListenLoopAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested && _beaconListener != null)
        {
            try
            {
                var result = await _beaconListener.ReceiveAsync(ct);
                var msg = Encoding.ASCII.GetString(result.Buffer);
                // "IONITY-EDGE <name> <ip> <port>"
                var parts = msg.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                if (parts.Length >= 3 && parts[0] == "IONITY-EDGE")
                    DeviceDiscovered?.Invoke(parts[1], parts[2]);
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch
            {
                await Task.Delay(1000, ct).ContinueWith(_ => { });
            }
        }
    }
}
