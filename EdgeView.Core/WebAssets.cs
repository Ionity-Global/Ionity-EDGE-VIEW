using System.Reflection;

namespace EdgeView.Core;

/// <summary>
/// The web console, compiled into the executable. The host can serve the whole
/// UI with no files on disk, so a single exe is genuinely the whole server.
/// </summary>
public static class WebAssets
{
    private const string Prefix = "EdgeView.Web.";
    private static readonly Assembly Asm = typeof(WebAssets).Assembly;
    private static readonly Dictionary<string, string> Map = BuildMap();

    public static bool Any => Map.Count > 0;

    /// <summary>Optional folder checked before the embedded copy, so firmware
    /// UF2s and UI tweaks can be dropped next to the executable.</summary>
    public static string? DiskRoot { get; set; }

    private static Dictionary<string, string> BuildMap()
    {
        var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var name in Asm.GetManifestResourceNames())
        {
            if (!name.StartsWith(Prefix, StringComparison.Ordinal)) continue;
            // "EdgeView.Web.js.app.js" -> "js/app.js": only the last dot is the extension.
            var rel = name[Prefix.Length..];
            var lastDot = rel.LastIndexOf('.');
            var route = lastDot <= 0 ? rel : rel[..lastDot].Replace('.', '/') + rel[lastDot..];
            map["/" + route] = name;
        }
        return map;
    }

    public static (byte[] Bytes, string ContentType)? Get(string path)
    {
        if (string.IsNullOrEmpty(path) || path == "/") path = "/index.html";
        if (path.EndsWith('/')) path += "index.html";

        if (DiskRoot != null)
        {
            var rel = path.TrimStart('/').Replace('/', Path.DirectorySeparatorChar);
            var full = Path.GetFullPath(Path.Combine(DiskRoot, rel));
            // Refuse anything that escapes the root.
            if (full.StartsWith(Path.GetFullPath(DiskRoot), StringComparison.OrdinalIgnoreCase) && File.Exists(full))
                return (File.ReadAllBytes(full), ContentTypeFor(path));
        }

        if (!Map.TryGetValue(path, out var resource)) return null;

        using var s = Asm.GetManifestResourceStream(resource);
        if (s == null) return null;
        using var ms = new MemoryStream();
        s.CopyTo(ms);
        return (ms.ToArray(), ContentTypeFor(path));
    }

    private static string ContentTypeFor(string path) => Path.GetExtension(path).ToLowerInvariant() switch
    {
        ".html" => "text/html; charset=utf-8",
        ".css" => "text/css; charset=utf-8",
        ".js" => "text/javascript; charset=utf-8",
        ".json" => "application/json",
        ".svg" => "image/svg+xml",
        ".png" => "image/png",
        ".ico" => "image/x-icon",
        ".uf2" => "application/octet-stream",
        _ => "application/octet-stream",
    };
}
