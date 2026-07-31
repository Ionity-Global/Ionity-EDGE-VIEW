using System.Reflection;
using EdgeView.Core;
using Microsoft.Web.WebView2.WinForms;
using Microsoft.Web.WebView2.Core;

namespace EdgeView.Host;

/// <summary>
/// The application window. The server runs inside this process and the console
/// is shown in an embedded browser view, so one executable is the whole thing:
/// no terminal, no separate browser, nothing to install.
/// </summary>
public sealed class MainWindow : Form
{
    private readonly WebView2 _web = new() { Dock = DockStyle.Fill };
    private readonly Panel _splash = new() { Dock = DockStyle.Fill, BackColor = Color.FromArgb(10, 10, 15) };
    private readonly Label _status = new()
    {
        Dock = DockStyle.Bottom,
        Height = 24,
        ForeColor = Color.FromArgb(138, 138, 160),
        BackColor = Color.FromArgb(10, 10, 15),
        TextAlign = ContentAlignment.MiddleLeft,
        Padding = new Padding(10, 0, 0, 0),
        Font = new Font("Consolas", 8.5f),
    };

    public MainWindow()
    {
        Text = "IO-nity EDGE-VIEW";
        BackColor = Color.FromArgb(10, 10, 15);
        MinimumSize = new Size(1024, 700);
        Size = new Size(1500, 1000);
        StartPosition = FormStartPosition.CenterScreen;
        Icon = LoadIcon();

        BuildSplash();
        Controls.Add(_status);
        Controls.Add(_splash);
        _status.Text = "starting…";

        BridgeServer.Log += Trace;
        FeedService.Log += Trace;
        AiEngine.Log += Trace;
        ChatService.Log += Trace;
        MirrorRelay.Log += Trace;

        Shown += async (_, _) => await StartAsync();
        FormClosing += (_, _) => Shutdown();
    }

    private static Icon LoadIcon()
    {
        using var s = Assembly.GetExecutingAssembly().GetManifestResourceStream("EdgeView.Icon.ico");
        return s != null ? new Icon(s) : SystemIcons.Application;
    }

    private void BuildSplash()
    {
        var logo = new PictureBox
        {
            SizeMode = PictureBoxSizeMode.Zoom,
            Size = new Size(220, 220),
            Anchor = AnchorStyles.None,
            Image = LoadIcon().ToBitmap(),
        };
        var title = new Label
        {
            Text = "IO-nity EDGE-VIEW",
            ForeColor = Color.FromArgb(233, 69, 96),
            Font = new Font("Segoe UI", 22f, FontStyle.Bold),
            AutoSize = true,
            Anchor = AnchorStyles.None,
        };
        var sub = new Label
        {
            Text = "Ionity Global (Pty) Ltd",
            ForeColor = Color.FromArgb(138, 138, 160),
            Font = new Font("Segoe UI", 10f),
            AutoSize = true,
            Anchor = AnchorStyles.None,
        };

        var stack = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 5,
            BackColor = Color.FromArgb(10, 10, 15),
        };
        stack.RowStyles.Add(new RowStyle(SizeType.Percent, 50));
        stack.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        stack.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        stack.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        stack.RowStyles.Add(new RowStyle(SizeType.Percent, 50));
        stack.Controls.Add(new Panel { Dock = DockStyle.Fill }, 0, 0);
        stack.Controls.Add(logo, 0, 1);
        stack.Controls.Add(title, 0, 2);
        stack.Controls.Add(sub, 0, 3);
        logo.Anchor = AnchorStyles.None;
        title.Anchor = AnchorStyles.None;
        sub.Anchor = AnchorStyles.None;
        _splash.Controls.Add(stack);
    }

    private void Trace(string line)
    {
        if (IsDisposed) return;
        try
        {
            if (InvokeRequired) BeginInvoke(() => _status.Text = line);
            else _status.Text = line;
        }
        catch { /* window is going away */ }
    }

    private async Task StartAsync()
    {
        PicoLink.StartDiscovery();
        BridgeServer.Start();

        if (!BridgeServer.IsRunning)
        {
            MessageBox.Show(
                $"Port {BridgeServer.Port} is already in use.\n\nAnother copy of EDGE-VIEW is probably running.",
                "EDGE-VIEW", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            Close();
            return;
        }

        if (Settings.Get("feeds_autostart", true)) FeedService.Start();
        if (Settings.Get("ai_autostart", true)) AiEngine.Start();

        Controls.Add(_web);
        _web.BringToFront();

        try
        {
            var userData = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "IonityEdgeView", "webview");
            Directory.CreateDirectory(userData);

            var env = await CoreWebView2Environment.CreateAsync(null, userData);
            await _web.EnsureCoreWebView2Async(env);

            _web.CoreWebView2.Settings.AreDefaultContextMenusEnabled = false;
            _web.CoreWebView2.Settings.IsStatusBarEnabled = false;
            _web.DefaultBackgroundColor = Color.FromArgb(10, 10, 15);

            // Hand the pairing token to the console so nobody has to copy it.
            _web.CoreWebView2.AddScriptToExecuteOnDocumentCreatedAsync(
                $"localStorage.setItem('edgeview.token', '{BridgeServer.Token}');");

            _web.Source = new Uri($"http://127.0.0.1:{BridgeServer.Port}/");
            _web.NavigationCompleted += (_, _) => { _splash.Visible = false; _web.Focus(); };
        }
        catch (Exception ex)
        {
            _splash.Visible = true;
            MessageBox.Show(
                "The WebView2 runtime is missing, so the console cannot be shown in this window.\n\n" +
                $"The server is still running — open http://127.0.0.1:{BridgeServer.Port}/ in a browser.\n\n" +
                $"Details: {ex.Message}",
                "EDGE-VIEW", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }

        Trace($"ready · http://127.0.0.1:{BridgeServer.Port}/ · device {PicoLink.DeviceIp}");
    }

    private void Shutdown()
    {
        AiEngine.Stop();
        FeedService.Stop();
        BridgeServer.Stop();
    }
}
