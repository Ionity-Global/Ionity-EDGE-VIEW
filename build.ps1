<#
.SYNOPSIS
    Station Pico - Build Script
.DESCRIPTION
    Builds the Station Pico DVI demo for Pico 2W (RP2350).
    Requires setup.ps1 to have been run first.
#>

[CmdletBinding()]
param(
    [string]$ProjectDir = $PSScriptRoot,
    [ValidateSet("edgeview", "gui_demo", "hello_dvi", "demo_bounce", "demo_colors", "demo_sysinfo", "all")]
    [string]$Target = "edgeview",
    [switch]$Flash,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$sdkDir = Join-Path $ProjectDir ".sdk\pico-sdk"
$srcDir = Join-Path $ProjectDir "_waveshare_src\Pico-DVI-LCD-Code\01-DVI"
$buildDir = Join-Path $srcDir "build_rp2350"
$armGccBin = Join-Path $ProjectDir ".sdk\bin"

function Write-Step { param([string]$msg) Write-Host "`n>> $msg" -ForegroundColor Cyan }
function Write-OK   { param([string]$msg) Write-Host "   [OK] $msg" -ForegroundColor Green }
function Write-Err  { param([string]$msg) Write-Host "   [FAIL] $msg" -ForegroundColor Red; exit 1 }

# Verify SDK
Write-Step "Verifying SDK..."
if (-not (Test-Path (Join-Path $sdkDir "pico_sdk_init.cmake"))) {
    Write-Err "Pico SDK not found. Run setup.ps1 first."
}
if (-not (Test-Path (Join-Path $armGccBin "arm-none-eabi-gcc.exe"))) {
    Write-Err "ARM GCC not found. Run setup.ps1 first."
}
Write-OK "SDK and toolchain found"

# Check for Ninja
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    Write-Err "Ninja build system not found on PATH. Install Ninja and try again."
}

# Set environment
$env:PICO_SDK_PATH = $sdkDir
$env:PATH = "$armGccBin;$env:PATH"

# Detect and set up host compiler (MSVC) for picotool/pioasm
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
$useVcvars = Test-Path $vcvars

# Clean if requested
if ($Clean -and (Test-Path $buildDir)) {
    Write-Step "Cleaning build directory..."
    Remove-Item -Recurse -Force $buildDir
}

# Create build directory
if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
}

# Configure
Write-Step "Configuring with CMake (RP2350)..."
Push-Location $buildDir

if ($useVcvars) {
    $cmakeCmd = "cd /d ""$buildDir"" && call ""$vcvars"" x64 >nul 2>&1 && cmake -G Ninja ""-DPICO_SDK_PATH=$sdkDir"" -DPICO_BOARD=pico2_w -DPICO_PLATFORM=rp2350 -DPICO_COPY_TO_RAM=0 -DDVI_DEFAULT_SERIAL_CONFIG=pico_sock_cfg -DCMAKE_BUILD_TYPE=Release ""$srcDir"""
    cmd /c $cmakeCmd
} else {
    cmake -G Ninja `
        "-DPICO_SDK_PATH=$sdkDir" `
        -DPICO_BOARD=pico2_w `
        -DPICO_PLATFORM=rp2350 `
        -DPICO_COPY_TO_RAM=0 `
        -DDVI_DEFAULT_SERIAL_CONFIG=pico_sock_cfg `
        -DCMAKE_BUILD_TYPE=Release `
        "$srcDir"
}

if ($LASTEXITCODE -ne 0) { Write-Err "CMake configuration failed" }
Write-OK "Configuration complete"

# Build
Write-Step "Building $Target..."
$jobs = [System.Environment]::ProcessorCount
if ($Target -eq "all") {
    if ($useVcvars) {
        $buildCmd = "cd /d ""$buildDir"" && call ""$vcvars"" x64 >nul 2>&1 && cmake --build . -- -j $jobs"
        cmd /c $buildCmd
    } else {
        cmake --build . -- -j $jobs
    }
} else {
    if ($useVcvars) {
        $buildCmd = "cd /d ""$buildDir"" && call ""$vcvars"" x64 >nul 2>&1 && cmake --build . --target $Target -- -j $jobs"
        cmd /c $buildCmd
    } else {
        cmake --build . --target $Target -- -j $jobs
    }
}
if ($LASTEXITCODE -ne 0) { Write-Err "Build failed" }
Pop-Location

$uf2File = Join-Path $buildDir "apps\$Target\$Target.uf2"
if ($Target -eq "all") {
    $uf2File = Join-Path $buildDir "apps\edgeview\edgeview.uf2"
}

Write-OK "Build complete: $uf2File"

# Flash
if ($Flash) {
    Write-Step "Looking for Pico in BOOTSEL mode..."
    $picoDrive = Get-Volume | Where-Object { $_.FileSystemLabel -eq "RP2350" -or $_.FileSystemLabel -eq "RPI-RP2" }
    if ($picoDrive) {
        Copy-Item -Path $uf2File -Destination "$($picoDrive.DriveLetter):\" -Force
        Write-OK "Flashed to Pico on drive $($picoDrive.DriveLetter):"
    } else {
        Write-Host "   [!!] Pico not found. Hold BOOTSEL and reconnect." -ForegroundColor Yellow
    }
}

Write-Host ""