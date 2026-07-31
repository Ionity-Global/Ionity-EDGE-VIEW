# Station Pico

**Raspberry Pi Pico 2W + Waveshare 10.1" DVI Display**

A custom firmware project that drives a Waveshare 10.1" IPS LCD panel via PIO-based DVI output from a Raspberry Pi Pico 2W (RP2350). Includes animated dashboards, WiFi connectivity, and a TCP stream server for remote control.

## Hardware

| Component | Details |
|-----------|---------|
| **MCU** | Raspberry Pi Pico 2W (RP2350, dual Cortex-M33) |
| **Display** | Waveshare 10.1" IPS LCD with DVI input (PICO-DVI-10.1") |
| **Video** | DVI via PIO TMDS, 640x480p60, 3-bit RGB (8 colors) |
| **WiFi** | CYW43439 (2.4 GHz, included on Pico 2W) |
| **Power** | Pico via micro-USB; Display board via USB-C (5V/2A+) |

### Wiring

| Pico GPIO | Signal |
|-----------|--------|
| GPIO 8 | DVI Clock |
| GPIO 10 | TMDS D0 (Blue) |
| GPIO 12 | TMDS D1 (Green) |
| GPIO 14 | TMDS D2 (Red) |

## Demos

| Demo | Description |
|------|-------------|
| `station_demo` | Animated dashboard with info panels, sprites, WiFi status, weather, AI stream |
| `demo_colors` | Cycles through all 8 display colors |
| `demo_bounce` | 12 animated balls bouncing off walls |
| `demo_sysinfo` | Hardware specs and diagnostics |
| `demo_rainbow` | Animated rainbow gradient sweep |
| `gui_demo` | Waveshare drawing primitives demo |
| `hello_dvi` | Scrolling test pattern image |

## Quick Start

### Prerequisites

- Windows 10/11
- Git
- CMake (3.12+)
- Ninja build system
- ARM GCC toolchain (automatically downloaded)
- Visual Studio Build Tools 2022 (MSVC for host tools)

### One-command setup

```powershell
.\StationPico.ps1 -Action setup
```

This downloads ARM GCC, the Pico SDK, and Waveshare source code into `.sdk\` and `_waveshare_src\`.

### Build & flash

```powershell
# Interactive menu
.\StationPico.ps1

# Build a specific demo
.\StationPico.ps1 -Action build -Target station_demo

# Build and flash in one step
.\StationPico.ps1 -Action buildflash -Target demo_rainbow

# Check system status
.\StationPico.ps1 -Action status
```

### Enter BOOTSEL mode

1. Hold the **BOOTSEL** button on the Pico 2W
2. Press and release **RESET**
3. Release BOOTSEL
4. The Pico appears as a mass storage drive labeled `RP2350`

## Build System

The project uses **CMake + Ninja** with the Pico SDK 2.1.1. The build scripts:

- `StationPico.ps1` — Main management tool (menu, setup, build, flash, status)
- `build_native.bat` — Low-level CMake build (MSVC + ARM GCC)
- `build.ps1` — PowerShell equivalent with additional options
- `flash.bat` — Quick build + flash wrapper
- `rebuild.bat` — Clean rebuild of station_demo
- `setup.bat` / `setup.ps1` — First-time dependency installation

## Architecture

```
libionity/       SDK-Ionity custom library
├── ionity_wifi.c/h    WiFi provisioning and management
├── ionity_stream.c/h  TCP stream server for remote commands
├── ionity_pixels.c/h  Pixel-level drawing utilities
└── ionity_brand.c/h   Branding, header/footer, panels

libdvi/          DVI/TMDS encoding library (PIO-based)
libgui/          GUI drawing library (Waveshare)
libsprite/       Sprite rendering library

apps/            Application demos
└── station_demo/    Main dashboard application
```

## Stream Protocol

The Pico runs a TCP server on port 4242. Send JSON commands to control the dashboard:

```json
{"type":"ping"}
{"type":"weather","temp":25,"cond":1}
{"type":"text","text":"Hello from AI"}
{"type":"sprite",0,100,200,2,2}
{"type":"reboot"}
```

## WiFi Credentials

WiFi SSID and password are compiled into the firmware at build time via `libionity/CMakeLists.txt`. Edit these values or configure them via environment variables:

```cmake
target_compile_definitions(libionity INTERFACE
    IONITY_WIFI_SSID="YourSSID"
    IONITY_WIFI_PASS="YourPassword"
    IONITY_STREAM_PORT=4242
)
```

## License

SDK-Ionity custom code — proprietary.
Waveshare PICO-DVI-LCD-Code — see Waveshare documentation.
Pico SDK — BSD 3-Clause (Raspberry Pi Ltd).