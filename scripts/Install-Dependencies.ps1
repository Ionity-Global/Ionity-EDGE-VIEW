<#
.SYNOPSIS
    Installs everything EDGE-VIEW needs to build and run.

.DESCRIPTION
    Checks for each dependency and only fetches what is missing. Uses winget
    where available so nothing is downloaded from unofficial mirrors.

    Host build:  .NET SDK 10
    Firmware:    CMake, Ninja, ARM GCC, Pico SDK 2.1.1
    Optional:    Ollama, for the local chat brain
#>
[CmdletBinding()]
param(
    [switch]$Firmware,
    [switch]$Brain,
    [switch]$All
)

$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if ($All) { $Firmware = $true; $Brain = $true }

function Head($msg) {
    Write-Host ''
    Write-Host "  $msg" -ForegroundColor Red
    Write-Host '  ------------------------------------------------' -ForegroundColor DarkGray
}
function Ok($msg)   { Write-Host "  [ok]   $msg" -ForegroundColor Green }
function Miss($msg) { Write-Host "  [get]  $msg" -ForegroundColor Yellow }
function Bad($msg)  { Write-Host "  [fail] $msg" -ForegroundColor Red }
function Note($msg) { Write-Host "         $msg" -ForegroundColor DarkGray }

$hasWinget = [bool](Get-Command winget -ErrorAction SilentlyContinue)

function Need($command, $label, $wingetId) {
    if (Get-Command $command -ErrorAction SilentlyContinue) { Ok $label; return $true }
    Miss $label
    if (-not $hasWinget) {
        Bad "winget is unavailable — install $label by hand"
        return $false
    }
    winget install --id $wingetId --accept-source-agreements --accept-package-agreements --silent | Out-Null
    if (Get-Command $command -ErrorAction SilentlyContinue) { Ok "$label installed"; return $true }
    Note "Installed, but not on PATH yet. Open a new terminal and re-run."
    return $false
}

Head 'IO-nity EDGE-VIEW dependencies'

# ---- Host ----
Head 'Host build'
$dotnetOk = Need 'dotnet' '.NET SDK' 'Microsoft.DotNet.SDK.10'
if ($dotnetOk) {
    $v = (dotnet --version 2>$null)
    Note "dotnet $v"
    if ($v -and [version]($v -replace '-.*$') -lt [version]'10.0') {
        Bad 'EDGE-VIEW needs .NET 10 or newer'
    }
}

# ---- Firmware ----
if ($Firmware) {
    Head 'Firmware toolchain'
    Need 'cmake' 'CMake' 'Kitware.CMake'  | Out-Null
    Need 'ninja' 'Ninja'  'Ninja-build.Ninja' | Out-Null

    $armGcc = Join-Path $root '.sdk\bin\arm-none-eabi-gcc.exe'
    if (Test-Path $armGcc) { Ok 'ARM GCC' }
    else {
        Miss 'ARM GCC + Pico SDK'
        Note 'Delegating to setup.ps1, which knows the exact versions.'
        & (Join-Path $root 'setup.ps1') -ProjectDir $root
    }

    $picoSdk = Join-Path $root '.sdk\pico-sdk'
    if (Test-Path (Join-Path $picoSdk 'pico_sdk_init.cmake')) { Ok 'Pico SDK' } else { Bad 'Pico SDK missing' }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) { Ok 'Visual Studio build tools (host compiler)' }
    else { Miss 'Visual Studio Build Tools — needed for pioasm and the host tests' }
}

# ---- Local brain ----
if ($Brain) {
    Head 'Local chat brain (optional)'
    Note 'Chat works without this, on built-in replies. A small local model'
    Note 'makes it conversational. Nothing is ever sent off the machine.'
    if (Get-Command ollama -ErrorAction SilentlyContinue) {
        Ok 'Ollama'
        $models = (ollama list 2>$null | Out-String)
        if ($models -match 'qwen2.5:1.5b') { Ok 'qwen2.5:1.5b' }
        else {
            Miss 'qwen2.5:1.5b (about 1 GB, comfortable alongside everything else)'
            ollama pull qwen2.5:1.5b
        }
    } else {
        Miss 'Ollama'
        if ($hasWinget) { winget install --id Ollama.Ollama --accept-package-agreements --silent | Out-Null }
        Note 'Then: ollama pull qwen2.5:1.5b'
    }
}

Head 'Next'
Write-Host '  .\build.bat            Build and refresh EdgeView.exe' -ForegroundColor Gray
Write-Host '  .\build.bat -Firmware  Also build the Pico UF2' -ForegroundColor Gray
Write-Host '  .\EdgeView.exe         Run the server, then open the printed URL' -ForegroundColor Gray
Write-Host ''
if (-not $Firmware) { Note 'Add -Firmware for the Pico toolchain, -All for everything.' }
Write-Host ''
