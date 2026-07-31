using System.Text;
using System.Text.RegularExpressions;

namespace EdgeView.Core;

/// <summary>
/// The conversation. A message comes in, the local brain answers, and the reply
/// is streamed to the console and onto the device's chat panel at the same time
/// — this is a streaming interface, so nothing waits for a completed answer.
/// Directives the brain emits are executed as they are found.
/// </summary>
public static partial class ChatService
{
    private const int HistoryTurns = 8;

    private static readonly List<(string Role, string Content)> History = new();
    private static readonly SemaphoreSlim Gate = new(1, 1);

    public static event Action<string>? Log;
    private static void Report(string m) => Log?.Invoke(m);

    [GeneratedRegex(@"\[(layout|data|scene|mode|browse)\s+([^\]]+)\]", RegexOptions.IgnoreCase)]
    private static partial Regex Directive();

    [GeneratedRegex(@"(\w+)=([^\s\]]+)")]
    private static partial Regex KeyValue();

    /// <summary>
    /// Handle one message. Returns the reply text once it has finished
    /// streaming. <paramref name="onChunk"/> fires for every token.
    /// </summary>
    public static async Task<string> SayAsync(string who, string text, Func<string, Task>? onChunk = null)
    {
        who = string.IsNullOrWhiteSpace(who) ? "web" : who.Trim();
        text = (text ?? "").Trim();
        if (text.Length == 0) return "";

        // Show the question on the glass before the answer starts arriving.
        await PicoLink.SendChatAsync(who, text);
        BridgeServer.PushChat(who, text);

        await Gate.WaitAsync();
        try
        {
            var reply = new StringBuilder();
            var pending = new StringBuilder();

            // The device gets whole words, not single tokens — a redraw per
            // character would be wasted frames.
            async Task Chunk(string piece)
            {
                reply.Append(piece);
                pending.Append(piece);
                if (onChunk != null) await onChunk(piece);

                if (pending.Length >= 12 || piece.Contains(' '))
                {
                    var slice = StripDirectives(pending.ToString());
                    pending.Clear();
                    if (slice.Length > 0) await PicoLink.SendChatAppendAsync(slice);
                }
            }

            await PicoLink.SendChatAsync("ionity-ai", "");
            var full = await LocalBrain.ChatAsync(text, History.TakeLast(HistoryTurns * 2), Chunk);

            if (pending.Length > 0)
            {
                var slice = StripDirectives(pending.ToString());
                if (slice.Length > 0) await PicoLink.SendChatAppendAsync(slice);
            }

            History.Add(("user", text));
            History.Add(("assistant", full));
            while (History.Count > HistoryTurns * 2) History.RemoveAt(0);

            var spoken = StripDirectives(full).Trim();
            BridgeServer.PushChat("ionity-ai", spoken.Length > 0 ? spoken : "(acting)");

            await RunDirectivesAsync(full);
            return full;
        }
        finally
        {
            Gate.Release();
        }
    }

    private static string StripDirectives(string s) => Directive().Replace(s, "").Trim();

    /// <summary>Execute whatever the brain asked the display to do.</summary>
    private static async Task RunDirectivesAsync(string reply)
    {
        foreach (Match m in Directive().Matches(reply))
        {
            var verb = m.Groups[1].Value.ToLowerInvariant();
            var arg = m.Groups[2].Value.Trim();
            var kv = KeyValue().Matches(arg)
                               .ToDictionary(x => x.Groups[1].Value.ToLowerInvariant(),
                                             x => x.Groups[2].Value);
            try
            {
                switch (verb)
                {
                    case "layout" when kv.TryGetValue("slot", out var slot) && kv.TryGetValue("panel", out var panel):
                        if (PicoLink.Slots.Contains(slot) && PicoLink.Panels.Contains(panel))
                        {
                            await PicoLink.SetLayoutAsync(slot, panel);
                            Report($"AI moved {panel} into {slot}");
                        }
                        break;

                    case "data" when kv.TryGetValue("key", out var key):
                        kv.TryGetValue("value", out var value);
                        await PicoLink.SetDataAsync(key, value ?? "");
                        Report($"AI set {key}");
                        break;

                    case "scene":
                        var id = kv.Count > 0 ? kv.Values.First() : arg;
                        if (await AiEngine.ApplySceneAsync(id.ToLowerInvariant()))
                            Report($"AI applied scene {id}");
                        break;

                    case "mode":
                        var mode = (kv.Count > 0 ? kv.Values.First() : arg).ToLowerInvariant();
                        if (new[] { "pacman", "snake", "bounce", "off" }.Contains(mode))
                            await PicoLink.SetGameModeAsync(mode);
                        break;

                    case "browse":
                        var url = kv.TryGetValue("url", out var u) ? u : arg;
                        Report($"AI browsing {url}");
                        var page = await WebReader.StreamToDeviceAsync(url);
                        await PicoLink.SetLayoutAsync("stage", "text");
                        BridgeServer.PushChat("ionity-ai", $"Put \"{page.Title}\" on the stage.");
                        break;
                }
            }
            catch (Exception ex)
            {
                Report($"Directive [{verb}] failed: {ex.Message}");
                BridgeServer.PushChat("ionity-ai", $"Could not {verb}: {ex.Message}");
            }
        }
    }

    public static void Clear()
    {
        History.Clear();
        Report("Conversation cleared");
    }

    public static object Snapshot() => new
    {
        turns = History.Count / 2,
        brain = LocalBrain.Snapshot(),
    };
}
