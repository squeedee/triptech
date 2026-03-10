# Triptech Prototype Wiring Guide

Breadboard/perfboard prototype using a bare Daisy Seed (Rev7) with off-the-shelf components.

## Components

| # | Component           | Part                                                          | Qty |
|---|---------------------|---------------------------------------------------------------|-----|
| 1 | MCU                 | Daisy Seed (Rev7)                                             | 1   |
| 2 | Rotary Encoder      | PEC11-4215F-S24 (detented, push-switch)                       | 3   |
| 3 | SPI OLED Display    | 128x32 SSD1306, 7-pin SPI (GND VCC D0 D1 RES DC CS)           | 3   |
| 4 | Momentary Switch    | Tactile pushbutton (normally open)                            | 7   |
| 5 | MIDI Shield         | Arduino MIDI Shield (SparkFun-style, 6N138 opto, IN/OUT/THRU) | 1   |
| 6 | Audio Jacks         | 1/4" mono jack (TS)                                           | 4   |
| 7 | Addressable RGB LED | WS2812B or SK6812 (e.g. NeoPixel strip/PCB)                   | 8   |
| 8 | DC Barrel Jack      | 2.1mm center-positive, 5V DC wall adapter                     | 1   |

## Power

### DC barrel jack (primary power)

A 5V center-positive DC adapter plugs into a 2.1mm barrel jack. This is the single power
source for the entire prototype.

```
  5V DC Adapter
       │
  ┌────┴────┐
  │  DC Jack│
  │  2.1mm  │
  └──┬───┬──┘
     │   │
   (+)  (-)
     │   │
     │   └──────── GND bus (DGND)
     │
     ├──────────── Seed VIN (header 39) ── powers Seed + generates 3V3 rails
     │
     ├──────────── LED chain VCC (5V)  ── 8x WS2812B
     │
     └──────────── MIDI shield 5V      ── optocoupler + output buffer
```

The Seed's onboard regulator converts VIN to 3.3V for the MCU and codec. The 3V3D output
(header 38) then powers the 3 OLED displays (~60mA total) and pullups.

It is safe to have the USB cable plugged in at the same time as the DC jack (the Seed
datasheet confirms simultaneous VIN + USB is fine). You'll typically have both connected:
USB for flashing/MIDI, DC jack for reliable 5V power.

### Power rails summary

| Rail   | Source                    | Header Pin | Supplies                | Notes                                        |
|--------|---------------------------|------------|-------------------------|----------------------------------------------|
| **5V** | DC jack → VIN             | 39         | Seed, LEDs, MIDI shield | 5V center-positive, >=1A adapter recommended |
| +3V3D  | Seed regulator (from VIN) | 38         | OLED displays, pullups  | Output only. ~200mA available.               |
| +3V3A  | Seed regulator (from VIN) | 21         | Codec analog section    | Output only. Do not use for digital loads.   |
| DGND   | DC jack GND               | 40         | Digital ground bus      |                                              |
| AGND   | Codec analog ground       | 20         | Audio jack sleeves      | **Must connect to DGND at one point.**       |

### Current budget (approximate)

| Load                                   | Current    |
|----------------------------------------|------------|
| Daisy Seed (MCU + codec + SDRAM)       | ~150mA     |
| 3x OLED display (SSD1306 SPI)          | ~60mA      |
| 8x WS2812B (coloured, ~25% brightness) | ~160mA     |
| MIDI shield (6N138 opto + buffer)      | ~30mA      |
| **Total**                              | **~400mA** |

A 5V / 1A wall adapter provides comfortable headroom. Do **not** power the LEDs or MIDI
shield from the 3V3D rail -- it can't supply the current, and WS2812B needs >=3.5V VCC.

---

## Pin Assignment Summary

**26 of 31 GPIO used. 5 GPIO free (D9, D18, D19, D29, D30).**

| Seed Pin | Header Pin | Daisy Name      | Function               | Wire To           |
|----------|------------|-----------------|------------------------|-------------------|
| D7       | 8          | SPI1 CS         | OLED CS 1              | Display 1 CS      |
| D8       | 9          | SPI1 SCK        | SPI CLK (shared)       | All displays D0   |
| D9       | 10         | SPI1 MISO       | *(unused, SPI1 group)* | --                |
| D10      | 11         | SPI1 MOSI       | SPI MOSI (shared)      | All displays D1   |
| D1       | 2          | GPIO            | OLED CS 2              | Display 2 CS      |
| D2       | 3          | GPIO            | OLED CS 3              | Display 3 CS      |
| D3       | 4          | GPIO            | OLED DC (shared)       | All displays DC   |
| D4       | 5          | GPIO            | OLED RES (shared)      | All displays RES  |
| D20      | 27         | GPIO            | Encoder 1 A            | Enc1 pin A        |
| D21      | 28         | GPIO            | Encoder 1 B            | Enc1 pin B        |
| D22      | 29         | GPIO            | Encoder 1 SW           | Enc1 push switch  |
| D23      | 30         | GPIO            | Encoder 2 A            | Enc2 pin A        |
| D24      | 31         | GPIO            | Encoder 2 B            | Enc2 pin B        |
| D25      | 32         | GPIO            | Encoder 2 SW           | Enc2 push switch  |
| D26      | 33         | GPIO            | Encoder 3 A            | Enc3 pin A        |
| D27      | 34         | GPIO            | Encoder 3 B            | Enc3 pin B        |
| D28      | 35         | GPIO            | Encoder 3 SW           | Enc3 push switch  |
| D5       | 6          | GPIO            | Switch 1 (RUN)         | SW1               |
| D6       | 7          | GPIO            | Switch 2 (BYP)         | SW2               |
| D11      | 12         | GPIO            | Switch 3 (SHIFT)       | SW3               |
| D12      | 13         | GPIO            | Switch 4 (CH1)         | SW4               |
| D13      | 14         | USART1 TX       | MIDI OUT (UART TX)     | MIDI Shield RX/D0 |
| D14      | 15         | USART1 RX       | MIDI IN (UART RX)      | MIDI Shield TX/D1 |
| D0       | 1          | GPIO            | Switch 5 (CH2)         | SW5               |
| D15      | 22         | GPIO            | Switch 6 (CH3)         | SW6               |
| D16      | 23         | GPIO            | Switch 7 (GLOBAL)      | SW7               |
| D17      | 24         | GPIO (TIM3_CH4) | LED Data (WS2812B)     | LED chain DIN     |

### Free pins (unused)

| Seed Pin | Header Pin | Notes                                        |
|----------|------------|----------------------------------------------|
| D9       | 10         | SPI1 MISO -- available if not needed for SPI |
| D18      | 25         | Spare GPIO (3.3V tolerant only)              |
| D19      | 26         | Spare GPIO                                   |
| D29      | 36         | USB D- (reserved for future USB-C)           |
| D30      | 37         | USB D+ (reserved for future USB-C)           |

---

## 1. SPI OLED Displays (x3)

Your displays have 7 pins: `GND VCC D0 D1 RES DC CS`

In the SSD1306 SPI module convention:

- **D0** = SPI Clock (SCK/SCLK)
- **D1** = SPI Data (MOSI/SDA)
- **RES** = Reset (active low)
- **DC** = Data/Command select
- **CS** = Chip Select (active low)

All three displays share the SPI bus (CLK + MOSI), the DC line, and the RES line.
Each display gets its own CS line.

### Wiring

```
                    Daisy Seed                 Display 1    Display 2    Display 3
                   ┌──────────┐               ┌────────┐   ┌────────┐   ┌────────┐
                   │          │               │        │   │        │   │        │
  Header 9   D8 ───┤SPI1_SCK  ├───────────────┤ D0     │───┤ D0     │───┤ D0     │
                   │          │               │        │   │        │   │        │
  Header 11  D10───┤SPI1_MOSI ├───────────────┤ D1     │───┤ D1     │───┤ D1     │
                   │          │               │        │   │        │   │        │
  Header 8   D7 ───┤CS1       ├───────────────┤ CS     │   │        │   │        │
                   │          │               │        │   │        │   │        │
  Header 2   D1 ───┤CS2       ├────────────────────────────┤ CS     │   │        │
                   │          │               │        │   │        │   │        │
  Header 3   D2 ───┤CS3       ├─────────────────────────────────────────┤ CS     │
                   │          │               │        │   │        │   │        │
  Header 4   D3 ───┤DC        ├───────────────┤ DC     │───┤ DC     │───┤ DC     │
                   │          │               │        │   │        │   │        │
  Header 5   D4 ───┤RES       ├───────────────┤ RES    │───┤ RES    │───┤ RES    │
                   │          │               │        │   │        │   │        │
             GND ──┤DGND (40) ├───────────────┤ GND    │───┤ GND    │───┤ GND    │
                   │          │               │        │   │        │   │        │
            +3V3 ──┤3V3D (38) ├───────────────┤ VCC    │───┤ VCC    │───┤ VCC    │
                   └──────────┘               └────────┘   └────────┘   └────────┘
```

### Display wiring table

| Display Pin | Display 1       | Display 2       | Display 3       |
|-------------|-----------------|-----------------|-----------------|
| GND         | DGND (pin 40)   | DGND (pin 40)   | DGND (pin 40)   |
| VCC         | +3V3D (pin 38)  | +3V3D (pin 38)  | +3V3D (pin 38)  |
| D0 (SCK)    | D8 (header 9)   | D8 (header 9)   | D8 (header 9)   |
| D1 (MOSI)   | D10 (header 11) | D10 (header 11) | D10 (header 11) |
| RES         | D4 (header 5)   | D4 (header 5)   | D4 (header 5)   |
| DC          | D3 (header 4)   | D3 (header 4)   | D3 (header 4)   |
| CS          | D7 (header 8)   | D1 (header 2)   | D2 (header 3)   |

**Notes:**

- D7/D8/D10 are the hardware SPI1 peripheral pins (NSS/SCK/MOSI). Using hardware SPI
  gives fast, DMA-capable display updates.
- D7 is the native SPI1_NSS but we use it as a manually-toggled CS for Display 1. The other
  two CS lines (D1, D2) are plain GPIO.
- DC and RES are shared -- all displays show different content, selected by asserting the
  correct CS before each transfer.
- If your display modules already have pullup resistors on CS, that's fine. The Seed drives
  these actively.

---

## 2. Rotary Encoders (x3) -- PEC11-4215F-S24

The PEC11 has 5 pins: **A, B, C (common), S1, S2 (switch).**

- Pins A and B are the quadrature outputs
- Pin C is the common (connect to **GND**)
- Pins S1 and S2 are the push switch (normally open, connect one to GND, one to a GPIO)

The STM32 GPIO has internal pullups, which libDaisy's `Encoder` class enables by default.
No external pullup resistors needed.

### Wiring

```
  PEC11 Encoder (rear view, pins down)
  ┌─────────────────┐
  │  A    C    B    │   ← Rotary pins
  │  │    │    │    │
  │  S1        S2   │   ← Switch pins
  └──┼────┼────┼────┘
     │    │    │
     │    │    │
     ▼    ▼    ▼

  A  → Seed GPIO (encoder A)
  C  → GND
  B  → Seed GPIO (encoder B)
  S1 → GND  (tie to ground)
  S2 → Seed GPIO (encoder switch)
```

### Encoder pin assignments

| Encoder   | Pin A           | Pin B           | Pin C | Switch (S2)     | S1  |
|-----------|-----------------|-----------------|-------|-----------------|-----|
| Encoder 1 | D20 (header 27) | D21 (header 28) | GND   | D22 (header 29) | GND |
| Encoder 2 | D23 (header 30) | D24 (header 31) | GND   | D25 (header 32) | GND |
| Encoder 3 | D26 (header 33) | D27 (header 34) | GND   | D28 (header 35) | GND |

**Notes:**

- D22, D23 are DAC-capable pins (DAC_OUT2, DAC_OUT1) but we don't need the DACs
  for this prototype since audio goes through the Seed's onboard codec.
- D22 and D23 are 3.3V-tolerant only (not 5V), which is fine since encoders just switch to
  GND.
- Internal pullups are enabled in firmware. No external components needed.
- The PEC11-4215F-S24 is detented (24 detents/revolution). Each detent produces one
  quadrature cycle, so the firmware should count 1 step per detent.

---

## 3. Momentary Switches (x7)

Simple normally-open tactile switches. One leg to the Seed GPIO, the other to GND.
The STM32 internal pullup holds the pin HIGH when the switch is open; pressing pulls it LOW.

```
  +3V3 (internal pullup)
    │
    ├──── Seed GPIO pin
    │
   ┌┴┐
   │  │  Tactile switch (NO)
   └┬┘
    │
   GND
```

### Switch pin assignments

| Switch | Function | Seed Pin | Header Pin |
|--------|----------|----------|------------|
| SW1    | RUN      | D5       | 6          |
| SW2    | BYP      | D6       | 7          |
| SW3    | SHIFT    | D11      | 12         |
| SW4    | CH1      | D12      | 13         |
| SW5    | CH2      | D0       | 1          |
| SW6    | CH3      | D15      | 22         |
| SW7    | GLOBAL   | D16      | 23         |

**Notes:**

- No external resistors needed. Use `GPIO::Mode::INPUT` with `GPIO::Pull::PULLUP` in
  firmware, or use the `Switch` class from libDaisy which configures this automatically.
- Active LOW: pressed = 0, released = 1.
- If you get bounce issues, add 100nF caps from each switch pin to GND, but the libDaisy
  `Switch` class has software debouncing built in.

---

## 4. MIDI Shield (Arduino-style)

The standard Arduino MIDI shield expects:

- **Arduino D0 (RX)** = MIDI serial in (from opto-isolator output)
- **Arduino D1 (TX)** = MIDI serial out (to DIN/TRS connector via buffer)
- **5V** and **GND** for power

The shield has its own optocoupler (6N138), output buffer, and THRU hard-wired from the opto
output. You only need to connect the UART lines and power.

### Wiring

| MIDI Shield Pin                 | Connect To                     | Notes                |
|---------------------------------|--------------------------------|----------------------|
| Arduino D1 (TX) / MIDI OUT data | D13 (header 14, USART1_TX)     | Seed transmits MIDI  |
| Arduino D0 (RX) / MIDI IN data  | D14 (header 15, USART1_RX)     | Seed receives MIDI   |
| 5V                              | 5V bus (VIN rail from DC jack) | Powers opto + buffer |
| GND                             | GND bus (DGND)                 | Common ground        |

**UART config:** 31250 baud, 8N1 (standard MIDI). Use USART1 peripheral on D13/D14.

### Level shifting

The Seed's UART pins are 3.3V logic. The MIDI shield's opto output is open-collector and will
work fine with 3.3V pullup (the Seed has internal pullups). The MIDI shield's TX input typically
has a series resistor to the DIN connector buffer -- 3.3V HIGH from the Seed is above the
threshold for standard CMOS/TTL buffers on the shield, so it should work without level
shifting. If you have issues, add a simple level shifter on the TX line.

---

## 5. Audio Jacks (x4 Mono 1/4")

The Daisy Seed's onboard codec handles all audio I/O. The audio pins are **AC-coupled** on the
Seed module itself -- you wire the jacks directly.

### Wiring

| Jack                    | Seed Pin    | Header Pin | Connection                              |
|-------------------------|-------------|------------|-----------------------------------------|
| Audio In L (mono jack)  | Audio In 1  | 16         | Jack TIP to pin 16, Jack SLEEVE to AGND |
| Audio In R (mono jack)  | Audio In 2  | 17         | Jack TIP to pin 17, Jack SLEEVE to AGND |
| Audio Out L (mono jack) | Audio Out 1 | 18         | Jack TIP to pin 18, Jack SLEEVE to AGND |
| Audio Out R (mono jack) | Audio Out 2 | 19         | Jack TIP to pin 19, Jack SLEEVE to AGND |

```
  1/4" Mono Jack (TS)
  ┌─────────────┐
  │  TIP ───────┼──── Seed Audio In/Out pin (16/17/18/19)
  │             │
  │  SLEEVE ────┼──── AGND (pin 20)
  └─────────────┘
```

**Notes:**

- Audio inputs are AC-coupled on the Seed, rated for 3.6Vpp (~1Vrms). Line level is fine.
  If your source is hot (+4dBu pro gear), you may want a simple resistive divider or pad.
- Audio outputs are ~1Vrms (0dBFS). Line level, suitable for driving mixers/interfaces
  directly. Output impedance is ~100 ohm.
- **AGND (pin 20) must be connected to DGND (pin 40).** Use a single point connection
  (star ground) to minimize noise coupling. On a breadboard, just tie them together.
- Use shielded cable for the audio runs if possible to reduce noise pickup.

---

## 6. Addressable RGB LEDs (x8) -- WS2812B / SK6812

8 LEDs wired as a single daisy-chain (data out of each LED feeds data in of the next).
Only 1 GPIO pin is needed for the entire chain.

### Wiring

```
  Seed D17 (header 24)
       │
       │  330R              LED 1        LED 2              LED 8
       ├──/\/\/──┐     ┌──────────┐ ┌──────────┐      ┌──────────┐
       │         └────►│ DIN  DOUT├─►│ DIN  DOUT├─ ··· ►│ DIN  DOUT├── (NC)
       │               │          │ │          │      │          │
  VIN (5V) ──────┬─────┤ VCC      │─┤ VCC      │──────┤ VCC      │
       │         │     │          │ │          │      │          │
      ─┴─ 100uF │     │          │ │          │      │          │
      ─┬─ (bulk)│     │          │ │          │      │          │
       │         │     │          │ │          │      │          │
  GND ──────────┴─────┤ GND      │─┤ GND      │──────┤ GND      │
                       └──────────┘ └──────────┘      └──────────┘
```

### Chain order (suggested)

| Position | Function                | Colour Use                     |
|----------|-------------------------|--------------------------------|
| LED 0    | SW1 (RUN)               | Green = running, off = stopped |
| LED 1    | SW2 (BYP)               | Blue/white = bypassed          |
| LED 2    | SW3 (SHIFT)             | Orange = shift held            |
| LED 3    | SW4 (CH1)               | Channel 1 colour (green)       |
| LED 4    | SW5 (CH2)               | Channel 2 colour (yellow)      |
| LED 5    | SW6 (CH3)               | Channel 3 colour (red)         |
| LED 6    | SW7 (GLOBAL)            | Accent colour (blue)           |
| LED 7    | Status / page indicator | Param group colour             |

### Notes

- **Data pin (D17/PB1):** This pin supports TIM3_CH4, which can be used for DMA-driven
  WS2812B timing via PWM. This avoids bit-banging in the main loop.
- **330 ohm series resistor** on the data line between the Seed and the first LED's DIN.
  This dampens reflections and protects the GPIO. Place it physically close to LED 0.
- **D17 is 3.3V tolerant only.** WS2812B datasheet specifies DIN high threshold at
  0.7 x VCC = 3.5V when VCC = 5V. In practice, 3.3V from the STM32 works reliably for
  most WS2812B and all SK6812 LEDs. If you get flickering or the first LED misbehaves,
  add a simple level shifter (a single N-channel MOSFET or a 74HCT125 buffer powered
  at 5V).
- **100uF bulk capacitor** across VCC and GND near the LED chain. Prevents voltage dip
  when LEDs switch on simultaneously.
- **Do not power LEDs from +3V3D.** Use the 5V bus (VIN rail from the DC jack).
  The ground must be shared with the Seed's DGND.

---

## 7. Complete Breadboard Hookup Checklist

### Ground bus

- [ ] AGND (header 20) connected to DGND (header 40) -- **single point**
- [ ] All display GND pins to ground bus
- [ ] All encoder C pins to ground bus
- [ ] All encoder S1 pins to ground bus
- [ ] All switch GND legs to ground bus
- [ ] LED chain GND to ground bus
- [ ] MIDI shield GND to ground bus
- [ ] All audio jack sleeves to AGND (header 20)

### Power (DC jack)

- [ ] DC jack center (+) to Seed VIN (header 39)
- [ ] DC jack sleeve (-) to GND bus (DGND, header 40)
- [ ] All display VCC pins to +3V3D (header 38)
- [ ] LED chain VCC to 5V bus (VIN rail) -- **not** 3V3D
- [ ] 100uF bulk cap across LED chain VCC/GND
- [ ] MIDI shield 5V to 5V bus (VIN rail)

### SPI bus (displays)

- [ ] D8 (header 9) to all three display D0 (SCK) pins
- [ ] D10 (header 11) to all three display D1 (MOSI) pins
- [ ] D3 (header 4) to all three display DC pins
- [ ] D4 (header 5) to all three display RES pins
- [ ] D7 (header 8) to Display 1 CS only
- [ ] D1 (header 2) to Display 2 CS only
- [ ] D2 (header 3) to Display 3 CS only

### Encoders

- [ ] Encoder 1: A→D20(27), B→D21(28), C→GND, S1→GND, S2→D22(29)
- [ ] Encoder 2: A→D23(30), B→D24(31), C→GND, S1→GND, S2→D25(32)
- [ ] Encoder 3: A→D26(33), B→D27(34), C→GND, S1→GND, S2→D28(35)

### Switches

- [ ] SW1 (RUN): D5(6) ↔ GND
- [ ] SW2 (BYP): D6(7) ↔ GND
- [ ] SW3 (SHIFT): D11(12) ↔ GND
- [ ] SW4 (CH1): D12(13) ↔ GND
- [ ] SW5 (CH2): D0(1) ↔ GND
- [ ] SW6 (CH3): D15(22) ↔ GND
- [ ] SW7 (GLOBAL): D16(23) ↔ GND

### LEDs

- [ ] D17 (header 24) → 330R resistor → LED 0 DIN
- [ ] LED chain: DOUT of each LED → DIN of next (8 LEDs total)
- [ ] All LED VCC → VIN (5V)
- [ ] All LED GND → ground bus
- [ ] 100uF cap across LED chain VCC/GND

### MIDI

- [ ] MIDI Shield TX → D14 (header 15, USART1_RX)
- [ ] MIDI Shield RX → D13 (header 14, USART1_TX)
- [ ] MIDI Shield GND → ground bus
- [ ] MIDI Shield 5V → 5V bus (VIN rail)

### Audio

- [ ] L IN jack TIP → header 16 (Audio In 1)
- [ ] R IN jack TIP → header 17 (Audio In 2)
- [ ] L OUT jack TIP → header 18 (Audio Out 1)
- [ ] R OUT jack TIP → header 19 (Audio Out 2)
- [ ] All jack sleeves → AGND (header 20)

---

## Pin Map Visual (Daisy Seed, component side up, USB at bottom)

```
          Left Header                              Right Header
     ┌──────────────────┐                    ┌──────────────────┐
  1  │ D0    - SW5(CH2) │                    │ AGND  - Audio GND│ 20
  2  │ D1    - OLED CS2 │                    │ Audio Out 2      │ 19
  3  │ D2    - OLED CS3 │                    │ Audio Out 1      │ 18
  4  │ D3    - OLED DC  │                    │ Audio In 2       │ 17
  5  │ D4    - OLED RES │                    │ Audio In 1       │ 16
  6  │ D5    - SW1(RUN) │                    │ D14 - MIDI RX    │ 15
  7  │ D6    - SW2(BYP) │                    │ D13 - MIDI TX    │ 14
  8  │ D7    - OLED CS1 │                    │ D12 - SW4(CH1)   │ 13
  9  │ D8    - SPI SCK  │                    │ D11 - SW3(SHIFT) │ 12
 10  │ D9    - (free)   │                    │ D10 - SPI MOSI   │ 11
     ├──────────────────┤      Daisy         ├──────────────────┤
     │  ┌────────────┐  │      Seed          │                  │
     │  │  STM32     │  │                    │                  │
     │  │  H750      │  │                    │                  │
     │  └────────────┘  │                    │                  │
     ├──────────────────┤                    ├──────────────────┤
 21  │ +3V3A           │                    │                  │
 22  │ D15   - SW6(CH3)│                    │                  │
 23  │ D16   - SW7(GLB)│                    │                  │
 24  │ D17   - LED DIN │                    │                  │
 25  │ D18   - (free)  │                    │                  │
 26  │ D19   - (free)  │                    │                  │
 27  │ D20   - Enc1 A  │                    │                  │
 28  │ D21   - Enc1 B  │                    │                  │
 29  │ D22   - Enc1 SW │                    │                  │
 30  │ D23   - Enc2 A  │                    │                  │
 31  │ D24   - Enc2 B  │                    │                  │
 32  │ D25   - Enc2 SW │                    │                  │
 33  │ D26   - Enc3 A  │                    │                  │
 34  │ D27   - Enc3 B  │                    │                  │
 35  │ D28   - Enc3 SW │       ┌─────┐      │                  │
 36  │ D29   - USB D-  │       │ USB │      │                  │
 37  │ D30   - USB D+  │       └─────┘      │                  │
 38  │ +3V3D - Display │   [RESET] [BOOT]   │                  │
 39  │ VIN   - MIDI 5V │                    │                  │
 40  │ DGND  - Gnd bus │                    │                  │
     └──────────────────┘                    └──────────────────┘
```

---

## Design Rationale

1. **SPI1 hardware peripheral** (D7/D8/D9/D10) used for displays. This allows DMA-driven
   SPI transfers so display updates don't block the audio callback. D9 (MISO) is unused
   since the displays are write-only.

2. **USART1** (D13 TX / D14 RX) used for MIDI. This is the natural USART1 peripheral pair
   on the STM32H750 and is what libDaisy's `MidiUartHandler` defaults to.

3. **Encoders on D20-D28** (left header, analog side). These pins are grouped contiguously
   making the wiring tidy. The pins have ADC capability but we use them as digital GPIO --
   no conflict since ADC is not needed (no pots in this prototype).

4. **Switches on D0, D5, D6, D11, D12, D15, D16** -- scattered across the remaining free
   pins. All used as digital input with internal pullup.

5. **USB MIDI** via the Seed's onboard micro-USB (USB FS peripheral). This is independent
   of D29/D30 (USB HS). The firmware already uses `MidiUsbHandler` for this.

6. **Addressable RGB LEDs on D17** (PB1/TIM3_CH4). Timer-capable pin allows
   DMA-driven WS2812B output without CPU bit-banging. 8 LEDs on a single data line.

7. **5 spare pins** remain (D9, D18, D19, D29, D30) for future additions like
   additional indicators, or USB-C on D29/D30.
