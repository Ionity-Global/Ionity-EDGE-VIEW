<#
.SYNOPSIS
    Station Pico - Complete Management Tool
.DESCRIPTION
    All-in-one tool for Pico 2W + Waveshare 10.1" DVI Display.
    Handles: detection, dependency installation, building, and flashing demos.
.USAGE
    .\StationPico.ps1                  # Interactive menu
    .\StationPico.ps1 -Action setup    # Auto-install all dependencies
    .\StationPico.ps1 -Action build -Target demo_rainbow
    .\StationPico.ps1 -Action flash -Target station_demo
    .\StationPico.ps1 -Action status   # Show system status
#>

[CmdletBinding()]
param(
    [ValidateSet("menu","setup","build","flash","buildflash","status","clean","help")]
    [string]$Action = "menu",
    [string]$Target = "",
    [string]$ProjectDir = "K:\.cli made\Station Pico"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

# ── Paths ────────────────────────────────────────────────────────────────────
$sdkDir      = Join-Path $ProjectDir ".sdk\pico-sdk"
$armGccBin   = Join-Path $ProjectDir ".sdk\bin"
$srcDir      = Join-Path $ProjectDir "_waveshare_src\Pico-DVI-LCD-Code\01-DVI"
$buildDir    = Join-Path $srcDir "build_rp2350"
$ninjaExe    = (Get-Command ninja -ErrorAction SilentlyContinue).Source
$vcvarsPath  = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"

# ── Available demos ──────────────────────────────────────────────────────────
$demos = @(
    @{ Name = "station_demo";  Desc = "Station Pico - Animated dashboard with info panels" },
    @{ Name = "demo_colors";   Desc = "Color Test - Cycles through all 8 display colors" },
    @{ Name = "demo_bounce";   Desc = "Bounce - 12 animated balls bouncing off walls" },
    @{ Name = "demo_sysinfo";  Desc = "System Info - Hardware specs and diagnostics" },
    @{ Name = "demo_rainbow";  Desc = "Rainbow - Animated rainbow gradient sweep" },
    @{ Name = "gui_demo";      Desc = "GUI Demo - Waveshare drawing primitives demo" },
    @{ Name = "hello_dvi";     Desc = "Test Card - Scrolling test pattern image" }
)

# ── Helpers ──────────────────────────────────────────────────────────────────
function Write-Banner {
    Write-Host ""
    Write-Host "  =============================================" -ForegroundColor Cyan
    Write-Host "   STATION PICO  -  Pico 2W + Waveshare 10.1""" -ForegroundColor Cyan
    Write-Host "  =============================================" -ForegroundColor Cyan
    Write-Host ""
}

function Write-Step  { param([string]$m) Write-Host "`n>> $m" -ForegroundColor Cyan }
function Write-OK    { param([string]$m) Write-Host "   [OK]   $m" -ForegroundColor Green }
function Write-Skip  { param([string]$m) Write-Host "   [SKIP] $m" -ForegroundColor Yellow }
function Write-Err   { param([string]$m) Write-Host "   [FAIL] $m" -ForegroundColor Red }
function Write-Info  { param([string]$m) Write-Host "   [INFO] $m" -ForegroundColor Gray }

function Test-Pico {
    return Get-Volume | Where-Object { $_.FileSystemLabel -eq "RP2350" -or $_.FileSystemLabel -eq "RPI-RP2" }
}

function Test-Deps {
    $ok = $true
    if (-not (Test-Path (Join-Path $sdkDir "pico_sdk_init.cmake"))) { $ok = $false }
    if (-not (Test-Path (Join-Path $armGccBin "arm-none-eabi-gcc.exe"))) { $ok = $false }
    if (-not (Test-Path (Join-Path $srcDir "CMakeLists.txt"))) { $ok = $false }
    return $ok
}

function Test-Built {
    param([string]$demoName)
    $uf2 = Join-Path $buildDir "apps\$demoName\$demoName.uf2"
    return Test-Path $uf2
}

# ── STATUS ───────────────────────────────────────────────────────────────────
function Invoke-Status {
    Write-Banner
    Write-Step "System Status"

    # Pico detection
    $pico = Test-Pico
    if ($pico) {
        Write-OK "Pico 2W detected on drive $($pico.DriveLetter): ($($pico.FileSystemLabel))"
    } else {
        Write-Err "Pico not detected (hold BOOTSEL + RESET to enter bootloader)"
    }

    # SDK
    if (Test-Path (Join-Path $sdkDir "pico_sdk_init.cmake")) {
        Write-OK "Pico SDK installed at $sdkDir"
    } else {
        Write-Err "Pico SDK not found - run 'setup'"
    }

    # ARM GCC
    if (Test-Path (Join-Path $armGccBin "arm-none-eabi-gcc.exe")) {
        $ver = & (Join-Path $armGccBin "arm-none-eabi-gcc.exe") --version 2>&1 | Select-Object -First 1
        Write-OK "ARM GCC: $ver"
    } else {
        Write-Err "ARM GCC not found - run 'setup'"
    }

    # Source code
    if (Test-Path (Join-Path $srcDir "CMakeLists.txt")) {
        Write-OK "Waveshare DVI code ready"
    } else {
        Write-Err "Waveshare code not found - run 'setup'"
    }

    # Build status
    Write-Step "Available Demos"
    foreach ($d in $demos) {
        $built = Test-Built $d.Name
        $status = if ($built) { "[BUILT]" } else { "[  -  ]" }
        $color  = if ($built) { "Green" } else { "Gray" }
        Write-Host "   $status  $($d.Name.PadRight(16)) $($d.Desc)" -ForegroundColor $color
    }

    # MSVC
    if (Test-Path $vcvarsPath) {
        Write-OK "MSVC Build Tools found"
    } else {
        Write-Err "MSVC Build Tools not found (needed for host tools pioasm/picotool)"
    }

    # CMake/Ninja
    if (Get-Command cmake -ErrorAction SilentlyContinue) { Write-OK "CMake available" } else { Write-Err "CMake not found" }
    if ($ninjaExe) { Write-OK "Ninja available" } else { Write-Err "Ninja not found" }

    Write-Host ""
}

# ── SETUP ────────────────────────────────────────────────────────────────────
function Invoke-Setup {
    Write-Banner
    Write-Step "Installing all dependencies..."

    $installDir = Join-Path $ProjectDir ".sdk"
    if (-not (Test-Path $installDir)) {
        New-Item -ItemType Directory -Path $installDir -Force | Out-Null
    }

    # ARM GCC
    Write-Step "ARM GCC Toolchain 14.2"
    if (Test-Path (Join-Path $armGccBin "arm-none-eabi-gcc.exe")) {
        Write-Skip "Already installed"
    } else {
        Write-Info "Downloading ARM GCC (~300MB)..."
        $armUrl = "https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-mingw-w64-i686-arm-none-eabi.zip"
        $armZip = Join-Path $installDir "arm-gcc.zip"
        Invoke-WebRequest -Uri $armUrl -OutFile $armZip -UseBasicParsing
        Write-Info "Extracting..."
        Expand-Archive -Path $armZip -DestinationPath $installDir -Force
        Remove-Item $armZip -ErrorAction SilentlyContinue
        Write-OK "ARM GCC installed"
    }

    # Pico SDK
    Write-Step "Pico SDK 2.1.1"
    if (Test-Path (Join-Path $sdkDir "pico_sdk_init.cmake")) {
        Write-Skip "Already installed"
    } else {
        Write-Info "Cloning Pico SDK..."
        git clone --depth 1 --branch 2.1.1 --recurse-submodules --shallow-submodules `
            https://github.com/raspberrypi/pico-sdk.git $sdkDir 2>&1 | Out-Null
        Write-OK "Pico SDK installed"
    }

    # Waveshare code
    Write-Step "Waveshare PICO-DVI-LCD Code"
    if (Test-Path (Join-Path $srcDir "CMakeLists.txt")) {
        Write-Skip "Already downloaded"
    } else {
        $zipUrl = "https://files.waveshare.com/upload/5/5a/Pico-DVI-LCD-Code.zip"
        $zipFile = Join-Path $ProjectDir "Waveshare-Code.zip"
        Write-Info "Downloading..."
        Invoke-WebRequest -Uri $zipUrl -OutFile $zipFile -UseBasicParsing
        $waveshareDir = Join-Path $ProjectDir "_waveshare_src"
        Expand-Archive -Path $zipFile -DestinationPath $waveshareDir -Force
        Remove-Item $zipFile -ErrorAction SilentlyContinue
        Write-OK "Waveshare code ready"
    }

    # Environment variables
    Write-Step "Setting environment variables"
    [System.Environment]::SetEnvironmentVariable("PICO_SDK_PATH", $sdkDir, "User")
    $pathCurrent = [System.Environment]::GetEnvironmentVariable("PATH", "User")
    if ($pathCurrent -notlike "*\.sdk\bin*") {
        [System.Environment]::SetEnvironmentVariable("PATH", "$armGccBin;$pathCurrent", "User")
    }
    Write-OK "Environment configured"

    Write-Host ""
    Write-Host "   Setup complete! Run again with -Action build to compile demos." -ForegroundColor Green
    Write-Host ""
}

# ── BUILD ────────────────────────────────────────────────────────────────────
function Invoke-Build {
    param([string]$demoName)

    if (-not (Test-Deps)) {
        Write-Err "Dependencies missing. Run with -Action setup first."
        return
    }

    Write-Banner
    if ($demoName) {
        Write-Step "Building: $demoName"
    } else {
        Write-Step "Building ALL demos"
    }

    # Check for MSVC
    if (-not (Test-Path $vcvarsPath)) {
        Write-Err "MSVC Build Tools required (install Visual Studio Build Tools)"
        return
    }

    # Build via batch file (to get MSVC env)
    $batchFile = Join-Path $ProjectDir "build_native.bat"
    if (-not $demoName -or -not (Test-Path (Join-Path $srcDir "build_rp2350\CMakeCache.txt"))) {
        # Full clean build
        Write-Info "Running full build via build_native.bat..."
        $output = cmd /c "`"$batchFile`" 2>&1"
        Write-Host $output
    } else {
        # Incremental rebuild of specific target
        Write-Info "Incremental rebuild of $demoName..."
        $batchContent = @"
@echo off
call "$vcvarsPath" x64 >nul 2>&1
set "PATH=$armGccBin;%PATH%"
cd /d "$buildDir"
cmake --build . --target $demoName -- -j8
if %ERRORLEVEL% NEQ 0 exit /b 1
echo [OK] Built $demoName
"@
        $tmpBat = Join-Path $ProjectDir "_tmp_build.bat"
        Set-Content -Path $tmpBat -Value $batchContent
        $output = cmd /c "`"$tmpBat`" 2>&1"
        Remove-Item $tmpBat -ErrorAction SilentlyContinue
        Write-Host $output
    }

    $uf2 = Join-Path $buildDir "apps\$demoName\$demoName.uf2"
    if ($demoName -and (Test-Path $uf2)) {
        Write-OK "Built: $uf2"
    }
}

# ── FLASH ────────────────────────────────────────────────────────────────────
function Invoke-Flash {
    param([string]$demoName)

    $pico = Test-Pico
    if (-not $pico) {
        Write-Err "Pico not in BOOTSEL mode!"
        Write-Info "Hold BOOTSEL button and press RESET to enter bootloader."
        return
    }

    if (-not $demoName) {
        $demoName = "station_demo"
    }

    $uf2 = Join-Path $buildDir "apps\$demoName\$demoName.uf2"
    if (-not (Test-Path $uf2)) {
        Write-Err "UF2 not found: $uf2"
        Write-Info "Build it first with -Action build -Target $demoName"
        return
    }

    Write-Step "Flashing $demoName to Pico on drive $($pico.DriveLetter):"
    Copy-Item -Path $uf2 -Destination "$($pico.DriveLetter):\" -Force
    Write-OK "Flashed! Pico will reboot and run $demoName."
}

# ── CLEAN ────────────────────────────────────────────────────────────────────
function Invoke-Clean {
    Write-Step "Cleaning build directory..."
    if (Test-Path $buildDir) {
        Remove-Item -Recurse -Force $buildDir
        Write-OK "Build directory removed"
    } else {
        Write-Skip "Nothing to clean"
    }
}

# ── MENU ─────────────────────────────────────────────────────────────────────
function Show-Menu {
    Write-Banner

    if (-not (Test-Deps)) {
        Write-Host "   Dependencies not installed." -ForegroundColor Yellow
        Write-Host "   Press [S] to run setup, or [Q] to quit.`n" -ForegroundColor Yellow
        $key = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown").Character
        if ($key -eq 's' -or $key -eq 'S') {
            Invoke-Setup
            Show-Menu
            return
        }
        return
    }

    Invoke-Status

    Write-Step "Select a demo to build & flash"
    Write-Host ""
    for ($i = 0; $i -lt $demos.Count; $i++) {
        $built = if (Test-Built $demos[$i].Name) { "*" } else { " " }
        Write-Host "   [$($i+1)] $built $($demos[$i].Name.PadRight(16)) $($demos[$i].Desc)"
    }
    Write-Host ""
    Write-Host "   [B] Build all    [S] Re-run setup    [C] Clean build    [Q] Quit" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "   Choose: " -NoNewline

    $key = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown").Character
    Write-Host $key

    if ($key -eq 'q' -or $key -eq 'Q') { return }
    if ($key -eq 'b' -or $key -eq 'B') {
        Invoke-Build
        return
    }
    if ($key -eq 's' -or $key -eq 'S') {
        Invoke-Setup
        Show-Menu
        return
    }
    if ($key -eq 'c' -or $key -eq 'C') {
        Invoke-Clean
        Show-Menu
        return
    }

    $num = [int]::Parse($key) - 1
    if ($num -ge 0 -and $num -lt $demos.Count) {
        $demo = $demos[$num]
        Write-Step "Selected: $($demo.Name) - $($demo.Desc)"

        # Build if needed
        if (-not (Test-Built $demo.Name)) {
            Write-Info "Building $($demo.Name)..."
            Invoke-Build -demoName $demo.Name
        }

        # Flash
        $pico = Test-Pico
        if ($pico) {
            Invoke-Flash -demoName $demo.Name
        } else {
            Write-Err "Pico not in BOOTSEL mode. Hold BOOTSEL + RESET, then press [R] to retry."
            $retry = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown").Character
            if ($retry -eq 'r' -or $retry -eq 'R') {
                Invoke-Flash -demoName $demo.Name
            }
        }
    }

    Write-Host ""
    Write-Host "   Press any key to return to menu..." -NoNewline
    $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown") | Out-Null
    Show-Menu
}

# ── HELP ─────────────────────────────────────────────────────────────────────
function Show-Help {
    Write-Banner
    Write-Host "  Usage: .\StationPico.ps1 -Action <action> [-Target <demo>]" -ForegroundColor White
    Write-Host ""
    Write-Host "  Actions:" -ForegroundColor Cyan
    Write-Host "    menu       Interactive menu (default)"
    Write-Host "    setup      Install all dependencies (SDK, ARM GCC, code)"
    Write-Host "    build      Build a specific demo or all demos"
    Write-Host "    flash      Flash a demo to Pico (must be in BOOTSEL)"
    Write-Host "    buildflash Build and flash in one step"
    Write-Host "    status     Show system and build status"
    Write-Host "    clean      Remove build directory"
    Write-Host "    help       Show this help"
    Write-Host ""
    Write-Host "  Targets:" -ForegroundColor Cyan
    foreach ($d in $demos) {
        Write-Host "    $($d.Name.PadRight(16)) $($d.Desc)"
    }
    Write-Host ""
    Write-Host "  Examples:" -ForegroundColor Cyan
    Write-Host "    .\StationPico.ps1                              # Interactive menu"
    Write-Host "    .\StationPico.ps1 -Action setup                # Install everything"
    Write-Host "    .\StationPico.ps1 -Action buildflash -Target demo_rainbow"
    Write-Host "    .\StationPico.ps1 -Action status               # Check status"
    Write-Host ""
}

# ── Main dispatch ────────────────────────────────────────────────────────────
switch ($Action) {
    "menu"       { Show-Menu }
    "setup"      { Invoke-Setup }
    "build"      { Invoke-Build -demoName $Target }
    "flash"      { Invoke-Flash -demoName $Target }
    "buildflash" { Invoke-Build -demoName $Target; Invoke-Flash -demoName $Target }
    "status"     { Invoke-Status }
    "clean"      { Invoke-Clean }
    "help"       { Show-Help }
}
