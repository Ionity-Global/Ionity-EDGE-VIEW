# Getting Started

## Prerequisites

- Windows 10/11
- Git
- CMake 3.12+
- Ninja build system
- Visual Studio Build Tools 2022 (for MSVC host tools)

## One-command Setup

```powershell
.\StationPico.ps1 -Action setup
```

This downloads:
- ARM GCC Toolchain 14.2 → `.sdk\bin\`
- Pico SDK 2.1.1 → `.sdk\pico-sdk\`
- Waveshare PICO-DVI-LCD-Code → `_waveshare_src\`

## Building

```powershell
# Build all demos
.\StationPico.ps1 -Action build

# Build a specific demo
.\StationPico.ps1 -Action build -Target station_demo

# Build and flash in one step
.\StationPico.ps1 -Action buildflash -Target demo_rainbow
```

## Entering BOOTSEL Mode

1. Hold the **BOOTSEL** button on the Pico 2W
2. Press and release **RESET**
3. Release BOOTSEL
4. The Pico appears as a mass storage drive labelled `RP2350`

## Flashing

```powershell
.\StationPico.ps1 -Action flash -Target station_demo
```

Or use the interactive menu:

```powershell
.\StationPico.ps1
```

## Quick Build (Command Line)

```bat
rebuild.bat          # Clean rebuild of station_demo + flash
build_native.bat     # Full build of all demos (uses MSVC for host tools)
flash.bat            # Build + flash station_demo
```