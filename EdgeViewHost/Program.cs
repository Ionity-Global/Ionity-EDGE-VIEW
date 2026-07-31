using EdgeView.Core;

namespace EdgeView.Host;

/// <summary>
/// The whole server in one executable: device link, discovery, feed scheduler,
/// AI engine and the web console. Run it and open the printed URL.
/// </summary>
internal static class Program
{
    private static int Main(string[] args)
    {
        if (args.Any(a => a is "-h" or "--help" or "/?"))
        {
            PrintHelp();
            return 0;
        }

        var dataDir = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "IonityEdgeView");
        Settings.Store = new JsonFileStore(Path.Combine(dataDir, "settings.json"));

        // A "web" folder beside the exe overrides the built-in console and is
        // where firmware UF2s go so the browser flasher can find them.
        var diskWeb = Path.Combine(AppContext.BaseDirectory, "web");
        if (Directory.Exists(diskWeb)) WebAssets.DiskRoot = diskWeb;

        ApplyArgs(args);

        Banner();

        BridgeServer.Log += Write;
        FeedService.Log += Write;
        AiEngine.Log += Write;
        PicoLink.DeviceDiscovered += (name, ip) => Write($"Discovered {name} at {ip}");

        PicoLink.StartDiscovery();
        BridgeServer.Start();
        if (!BridgeServer.IsRunning)
        {
            Console.Error.WriteLine($"Could not bind port {BridgeServer.Port}. Is another host already running?");
            return 1;
        }

        if (Settings.Get("feeds_autostart", true)) FeedService.Start();
        if (Settings.Get("ai_autostart", true)) AiEngine.Start();

        Console.WriteLine();
        Console.ForegroundColor = ConsoleColor.Cyan;
        Console.WriteLine($"  Console   http://127.0.0.1:{BridgeServer.Port}/");
        Console.WriteLine($"  Token     {BridgeServer.Token}");
        Console.ResetColor();
        Console.WriteLine($"  Device    {PicoLink.DeviceIp}");
        Console.WriteLine($"  Settings  {Path.Combine(dataDir, "settings.json")}");
        Console.WriteLine();
        Console.WriteLine("  Ctrl+C to stop.");
        Console.WriteLine();

        using var quit = new ManualResetEventSlim(false);
        Console.CancelKeyPress += (_, e) => { e.Cancel = true; quit.Set(); };
        AppDomain.CurrentDomain.ProcessExit += (_, _) => quit.Set();
        quit.Wait();

        Console.WriteLine();
        Write("Shutting down");
        AiEngine.Stop();
        FeedService.Stop();
        BridgeServer.Stop();
        return 0;
    }

    private static void ApplyArgs(string[] args)
    {
        for (int i = 0; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--device" when i + 1 < args.Length:
                    PicoLink.DeviceIp = args[++i];
                    break;
                case "--token" when i + 1 < args.Length:
                    Settings.Set("bridge_token", args[++i]);
                    break;
                case "--new-token":
                    BridgeServer.NewToken();
                    break;
                case "--origins" when i + 1 < args.Length:
                    Settings.Set("bridge_origins", args[++i]);
                    break;
                case "--no-feeds":
                    Settings.Set("feeds_autostart", false);
                    break;
                case "--no-ai":
                    Settings.Set("ai_autostart", false);
                    break;
                case "--autopilot":
                    AiEngine.Autopilot = true;
                    break;
                case "--gist" when i + 1 < args.Length:
                    AiEngine.GistSource = args[++i];
                    AiEngine.GistAuto = true;
                    break;
            }
        }
    }

    private static void Banner()
    {
        Console.ForegroundColor = ConsoleColor.Red;
        Console.WriteLine();
        Console.WriteLine("  IO-nity EDGE-VIEW Host");
        Console.ResetColor();
        Console.WriteLine($"  {EdgeViewInfo.Company} · v{EdgeViewInfo.Version}");
        Console.WriteLine("  ------------------------------------------------");
    }

    private static void PrintHelp()
    {
        Banner();
        Console.WriteLine("""
          The server that drives the display. It fetches the feeds, holds the
          device link, runs the AI and serves the web console.

            EdgeViewHost [options]

            --device <ip>      Device address (otherwise found by beacon)
            --token <value>    Set the pairing token
            --new-token        Generate a fresh pairing token
            --origins <list>   Comma-separated allowed browser origins
            --no-feeds         Do not start the feed scheduler
            --no-ai            Do not start the AI engine
            --autopilot        Let the AI choose scenes by time of day
            --gist <id|url>    Poll a Gist for inbound directives
            -h, --help         This text

          Drop firmware UF2s in a "web/firmware" folder beside the exe to flash
          them from the browser.
          """);
    }

    private static void Write(string line)
    {
        Console.WriteLine("  " + line);
    }
}
