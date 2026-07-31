using StationPicoInstaller.Services;

namespace StationPicoInstaller;

public partial class FeedsPage : ContentPage
{
    private bool _loading = true;

    public FeedsPage()
    {
        InitializeComponent();
        VerseSwitch.IsToggled = Preferences.Get("feed_verse", true);
        NewsSwitch.IsToggled = Preferences.Get("feed_news", true);
        WeatherSwitch.IsToggled = Preferences.Get("feed_weather", true);
        TimeSwitch.IsToggled = Preferences.Get("feed_time", true);
        _loading = false;
        FeedService.Log += AppendLog;
        UpdateButtons();
    }

    private void AppendLog(string msg)
    {
        MainThread.BeginInvokeOnMainThread(() =>
        {
            LogLabel.Text += msg + "\n";
            _ = LogScroll.ScrollToAsync(0, double.MaxValue, false);
        });
    }

    private void OnToggled(object? sender, ToggledEventArgs e)
    {
        if (_loading) return;
        Preferences.Set("feed_verse", VerseSwitch.IsToggled);
        Preferences.Set("feed_news", NewsSwitch.IsToggled);
        Preferences.Set("feed_weather", WeatherSwitch.IsToggled);
        Preferences.Set("feed_time", TimeSwitch.IsToggled);
    }

    private void UpdateButtons()
    {
        StartBtn.IsEnabled = !FeedService.IsRunning;
        StopBtn.IsEnabled = FeedService.IsRunning;
    }

    private void OnStartClicked(object? sender, EventArgs e)
    {
        FeedService.Start();
        UpdateButtons();
    }

    private void OnStopClicked(object? sender, EventArgs e)
    {
        FeedService.Stop();
        UpdateButtons();
    }

    private async void OnPushNowClicked(object? sender, EventArgs e)
    {
        PushNowBtn.IsEnabled = false;
        try
        {
            AppendLog($"[{DateTime.Now:HH:mm:ss}] Manual push to {PicoLink.DeviceIp}...");

            if (TimeSwitch.IsToggled && await PicoLink.SendTimeAsync())
                AppendLog($"[{DateTime.Now:HH:mm:ss}] Time synced");

            if (VerseSwitch.IsToggled)
            {
                var v = await FeedService.FetchVerseAsync();
                if (v != null && await PicoLink.SendVerseAsync(v.Value.Reference, v.Value.Text))
                    AppendLog($"[{DateTime.Now:HH:mm:ss}] Verse pushed: {v.Value.Reference}");
            }

            if (NewsSwitch.IsToggled)
            {
                var items = await FeedService.FetchAiNewsAsync();
                int sent = 0;
                foreach (var h in items)
                    if (await PicoLink.SendNewsAsync(h)) sent++;
                AppendLog($"[{DateTime.Now:HH:mm:ss}] AI news pushed: {sent} headlines");
            }

            if (WeatherSwitch.IsToggled)
            {
                var w = await FeedService.FetchWeatherAsync();
                if (w != null && await PicoLink.SendWeatherAsync(w.Value.TempC, w.Value.Condition))
                    AppendLog($"[{DateTime.Now:HH:mm:ss}] Weather pushed: {w.Value.TempC}C");
            }

            AppendLog($"[{DateTime.Now:HH:mm:ss}] Manual push complete");
        }
        finally
        {
            PushNowBtn.IsEnabled = true;
        }
    }
}
