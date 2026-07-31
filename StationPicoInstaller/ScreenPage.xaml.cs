using System.Text.Json;
using StationPicoInstaller.Services;

namespace StationPicoInstaller;

public partial class ScreenPage : ContentPage
{
    private static readonly Dictionary<string, string> DefaultLayout = new()
    {
        ["header"] = "header",
        ["info_a"] = "network",
        ["info_b"] = "feed",
        ["info_c"] = "weather",
        ["stage"] = "game",
        ["side"] = "rotate",
        ["marquee"] = "marquee",
        ["ticker"] = "news",
        ["stats"] = "stats",
        ["footer"] = "footer",
    };

    private readonly Dictionary<string, Picker> _pickers = new();

    public ScreenPage()
    {
        InitializeComponent();
        BuildSlotRows();
    }

    private void BuildSlotRows()
    {
        foreach (var slot in PicoLink.Slots)
        {
            var grid = new Grid
            {
                ColumnDefinitions =
                {
                    new ColumnDefinition { Width = new GridLength(110) },
                    new ColumnDefinition { Width = GridLength.Star },
                },
                ColumnSpacing = 8,
            };

            grid.Add(new Label
            {
                Text = slot,
                TextColor = Color.FromArgb("#00bcd4"),
                FontFamily = "Consolas",
                VerticalOptions = LayoutOptions.Center,
            }, 0);

            var picker = new Picker
            {
                TextColor = Color.FromArgb("#eee"),
                TitleColor = Color.FromArgb("#666"),
                BackgroundColor = Color.FromArgb("#0f3460"),
            };
            foreach (var p in PicoLink.Panels) picker.Items.Add(p);
            picker.SelectedItem = DefaultLayout.TryGetValue(slot, out var d) ? d : "blank";
            picker.SelectedIndexChanged += async (_, _) => await ApplySlotAsync(slot, picker);

            grid.Add(picker, 1);
            _pickers[slot] = picker;
            SlotList.Add(grid);
        }
    }

    private void Log(string msg) =>
        LogLabel.Text = $"{DateTime.Now:HH:mm:ss}  {msg}\n{LogLabel.Text}";

    private async Task ApplySlotAsync(string slot, Picker picker)
    {
        var panel = picker.SelectedItem as string;
        if (string.IsNullOrEmpty(panel)) return;
        var ok = await PicoLink.SetLayoutAsync(slot, panel);
        Log(ok ? $"{slot} -> {panel}" : $"FAILED {slot} -> {panel}");
    }

    private async void OnApplyAllClicked(object? sender, EventArgs e)
    {
        foreach (var (slot, picker) in _pickers)
        {
            if (picker.SelectedItem is string panel)
            {
                await PicoLink.SetLayoutAsync(slot, panel);
                await Task.Delay(40);
            }
        }
        Log("Applied all slot bindings.");
    }

    private void OnDefaultLayoutClicked(object? sender, EventArgs e)
    {
        foreach (var (slot, panel) in DefaultLayout)
            if (_pickers.TryGetValue(slot, out var p)) p.SelectedItem = panel;
        Log("Reset to default layout (applied as each picker changed).");
    }

    private async void OnQueryClicked(object? sender, EventArgs e)
    {
        var reply = await PicoLink.QueryLayoutAsync();
        if (reply == null)
        {
            Log("Device did not answer. Check the IP on the Control tab.");
            return;
        }
        try
        {
            using var doc = JsonDocument.Parse(reply);
            if (doc.RootElement.TryGetProperty("slots", out var slots))
            {
                foreach (var s in slots.EnumerateObject())
                {
                    if (_pickers.TryGetValue(s.Name, out var picker))
                    {
                        var v = s.Value.GetString();
                        if (v != null && picker.Items.Contains(v)) picker.SelectedItem = v;
                    }
                }
                Log("Layout read back from device.");
            }
        }
        catch
        {
            Log($"Unparsed reply: {reply.Trim()}");
        }
    }

    private async void OnSendDataClicked(object? sender, EventArgs e)
    {
        var key = KeyEntry.Text?.Trim();
        if (string.IsNullOrEmpty(key)) { Log("Enter a key first."); return; }
        var ok = await PicoLink.SetDataAsync(key, ValueEntry.Text ?? "");
        Log(ok ? $"data {key} = {ValueEntry.Text}" : $"FAILED data {key}");
    }

    private async void OnSetWifiClicked(object? sender, EventArgs e)
    {
        var ssid = SsidEntry.Text?.Trim();
        if (string.IsNullOrEmpty(ssid)) { Log("Enter the new SSID first."); return; }

        var confirm = await DisplayAlertAsync("Move device to a new network?",
            $"The device will save \"{ssid}\", reboot and rejoin on that network. " +
            "If the credentials are wrong you will need physical access to recover it.",
            "Move it", "Cancel");
        if (!confirm) return;

        WifiBtn.IsEnabled = false;
        var ok = await PicoLink.SetWifiAsync(ssid, PassEntry.Text ?? "");
        Log(ok ? $"WiFi set to {ssid}; device rebooting." : "Failed to send WiFi credentials.");
        PassEntry.Text = "";
        WifiBtn.IsEnabled = true;
    }

    private async void OnResetWifiClicked(object? sender, EventArgs e)
    {
        var confirm = await DisplayAlertAsync("Forget saved WiFi?",
            "The device erases its stored credentials and reboots. If no network was compiled into the " +
            "firmware it will come back on its own setup access point, which means you must be within " +
            "WiFi range to recover it.",
            "Forget it", "Cancel");
        if (!confirm) return;

        WifiResetBtn.IsEnabled = false;
        var ok = await PicoLink.ResetWifiAsync();
        Log(ok ? "WiFi credentials cleared; device rebooting." : "Failed to clear WiFi credentials.");
        WifiResetBtn.IsEnabled = true;
    }
}
