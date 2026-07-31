namespace StationPicoInstaller;

public partial class AboutPage : ContentPage
{
    public AboutPage()
    {
        InitializeComponent();
        VersionLabel.Text = $"Version {AppInfo.VersionString} · build {AppInfo.BuildString}";
    }
}
