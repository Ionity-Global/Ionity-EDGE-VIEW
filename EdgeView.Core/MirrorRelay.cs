using System.Net.Sockets;

namespace EdgeView.Core;

/// <summary>
/// Pulls live frames off the device's mirror port and hands them to whoever is
/// watching. Only connects while somebody is actually looking, so an idle
/// console costs the device nothing.
/// </summary>
public static class MirrorRelay
{
    public const int Port = 4244;
    private const uint Magic = 0x31464D49; // 'IMF1'

    public sealed record Frame(int Width, int Height, int Format, byte[] Payload);

    /// <summary>Raised for every decoded frame header + payload.</summary>
    public static event Action<Frame>? FrameReceived;
    public static event Action<string>? Log;

    private static CancellationTokenSource? _cts;
    private static int _viewers;

    public static bool Running => _cts != null;
    public static int Viewers => _viewers;
    public static long FramesRelayed { get; private set; }
    public static DateTime LastFrame { get; private set; } = DateTime.MinValue;
    public static byte Fps { get; set; } = 5;

    private static void Report(string m) => Log?.Invoke(m);

    /// <summary>Call when a viewer attaches. Starts the pump on the first one.</summary>
    public static void AddViewer()
    {
        if (Interlocked.Increment(ref _viewers) == 1) Start();
    }

    /// <summary>Call when a viewer leaves. Stops the pump when the last one goes.</summary>
    public static void RemoveViewer()
    {
        if (Interlocked.Decrement(ref _viewers) <= 0)
        {
            Interlocked.Exchange(ref _viewers, 0);
            Stop();
        }
    }

    private static void Start()
    {
        if (_cts != null) return;
        _cts = new CancellationTokenSource();
        _ = PumpAsync(_cts.Token);
        Report("Mirror: connecting");
    }

    private static void Stop()
    {
        _cts?.Cancel();
        _cts = null;
        Report("Mirror: stopped");
    }

    private static async Task PumpAsync(CancellationToken ct)
    {
        var header = new byte[12];

        while (!ct.IsCancellationRequested)
        {
            try
            {
                using var client = new TcpClient();
                using var connectCts = CancellationTokenSource.CreateLinkedTokenSource(ct);
                connectCts.CancelAfter(4000);
                await client.ConnectAsync(PicoLink.DeviceIp, Port, connectCts.Token);

                var stream = client.GetStream();
                await stream.WriteAsync(new[] { Fps }, ct);   // ask for a frame rate
                Report($"Mirror: streaming from {PicoLink.DeviceIp}:{Port}");

                while (!ct.IsCancellationRequested)
                {
                    await ReadExactAsync(stream, header, 12, ct);

                    var magic = (uint)(header[0] | header[1] << 8 | header[2] << 16 | header[3] << 24);
                    if (magic != Magic) throw new InvalidDataException("frame magic lost");

                    int w = header[4] | header[5] << 8;
                    int h = header[6] | header[7] << 8;
                    int format = header[8];
                    int len = header[10] | header[11] << 8;
                    if (len <= 0 || len > 64 * 1024) throw new InvalidDataException($"bad frame length {len}");

                    var payload = new byte[len];
                    await ReadExactAsync(stream, payload, len, ct);

                    FramesRelayed++;
                    LastFrame = DateTime.UtcNow;
                    FrameReceived?.Invoke(new Frame(w, h, format, payload));
                }
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (Exception ex)
            {
                Report($"Mirror: {ex.Message}");
                try { await Task.Delay(3000, ct); } catch { break; }
            }
        }
    }

    private static async Task ReadExactAsync(NetworkStream s, byte[] buffer, int count, CancellationToken ct)
    {
        int got = 0;
        while (got < count)
        {
            int n = await s.ReadAsync(buffer.AsMemory(got, count - got), ct);
            if (n == 0) throw new IOException("mirror socket closed");
            got += n;
        }
    }

    public static object Snapshot() => new
    {
        running = Running,
        viewers = _viewers,
        frames = FramesRelayed,
        fps = Fps,
        age = LastFrame == DateTime.MinValue ? -1 : (int)(DateTime.UtcNow - LastFrame).TotalMilliseconds,
    };
}
