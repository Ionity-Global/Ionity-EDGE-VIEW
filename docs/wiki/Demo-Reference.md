# Demo Reference

## edgeview (Main Application)

The flagship app. It is a **renderer**: EDGE-VIEW Studio streams key/value data
over WiFi and the panels draw whatever is in the store, falling back to local
sources when the server goes quiet. See [Web Console](Web-Console) and
[Stream Protocol](Stream-Protocol).

### Slots

The screen is ten fixed rectangles. Any panel can be bound to any slot at
runtime with `{"type":"layout","slot":"stage","panel":"clock"}` — no reflash.

| Slot | Region | Default panel |
|---|---|---|
| `header` | y 0–34, full width | `header` |
| `info_a` | y 38–129, x 4–243 | `network` |
| `info_b` | y 38–129, x 248–493 | `feed` |
| `info_c` | y 38–129, x 498–635 | `weather` |
| `stage` | y 134–345, x 4–413 | `game` |
| `side` | y 134–345, x 418–635 | `rotate` |
| `marquee` | y 350–379, full width | `marquee` |
| `ticker` | y 383–424, full width | `news` |
| `stats` | y 429–448, full width | `stats` |
| `footer` | y 453–479, full width | `footer` |

### Panels

| Panel | Draws | Server keys | Local fallback |
|---|---|---|---|
| `header` | Brand mark, title, subtitle, clock, link dot | `hdr.title`, `hdr.sub`, `clock` | NTP clock |
| `clock` | Large centred clock, scales to its slot | `clock`, `date` | NTP |
| `weather` | Temperature, condition, place, animated icon | `wx.temp`, `wx.cond`, `wx.place` | On-device Open-Meteo |
| `network` | State, SSID, IP, ports, 4-bar RSSI meter | — | live |
| `feed` | `STREAM n keys` + age, or `LOCAL FALLBACK MODE` | — | live |
| `game` | Self-playing Pac-Man / Snake / Bounce | — | on-device AI |
| `verse` | Word-wrapped Today's Verse | `verse.text`, `verse.ref` | last pushed |
| `qr` | QR of `http://<pico-ip>/` | — | live |
| `rotate` | Alternates verse and QR every ~12 s | as above | as above |
| `text` | Free-form title + body | `text.title`, `text.body` | — |
| `marquee` | Scrolling line | `marquee` | HTTP message board |
| `news` | Headline ticker (12-entry ring) | pushed by Studio | built-in fallbacks |
| `stats` | FPS, clock speed, uptime, `SERVER-DRIVEN`/`LOCAL` | — | live |
| `logo` | Brand mark only | — | — |
| `footer` | Footer bar | `foot.left` | — |
| `blank` | Clears the slot | — | — |

### On-device behaviour

- **Game AI**: BFS-driven Pac-Man (4 ghost personalities, scatter/chase cycles,
  frightened mode, 3 lives, auto level advance), Snake, or Bounce. Switch with
  `{"type":"mode","name":"pacman"}` — the field is `name`.
- **Clock**: NTP against `pool.ntp.org`, +2 h SAST, resynced every 6 hours.
  A `clock` key from the server takes precedence.
- **Weather**: on-device Open-Meteo over plain HTTP for Centurion ZA, every
  15 minutes. Server values win while they are fresh.
- **Staleness**: a streamed value expires after 5 minutes, at which point the
  panel reverts to its local source.
- **WiFi LED**: solid when connected, 500 ms blink while connecting. A watchdog
  reconnects if the router drops; the screen keeps running offline.

Core 0: WiFi, NTP, weather, stream, rendering. Core 1: DVI TMDS output.

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