using System.Text.Json;

namespace EdgeView.Core;

/// <summary>Where settings live. MAUI backs this with Preferences; the
/// headless host backs it with a JSON file next to the executable.</summary>
public interface ISettingsStore
{
    string Get(string key, string fallback);
    void Set(string key, string value);
}

public static class Settings
{
    public static ISettingsStore Store { get; set; } = new MemoryStore();

    public static string Get(string key, string fallback) => Store.Get(key, fallback);
    public static void Set(string key, string value) => Store.Set(key, value);

    public static bool Get(string key, bool fallback) =>
        bool.TryParse(Store.Get(key, fallback.ToString()), out var v) ? v : fallback;

    public static void Set(string key, bool value) => Store.Set(key, value.ToString());

    public static int Get(string key, int fallback) =>
        int.TryParse(Store.Get(key, fallback.ToString()), out var v) ? v : fallback;
}

public sealed class MemoryStore : ISettingsStore
{
    private readonly Dictionary<string, string> _map = new();
    public string Get(string key, string fallback) => _map.TryGetValue(key, out var v) ? v : fallback;
    public void Set(string key, string value) => _map[key] = value;
}

/// <summary>JSON file store used by the headless host. Writes are debounced by
/// nothing — the settings volume is tiny and changes are rare.</summary>
public sealed class JsonFileStore : ISettingsStore
{
    private readonly string _path;
    private readonly Dictionary<string, string> _map;
    private readonly Lock _gate = new();

    public JsonFileStore(string path)
    {
        _path = path;
        try
        {
            _map = File.Exists(path)
                ? JsonSerializer.Deserialize<Dictionary<string, string>>(File.ReadAllText(path)) ?? new()
                : new();
        }
        catch
        {
            _map = new();
        }
    }

    public string Get(string key, string fallback)
    {
        lock (_gate) return _map.TryGetValue(key, out var v) ? v : fallback;
    }

    public void Set(string key, string value)
    {
        lock (_gate)
        {
            _map[key] = value;
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(_path)!);
                File.WriteAllText(_path, JsonSerializer.Serialize(_map, new JsonSerializerOptions { WriteIndented = true }));
            }
            catch
            {
                // A read-only profile still runs fine, it just forgets on exit.
            }
        }
    }
}

public static class EdgeViewInfo
{
    public const string Product = "IO-nity EDGE-VIEW";
    public const string Company = "Ionity Global (Pty) Ltd";
    public const string Version = "2.1";
}
