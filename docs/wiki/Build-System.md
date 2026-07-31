# Build System

The EDGE-VIEW firmware uses **CMake + Ninja** with the Pico SDK 2.1.1 targeting the **RP2350** (Pico 2W).

## Key Build Options

| Option | Value | Purpose |
|--------|-------|---------|
| `PICO_BOARD` | `pico2_w` | Target board |
| `PICO_PLATFORM` | `rp2350` | ARM Cortex-M33 |
| `PICO_COPY_TO_RAM` | `0` | Run from flash (frees SRAM for CYW43) |
| `DVI_DEFAULT_SERIAL_CONFIG` | `pico_sock_cfg` | DVI pin mapping |
| `CMAKE_BUILD_TYPE` | `Release` | Optimised build |

## Build Scripts

| Script | Purpose |
|--------|---------|
| `StationPico.ps1` | Main management tool (menu, setup, build, flash, status) |
| `build_native.bat` | Low-level CMake build via MSVC + ARM GCC |
| `build.ps1` | PowerShell equivalent with -Flash and -Clean flags |
| `rebuild.bat` | Clean rebuild of `edgeview` only |
| `flash.bat` | Build + flash wrapper |
| `setup.bat` / `setup.ps1` | First-time dependency installation |

## CMake Structure

```
01-DVI/CMakeLists.txt           # Top-level: project, SDK init, subdirs
├── libdvi/CMakeLists.txt       # DVI library
├── libsprite/CMakeLists.txt    # Sprite library
├── libgui/CMakeLists.txt       # GUI library
├── libionity/CMakeLists.txt    # SDK-Ionity library
└── apps/CMakeLists.txt         # Demo applications
    ├── edgeview/
    ├── demo_colors/
    ├── demo_bounce/
    ├── demo_sysinfo/
    ├── demo_rainbow/
    ├── gui_demo/
    └── hello_dvi/
```

## WiFi Configuration

WiFi credentials are set at build time in `libionity/CMakeLists.txt`:

```cmake
target_compile_definitions(libionity INTERFACE
    IONITY_WIFI_SSID="YourSSID"
    IONITY_WIFI_PASS="YourPassword"
    IONITY_STREAM_PORT=4242
)
```

## Important: Init Order

WiFi must be initialised **before** the DVI clock change. The system clock is set to 252 MHz for DVI output, but the CYW43439 SPI cannot communicate at that frequency. See `edgeview/main.c` for the correct init sequence.