# Triptech Carrier Board Design

## Overview

Custom carrier board for the Daisy Seed Rev7, housing the Triptech three-channel filtered gate effect in a low-profile
sheet metal desktop enclosure. The carrier breaks out all user-facing I/O: displays, illuminated keys, encoders, MIDI,
USB, audio, and power.

**EDA**: KiCad 8 — schematic and PCB in `hardware/`
**Enclosure**: OpenSCAD parametric model in `enclosure/`
**BOM**: `hardware/bom.csv` with sourced part numbers

## Core Module

**Daisy Seed** (Rev7) — STM32H750 module with stereo codec, 64 MB RAM, 8 MB flash. Mounts to the carrier board via two
rows of 20-pin female headers (2x20, 0.1" pitch). The Seed's on-board micro-USB handles DFU flashing (internal access —
remove lid).

A future revision may migrate to the Seed2 DFM (castellated, soldered) for production.

## User Interface

### Displays

- 3x white 128x32 OLED (SSD1306), 0.91" form factor, 7-pin SPI modules
- Mounted horizontally in a row in the lower section of the top panel
- Each displays the active parameter name, value, and bar for its corresponding encoder
- Driven via **SPI** with shared SCK/MOSI/DC/RES, individual CS lines per display

### Keys

- 7x **Kailh Choc V1** low-profile mechanical switches in hotswap sockets
- **SK6812 Mini-E** reverse-mount addressable RGB LEDs underneath translucent keycaps
- Layout:
    - Left column (3 keys): RUN, BYP, SHIFT
    - Top row (4 keys): CH1, CH2, CH3, GLOBAL

### Encoders

- 3x **Bourns PEC11R-4215F-S0024** rotary encoders with push-switch (24 detents)
- One centered below each OLED display
- 2 GPIOs per encoder (A/B quadrature) + 1 GPIO per push-switch = 9 GPIO total

### Addressable LED Chain

- **11x SK6812 Mini-E** on a single data chain (1 GPIO — D17, TIM3_CH4 DMA-capable)
- LED 1–7: key backlights (one under each switch)
- LED 8–11: page indicator LEDs (4 pages for extensibility)
- 330R series resistor on data line, 100uF bulk cap on +5V rail
- Powered from +5V (not 3.3V)

## Connectivity

### Power

- **DC barrel jack**: 2.1mm center-positive, **12V DC** input
- **Reverse polarity protection**: P-MOSFET (SI2301CDS, SOT-23) with 10k gate pull-down
- **TVS diode**: SMBJ15A for overvoltage clamping
- **5V buck regulator**: TPS54302 (3A, SOT-23-6), 10uH inductor, 2x 22uF ceramic output
    - FB divider: R_FB1=100k / R_FB2=13.3k → VOUT ≈ 5.11V (VREF=0.6V)
    - R_SER=49.9R series filter resistor between output and FB divider
    - C_FF=75pF feedforward cap in parallel with R_FB1 (loop stability)
    - UVLO via EN divider: R_EN1=100k / R_EN2=13.7k → enable at ~10V
- **±12V DC-DC module** (Traco TMA 1212D, SIP-7): populated for balanced audio I/O
    - +12V_RAW feeds directly to TMA 1212D +Vin
    - COM (pin 5) connects to GNDA (analog ground)
    - Supplies ±12V for THAT1246 input receivers and DRV134 output drivers
- **3.3V**: from Daisy Seed's internal regulator (VIN → 3V3D)

### MIDI

- **3x 5-pin DIN** connectors (CUI SDS-50J, right-angle PCB mount)
- **MIDI IN**: opto-isolated (6N138, DIP-8), 220R input resistor, 1N4148 protection diode,
  270R output pull-up to +3V3D, 4.7k base pull-up
- **MIDI OUT**: 33R series resistors on pins 4 and 5 (3.3V UART drive), UART TX from Seed D13
- **MIDI THRU**: hard-wired from opto-isolator output (pin 6), same 33R series resistors
- UART config: 31250 baud, 8N1 via USART1 (D13 TX / D14 RX)

### USB

- **USB-C on carrier** (primary): GCT USB4085-GF-A receptacle, USB 2.0 device mode
    - Wired to Seed D29/D30 (USB OTG HS D-/D+), running in FS device mode
    - CC1/CC2: 5.1k pull-downs (device identification)
    - 22R series resistors on D+/D-
    - Shield: 1M + 4.7nF to GND (EMI)
    - VBUS: 0R resistor to +5V (DNP — only populate if USB power desired)
- **Seed's on-board micro-USB** (secondary): DFU flashing only, internal access (remove lid)

### Audio I/O

- **4x 1/4" TRS jacks** (Neutrik NRJ6HF): 2 inputs, 2 outputs
- **Balanced I/O**:
    - Input: 2x THAT1246 balanced line receiver (SOIC-8)
    - Output: 2x DRV134 balanced line driver (SOIC-8) with 10uF coupling caps
- **Bypass jumpers** (1x3 pin headers): select direct (unbalanced) or balanced path
    - Unbalanced: jumper pins 1-2 (TRS tip ↔ Seed audio pin directly)
    - Balanced: jumper pins 2-3 (IC output ↔ Seed audio pin)
- TRS sleeves → GNDA, connected to GND via 0R star ground bridge (R_GND, 0805)
- No firmware changes needed — balanced stage is a transparent analog front-end

## GPIO Budget (Carrier Board)

| Function              | Pins | Seed Pins            | Interface                 |
|-----------------------|------|----------------------|---------------------------|
| OLED displays (SPI)   | 6    | D7,D8,D10,D1,D2,D3  | SPI1 SCK/MOSI, 3x CS, DC |
| OLED reset            | 1    | D4                   | Shared reset line         |
| Addressable RGB LEDs  | 1    | D17                  | Single data pin (TIM3)    |
| Encoder A/B (x3)      | 6    | D20-D21,D23-D24,D26-D27 | Digital GPIO          |
| Encoder switch (x3)   | 3    | D22,D25,D28          | Digital GPIO              |
| Key switches (x7)     | 7    | D0,D5,D6,D11,D12,D15,D16 | Digital GPIO         |
| MIDI UART TX/RX       | 2    | D13,D14              | USART1                    |
| USB HS D-/D+ (USB-C)  | 2    | D29,D30              | USB OTG HS (pins 36/37)   |
| **Total used**        | **28** |                    |                           |
| **Spare**             | **3**  | D9,D18,D19         | Available for expansion   |

## Enclosure

- **Sheet metal**: 1.2mm aluminum or steel, bent U-shell with removable top lid
- **Dimensions**: 144.78mm x 95.25mm x 42mm (5.7" x 3.75" x ~1.65")
- **Top panel cutouts**: 3x encoder shafts, 3x OLED windows, 7x Choc switch holes, 4x LED windows
- **Rear panel cutouts**: DC jack, 3x DIN-5 MIDI, USB-C, 4x 1/4" TRS audio
- **No external DFU access**: remove lid to access Seed's micro-USB
- **Mounting**: 4x M3 standoffs, 4x M3 lid screws into tapped bosses
- **OpenSCAD model**: `enclosure/triptech-enclosure.scad` (parametric, exports DXF for fabrication)

## Schematic Structure (KiCad)

| Sheet          | File                | Contents                                        |
|----------------|---------------------|-------------------------------------------------|
| Root           | triptech.kicad_sch  | Hierarchical sheet references, title block       |
| Power          | power.kicad_sch     | DC jack, reverse polarity, TPS54302, ±12V DC-DC  |
| Daisy Seed     | daisy_seed.kicad_sch| Seed module, GPIO labels, bypass caps, star GND  |
| Displays       | displays.kicad_sch  | 3x SSD1306 SPI OLED with bypass caps             |
| UI             | ui.kicad_sch        | 7x Choc switches, 3x encoders, 11x SK6812 LEDs  |
| MIDI           | midi.kicad_sch      | 3x DIN-5, 6N138 opto, OUT/THRU buffers           |
| USB            | usb.kicad_sch       | USB-C receptacle, CC resistors, ESD              |
| Audio          | audio.kicad_sch     | 4x TRS jacks, THAT1246/DRV134, bypass jumpers    |

## Design Decisions (Resolved)

1. **MIDI connectors**: 5-pin DIN. Three connectors: IN, OUT, THRU.
2. **Display interface**: SPI (hardware SPI1 with DMA). Shared bus, individual CS.
3. **Key switches**: Kailh Choc V1 in hotswap sockets with SK6812 Mini-E reverse-mount LEDs.
4. **Indicator LEDs**: 4x SK6812 Mini-E appended to the addressable RGB chain (11 total).
5. **Micro-USB access**: Internal only — remove lid to flash via DFU.
6. **Audio I/O**: Balanced I/O populated (THAT1246 in, DRV134 out), bypass jumpers for optional unbalanced.
7. **Power input**: 12V DC barrel jack → 5V buck (TPS54302) + ±12V DC-DC (TMA 1212D).
8. **Pin assignments**: Open to rearrangement from prototype for PCB routing optimization.
9. **Enclosure**: Sheet metal, 5.7" x 3.75" top panel, parametric OpenSCAD model.
10. **BOM**: Full structured CSV with Mouser/DigiKey part numbers in `hardware/bom.csv`.
