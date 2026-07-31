using EdgeView.Core;

namespace StationPicoInstaller.Services;

/// <summary>Backs the shared settings with MAUI Preferences.</summary>
public sealed class PreferencesStore : ISettingsStore
{
    public string Get(string key, string fallback) => Preferences.Get(key, fallback);
    public void Set(string key, string value) => Preferences.Set(key, value);
}
