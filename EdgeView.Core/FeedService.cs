using System.Text.Json;
using System.Text.RegularExpressions;
using System.Xml.Linq;

namespace EdgeView.Core;

/// <summary>
/// Fetches HTTPS content the Pico cannot reach itself (no TLS on-device) and
/// pushes it over the WiFi stream: Heartlight Today's Verse, AI news RSS,
/// Open-Meteo weather fallback and time sync.
/// </summary>
public static class FeedService
{
    private static readonly HttpClient Http = CreateClient();
    private static CancellationTokenSource? _cts;

    public static event Action<string>? Log;
    public static bool IsRunning => _cts != null;

    /// <summary>Default AI news sources (RSS).</summary>
    public static readonly string[] AiNewsFeeds =
    {
        "https://news.google.com/rss/search?q=artificial+intelligence&hl=en-ZA&gl=ZA&ceid=ZA:en",
        "https://techcrunch.com/category/artificial-intelligence/feed/",
    };

    public const string VerseUrl = "https://www.heartlight.org/cgi-shl/todaysverse.cgi";
    public const string WeatherUrl =
        "https://api.open-meteo.com/v1/forecast?latitude=-25.86&longitude=28.19&current_weather=true";

    private static HttpClient CreateClient()
    {
        var c = new HttpClient() { Timeout = TimeSpan.FromSeconds(20) };
        c.DefaultRequestHeaders.UserAgent.ParseAdd("IonityEdgeViewStudio/2.0 (+https://ionity.co.za)");
        return c;
    }

    private static void Report(string msg) => Log?.Invoke($"[{DateTime.Now:HH:mm:ss}] {msg}");

    // ---- Heartlight Today's Verse ----
    public static async Task<(string Reference, string Text)?> FetchVerseAsync()
    {
        try
        {
            var html = await Http.GetStringAsync(VerseUrl);

            // Reference from the <title> ("Today's Verse: Matthew 5:14-16 ...")
            var reference = "Today's Verse";
            var t = Regex.Match(html, @"Today's Verse:\s*([^<|""]+)", RegexOptions.IgnoreCase);
            if (t.Success) reference = t.Groups[1].Value.Trim().TrimEnd('-', ' ');

            // Verse text: prefer og:description / meta description
            string? text = null;
            var og = Regex.Match(html,
                @"property=[""']og:description[""']\s+content=[""']([^""']+)[""']",
                RegexOptions.IgnoreCase);
            if (og.Success) text = og.Groups[1].Value;
            if (text == null)
            {
                var md = Regex.Match(html,
                    @"name=[""']description[""']\s+content=[""']([^""']+)[""']",
                    RegexOptions.IgnoreCase);
                if (md.Success) text = md.Groups[1].Value;
            }
            if (text == null)
            {
                // Last resort: first double-quoted passage in the body
                var q = Regex.Match(html, "\"([^\"]{60,340})\"");
                if (q.Success) text = q.Groups[1].Value;
            }
            if (text == null) return null;

            text = System.Net.WebUtility.HtmlDecode(Regex.Replace(text, "<[^>]+>", " "));
            text = Regex.Replace(text, @"\s+", " ").Trim();
            if (text.Length > 340) text = text[..337] + "...";
            return (reference, text);
        }
        catch (Exception ex)
        {
            Report($"Verse fetch failed: {ex.Message}");
            return null;
        }
    }

    // ---- AI news RSS ----
    public static async Task<List<string>> FetchAiNewsAsync(int max = 10)
    {
        var headlines = new List<string>();
        foreach (var feed in AiNewsFeeds)
        {
            try
            {
                var xml = await Http.GetStringAsync(feed);
                var doc = XDocument.Parse(xml);
                foreach (var item in doc.Descendants("item"))
                {
                    var title = item.Element("title")?.Value?.Trim();
                    if (string.IsNullOrWhiteSpace(title)) continue;
                    title = System.Net.WebUtility.HtmlDecode(title);
                    title = Regex.Replace(title, @"\s+", " ").Trim();
                    if (title.Length > 160) title = title[..157] + "...";
                    if (!headlines.Any(h => string.Equals(h, title, StringComparison.OrdinalIgnoreCase)))
                        headlines.Add(title);
                    if (headlines.Count >= max * 2) break;
                }
            }
            catch (Exception ex)
            {
                Report($"News feed failed ({new Uri(feed).Host}): {ex.Message}");
            }
        }
        return headlines.Take(max).ToList();
    }

    // ---- Open-Meteo weather (Studio-side fallback for the device fetch) ----
    public static async Task<(int TempC, int Condition)?> FetchWeatherAsync()
    {
        try
        {
            var json = await Http.GetStringAsync(WeatherUrl);
            using var doc = JsonDocument.Parse(json);
            var cur = doc.RootElement.GetProperty("current_weather");
            var temp = (int)Math.Round(cur.GetProperty("temperature").GetDouble());
            var code = cur.GetProperty("weathercode").GetInt32();
            return (temp, WmoToCondition(code));
        }
        catch (Exception ex)
        {
            Report($"Weather fetch failed: {ex.Message}");
            return null;
        }
    }

    private static int WmoToCondition(int code) => code switch
    {
        0 => 0,                                        // sunny
        <= 3 => 1,                                     // cloudy
        45 or 48 => 5,                                 // fog
        >= 51 and <= 67 => 2,                          // rain
        >= 71 and <= 77 or 85 or 86 => 4,              // snow
        >= 80 and <= 82 => 2,                          // showers
        >= 95 => 3,                                    // storm
        _ => 1,
    };

    // ---- Scheduler ----
    public static void Start()
    {
        if (_cts != null) return;
        _cts = new CancellationTokenSource();
        _ = RunAsync(_cts.Token);
        Report("Feed service started");
    }

    public static void Stop()
    {
        _cts?.Cancel();
        _cts = null;
        Report("Feed service stopped");
    }

    private static async Task RunAsync(CancellationToken ct)
    {
        var lastVerse = DateTime.MinValue;
        var lastNews = DateTime.MinValue;
        var lastWeather = DateTime.MinValue;
        var lastTime = DateTime.MinValue;

        while (!ct.IsCancellationRequested)
        {
            try
            {
                var now = DateTime.UtcNow;

                // Heartbeat: keeps the device in server-driven mode and refreshes
                // everything the panels read straight off the wire.
                if (Settings.Get("feed_heartbeat", true))
                    await PushHeartbeatAsync();

                if (Settings.Get("feed_time", true) && now - lastTime > TimeSpan.FromHours(12))
                {
                    if (await PicoLink.SendTimeAsync()) { Report("Time synced to device"); lastTime = now; }
                }

                if (Settings.Get("feed_verse", true) && now - lastVerse > TimeSpan.FromHours(6))
                {
                    var v = await FetchVerseAsync();
                    if (v != null && await PicoLink.SendVerseAsync(v.Value.Reference, v.Value.Text))
                    {
                        await PicoLink.SetDataAsync("verse.ref", v.Value.Reference);
                        await PicoLink.SetDataAsync("verse.text", v.Value.Text);
                        Report($"Verse pushed: {v.Value.Reference}");
                        lastVerse = now;
                    }
                }

                if (Settings.Get("feed_news", true) && now - lastNews > TimeSpan.FromMinutes(15))
                {
                    var items = await FetchAiNewsAsync();
                    int sent = 0;
                    foreach (var h in items)
                        if (await PicoLink.SendNewsAsync(h)) sent++;
                    if (sent > 0)
                    {
                        await PicoLink.SetDataAsync("marquee", string.Join("   \u2022   ", items.Take(4)));
                        Report($"AI news pushed: {sent} headlines");
                        lastNews = now;
                    }
                }

                if (Settings.Get("feed_weather", true) && now - lastWeather > TimeSpan.FromMinutes(10))
                {
                    var w = await FetchWeatherAsync();
                    if (w != null && await PicoLink.SendWeatherAsync(w.Value.TempC, w.Value.Condition))
                    {
                        await PicoLink.SetDataAsync("wx.temp", w.Value.TempC.ToString());
                        await PicoLink.SetDataAsync("wx.cond", ConditionName(w.Value.Condition));
                        await PicoLink.SetDataAsync("wx.place", Settings.Get("wx_place", "CENTURION"));
                        Report($"Weather pushed: {w.Value.TempC}C cond {w.Value.Condition}");
                        lastWeather = now;
                    }
                }
            }
            catch (Exception ex)
            {
                Report($"Feed cycle error: {ex.Message}");
            }

            try { await Task.Delay(TimeSpan.FromSeconds(30), ct); }
            catch (OperationCanceledException) { break; }
        }
    }

    /// <summary>
    /// Pushes the values that must stay live. While these keep arriving the
    /// device reports SERVER-DRIVEN; if they stop it falls back to local data.
    /// </summary>
    public static async Task PushHeartbeatAsync()
    {
        var now = DateTime.Now;
        await PicoLink.SetDataAsync("clock", now.ToString("HH:mm:ss"));
        await PicoLink.SetDataAsync("date", now.ToString("ddd dd MMM yyyy").ToUpperInvariant());
        await PicoLink.SetDataAsync("hdr.title", Settings.Get("hdr_title", "IO-NITY EDGE-VIEW"));
        await PicoLink.SetDataAsync("hdr.sub", Settings.Get("hdr_sub", "IONITY GLOBAL (PTY) LTD"));
        await PicoLink.SetDataAsync("foot.left", Settings.Get("foot_left", "IONITY.CO.ZA"));
    }

    private static string ConditionName(int cond) => cond switch
    {
        0 => "SUNNY",
        1 => "CLOUDY",
        2 => "RAIN",
        3 => "STORM",
        4 => "SNOW",
        5 => "FOG",
        _ => "CLEAR",
    };
}
