using StationPicoInstaller.Services;

namespace StationPicoInstaller;

public partial class ControlPage : ContentPage
{
    public ControlPage()
    {
        InitializeComponent();
        IpEntry.Text = PicoLink.DeviceIp;
        TokenEntry.Text = BridgeServer.Token;
        BridgeSwitch.IsToggled = BridgeServer.IsRunning;
        BridgeServer.Log += AppendLog;
        PicoLink.DeviceDiscovered += OnDeviceDiscovered;
        PicoLink.StartDiscovery();
    }

    // ---- Web console bridge ----

    private void OnBridgeToggled(object? sender, ToggledEventArgs e)
    {
        if (e.Value) BridgeServer.Start(); else BridgeServer.Stop();
        BridgeStatus.Text = BridgeServer.IsRunning
            ? $"Listening on http://127.0.0.1:{BridgeServer.Port}"
            : "Bridge off";
        BridgeStatus.TextColor = Color.FromArgb(BridgeServer.IsRunning ? "#44ff44" : "#888");
        if (e.Value && !BridgeServer.IsRunning) BridgeSwitch.IsToggled = false;
    }

    private async void OnCopyTokenClicked(object? sender, EventArgs e)
    {
        await Clipboard.SetTextAsync(BridgeServer.Token);
        AppendLog("Pairing token copied to clipboard");
    }

    private void OnNewTokenClicked(object? sender, EventArgs e)
    {
        TokenEntry.Text = BridgeServer.NewToken();
        AppendLog("New pairing token generated — re-pair the web console");
    }

    private async void OnOpenConsoleClicked(object? sender, EventArgs e)
    {
        if (!BridgeServer.IsRunning) BridgeSwitch.IsToggled = true;
        await Launcher.OpenAsync("https://ionity-global.github.io/Ionity-EDGE-VIEW/app/");
    }

    private void OnDeviceDiscovered(string name, string ip)
    {
        MainThread.BeginInvokeOnMainThread(() =>
        {
            DeviceStatus.Text = $"✅ Discovered: {name} @ {ip}";
            DeviceStatus.TextColor = Color.FromArgb("#44ff44");
            if (string.IsNullOrWhiteSpace(IpEntry.Text) || IpEntry.Text != ip)
            {
                IpEntry.Text = ip;
                PicoLink.DeviceIp = ip;
            }
        });
    }

    private void AppendLog(string msg)
    {
        MainThread.BeginInvokeOnMainThread(() =>
        {
            LogLabel.Text += $"[{DateTime.Now:HH:mm:ss}] {msg}\n";
            _ = LogScroll.ScrollToAsync(0, double.MaxValue, false);
        });
    }

    private void OnSaveIpClicked(object? sender, EventArgs e)
    {
        var ip = IpEntry.Text?.Trim() ?? "";
        if (ip.Length == 0) return;
        PicoLink.DeviceIp = ip;
        AppendLog($"Device IP saved: {ip}");
    }

    private async void OnPingClicked(object? sender, EventArgs e)
    {
        OnSaveIpClicked(sender, e);
        AppendLog($"Pinging {PicoLink.DeviceIp}:{PicoLink.StreamPort}...");
        var ok = await PicoLink.PingAsync();
        AppendLog(ok ? "✅ PONG — device online" : "❌ No reply (check IP / WiFi)");
    }

    private async Task SetMode(string mode)
    {
        var ok = await PicoLink.SetGameModeAsync(mode);
        AppendLog(ok ? $"✅ Game mode → {mode.ToUpperInvariant()}" : $"❌ Failed to set mode {mode}");
    }

    private async void OnModePacmanClicked(object? s, EventArgs e) => await SetMode("pacman");
    private async void OnModeSnakeClicked(object? s, EventArgs e) => await SetMode("snake");
    private async void OnModeBounceClicked(object? s, EventArgs e) => await SetMode("bounce");
    private async void OnModeOffClicked(object? s, EventArgs e) => await SetMode("off");

    private async void OnSyncTimeClicked(object? sender, EventArgs e)
    {
        var ok = await PicoLink.SendTimeAsync();
        AppendLog(ok ? "✅ Time pushed (UTC epoch)" : "❌ Time sync failed");
    }

    private async void OnRebootClicked(object? sender, EventArgs e)
    {
        var confirm = await DisplayAlertAsync("Reboot", "Reboot the EDGE-VIEW device?", "Reboot", "Cancel");
        if (!confirm) return;
        var ok = await PicoLink.RebootAsync();
        AppendLog(ok ? "✅ Reboot command sent" : "❌ Reboot failed");
    }
}
