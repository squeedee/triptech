# Triptech Carrier Board Design

## Overview

Custom carrier board for the Daisy Seed2 DFM, housing the Triptech three-channel filtered gate effect in a low-profile
metal desktop enclosure. The carrier breaks out all user-facing I/O: displays, illuminated keys, encoders, MIDI, USB,
and power.

## Core Module

**Daisy Seed** (Rev7) — STM32H750 module with stereo codec, 64 MB RAM, 8 MB flash. Mounts to the carrier board via two
rows of 20-pin headers (2x20, 0.1" pitch). The Seed's on-board micro-USB handles DFU flashing directly.

A future revision may migrate to the Seed2 DFM (castellated, soldered) for production.

## User Interface

### Displays

- 3x white 128x32 OLED (SSD1306 or SH1106 driver), 0.91" form factor
- Mounted horizontally in a row in the lower section
- Each displays the active parameter name, value, and bar for its corresponding encoder
- Driven via SPI (preferred for refresh rate) with individual CS lines, or I2C via multiplexer

### Keys

- 7x low-profile mechanical keyboard-style switches with integrated or underlit addressable RGB LEDs
- Layout:
    - Left column (3 keys): RUN, BYP, SHIFT
    - Top row (4 keys): CH1, CH2, CH3, GLOBAL
- RGB LEDs driven as a single addressable chain (WS2812B / SK6812 compatible) — 1 GPIO

### Encoders

- 3x rotary encoders with push-switch (detented)
- One centered below each OLED display
- 2 GPIOs per encoder (A/B quadrature) + 1 GPIO per push-switch = 9 GPIO total

### Indicator LEDs

- 3x single-color LEDs alongside the parameter group reference diagram
- Accent LEDs indicating the active parameter page (FILTER / AMP / MIXER)
- Can be added to the addressable RGB chain (10 LEDs total) or driven as discrete GPIOs

## Connectivity

### Power

- DC barrel jack (2.1mm center-positive)
- Input: 9–12 V DC
- On-board regulation: 5 V (for LEDs, displays) and 3.3 V (via Seed2 DFM internal regulator)
- The Seed2 DFM accepts 5 V on its VIN pin

### MIDI

- TRS or 5-pin DIN connectors (TBD) for MIDI IN, OUT, and THRU
- MIDI IN: opto-isolated (6N138 or H11L1), active circuit into Seed UART RX
- MIDI OUT: buffered UART TX
- MIDI THRU: hard-wired from opto-isolator output (no MCU involvement)

### USB (dual-port)

- **USB-C on carrier** (primary): wired to Seed header pins 36/37 (USB OTG HS D-/D+), running in FS device mode. Used
  for MIDI-over-USB in normal operation.
- **Seed's on-board micro-USB** (secondary): USB OTG FS, used for DFU firmware flashing. Accessible via a rear/side
  panel opening. These are two independent USB peripherals on the STM32H750.

## GPIO Budget (Seed2 DFM)

| Function             | Pins       | Interface                |
|----------------------|------------|--------------------------|
| OLED displays (SPI)  | 5–6        | SPI CLK, MOSI, 3x CS, DC |
| OLED reset           | 1          | Shared reset line        |
| Addressable RGB LEDs | 1          | Single data pin          |
| Encoder A/B (×3)     | 6          | Digital GPIO             |
| Encoder switch (×3)  | 3          | Digital GPIO             |
| MIDI UART TX/RX      | 2          | UART                     |
| USB HS D-/D+ (USB-C) | 2          | USB OTG HS (pins 36/37)  |
| Page indicator LEDs  | 0–3        | GPIO or on RGB chain     |
| **Total**            | **~20–22** |                          |

The Daisy Seed provides 31 GPIO — sufficient with headroom. USB is handled by the Seed's on-board micro-USB port.

## Components to Source

| Component                        | Qty | Key Requirements                                                                       |
|----------------------------------|-----|----------------------------------------------------------------------------------------|
| 128x32 white OLED (SSD1306, SPI) | 3   | 0.91", 4-pin or 7-pin, US distributor                                                  |
| Keyboard switch with RGB LED     | 7   | Low-profile, mechanical, Kailh Choc or similar with SK6812 mini-e or WS2812B underglow |
| Rotary encoder with push-switch  | 3   | Detented, PCB mount, good tactile feel (Bourns PEC11 or Alps EC11 series)              |
| DC barrel jack                   | 1   | 2.1mm center-positive, panel or PCB mount                                              |
| USB-C receptacle                 | 1   | USB 2.0 device, 16-pin SMD                                                             |
| 2x20 pin headers (female)        | 2   | 0.1" pitch, for Daisy Seed mounting                                                    |
| Opto-isolator (MIDI IN)          | 1   | 6N138 or H11L1M                                                                        |
| 5V regulator                     | 1   | LDO or buck, ≥500 mA for LEDs + displays                                               |

## Open Questions

1. **MIDI connectors**: 5-pin DIN or TRS type-A? TRS saves panel space in a low-profile enclosure.
2. **Display interface**: SPI (faster, more pins) vs I2C + TCA9548A mux (fewer pins, slower refresh). SPI is
   recommended.
3. **Key switches**: Off-the-shelf Kailh Choc with separate SK6812 LEDs, or a combined key+LED module?
4. **Indicator LEDs**: Discrete GPIO-driven or appended to the addressable RGB chain?
5. **Micro-USB access**: Placement of rear/side panel opening for the Seed's on-board micro-USB (DFU flashing only).
