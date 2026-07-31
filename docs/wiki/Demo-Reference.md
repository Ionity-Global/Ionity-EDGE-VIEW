# Demo Reference

## station_demo (Main Dashboard)

The primary EDGE-VIEW application. Displays an animated dashboard with:

- **IO-nity header** with red brand bar
- **Hardware Info panel**: board, chip, display, interface, panel res, DVI mode
- **Network panel**: WiFi SSID, IP, connection state, RSSI, stream status
- **Weather panel**: temperature and condition (updated via TCP stream)
- **AI status**: online/offline indicator with last message
- **6 animated sprites** bouncing within the safe area
- **FPS counter** (time-based, updated every second)
- **Footer** with clock speed and uptime
- WiFi LED: solid when connected, 500ms blink when connecting

Core 0: WiFi + stream + rendering. Core 1: DVI TMDS output.

## demo_colors

Cycles through all 8 available display colours (3-bit RGB: black, blue, green, cyan, red, magenta, yellow, white) at 2-second intervals. Each colour fills the screen with the colour name and a "Station Pico" label.

## demo_bounce

12 animated balls with varying sizes (radius 8-35), speeds, and colours bounce off the walls with a white border frame. 640×480 at 60 fps.

## demo_sysinfo

Diagnostic display showing:
- CPU: system clock, DVI clock, platform, cores
- Memory: SRAM, flash, framebuffer size, colour mode
- Display: Waveshare model, panel specs, DVI pinout
- Power: voltage, WiFi/BT info

## demo_rainbow

Animated rainbow gradient with:
- 12-colour vertical rainbow bars (left panel, 120px wide)
- Horizontal animated stripes (right panel)
- Centre overlay with "RAINBOW DEMO" title and system info
- Smooth 30ms animation loop