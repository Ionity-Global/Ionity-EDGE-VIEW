using System.Diagnostics;
using System.Text;

namespace StationPicoInstaller;

public partial class MainPage : ContentPage
{
    private readonly string _projectDir;
    private readonly string _sdkDir;
    private readonly string _armGccBin;
    private readonly string _srcDir;
    private readonly string _buildDir;

    private static readonly Dictionary<string, string> DemoTargets = new()
    {
        ["EDGE-VIEW - Server-driven display"] = "edgeview",
        ["Color Test - Cycles all 8 colors"] = "demo_colors",
        ["Bounce - Animated bouncing balls"] = "demo_bounce",
        ["System Info - Hardware diagnostics"] = "demo_sysinfo",
        ["Rainbow - Animated gradient sweep"] = "demo_rainbow",
        ["GUI Demo - Waveshare drawing primitives"] = "gui_demo",
        ["Hello DVI - Scrolling test card"] = "hello_dvi",
    };

    public MainPage()
    {
        InitializeComponent();
        _projectDir = Path.GetDirectoryName(AppContext.BaseDirectory) ??
                      Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Station Pico");

        // Walk up from bin to find the project root - try common locations
        var candidate = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", ".."));
        if (File.Exists(Path.Combine(candidate, "setup.ps1")))
            _projectDir = candidate;
        else
        {
            // Fallback: look for the project dir
            var searchPaths = new[]
            {
                @"K:\.cli made\Station Pico",
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Station Pico"),
            };
            foreach (var p in searchPaths)
            {
                if (Directory.Exists(p))
                {
                    _projectDir = p;
                    break;
                }
            }
        }

        _sdkDir = Path.Combine(_projectDir, ".sdk");
        _armGccBin = Path.Combine(_sdkDir, "bin");
        _srcDir = Path.Combine(_projectDir, "_waveshare_src", "Pico-DVI-LCD-Code", "01-DVI");
        _buildDir = Path.Combine(_srcDir, "build_rp2350");

        DemoPicker.SelectedIndex = 0;
    }

    protected override async void OnAppearing()
    {
        base.OnAppearing();
        await CheckEnvironment();
    }

    private void Log(string msg)
    {
        MainThread.BeginInvokeOnMainThread(() =>
        {
            LogLabel.Text += $"\n{msg}";
            LogScroll.ScrollToAsync(LogLabel, ScrollToPosition.End, true);
        });
    }

    private void SetStatus(Label icon, Label status, bool ok, string text)
    {
        MainThread.BeginInvokeOnMainThread(() =>
        {
            icon.Text = ok ? "✅" : "❌";
            status.Text = text;
            status.TextColor = ok ? Colors.LimeGreen : Colors.OrangeRed;
        });
    }

    private void SetStatusPending(Label icon, Label status, string text)
    {
        MainThread.BeginInvokeOnMainThread(() =>
        {
            icon.Text = "⏳";
            status.Text = text;
            status.TextColor = Colors.Gray;
        });
    }

    private async Task<(bool found, string version)> CheckCommand(string cmd, string args = "--version")
    {
        try
        {
            var output = await RunCommand(cmd, args, throwOnError: false);
            if (!string.IsNullOrWhiteSpace(output) && !output.Contains("not recognized"))
                return (true, output.Split('\n')[0].Trim());
            return (false, "");
        }
        catch
        {
            return (false, "");
        }
    }

    private async Task CheckEnvironment()
    {
        Log("Checking environment...");

        // Git
        var (gitOk, gitVer) = await CheckCommand("git");
        SetStatus(GitIcon, GitStatus, gitOk, gitOk ? gitVer : "Not found");

        // CMake
        var (cmakeOk, cmakeVer) = await CheckCommand("cmake");
        SetStatus(CMakeIcon, CMakeStatus, cmakeOk, cmakeOk ? cmakeVer : "Not found");

        // Ninja
        var (ninjaOk, ninjaVer) = await CheckCommand("ninja");
        SetStatus(NinjaIcon, NinjaStatus, ninjaOk, ninjaOk ? ninjaVer : "Not found");

        // ARM GCC
        var armGccExe = Path.Combine(_armGccBin, "arm-none-eabi-gcc.exe");
        if (File.Exists(armGccExe))
        {
            var (_, armVer) = await CheckCommand(armGccExe);
            SetStatus(ArmGccIcon, ArmGccStatus, true, armVer);
        }
        else
        {
            SetStatus(ArmGccIcon, ArmGccStatus, false, "Not installed");
        }

        // Pico SDK
        var sdkInit = Path.Combine(_sdkDir, "pico-sdk", "pico_sdk_init.cmake");
        SetStatus(SdkIcon, SdkStatus, File.Exists(sdkInit), File.Exists(sdkInit) ? "v2.1.1" : "Not installed");

        // Pico detection
        await CheckPico();

        Log("Environment check complete.");
    }

    private async Task CheckPico()
    {
        PicoIcon.Text = "⏳";
        PicoStatus.Text = "Scanning...";
        PicoStatus.TextColor = Colors.Gray;

        var result = await RunCommand("powershell", "-NoProfile -Command \"Get-Volume | Where-Object { $_.FileSystemLabel -eq 'RP2350' -or $_.FileSystemLabel -eq 'RPI-RP2' } | Select-Object -ExpandProperty DriveLetter\"", throwOnError: false);
        var letter = result?.Trim();

        if (!string.IsNullOrEmpty(letter) && letter.Length == 1)
        {
            PicoIcon.Text = "✅";
            PicoStatus.Text = $"Pico 2W detected on {letter}:";
            PicoStatus.TextColor = Colors.LimeGreen;
        }
        else
        {
            PicoIcon.Text = "❌";
            PicoStatus.Text = "Not detected - hold BOOTSEL + RESET";
            PicoStatus.TextColor = Colors.OrangeRed;
        }
    }

    private async void OnRefreshClicked(object? sender, EventArgs e)
    {
        await CheckPico();
    }

    private async void OnSetupClicked(object? sender, EventArgs e)
    {
        SetupBtn.IsEnabled = false;
        BuildFlashBtn.IsEnabled = false;
        ProgressBar.Progress = 0;
        LogLabel.Text = "Starting full setup...";

        try
        {
            // Step 1: Check Git
            SetProgress(0.05, "Checking Git...");
            var (gitOk, _) = await CheckCommand("git");
            if (!gitOk) { Log("ERROR: Git is required. Install from https://git-scm.com"); return; }

            // Step 2: Check CMake
            SetProgress(0.1, "Checking CMake...");
            var (cmakeOk, _) = await CheckCommand("cmake");
            if (!cmakeOk) { Log("ERROR: CMake is required. Install from https://cmake.org"); return; }

            // Step 3: Install ARM GCC
            SetProgress(0.15, "Checking ARM GCC...");
            if (!File.Exists(Path.Combine(_armGccBin, "arm-none-eabi-gcc.exe")))
            {
                SetProgress(0.2, "Downloading ARM GCC Toolchain (~300MB)...");
                Log("Downloading ARM GCC 14.2...");
                Directory.CreateDirectory(_sdkDir);
                var zipPath = Path.Combine(_sdkDir, "arm-gcc.zip");
                await DownloadFile("https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-mingw-w64-i686-arm-none-eabi.zip", zipPath);

                SetProgress(0.5, "Extracting ARM GCC...");
                Log("Extracting ARM GCC...");
                System.IO.Compression.ZipFile.ExtractToDirectory(zipPath, _sdkDir);
                var extracted = Directory.GetDirectories(_sdkDir, "arm-gnu-toolchain-*").FirstOrDefault();
                if (extracted != null && !Directory.Exists(Path.Combine(_sdkDir, "arm-gnu-toolchain")))
                    Directory.Move(extracted, Path.Combine(_sdkDir, "arm-gnu-toolchain"));
                File.Delete(zipPath);
                Log("ARM GCC installed.");
            }
            else
            {
                Log("ARM GCC already installed.");
            }
            SetStatus(ArmGccIcon, ArmGccStatus, true, "Installed");

            // Step 4: Clone Pico SDK
            SetProgress(0.6, "Checking Pico SDK...");
            var sdkPath = Path.Combine(_sdkDir, "pico-sdk");
            if (!File.Exists(Path.Combine(sdkPath, "pico_sdk_init.cmake")))
            {
                SetProgress(0.65, "Cloning Pico SDK...");
                Log("Cloning Pico SDK 2.1.1...");
                await RunCommand("git", $"clone --depth 1 --branch 2.1.1 --recurse-submodules --shallow-submodules https://github.com/raspberrypi/pico-sdk.git \"{sdkPath}\"");
                Log("Pico SDK installed.");
            }
            else
            {
                Log("Pico SDK already installed.");
            }
            SetStatus(SdkIcon, SdkStatus, true, "v2.1.1");

            // Step 5: Download Waveshare code
            SetProgress(0.8, "Checking Waveshare code...");
            if (!File.Exists(Path.Combine(_srcDir, "CMakeLists.txt")))
            {
                SetProgress(0.82, "Downloading Waveshare DVI code...");
                Log("Downloading Waveshare PICO-DVI-LCD code...");
                var zipPath = Path.Combine(_projectDir, "Waveshare-Code.zip");
                await DownloadFile("https://files.waveshare.com/upload/5/5a/Pico-DVI-LCD-Code.zip", zipPath);
                System.IO.Compression.ZipFile.ExtractToDirectory(zipPath, Path.Combine(_projectDir, "_waveshare_src"));
                File.Delete(zipPath);
                Log("Waveshare code downloaded.");
            }
            else
            {
                Log("Waveshare code already present.");
            }

            // Step 6: Write WiFi config if SSID provided
            SetProgress(0.9, "Configuring WiFi...");
            var ssid = SsidEntry.Text?.Trim();
            var pass = PasswordEntry.Text?.Trim();
            if (!string.IsNullOrEmpty(ssid))
            {
                WriteWifiConfig(ssid, pass ?? "");
                Log($"WiFi configured: {ssid}");
            }

            // Step 7: Build all demos
            SetProgress(0.92, "Building all demos...");
            Log("Building all demos...");
            await BuildFirmware(null);
            Log("All demos built successfully!");

            SetProgress(1.0, "Setup complete!");
            Log("✅ Full setup complete!");

            await CheckEnvironment();
        }
        catch (Exception ex)
        {
            Log($"ERROR: {ex.Message}");
        }
        finally
        {
            SetupBtn.IsEnabled = true;
            BuildFlashBtn.IsEnabled = true;
        }
    }

    private async void OnBuildFlashClicked(object? sender, EventArgs e)
    {
        SetupBtn.IsEnabled = false;
        BuildFlashBtn.IsEnabled = false;
        ProgressBar.Progress = 0;

        try
        {
            var selectedDemo = DemoPicker.SelectedItem?.ToString() ?? "";
            if (!DemoTargets.TryGetValue(selectedDemo, out var target))
            {
                Log("ERROR: Please select a demo from the dropdown.");
                return;
            }

            // Write WiFi config if SSID provided
            var ssid = SsidEntry.Text?.Trim();
            var pass = PasswordEntry.Text?.Trim();
            if (!string.IsNullOrEmpty(ssid) && target == "wifi_demo")
            {
                WriteWifiConfig(ssid, pass ?? "");
                Log($"WiFi config updated: {ssid}");
            }

            // Build
            SetProgress(0.3, $"Building {target}...");
            Log($"Building {target}...");
            await BuildFirmware(target);
            Log($"Build complete: {target}");
            SetProgress(0.7, "Build complete.");

            // Flash
            await CheckPico();
            var result = await RunCommand("powershell",
                "-NoProfile -Command \"Get-Volume | Where-Object { $_.FileSystemLabel -eq 'RP2350' -or $_.FileSystemLabel -eq 'RPI-RP2' } | Select-Object -ExpandProperty DriveLetter\"",
                throwOnError: false);
            var letter = result?.Trim();

            if (!string.IsNullOrEmpty(letter) && letter.Length == 1)
            {
                SetProgress(0.85, "Flashing...");
                var uf2Path = Path.Combine(_buildDir, "apps", target, $"{target}.uf2");
                if (File.Exists(uf2Path))
                {
                    File.Copy(uf2Path, Path.Combine($"{letter}:\\", $"{target}.uf2"), overwrite: true);
                    SetProgress(1.0, "Flashed!");
                    Log($"✅ Flashed {target} to Pico on {letter}:");
                }
                else
                {
                    Log($"ERROR: UF2 not found at {uf2Path}");
                }
            }
            else
            {
                Log("⚠ Pico not in BOOTSEL mode. Hold BOOTSEL + RESET, then click Refresh and Build & Flash again.");
            }
        }
        catch (Exception ex)
        {
            Log($"ERROR: {ex.Message}");
        }
        finally
        {
            SetupBtn.IsEnabled = true;
            BuildFlashBtn.IsEnabled = true;
        }
    }

    private void WriteWifiConfig(string ssid, string password)
    {
        // Single source of truth: wifi_config.cmake feeds ALL firmware targets
        // (libionity reads it; wifi_demo receives IONITY_* defines from it).
        var cmakePath = Path.Combine(_srcDir, "wifi_config.cmake");
        var cmake = $@"# WiFi credentials — loaded by libionity/CMakeLists.txt
# Written by IO-nity EDGE-VIEW Studio. This file is gitignored.

set(WIFI_SSID ""{ssid}"")
set(WIFI_PASS ""{password}"")
";
        File.WriteAllText(cmakePath, cmake);
    }

    private async Task BuildFirmware(string? target)
    {
        Directory.CreateDirectory(_buildDir);

        var envPath = $"{_armGccBin};{Environment.GetEnvironmentVariable("PATH")}";

        // Find vcvarsall
        var vcvars = @"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat";
        if (!File.Exists(vcvars))
        {
            // Try other common paths
            var candidates = new[]
            {
                @"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat",
                @"C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat",
                @"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat",
            };
            foreach (var c in candidates)
            {
                if (File.Exists(c)) { vcvars = c; break; }
            }
        }

        var sdkPath = Path.Combine(_sdkDir, "pico-sdk").Replace("\\", "/");
        var srcPath = _srcDir.Replace("\\", "/");

        var targetArg = string.IsNullOrEmpty(target) ? "" : $"--target {target}";

        var script = $@"
@echo off
call ""{vcvars}"" x64 >nul 2>&1
set ""PATH={envPath}""
cd /d ""{_buildDir}""
if not exist CMakeCache.txt (
    cmake -G Ninja -DPICO_SDK_PATH=""{sdkPath}"" -DPICO_BOARD=pico2_w -DPICO_PLATFORM=rp2350 -DDVI_DEFAULT_SERIAL_CONFIG=pico_sock_cfg -DCMAKE_BUILD_TYPE=Release -DPICO_TOOLCHAIN_PATH=""{_armGccBin.Replace("\\", "/")}"" ""{srcPath}""
)
cmake --build . {targetArg} -- -j8
";
        var batPath = Path.Combine(_projectDir, "_build_temp.bat");
        File.WriteAllText(batPath, script);

        try
        {
            var output = await RunCommand("cmd", $"/c \"{batPath}\"");
            Log(output);
        }
        finally
        {
            if (File.Exists(batPath)) File.Delete(batPath);
        }
    }

    private async Task<string> RunCommand(string cmd, string args, bool throwOnError = true)
    {
        var psi = new ProcessStartInfo
        {
            FileName = cmd,
            Arguments = args,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };

        using var process = Process.Start(psi);
        if (process == null) throw new Exception($"Failed to start {cmd}");

        var output = await process.StandardOutput.ReadToEndAsync();
        var error = await process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync();

        if (throwOnError && process.ExitCode != 0)
        {
            var msg = string.IsNullOrEmpty(error) ? output : error;
            throw new Exception($"{cmd} failed (exit {process.ExitCode}): {msg}");
        }

        return output + error;
    }

    private async Task DownloadFile(string url, string destPath)
    {
        using var client = new HttpClient();
        using var response = await client.GetAsync(url, HttpCompletionOption.ResponseHeadersRead);
        response.EnsureSuccessStatusCode();

        var totalBytes = response.Content.Headers.ContentLength ?? -1;
        using var stream = await response.Content.ReadAsStreamAsync();
        using var fileStream = new FileStream(destPath, FileMode.Create, FileAccess.Write, FileShare.None, 8192, true);

        var buffer = new byte[65536];
        long bytesRead = 0;
        int read;
        while ((read = await stream.ReadAsync(buffer)) > 0)
        {
            await fileStream.WriteAsync(buffer.AsMemory(0, read));
            bytesRead += read;
            if (totalBytes > 0)
            {
                var pct = (double)bytesRead / totalBytes;
                MainThread.BeginInvokeOnMainThread(() =>
                    ProgressBar.Progress = Math.Max(ProgressBar.Progress, pct * 0.35 + 0.15));
            }
        }
    }

    private void SetProgress(double value, string msg)
    {
        MainThread.BeginInvokeOnMainThread(() =>
        {
            ProgressBar.Progress = value;
            Log(msg);
        });
    }
}
