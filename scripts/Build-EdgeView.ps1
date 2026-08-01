<#
.SYNOPSIS
    Builds EDGE-VIEW and refreshes the master executable in the repo root.

.DESCRIPTION
    Produces EdgeView.exe in the main folder: one self-contained file with the
    bridge, feed scheduler, AI, local chat brain and the whole web console
    compiled in. No .NET runtime needed on the machine that runs it.

.EXAMPLE
    .\build.bat                 Release build, refresh EdgeView.exe
    .\build.bat -Debug          Debug build with symbols and console tracing
    .\build.bat -Firmware       Also build the Pico firmware UF2
    .\build.bat -Test           Run the host tests
    .\build.bat -Clean          Wipe intermediates first
#>
param(
    [switch]$Debug,
    [switch]$Firmware,
    [switch]$Test,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$config = if ($Debug) { 'Debug' } else { 'Release' }
$exeName = 'EdgeView.exe'
$target  = Join-Path $root $exeName

function Say($msg, $colour = 'Gray') { Write-Host "  $msg" -ForegroundColor $colour }
function Head($msg) {
    Write-Host ''
    Write-Host "  $msg" -ForegroundColor Red
    Write-Host '  ------------------------------------------------' -ForegroundColor DarkGray
}

Head "IO-nity EDGE-VIEW build ($config)"

if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
    Say 'The .NET SDK is missing. Run install.bat first.' Yellow
    exit 1
}

if ($Clean) {
    Say 'Cleaning intermediates...'
    Get-ChildItem -Path $root -Include bin, obj -Recurse -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notlike '*\.sdk\*' } |
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
}

# A running instance holds the file open and the publish would fail late.
Get-Process EdgeView, EdgeViewHost, StationPicoInstaller -ErrorAction SilentlyContinue |
    ForEach-Object { Say "Stopping $($_.ProcessName) (pid $($_.Id))"; $_ | Stop-Process -Force }

Say 'Building solution...'
dotnet build EdgeView.slnx -c $config -v q --nologo | Out-Null
if ($LASTEXITCODE -ne 0) { Say 'Solution build failed.' Red; exit 1 }
Say 'Solution OK' Green

if ($Test) {
    Head 'Host tests'
    $dvi = Join-Path $root '_waveshare_src\Pico-DVI-LCD-Code\01-DVI'
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -property installationPath
        $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
        $out = Join-Path $env:TEMP 'edgeview-tests'
        New-Item -ItemType Directory -Force -Path $out | Out-Null
        cmd /c "call `"$vcvars`" >nul 2>&1 && cl /nologo /W4 /I `"$dvi\libionity`" /Fe:$out\mirror_test.exe /Fo:$out\ `"$dvi\tools\mirror_test.c`" `"$dvi\libionity\ionity_mirror_encode.c`" >nul && $out\mirror_test.exe"
        if ($LASTEXITCODE -ne 0) { Say 'Mirror encoder test FAILED' Red; exit 1 }
    } else {
        Say 'MSVC not found, skipping host tests' Yellow
    }
}

# Stage firmware first so the single-file desktop publish embeds the same UF2
# offered by the repo and browser console.
if ($Firmware) {
    Head 'Firmware'
    & (Join-Path $root 'build_native.bat') | Out-Null
    $uf2 = Join-Path $root '_waveshare_src\Pico-DVI-LCD-Code\01-DVI\build_rp2350\apps\edgeview\edgeview.uf2'
    if (-not (Test-Path $uf2)) { Say 'Firmware build produced no UF2' Red; exit 1 }

    $firmwareDirs = @(
        (Join-Path $root 'docs\app\firmware'),
        (Join-Path $root 'web\firmware')
    )
    $bytes = (Get-Item $uf2).Length
    $manifest = @{
        commit = (git describe --always --dirty 2>$null)
        built  = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        board  = 'pico2_w'
        builds = @(@{ name = 'edgeview'; file = 'edgeview.uf2'; size = $bytes })
    } | ConvertTo-Json -Depth 4

    foreach ($dir in $firmwareDirs) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        Copy-Item $uf2 (Join-Path $dir 'edgeview.uf2') -Force
        $manifest | Set-Content (Join-Path $dir 'manifest.json')
    }
    Copy-Item $uf2 (Join-Path $root 'edgeview.uf2') -Force
    Say "edgeview.uf2 staged and ready to embed ($([math]::Round($bytes/1KB)) KB)" Green
}

Head 'Publishing the master executable'
$publish = Join-Path $env:TEMP 'edgeview-publish'
Remove-Item $publish -Recurse -Force -ErrorAction SilentlyContinue

$publishArgs = @(
    'publish', 'EdgeViewHost',
    '-c', $config,
    '-o', $publish,
    '--nologo', '-v', 'q'
)
if ($Debug) { $publishArgs += @('-p:DebugType=embedded') }

dotnet @publishArgs | Out-Null
if ($LASTEXITCODE -ne 0) { Say 'Publish failed.' Red; exit 1 }

$built = Join-Path $publish 'EdgeView.exe'
if (-not (Test-Path $built)) { Say 'Publish produced no executable.' Red; exit 1 }

Copy-Item $built $target -Force
$size = [math]::Round((Get-Item $target).Length / 1MB, 1)
Say "$exeName refreshed ($size MB, $config)" Green

Head 'Done'
Say "Run it:  .\$exeName"
Say "Console: http://127.0.0.1:8787/"
Write-Host ''
