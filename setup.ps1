<#
.SYNOPSIS
    Station Pico - Automated Setup Script
.DESCRIPTION
    Downloads and configures all dependencies for Pico 2W DVI development:
    - ARM GCC Toolchain 14.2
    - Raspberry Pi Pico SDK 2.1.1
    - Waveshare PICO-DVI-LCD example code
    Builds the Station Pico demo and flashes it to the Pico.
#>

[CmdletBinding()]
param(
    [string]$ProjectDir = $PSScriptRoot,
    [switch]$SkipFlash,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$installDir = Join-Path $ProjectDir ".sdk"
$srcDir     = Join-Path $ProjectDir "_waveshare_src\Pico-DVI-LCD-Code\01-DVI"

function Write-Step { param([string]$msg) Write-Host "`n>> $msg" -ForegroundColor Cyan }
function Write-OK   { param([string]$msg) Write-Host "   [OK] $msg" -ForegroundColor Green }
function Write-Skip { param([string]$msg) Write-Host "   [SKIP] $msg" -ForegroundColor Yellow }
function Write-Err  { param([string]$msg) Write-Host "   [FAIL] $msg" -ForegroundColor Red; exit 1 }

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host " Station Pico - Automated Setup" -ForegroundColor Cyan
Write-Host " Pico 2W + Waveshare 10.1`" DVI Display" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# ── Step 0: Detect Pico ──────────────────────────────────────────────────────
Write-Step "Detecting Pico 2W in BOOTSEL mode..."
$picoDrive = Get-Volume | Where-Object { $_.FileSystemLabel -eq "RP2350" -or $_.FileSystemLabel -eq "RPI-RP2" }
if ($picoDrive) {
    $picoLetter = $picoDrive.DriveLetter
    Write-OK "Pico found on drive ${picoLetter}:"
} else {
    Write-Host "   [!!] Pico not detected in BOOTSEL mode." -ForegroundColor Yellow
    Write-Host "   Hold BOOTSEL and plug in USB to flash later." -ForegroundColor Yellow
    $SkipFlash = $true
}

# ── Step 1: Create install directory ─────────────────────────────────────────
Write-Step "Preparing SDK directory..."
if (-not (Test-Path $installDir)) {
    New-Item -ItemType Directory -Path $installDir -Force | Out-Null
}
Write-OK "SDK dir: $installDir"

# ── Step 2: Download ARM GCC Toolchain ───────────────────────────────────────
$armGccDir = Join-Path $installDir "arm-gnu-toolchain"
$armGccExe = Join-Path $armGccDir "bin\arm-none-eabi-gcc.exe"

Write-Step "ARM GCC Toolchain 14.2..."
if (Test-Path $armGccExe) {
    Write-Skip "Already installed"
} else {
    $armUrl = "https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-mingw-w64-i686-arm-none-eabi.zip"
    $armZip = Join-Path $installDir "arm-gcc.zip"
    Write-Host "   Downloading (~300MB, please wait)..." -ForegroundColor Gray
    Invoke-WebRequest -Uri $armUrl -OutFile $armZip -UseBasicParsing
    Write-Host "   Extracting (this may take a minute)..." -ForegroundColor Gray
    Expand-Archive -Path $armZip -DestinationPath $installDir -Force
    $extracted = Get-ChildItem -Path $installDir -Directory -Filter "arm-gnu-toolchain-*"
    if ($extracted) {
        Rename-Item -Path $extracted.FullName -NewName "arm-gnu-toolchain"
    }
    Remove-Item $armZip -ErrorAction SilentlyContinue
    Write-OK "ARM GCC installed"
}
$gccVersion = & $armGccExe --version 2>&1 | Select-Object -First 1
Write-OK "arm-none-eabi-gcc: $gccVersion"

# ── Step 3: Clone Pico SDK ──────────────────────────────────────────────────
$sdkDir = Join-Path $installDir "pico-sdk"

Write-Step "Raspberry Pi Pico SDK 2.1.1..."
if (Test-Path (Join-Path $sdkDir "pico_sdk_init.cmake")) {
    Write-Skip "Already installed"
} else {
    Write-Host "   Cloning (this may take a few minutes)..." -ForegroundColor Gray
    git clone --depth 1 --branch 2.1.1 --recurse-submodules --shallow-submodules `
        https://github.com/raspberrypi/pico-sdk.git $sdkDir 2>&1 | Out-Null
}
Write-OK "Pico SDK ready"

# ── Step 4: Download Waveshare code ─────────────────────────────────────────
$waveshareDir = Join-Path $ProjectDir "_waveshare_src"

Write-Step "Waveshare PICO-DVI-LCD code..."
if (Test-Path (Join-Path $srcDir "CMakeLists.txt")) {
    Write-Skip "Already downloaded"
} else {
    $zipUrl = "https://files.waveshare.com/upload/5/5a/Pico-DVI-LCD-Code.zip"
    $zipFile = Join-Path $ProjectDir "Waveshare-Code.zip"
    Write-Host "   Downloading..." -ForegroundColor Gray
    Invoke-WebRequest -Uri $zipUrl -OutFile $zipFile -UseBasicParsing
    Write-Host "   Extracting..." -ForegroundColor Gray
    Expand-Archive -Path $zipFile -DestinationPath $waveshareDir -Force
    Remove-Item $zipFile -ErrorAction SilentlyContinue
    Write-OK "Waveshare code ready"
}

# ── Step 5: Add Station Pico demo ────────────────────────────────────────────
Write-Step "Station Pico custom demo..."
$demoDir = Join-Path $srcDir "apps\station_demo"
if (-not (Test-Path (Join-Path $demoDir "main.c"))) {
    New-Item -ItemType Directory -Path $demoDir -Force | Out-Null
    Write-Host "   Demo source will be created during first build" -ForegroundColor Gray
}
Write-OK "station_demo registered"

# ── Step 6: Verify Ninja is available ───────────────────────────────────────
Write-Step "Checking build tools..."
if (Get-Command ninja -ErrorAction SilentlyContinue) {
    Write-OK "Ninja found on PATH"
} else {
    Write-Host "   [!!] Ninja build system not found on PATH." -ForegroundColor Yellow
    Write-Host "   Install Ninja: winget install Ninja-build.Ninja" -ForegroundColor Yellow
    Write-Host "   Or: choco install ninja" -ForegroundColor Yellow
    Write-Err "Ninja is required. Install it and re-run setup."
}

# ── Step 7: Set environment variables ────────────────────────────────────────
Write-Step "Setting environment variables..."
$env:PICO_SDK_PATH = $sdkDir
$env:PATH = "$(Join-Path $armGccDir 'bin');$env:PATH"
[System.Environment]::SetEnvironmentVariable("PICO_SDK_PATH", $sdkDir, "User")
$armBin = Join-Path $armGccDir "bin"
$pathCurrent = [System.Environment]::GetEnvironmentVariable("PATH", "User")
if ($pathCurrent -notlike "*arm-gnu-toolchain*") {
    [System.Environment]::SetEnvironmentVariable("PATH", "$armBin;$pathCurrent", "User")
}
Write-OK "PICO_SDK_PATH = $sdkDir"
Write-OK "ARM GCC added to user PATH"

# ── Step 8: Build ────────────────────────────────────────────────────────────
if (-not $SkipBuild) {
    Write-Step "Building Station Pico DVI Demo (RP2350)..."
    $buildDir = Join-Path $srcDir "build_rp2350"
    if (-not (Test-Path $buildDir)) {
        New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
    }

    Push-Location $buildDir
    Write-Host "   Configuring with CMake..." -ForegroundColor Gray

    $vcvarsBat = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
    if (Test-Path $vcvarsBat) {
        $cmakeCmd = "cd /d ""$buildDir"" && call ""$vcvarsBat"" x64 >nul 2>&1 && cmake -G Ninja ""-DPICO_SDK_PATH=$sdkDir"" -DPICO_BOARD=pico2_w -DPICO_PLATFORM=rp2350 -DPICO_COPY_TO_RAM=1 -DDVI_DEFAULT_SERIAL_CONFIG=pico_sock_cfg -DCMAKE_BUILD_TYPE=Release ""$srcDir"""
        cmd /c $cmakeCmd
    } else {
        cmake -G Ninja `
            "-DPICO_SDK_PATH=$sdkDir" `
            -DPICO_BOARD=pico2_w `
            -DPICO_PLATFORM=rp2350 `
            -DPICO_COPY_TO_RAM=1 `
            -DDVI_DEFAULT_SERIAL_CONFIG=pico_sock_cfg `
            -DCMAKE_BUILD_TYPE=Release `
            "$srcDir"
    }

    if ($LASTEXITCODE -ne 0) {
        Pop-Location
        Write-Err "CMake configuration failed"
    }

    Write-Host "   Compiling station_demo..." -ForegroundColor Gray
    $jobs = [System.Environment]::ProcessorCount
    if (Test-Path $vcvarsBat) {
        $buildCmd = "cd /d ""$buildDir"" && call ""$vcvarsBat"" x64 >nul 2>&1 && cmake --build . --target station_demo -- -j $jobs"
        cmd /c $buildCmd
    } else {
        cmake --build . --target station_demo -- -j $jobs
    }
    if ($LASTEXITCODE -ne 0) {
        Pop-Location
        Write-Err "Build failed"
    }
    Pop-Location
    Write-OK "Build complete!"
}

# ── Step 9: Flash to Pico ───────────────────────────────────────────────────
if (-not $SkipFlash -and $picoDrive) {
    Write-Step "Flashing firmware to Pico..."
    $uf2File = Join-Path $srcDir "build_rp2350\apps\station_demo\station_demo.uf2"
    if (Test-Path $uf2File) {
        Copy-Item -Path $uf2File -Destination "${picoLetter}:\" -Force
        Write-OK "Firmware flashed! Pico will reboot and start the demo."
    } else {
        Write-Host "   [FAIL] UF2 not found at $uf2File" -ForegroundColor Red
    }
}

# ── Done ─────────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "============================================" -ForegroundColor Green
Write-Host " Station Pico Setup Complete!" -ForegroundColor Green
Write-Host "============================================" -ForegroundColor Green
Write-Host ""
Write-Host "  SDK:      $sdkDir"
Write-Host "  ARM GCC:  $armGccDir"
Write-Host "  Source:   $srcDir"
Write-Host "  Build:    $(Join-Path $srcDir 'build_rp2350')"
Write-Host ""
Write-Host " Quick commands:" -ForegroundColor Yellow
Write-Host "   .\build.ps1                    # Rebuild station_demo"
Write-Host "   .\build.ps1 -Flash             # Build and flash"
Write-Host "   .\build.ps1 -Target gui_demo   # Build Waveshare GUI demo"
Write-Host ""