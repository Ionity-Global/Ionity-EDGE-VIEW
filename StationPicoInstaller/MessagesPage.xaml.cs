using System.Text.Json;
using StationPicoInstaller.Services;

namespace StationPicoInstaller;

public partial class MessagesPage : ContentPage
{
    public MessagesPage()
    {
        InitializeComponent();
        AuthorEntry.Text = Preferences.Get("msg_author", Environment.UserName);
        UpdateShareUrl();
    }

    protected override void OnAppearing()
    {
        base.OnAppearing();
        UpdateShareUrl();
        _ = LoadMessagesAsync();
    }

    private void UpdateShareUrl() => ShareUrlLabel.Text = $"http://{PicoLink.DeviceIp}/";

    private async void OnSendClicked(object? sender, EventArgs e)
    {
        var author = AuthorEntry.Text?.Trim() ?? "";
        var text = TextEditor.Text?.Trim() ?? "";
        if (text.Length == 0)
        {
            SendStatus.Text = "Enter a message first.";
            return;
        }
        if (author.Length == 0) author = "Studio";
        Preferences.Set("msg_author", author);

        SendBtn.IsEnabled = false;
        SendStatus.Text = "Sending...";
        try
        {
            var ok = await PicoLink.PostMessageAsync(author, text);
            SendStatus.Text = ok
                ? $"✅ Sent to {PicoLink.DeviceIp} at {DateTime.Now:HH:mm:ss}"
                : "❌ Send failed — check the device IP on the Control tab.";
            if (ok)
            {
                TextEditor.Text = "";
                await LoadMessagesAsync();
            }
        }
        finally
        {
            SendBtn.IsEnabled = true;
        }
    }

    private async void OnRefreshClicked(object? sender, EventArgs e)
    {
        UpdateShareUrl();
        await LoadMessagesAsync();
    }

    private async void OnCopyUrlClicked(object? sender, EventArgs e)
    {
        await Clipboard.SetTextAsync(ShareUrlLabel.Text);
        SendStatus.Text = "Link copied to clipboard.";
    }

    private async Task LoadMessagesAsync()
    {
        var json = await PicoLink.GetMessagesJsonAsync();
        if (json == null)
        {
            MessagesLabel.Text = "Device unreachable.";
            return;
        }
        try
        {
            using var doc = JsonDocument.Parse(json);
            var msgs = doc.RootElement.GetProperty("messages");
            if (msgs.GetArrayLength() == 0)
            {
                MessagesLabel.Text = "No messages on device yet.";
                return;
            }
            var lines = msgs.EnumerateArray()
                .Reverse()
                .Take(15)
                .Select(m =>
                {
                    var a = m.TryGetProperty("author", out var ae) ? ae.GetString() : "?";
                    var t = m.TryGetProperty("text", out var te) ? te.GetString() : "";
                    return $"• {a}: {t}";
                });
            MessagesLabel.Text = string.Join("\n", lines);
        }
        catch
        {
            MessagesLabel.Text = "Could not parse device response.";
        }
    }
}
