using Microsoft.Extensions.DependencyInjection;

namespace StationPicoInstaller;

public partial class App : Application
{
	public App()
	{
		InitializeComponent();
		EdgeView.Core.Settings.Store = new Services.PreferencesStore();
#if WINDOWS
		Services.UrlProtocol.Register();
		// Launched from the web console's "Start server" button.
		if (Environment.GetCommandLineArgs().Any(a => a.StartsWith("edgeview://", StringComparison.OrdinalIgnoreCase)))
			EdgeView.Core.BridgeServer.Start();
#endif
	}

	protected override Window CreateWindow(IActivationState? activationState)
	{
		return new Window(new AppShell()) { Title = "IO-nity EDGE-VIEW Studio" };
	}
}