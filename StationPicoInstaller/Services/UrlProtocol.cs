#if WINDOWS
using Microsoft.Win32;

namespace StationPicoInstaller.Services;

/// <summary>
/// Registers the <c>edgeview://</c> URL scheme under HKCU so the web console's
/// "Start server" button can launch Studio. Per-user only — no elevation.
/// </summary>
public static class UrlProtocol
{
    public const string Scheme = "edgeview";

    public static void Register()
    {
        try
        {
            var exe = Environment.ProcessPath;
            if (string.IsNullOrEmpty(exe)) return;

            using var root = Registry.CurrentUser.CreateSubKey($@"Software\Classes\{Scheme}");
            if (root == null) return;
            root.SetValue("", "URL:IO-nity EDGE-VIEW");
            root.SetValue("URL Protocol", "");

            using (var icon = root.CreateSubKey("DefaultIcon"))
                icon?.SetValue("", $"\"{exe}\",0");

            using var cmd = root.CreateSubKey(@"shell\open\command");
            cmd?.SetValue("", $"\"{exe}\" \"%1\"");
        }
        catch
        {
            // A locked-down profile just means the console falls back to the
            // download link; not worth interrupting startup over.
        }
    }
}
#endif
