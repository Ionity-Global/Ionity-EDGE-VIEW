using System.Net;
using System.Text;
using System.Text.RegularExpressions;

namespace EdgeView.Core;

/// <summary>
/// Fetches a page and reduces it to something a 640x480 3-bit panel can show.
/// The device has no TLS and no HTML parser, so all of this happens here and
/// only the digest is streamed across.
/// </summary>
public static partial class WebReader
{
    private static readonly HttpClient Http = CreateClient();

    private static HttpClient CreateClient()
    {
        var c = new HttpClient(new HttpClientHandler { AllowAutoRedirect = true, MaxAutomaticRedirections = 5 })
        {
            Timeout = TimeSpan.FromSeconds(20),
            MaxResponseContentBufferSize = 4 * 1024 * 1024,
        };
        c.DefaultRequestHeaders.UserAgent.ParseAdd($"IonityEdgeView/{EdgeViewInfo.Version} (+https://ionity.co.za)");
        return c;
    }

    public sealed record Page(string Url, string Title, string Text, List<string> Headlines);

    [GeneratedRegex(@"<(script|style|noscript|svg|head)[^>]*>.*?</\1>", RegexOptions.Singleline | RegexOptions.IgnoreCase)]
    private static partial Regex BlockTags();

    [GeneratedRegex(@"<h[1-3][^>]*>(.*?)</h[1-3]>", RegexOptions.Singleline | RegexOptions.IgnoreCase)]
    private static partial Regex Headings();

    [GeneratedRegex(@"<[^>]+>")]
    private static partial Regex AnyTag();

    [GeneratedRegex(@"\s+")]
    private static partial Regex Whitespace();

    /// <summary>
    /// Only http/https, and never to a private address — the AI can be steered
    /// by anyone in the chat, so it must not be usable to probe the LAN.
    /// </summary>
    private static async Task<Uri> ValidateAsync(string url)
    {
        if (!Uri.TryCreate(url.Trim(), UriKind.Absolute, out var uri))
            throw new InvalidOperationException("that is not a URL");
        if (uri.Scheme != Uri.UriSchemeHttp && uri.Scheme != Uri.UriSchemeHttps)
            throw new InvalidOperationException("only http and https are allowed");

        IPAddress[] addresses;
        try { addresses = await Dns.GetHostAddressesAsync(uri.Host); }
        catch { throw new InvalidOperationException($"cannot resolve {uri.Host}"); }

        if (addresses.Length == 0) throw new InvalidOperationException($"cannot resolve {uri.Host}");
        if (addresses.Any(IsPrivate))
            throw new InvalidOperationException("refusing to browse a private address");

        return uri;
    }

    private static bool IsPrivate(IPAddress ip)
    {
        if (IPAddress.IsLoopback(ip)) return true;
        if (ip.IsIPv6LinkLocal || ip.IsIPv6SiteLocal) return true;

        var b = ip.GetAddressBytes();
        if (b.Length != 4) return false;
        return b[0] switch
        {
            10 => true,
            127 => true,
            169 when b[1] == 254 => true,
            172 when b[1] >= 16 && b[1] <= 31 => true,
            192 when b[1] == 168 => true,
            0 or 255 => true,
            _ => false,
        };
    }

    public static async Task<Page> ReadAsync(string url, CancellationToken ct = default)
    {
        var uri = await ValidateAsync(url);
        var html = await Http.GetStringAsync(uri, ct);

        var title = Regex.Match(html, @"<title[^>]*>(.*?)</title>",
                                RegexOptions.Singleline | RegexOptions.IgnoreCase);
        var pageTitle = title.Success ? Clean(title.Groups[1].Value) : uri.Host;

        var stripped = BlockTags().Replace(html, " ");

        var headlines = Headings().Matches(stripped)
            .Select(m => Clean(m.Groups[1].Value))
            .Where(h => h.Length is > 8 and < 160)
            .Distinct()
            .Take(12)
            .ToList();

        var text = Clean(AnyTag().Replace(stripped, " "));
        if (text.Length > 4000) text = text[..4000];

        return new Page(uri.ToString(), pageTitle, text, headlines);
    }

    private static string Clean(string s) =>
        Whitespace().Replace(WebUtility.HtmlDecode(s) ?? "", " ").Trim();

    /// <summary>
    /// Fetch a page and stream it onto the display: title into the header,
    /// a digest into the text panel, headings into the marquee.
    /// </summary>
    public static async Task<Page> StreamToDeviceAsync(string url, CancellationToken ct = default)
    {
        var page = await ReadAsync(url, ct);

        var body = page.Headlines.Count > 0
            ? string.Join("  |  ", AiEngine.Curate(page.Headlines, 5))
            : Summarise(page.Text, 320);

        await PicoLink.SetDataAsync("text.title", Cap(page.Title, 40));
        await PicoLink.SetDataAsync("text.body", Cap(body, 180));
        await PicoLink.SetDataAsync("hdr.sub", Cap(new Uri(page.Url).Host.ToUpperInvariant(), 40));

        if (page.Headlines.Count > 0)
            await PicoLink.SetDataAsync("marquee",
                Cap(string.Join("   \u2022   ", AiEngine.Curate(page.Headlines, 6)), 180));

        return page;
    }

    /// <summary>Crude extractive summary: the longest sentences carry the most.</summary>
    public static string Summarise(string text, int max)
    {
        var sentences = Regex.Split(text, @"(?<=[.!?])\s+")
                             .Select(s => s.Trim())
                             .Where(s => s.Length is > 40 and < 300)
                             .Take(40)
                             .ToList();
        if (sentences.Count == 0) return Cap(text, max);

        var sb = new StringBuilder();
        foreach (var s in sentences.OrderByDescending(s => s.Length))
        {
            if (sb.Length + s.Length + 1 > max) continue;
            sb.Append(s).Append(' ');
            if (sb.Length > max * 0.8) break;
        }
        return sb.Length > 0 ? sb.ToString().Trim() : Cap(text, max);
    }

    private static string Cap(string s, int max)
    {
        s = Whitespace().Replace(s, " ").Trim();
        // The panel font is ASCII only.
        var ascii = new string(s.Where(c => c is >= ' ' and < (char)127).ToArray());
        return ascii.Length <= max ? ascii : ascii[..(max - 1)] + "\u2026";
    }
}
