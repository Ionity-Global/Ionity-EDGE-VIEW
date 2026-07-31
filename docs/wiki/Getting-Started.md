# Getting Started

There are three ways to get firmware onto the board. Pick whichever suits you —
they all end up at the same place.

| Route | You need | Best for |
|---|---|---|
| **Web console** | Chrome or Edge | Fastest. No toolchain, no clone |
| **EDGE-VIEW Studio** | Windows | Guided install, then day-to-day control |
| **Command line** | Toolchain below | Developing the firmware itself |

## Route 1: the web console

1. Open the [Web Console](Web-Console) (`/app/` on the project site).
2. Hold **BOOTSEL**, plug the Pico in. It appears as `RP2350`.
3. Pick a build, press **⚡ Flash over USB**, approve the device prompt.

That is the whole thing — the UF2s are built by CI and published next to the
site. If WebUSB is blocked on your machine, **💾 Copy to RPI-RP2** does the same
job through the file picker.

To *control* the display afterwards you still need the server running: install
EDGE-VIEW Studio, turn on the **Web Console Bridge** and pair with the token.

## Route 2: EDGE-VIEW Studio

The Studio app installs the toolchain for you, builds, flashes, and then becomes
the server that drives the screen.

| Tab | What it does |
|---|---|
| **Install** | Downloads ARM GCC + Pico SDK, writes WiFi config, builds, flashes |
| **Control** | Device IP and discovery, game mode, time sync, reboot, web bridge |
| **Screen** | Slot/panel composer, stream a value, remote WiFi reprovisioning |
| **Feeds** | Verse, AI news, weather and time schedulers |
| **Messages** | Post to the on-screen message board, share the QR link |
| **About** | Version, contact, licence |

## Route 3: command line

### Prerequisites

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
.\StationPico.ps1 -Action build -Target edgeview

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
.\StationPico.ps1 -Action flash -Target edgeview
```

Or use the interactive menu:

```powershell
.\StationPico.ps1
```

## Quick Build (Command Line)

```bat
rebuild.bat          # Clean rebuild of edgeview + flash
build_native.bat     # Full build of all demos (uses MSVC for host tools)
flash.bat            # Build + flash edgeview
```

## First boot

If no WiFi credentials were compiled in, the display shows a provisioning
screen with its own access point details. Join it, enter your network, and the
device saves the credentials to flash and reboots.

Afterwards it broadcasts a discovery beacon on UDP 4243 every 5 seconds, so
Studio and the console find it without you typing an IP.

## Next steps

- [Web Console](Web-Console) — flashing, live stream, scenes, AI
- [Stream Protocol](Stream-Protocol) — every command the device accepts
- [Demo Reference](Demo-Reference) — slots, panels and what each one draws
