using EdgeView.Core;

namespace EdgeView.Host;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        var dataDir = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "IonityEdgeView");
        Settings.Store = new JsonFileStore(Path.Combine(dataDir, "settings.json"));

        // A "web" folder beside the exe overrides the built-in console and is
        // where firmware UF2s go so the browser flasher can find them.
        var diskWeb = Path.Combine(AppContext.BaseDirectory, "web");
        if (Directory.Exists(diskWeb)) WebAssets.DiskRoot = diskWeb;

        ApplyArgs(args);

        if (args.Any(a => a is "--headless" or "-h" or "--help" or "/?"))
        {
            Headless.Run(args);
            return;
        }

        ApplicationConfiguration.Initialize();
        Application.Run(new MainWindow());
    }

    private static void ApplyArgs(string[] args)
    {
        for (int i = 0; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--device" when i + 1 < args.Length: PicoLink.DeviceIp = args[++i]; break;
                case "--token" when i + 1 < args.Length: Settings.Set("bridge_token", args[++i]); break;
                case "--new-token": BridgeServer.NewToken(); break;
                case "--origins" when i + 1 < args.Length: Settings.Set("bridge_origins", args[++i]); break;
                case "--no-feeds": Settings.Set("feeds_autostart", false); break;
                case "--no-ai": Settings.Set("ai_autostart", false); break;
                case "--autopilot": AiEngine.Autopilot = true; break;
                case "--gist" when i + 1 < args.Length:
                    AiEngine.GistSource = args[++i];
                    AiEngine.GistAuto = true;
                    break;
            }
        }
    }
}

/// <summary>Console mode, for running it on a machine with no desktop.</summary>
internal static class Headless
{
    public static void Run(string[] args)
    {
        AllocConsole();

        if (args.Any(a => a is "-h" or "--help" or "/?"))
        {
            Console.WriteLine("""
              IO-nity EDGE-VIEW

                EdgeView.exe               Open the application window
                EdgeView.exe --headless    Run the server with no window

                --device <ip>      Device address (otherwise found by beacon)
                --token <value>    Set the pairing token
                --new-token        Generate a fresh pairing token
                --origins <list>   Comma-separated allowed browser origins
                --no-feeds         Do not start the feed scheduler
                --no-ai            Do not start the AI engine
                --autopilot        Let the AI choose scenes by time of day
                --gist <id|url>    Poll a Gist for inbound directives
              """);
            return;
        }

        BridgeServer.Log += Console.WriteLine;
        FeedService.Log += Console.WriteLine;
        AiEngine.Log += Console.WriteLine;

        PicoLink.StartDiscovery();
        BridgeServer.Start();
        if (!BridgeServer.IsRunning)
        {
            Console.Error.WriteLine($"Port {BridgeServer.Port} is already in use.");
            return;
        }

        if (Settings.Get("feeds_autostart", true)) FeedService.Start();
        if (Settings.Get("ai_autostart", true)) AiEngine.Start();

        Console.WriteLine($"  Console  http://127.0.0.1:{BridgeServer.Port}/");
        Console.WriteLine($"  Token    {BridgeServer.Token}");
        Console.WriteLine("  Ctrl+C to stop.");

        using var quit = new ManualResetEventSlim(false);
        Console.CancelKeyPress += (_, e) => { e.Cancel = true; quit.Set(); };
        quit.Wait();

        AiEngine.Stop();
        FeedService.Stop();
        BridgeServer.Stop();
    }

    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    private static extern bool AllocConsole();
}
