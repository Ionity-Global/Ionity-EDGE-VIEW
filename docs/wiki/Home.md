# Welcome to the IO-nity EDGE-VIEW Wiki

**EDGE-VIEW** is a Raspberry Pi Pico 2W (RP2350) powered display station driving a Waveshare 10.1" DVI LCD panel via PIO-based TMDS output.

## Quick Links

- [Getting Started](Getting-Started)
- [Build System](Build-System)
- [Stream Protocol](Stream-Protocol)
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
│       ├── libionity/       SDK-Ionity custom library
│       ├── libdvi/          DVI/TMDS encoding library
│       ├── libgui/          GUI drawing library
│       ├── libsprite/       Sprite rendering library
│       └── apps/            Application demos
├── docs/                    GitHub Pages website
├── .sdk/                    SDK and toolchain (gitignored)
├── StationPico.ps1          Main management tool
├── build_native.bat         Low-level build script
└── README.md                Project documentation
```

## Supported Demos

| Demo | Description |
|------|-------------|
| `station_demo` | Animated dashboard with sprites, panels, WiFi, weather, AI |
| `demo_colors` | 8-colour cycle test |
| `demo_bounce` | 12 bouncing balls |
| `demo_sysinfo` | Hardware diagnostics |
| `demo_rainbow` | Animated rainbow sweep |
| `gui_demo` | Waveshare drawing primitives |
| `hello_dvi` | Scrolling test pattern |

## License

SDK-Ionity custom code — proprietary.  
Waveshare PICO-DVI-LCD-Code — see Waveshare documentation.  
Pico SDK — BSD 3-Clause (Raspberry Pi Ltd).