# Welcome to the IO-nity EDGE-VIEW Wiki

**EDGE-VIEW** is a Raspberry Pi Pico 2W (RP2350) display station driving a
Waveshare 10.1" DVI LCD panel via PIO-based TMDS output.

The Pico is a **renderer**, not the brain. EDGE-VIEW Studio fetches the clock,
weather, scripture and AI headlines and streams them as key/value data into
named screen slots. The layout is interchangeable at runtime — any panel can be
moved to any slot over the wire, with no reflash. When the server goes quiet the
device falls back to what it can work out on its own.

## Quick Links

- [Getting Started](Getting-Started)
- [Build System](Build-System)
- [Stream Protocol](Stream-Protocol)
- [Web Console](Web-Console)
- [Hardware Setup](Hardware-Setup)
- [Demo Reference](Demo-Reference)

## System Overview

| Component | Detail |
|-----------|--------|
| **MCU** | RP2350 (Dual Cortex-M33) @ 252 MHz |
| **Display** | Waveshare 10.1" IPS, DVI input, 1024×600 |
| **Video** | PIO TMDS, 640×480p60, 3-bit RGB (8 colours) |
| **WiFi** | CYW43439 (2.4 GHz, 802.11 b/g/n) |
| **RAM** | 520 KB SRAM (PICO_COPY_TO_RAM: OFF) |
| **Power** | Pico: micro-USB; Display: USB-C 5V/2A+ |

## Repository Structure

```
.
├── _waveshare_src/          Waveshare PICO-DVI-LCD-Code
│   └── 01-DVI/
│       ├── libionity/       SDK-Ionity (wifi, stream, data store, scene, http)
│       ├── libdvi/          DVI/TMDS encoding library
│       ├── libgui/          GUI drawing library
│       ├── libsprite/       Sprite rendering library
│       └── apps/            edgeview + demos
├── EdgeViewHost/             self-contained EDGE-VIEW Windows app
├── StationPicoInstaller/     source-build and hardware setup GUI
├── docs/                     GitHub Pages website
│   └── app/                  the web console
├── .sdk/                     SDK and toolchain (gitignored)
├── StationPico.ps1           Main management tool
├── build_native.bat          Low-level build script
└── README.md                 Project documentation
```

## Supported Demos

| Demo | Description |
|------|-------------|
| `edgeview` | Server-driven scene engine: WiFi, NTP, weather, games, verse, QR, news |
| `demo_colors` | 8-colour cycle test |
| `demo_bounce` | 12 bouncing balls |
| `demo_sysinfo` | Hardware diagnostics |
| `demo_rainbow` | Animated rainbow sweep |
| `gui_demo` | Waveshare drawing primitives |
| `hello_dvi` | Scrolling test pattern |

## License

Code: MIT. Trademarks, third-party components and data sources: see the
repository `NOTICE`.

Ionity Global (Pty) Ltd is not affiliated with IONITY GmbH, the European EV
charging network.