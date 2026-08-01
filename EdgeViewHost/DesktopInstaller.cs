using System.Diagnostics;
using Microsoft.Win32;

namespace EdgeView.Host;

internal static class DesktopInstaller
{
    private const string ProductName = "IO-nity EDGE-VIEW";
    private const string Scheme = "edgeview";

    public static bool HandleCommand(string[] args)
    {
        var quiet = args.Any(arg => arg.Equals("--quiet", StringComparison.OrdinalIgnoreCase));
        if (args.Any(arg => arg.Equals("--uninstall", StringComparison.OrdinalIgnoreCase)))
        {
            Uninstall(quiet);
            return true;
        }

        var processName = Path.GetFileNameWithoutExtension(Environment.ProcessPath ?? "");
        if (processName.StartsWith("EdgeView-Setup", StringComparison.OrdinalIgnoreCase) ||
            args.Any(arg => arg.Equals("--install", StringComparison.OrdinalIgnoreCase)))
        {
            InstallAndLaunch(quiet);
            return true;
        }

        return false;
    }

    private static void InstallAndLaunch(bool quiet)
    {
        ApplicationConfiguration.Initialize();
        try
        {
            var source = Environment.ProcessPath ?? throw new InvalidOperationException("Installer path is unavailable.");
            var installDir = InstallDirectory();
            var target = Path.Combine(installDir, "EdgeView.exe");

            Directory.CreateDirectory(installDir);
            StopRunningCopies();

            if (!Path.GetFullPath(source).Equals(Path.GetFullPath(target), StringComparison.OrdinalIgnoreCase))
            {
                var staged = target + ".new";
                File.Copy(source, staged, true);
                File.Move(staged, target, true);
            }

            RegisterApplication(target);
            WriteStartMenuShortcut(target);

            Process.Start(new ProcessStartInfo(target) { UseShellExecute = true });
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                $"Installation failed.\n\n{ex.Message}",
                ProductName, MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private static void Uninstall(bool quiet)
    {
        ApplicationConfiguration.Initialize();
        try
        {
            StopRunningCopies();
            Registry.CurrentUser.DeleteSubKeyTree($@"Software\Classes\{Scheme}", false);
            Registry.CurrentUser.DeleteSubKeyTree(
                @"Software\Microsoft\Windows\CurrentVersion\Uninstall\IonityEdgeView", false);
            File.Delete(StartMenuShortcut());

            var executable = Environment.ProcessPath ?? Path.Combine(InstallDirectory(), "EdgeView.exe");
            var command = $"/d /c ping 127.0.0.1 -n 3 >nul & del /f /q \"{executable}\" & rmdir \"{InstallDirectory()}\"";
            Process.Start(new ProcessStartInfo("cmd.exe", command)
            {
                CreateNoWindow = true,
                UseShellExecute = false,
            });
            if (!quiet) MessageBox.Show($"{ProductName} was removed.", ProductName);
        }
        catch (Exception ex)
        {
            MessageBox.Show($"Uninstall failed.\n\n{ex.Message}", ProductName);
        }
    }

    private static void StopRunningCopies()
    {
        foreach (var name in new[] { "EdgeView", "EdgeViewHost" })
        {
            foreach (var process in Process.GetProcessesByName(name))
            {
                if (process.Id == Environment.ProcessId) continue;
                try
                {
                    if (process.CloseMainWindow() && process.WaitForExit(3000)) continue;
                    process.Kill(true);
                    process.WaitForExit(3000);
                }
                catch { }
            }
        }
    }

    private static void RegisterApplication(string executable)
    {
        using (var root = Registry.CurrentUser.CreateSubKey($@"Software\Classes\{Scheme}"))
        {
            root?.SetValue("", $"URL:{ProductName}");
            root?.SetValue("URL Protocol", "");
            root?.CreateSubKey("DefaultIcon")?.SetValue("", $"\"{executable}\",0");
            root?.CreateSubKey(@"shell\open\command")?.SetValue("", $"\"{executable}\" \"%1\"");
        }

        using var uninstall = Registry.CurrentUser.CreateSubKey(
            @"Software\Microsoft\Windows\CurrentVersion\Uninstall\IonityEdgeView");
        uninstall?.SetValue("DisplayName", ProductName);
        uninstall?.SetValue("DisplayIcon", executable);
        uninstall?.SetValue("DisplayVersion", FileVersionInfo.GetVersionInfo(executable).ProductVersion ?? "2.2.0");
        uninstall?.SetValue("InstallLocation", InstallDirectory());
        uninstall?.SetValue("Publisher", "Ionity Global (Pty) Ltd");
        uninstall?.SetValue("UninstallString", $"\"{executable}\" --uninstall");
        uninstall?.SetValue("NoModify", 1, RegistryValueKind.DWord);
        uninstall?.SetValue("NoRepair", 1, RegistryValueKind.DWord);
    }

    private static void WriteStartMenuShortcut(string executable)
    {
        var content = $"[InternetShortcut]\r\nURL={Scheme}://start\r\nIconFile={executable}\r\nIconIndex=0\r\n";
        File.WriteAllText(StartMenuShortcut(), content);
    }

    private static string InstallDirectory() => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "Programs", "Ionity EDGE-VIEW");

    private static string StartMenuShortcut() => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.Programs),
        $"{ProductName}.url");
}