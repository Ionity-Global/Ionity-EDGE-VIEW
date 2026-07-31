using System.Text.Json;
using System.Text.RegularExpressions;

namespace EdgeView.Core;

/// <summary>One thing the AI decided to do.</summary>
public readonly record struct AiAction(string Kind, string A = "", string B = "");

/// <summary>
/// The AI runs here, on the server, not in the browser — so autopilot,
/// self-healing and inbound directives keep working with no console open.
/// No API keys and no round trips: everything is local reasoning.
/// </summary>
public static class AiEngine
{
    public static event Action<string>? Log;
    public static bool IsRunning => _cts != null;

    private static CancellationTokenSource? _cts;
    private static string _lastAutoScene = "";
    private static DateTime _lastGistPull = DateTime.MinValue;

    public static bool Autopilot
    {
        get => Settings.Get("ai_autopilot", false);
        set => Settings.Set("ai_autopilot", value);
    }

    public static bool Sentinel
    {
        get => Settings.Get("ai_sentinel", true);
        set => Settings.Set("ai_sentinel", value);
    }

    public static bool Curator
    {
        get => Settings.Get("ai_curator", true);
        set => Settings.Set("ai_curator", value);
    }

    public static string GistSource
    {
        get => Settings.Get("ai_gist", "");
        set => Settings.Set("ai_gist", value);
    }

    public static bool GistAuto
    {
        get => Settings.Get("ai_gist_auto", false);
        set => Settings.Set("ai_gist_auto", value);
    }

    private static void Report(string m) => Log?.Invoke($"[{DateTime.Now:HH:mm:ss}] {m}");

    // ───────────────────────── scenes ─────────────────────────

    public sealed record Scene(string Label, string Why, Dictionary<string, string> Layout);

    public static readonly Dictionary<string, string> DefaultLayout = new()
    {
        ["header"] = "header", ["info_a"] = "network", ["info_b"] = "feed", ["info_c"] = "weather",
        ["stage"] = "game", ["side"] = "rotate", ["marquee"] = "marquee", ["ticker"] = "news",
        ["stats"] = "stats", ["footer"] = "footer",
    };

    private static Dictionary<string, string> With(params (string Slot, string Panel)[] changes)
    {
        var m = new Dictionary<string, string>(DefaultLayout);
        foreach (var (slot, panel) in changes) m[slot] = panel;
        return m;
    }

    public static readonly Dictionary<string, Scene> Scenes = new()
    {
        ["default"] = new("Default", "Everything on.", new(DefaultLayout)),
        ["focus"] = new("Focus", "One big clock, everything else quiet.",
            With(("stage", "clock"), ("side", "weather"), ("info_b", "blank"), ("ticker", "blank"))),
        ["newswall"] = new("News wall", "Headlines take the stage, the crawl doubles up underneath.",
            With(("stage", "news"), ("side", "weather"), ("info_b", "clock"), ("ticker", "news"))),
        ["kiosk"] = new("Kiosk", "Scannable QR on the stage for walk-up visitors.",
            With(("stage", "qr"), ("side", "text"), ("info_a", "clock"), ("ticker", "marquee"))),
        ["reflect"] = new("Reflect", "Verse front and centre, minimal noise.",
            With(("stage", "verse"), ("side", "clock"), ("info_b", "blank"), ("ticker", "blank"), ("marquee", "blank"))),
        ["ops"] = new("Ops", "Diagnostics everywhere — network, stream health, stats.",
            With(("stage", "stats"), ("side", "network"), ("info_a", "network"), ("info_b", "feed"), ("info_c", "clock"))),
        ["night"] = new("Night", "Dark hours: clock only, no motion to distract.",
            With(("info_a", "blank"), ("info_b", "blank"), ("info_c", "blank"), ("stage", "clock"),
                 ("side", "blank"), ("marquee", "blank"), ("ticker", "blank"), ("stats", "blank"))),
        ["showcase"] = new("Showcase", "Logo and motion — for a stand or a demo table.",
            With(("stage", "game"), ("side", "logo"), ("info_b", "clock"), ("ticker", "news"))),
    };

    /// <summary>Night is quiet, mornings inform, evenings reflect.</summary>
    public static string AutoScene(DateTime? at = null)
    {
        var h = (at ?? DateTime.Now).Hour;
        if (h >= 23 || h < 6) return "night";
        if (h < 9) return "newswall";
        if (h < 17) return "ops";
        if (h < 21) return "showcase";
        return "reflect";
    }

    // ───────────────────────── intent parsing ─────────────────────────

    private static readonly Dictionary<string, string[]> SlotWords = new()
    {
        ["header"] = ["header", "top bar", "title bar"],
        ["info_a"] = ["info a", "first panel", "left panel", "left"],
        ["info_b"] = ["info b", "second panel", "middle panel", "centre panel", "center panel"],
        ["info_c"] = ["info c", "third panel", "right panel", "right"],
        ["stage"] = ["stage", "middle", "centre", "center", "main", "big", "hero"],
        ["side"] = ["side", "sidebar", "right side"],
        ["marquee"] = ["marquee", "scroller", "scrolling bar"],
        ["ticker"] = ["ticker", "news bar", "bottom bar"],
        ["stats"] = ["stats", "statistics", "metrics"],
        ["footer"] = ["footer", "bottom"],
    };

    private static readonly Dictionary<string, string[]> PanelWords = new()
    {
        ["clock"] = ["clock", "time"],
        ["weather"] = ["weather", "temperature", "temp", "forecast"],
        ["news"] = ["news", "headlines"],
        ["verse"] = ["verse", "scripture", "bible"],
        ["qr"] = ["qr", "qr code", "scan code"],
        ["game"] = ["game", "pacman", "pac-man", "snake", "bounce", "animation"],
        ["network"] = ["network", "wifi", "ip", "connection"],
        ["feed"] = ["feed", "stream status", "link status"],
        ["stats"] = ["stats", "diagnostics", "metrics"],
        ["text"] = ["text", "message", "note"],
        ["logo"] = ["logo", "brand"],
        ["marquee"] = ["marquee", "crawl"],
        ["rotate"] = ["rotate", "rotating", "carousel"],
        ["blank"] = ["blank", "nothing", "empty", "clear", "hide"],
        ["header"] = ["header"],
        ["footer"] = ["footer"],
    };

    private static string? MatchWord(string text, Dictionary<string, string[]> table)
    {
        string? best = null;
        int bestLen = 0;
        foreach (var (key, words) in table)
            foreach (var w in words)
                if (text.Contains(w, StringComparison.OrdinalIgnoreCase) && w.Length > bestLen)
                {
                    best = key;
                    bestLen = w.Length;
                }
        return best;
    }

    /// <summary>Turn a sentence into concrete device actions.</summary>
    public static (List<AiAction> Actions, string Say) Interpret(string raw)
    {
        var t = " " + raw.ToLowerInvariant().Trim() + " ";
        var actions = new List<AiAction>();
        var notes = new List<string>();

        foreach (var (id, s) in Scenes)
        {
            if (t.Contains(id) || t.Contains(s.Label.ToLowerInvariant()))
            {
                if (Regex.IsMatch(t, @"(scene|mode|layout|look|preset)") || id != "default")
                {
                    actions.Add(new AiAction("scene", id));
                    notes.Add($"scene → {s.Label}");
                    break;
                }
            }
        }

        if (Regex.IsMatch(t, @"\b(reboot|restart|reset)\b")) { actions.Add(new AiAction("reboot")); notes.Add("reboot device"); }
        if (Regex.IsMatch(t, @"\b(reload|refresh|re-?push|resend)\b")) { actions.Add(new AiAction("reload")); notes.Add("re-push content"); }

        foreach (var g in new[] { "pacman", "snake", "bounce" })
            if (t.Contains(g)) { actions.Add(new AiAction("mode", g)); notes.Add($"game → {g}"); }
        if (Regex.IsMatch(t, @"\b(stop|kill|no) (the )?game\b")) { actions.Add(new AiAction("mode", "off")); notes.Add("game off"); }

        var say = Regex.Match(raw, @"\b(?:say|announce|post|tell (?:them|everyone))\s+(.{2,140})", RegexOptions.IgnoreCase);
        if (say.Success) { actions.Add(new AiAction("chat", say.Groups[1].Value.Trim().Trim('"', '\''))); notes.Add("announce"); }

        var title = Regex.Match(raw, @"\b(?:title|heading|header text)\s+(?:to\s+)?(.{2,60})", RegexOptions.IgnoreCase);
        if (title.Success) { actions.Add(new AiAction("data", "hdr.title", title.Groups[1].Value.Trim())); notes.Add("header title"); }

        foreach (Match m in Regex.Matches(raw,
                     @"(?:put|show|move|place|display)\s+(?:the\s+)?(.{2,24}?)\s+(?:in|on|at|to)\s+(?:the\s+)?(.{2,24}?)(?:\s|$|[.,])",
                     RegexOptions.IgnoreCase))
        {
            var panel = MatchWord(m.Groups[1].Value, PanelWords);
            var slot = MatchWord(m.Groups[2].Value, SlotWords);
            if (panel != null && slot != null) { actions.Add(new AiAction("layout", slot, panel)); notes.Add($"{slot} ← {panel}"); }
        }

        if (actions.Count == 0)
        {
            var panel = MatchWord(t, PanelWords);
            var slot = MatchWord(t, SlotWords);
            if (panel != null && slot != null) { actions.Add(new AiAction("layout", slot, panel)); notes.Add($"{slot} ← {panel}"); }
            else if (panel != null) { actions.Add(new AiAction("layout", "stage", panel)); notes.Add($"stage ← {panel}"); }
        }

        return (actions, notes.Count > 0 ? string.Join(", ", notes) : "I could not find anything to do in that.");
    }

    // ───────────────────────── curator ─────────────────────────

    private static readonly Dictionary<string, int> TopicWeights = new()
    {
        ["ai"] = 3, ["artificial intelligence"] = 4, ["model"] = 2, ["llm"] = 3, ["openai"] = 3,
        ["anthropic"] = 3, ["gemini"] = 2, ["chip"] = 2, ["silicon"] = 2, ["robot"] = 2, ["agent"] = 2,
        ["open source"] = 2, ["africa"] = 3, ["south africa"] = 4, ["edge"] = 2, ["embedded"] = 2,
        ["raspberry"] = 3, ["pico"] = 4, ["iot"] = 2, ["energy"] = 2, ["solar"] = 2,
    };

    private static readonly Regex Noise = new(@"\b(sponsored|advertisement|deal of the day|shop now|coupon)\b", RegexOptions.IgnoreCase);

    private static HashSet<string> Tokens(string s) =>
        Regex.Replace(s.ToLowerInvariant(), "[^a-z0-9 ]", " ")
             .Split(' ', StringSplitOptions.RemoveEmptyEntries)
             .Where(w => w.Length > 3)
             .ToHashSet();

    private static double Overlap(HashSet<string> a, HashSet<string> b)
    {
        int hit = a.Count(b.Contains);
        return hit / (double)Math.Max(1, Math.Min(a.Count, b.Count));
    }

    /// <summary>Score, de-duplicate and trim headlines before they hit the glass.</summary>
    public static List<string> Curate(IEnumerable<string> headlines, int max = 8)
    {
        var scored = new List<(string H, double Score, HashSet<string> Tk)>();
        foreach (var rawItem in headlines)
        {
            var h = Regex.Replace(rawItem ?? "", @"\s+", " ").Trim();
            if (h.Length < 12 || Noise.IsMatch(h)) continue;
            var low = h.ToLowerInvariant();
            double score = Math.Min(6, h.Length / 40.0);
            foreach (var (k, w) in TopicWeights) if (low.Contains(k)) score += w;
            if (Regex.IsMatch(h, @"\b(20\d\d|\d+%|\$\d)")) score += 1;
            if (h.EndsWith('?')) score -= 1;
            scored.Add((h, score, Tokens(h)));
        }

        var kept = new List<(string H, double Score, HashSet<string> Tk)>();
        foreach (var c in scored.OrderByDescending(x => x.Score))
        {
            if (kept.Any(k => Overlap(c.Tk, k.Tk) > 0.6)) continue;
            kept.Add(c);
            if (kept.Count >= max) break;
        }
        return kept.Select(k => k.H).ToList();
    }

    // ───────────────────────── sentinel ─────────────────────────

    private static int _tx, _rx, _err;
    private static DateTime _lastTx = DateTime.MinValue;
    private static readonly DateTime _since = DateTime.UtcNow;

    public static void NoteTraffic(string dir)
    {
        if (dir == "tx") { Interlocked.Increment(ref _tx); _lastTx = DateTime.UtcNow; }
        else if (dir == "rx") Interlocked.Increment(ref _rx);
        else if (dir == "err") Interlocked.Increment(ref _err);
    }

    public sealed record Health(double Score, string Label, string? Advice, AiAction? Fix);

    public static Health Assess()
    {
        int total = _tx + _err;
        double errRate = total > 0 ? _err / (double)total : 0;
        var quiet = DateTime.UtcNow - (_lastTx == DateTime.MinValue ? _since : _lastTx);

        double score = 1;
        score -= errRate * 0.7;
        if (quiet.TotalSeconds > 120) score -= 0.35;
        else if (quiet.TotalSeconds > 60) score -= 0.15;
        if (!FeedService.IsRunning) score -= 0.2;
        score = Math.Clamp(score, 0, 1);

        string? advice = null;
        AiAction? fix = null;
        if (errRate > 0.4 && total > 4)
        {
            advice = "Most commands are failing — the device address is probably stale.";
            fix = new AiAction("rediscover");
        }
        else if (!FeedService.IsRunning)
        {
            advice = "Feeds are stopped, so the glass is running on local fallbacks.";
            fix = new AiAction("feeds", "on");
        }
        else if (quiet.TotalSeconds > 120)
        {
            advice = "Nothing has been sent for two minutes — re-pushing the content.";
            fix = new AiAction("reload");
        }

        var label = score > 0.85 ? "Healthy" : score > 0.6 ? "Degraded" : score > 0.3 ? "Struggling" : "Down";
        return new Health(score, label, advice, fix);
    }

    // ───────────────────────── inbound (gist) ─────────────────────────

    private static readonly HttpClient Http = CreateClient();

    private static HttpClient CreateClient()
    {
        var c = new HttpClient { Timeout = TimeSpan.FromSeconds(20) };
        c.DefaultRequestHeaders.UserAgent.ParseAdd($"IonityEdgeViewHost/{EdgeViewInfo.Version} (+https://ionity.co.za)");
        return c;
    }

    private static readonly Regex KeyRe = new(@"^[a-z][a-z0-9._-]{0,23}$", RegexOptions.IgnoreCase);

    /// <summary>Pull a directive document from a public Gist (or any GitHub raw
    /// URL) and turn it into actions. The document is untrusted, so every field
    /// is validated against the firmware's own slot and panel names.</summary>
    public static async Task<List<AiAction>> PullInboundAsync(string idOrUrl)
    {
        var src = (idOrUrl ?? "").Trim();
        if (src.Length == 0) throw new InvalidOperationException("no gist configured");

        string text;
        if (Uri.TryCreate(src, UriKind.Absolute, out var url) && (url.Scheme == "http" || url.Scheme == "https"))
        {
            if (!url.Host.EndsWith("githubusercontent.com", StringComparison.OrdinalIgnoreCase) &&
                !url.Host.EndsWith("github.com", StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("only github/gist URLs are accepted");
            text = await Http.GetStringAsync(url);
        }
        else
        {
            var id = src.Split('/').Last();
            if (!Regex.IsMatch(id, "^[0-9a-f]{6,64}$", RegexOptions.IgnoreCase))
                throw new InvalidOperationException("that does not look like a gist id");
            var meta = await Http.GetStringAsync($"https://api.github.com/gists/{id}");
            using var doc0 = JsonDocument.Parse(meta);
            if (!doc0.RootElement.TryGetProperty("files", out var files))
                throw new InvalidOperationException("gist not found");
            var file = files.EnumerateObject()
                            .FirstOrDefault(f => f.Name.EndsWith(".json", StringComparison.OrdinalIgnoreCase));
            var chosen = file.Value.ValueKind == JsonValueKind.Object ? file.Value : files.EnumerateObject().First().Value;
            text = chosen.TryGetProperty("truncated", out var tr) && tr.GetBoolean()
                ? await Http.GetStringAsync(chosen.GetProperty("raw_url").GetString()!)
                : chosen.GetProperty("content").GetString() ?? "";
        }

        using var doc = JsonDocument.Parse(text);
        return InboundToActions(doc.RootElement);
    }

    public static List<AiAction> InboundToActions(JsonElement doc)
    {
        var actions = new List<AiAction>();
        if (doc.ValueKind != JsonValueKind.Object) return actions;

        if (doc.TryGetProperty("scene", out var scene) && scene.ValueKind == JsonValueKind.String &&
            Scenes.ContainsKey(scene.GetString()!))
            actions.Add(new AiAction("scene", scene.GetString()!));

        if (doc.TryGetProperty("layout", out var layout) && layout.ValueKind == JsonValueKind.Object)
            foreach (var p in layout.EnumerateObject())
                if (PicoLink.Slots.Contains(p.Name) && p.Value.ValueKind == JsonValueKind.String &&
                    PicoLink.Panels.Contains(p.Value.GetString()))
                    actions.Add(new AiAction("layout", p.Name, p.Value.GetString()!));

        if (doc.TryGetProperty("title", out var title) && title.ValueKind == JsonValueKind.String)
            actions.Add(new AiAction("data", "hdr.title", Trim(title.GetString(), 60)));

        if (doc.TryGetProperty("data", out var data) && data.ValueKind == JsonValueKind.Object)
            foreach (var p in data.EnumerateObject())
            {
                if (!KeyRe.IsMatch(p.Name)) continue;
                var v = p.Value.ValueKind switch
                {
                    JsonValueKind.String => p.Value.GetString(),
                    JsonValueKind.Number => p.Value.ToString(),
                    _ => null,
                };
                if (v != null) actions.Add(new AiAction("data", p.Name, Trim(v, 180)));
            }

        if (doc.TryGetProperty("news", out var news) && news.ValueKind == JsonValueKind.Array)
        {
            var items = Curate(news.EnumerateArray()
                                   .Where(e => e.ValueKind == JsonValueKind.String)
                                   .Select(e => e.GetString()!), 6);
            if (items.Count > 0)
                actions.Add(new AiAction("data", "marquee", Trim(string.Join("   \u2022   ", items), 180)));
        }

        if (doc.TryGetProperty("chat", out var chat) && chat.ValueKind == JsonValueKind.Array)
            foreach (var line in chat.EnumerateArray().Take(8))
                if (line.ValueKind == JsonValueKind.String && !string.IsNullOrWhiteSpace(line.GetString()))
                    actions.Add(new AiAction("chat", Trim(line.GetString()!.Trim(), 140)));

        if (doc.TryGetProperty("mode", out var mode) && mode.ValueKind == JsonValueKind.String &&
            new[] { "pacman", "snake", "bounce", "off" }.Contains(mode.GetString()))
            actions.Add(new AiAction("mode", mode.GetString()!));

        return actions;
    }

    private static string Trim(string? s, int max) =>
        s == null ? "" : s.Length <= max ? s : s[..max];

    // ───────────────────────── execution ─────────────────────────

    public static async Task<int> ApplyAsync(IEnumerable<AiAction> actions)
    {
        int done = 0;
        foreach (var a in actions)
        {
            bool ok = a.Kind switch
            {
                "scene" => await ApplySceneAsync(a.A),
                "layout" => await PicoLink.SetLayoutAsync(a.A, a.B),
                "data" => await PicoLink.SetDataAsync(a.A, a.B),
                "mode" => await PicoLink.SetGameModeAsync(a.A),
                "reboot" => await PicoLink.RebootAsync(),
                "reload" => await ReloadAsync(),
                "chat" => await ChatAsync(a.A),
                "feeds" => Feeds(a.A == "on"),
                "rediscover" => Rediscover(),
                _ => false,
            };
            if (ok) done++;
            await Task.Delay(40);
        }
        return done;
    }

    private static async Task<bool> ReloadAsync() { await FeedService.PushHeartbeatAsync(); return true; }

    private static async Task<bool> ChatAsync(string text)
    {
        var ok = await PicoLink.PostMessageAsync("ionity-ai", text);
        BridgeServer.PushChat("ionity-ai", text);
        return ok;
    }

    private static bool Feeds(bool on)
    {
        if (on) FeedService.Start(); else FeedService.Stop();
        return true;
    }

    private static bool Rediscover()
    {
        PicoLink.StartDiscovery();
        Report("Waiting for the next discovery beacon to correct the device address.");
        return true;
    }

    public static async Task<bool> ApplySceneAsync(string id)
    {
        if (!Scenes.TryGetValue(id, out var scene)) return false;
        foreach (var (slot, panel) in scene.Layout)
        {
            await PicoLink.SetLayoutAsync(slot, panel);
            await Task.Delay(40);
        }
        Report($"Scene \"{scene.Label}\" applied. {scene.Why}");
        return true;
    }

    /// <summary>Parse a sentence and carry it out.</summary>
    public static async Task<(string Say, int Applied)> SayAsync(string sentence)
    {
        var (actions, say) = Interpret(sentence);
        var applied = await ApplyAsync(actions);
        Report($"\"{sentence}\" → {say}");
        return (say, applied);
    }

    // ───────────────────────── background loop ─────────────────────────

    public static void Start()
    {
        if (_cts != null) return;
        _cts = new CancellationTokenSource();
        PicoLink.Traffic += OnTraffic;
        _ = RunAsync(_cts.Token);
        Report("AI started");
    }

    public static void Stop()
    {
        PicoLink.Traffic -= OnTraffic;
        _cts?.Cancel();
        _cts = null;
        Report("AI stopped");
    }

    private static void OnTraffic(string dir, string _) => NoteTraffic(dir);

    private static async Task RunAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            try { await Task.Delay(TimeSpan.FromSeconds(20), ct); }
            catch (OperationCanceledException) { break; }

            try
            {
                if (Sentinel)
                {
                    var h = Assess();
                    if (h.Score < 0.5 && h.Fix is { } fix)
                    {
                        Report($"Self-heal: {h.Advice}");
                        await ApplyAsync([fix]);
                    }
                }

                if (Autopilot)
                {
                    var want = AutoScene();
                    if (want != _lastAutoScene)
                    {
                        _lastAutoScene = want;
                        Report($"Autopilot: it is {DateTime.Now:HH}:00, switching to \"{Scenes[want].Label}\".");
                        await ApplySceneAsync(want);
                    }
                }

                if (GistAuto && GistSource.Length > 0 && DateTime.UtcNow - _lastGistPull > TimeSpan.FromSeconds(60))
                {
                    _lastGistPull = DateTime.UtcNow;
                    try
                    {
                        var actions = await PullInboundAsync(GistSource);
                        if (actions.Count > 0)
                        {
                            Report($"Inbound: {actions.Count} directive(s) from the gist.");
                            await ApplyAsync(actions);
                        }
                    }
                    catch (Exception ex)
                    {
                        Report($"Inbound failed: {ex.Message}");
                    }
                }
            }
            catch (Exception ex)
            {
                Report($"AI cycle error: {ex.Message}");
            }
        }
    }
}
