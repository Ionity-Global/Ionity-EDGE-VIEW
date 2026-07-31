# Hardware Setup

## Required Components

- Raspberry Pi Pico 2W (RP2350)
- Waveshare 10.1" DVI LCD display (PICO-DVI-10.1")
- Micro-USB cable (data) for the Pico
- USB-C cable (power) for the display board
- 5V/2A+ USB-C power adapter for the display

## Wiring

| Pico GPIO | DVI Signal | Colour Channel |
|-----------|------------|----------------|
| GPIO 8 | CLK+ | Clock |
| GPIO 10 | TMDS D0+ | Blue |
| GPIO 12 | TMDS D1+ | Green |
| GPIO 14 | TMDS D2+ | Red |

The pin configuration uses `pico_sock_cfg` with inverted differential pairs.

## Power Requirements

**Critical: Two separate power sources are required.**

| Component | Power | Purpose |
|-----------|-------|---------|
| **Pico 2W** | micro-USB (5V) | Firmware, WiFi, PIO DVI output |
| **Display Board** | USB-C (5V/2A+) | LCD panel backlight and DVI receiver |

> ⚠️ Do **not** attempt to power the display from the Pico's 5V rail. The 10.1" panel draws more current than the Pico can supply.

## Voltage Settings

The firmware sets the core voltage to 1.10V (`VREG_VOLTAGE_1_10`) to support the 252 MHz system clock required for DVI output. This is within the RP2350's specification.