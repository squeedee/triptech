# Triptech Power Supply — Component & Wiring Reference

## Overview

There are three functional blocks:
1. **Input protection** (barrel jack → protected +12V_RAW rail)
2. **5V buck regulator** (TPS54302, 12V → 5V)
3. **±12V DC-DC module** (Traco TMA 1212D, for balanced audio)

---

## ASCII Schematic

```
                         POWER SUPPLY — TRIPTECH CARRIER BOARD
                         ======================================

  12V DC INPUT & PROTECTION
  ─────────────────────────

                        Q1 (SI2301CDS, P-MOSFET, SOT-23)
                        S         D
   J_DC                 │         │                        +12V_RAW
   Barrel Jack   TIP ───┤         ├───┬─────────────────── ● ──────┐
   2.1mm                │    G    │   │                             │
   (12V in)             │    │    │   │                             │
                        │    │    │  ─┴─ D1                        │
   RING ─── GND         └────│────┘  TVS (SMBJ15A)                 │
                             │       ─┬─ 15V standoff              │
                             │        │                             │
                        ┌────┘        │                             │
                        │             │                             │
                     R_GATE           │                             │
                      10k             │                             │
                     (0402)           │                             │
                        │             │                             │
                       GND           GND                            │
                                                                    │
                                                                    │
   Input Capacitors (on +12V_RAW rail):                             │
                                                                    │
       +12V_RAW ──┬────────────┐                                    │
                  │            │                                    │
                 C1           C2                                    │
              100uF/25V     100nF                                   │
              (electro)     (0402)                                  │
                  │            │                                    │
                 GND          GND                                   │
                                                                    │
                                                                    │
   How it works:                                                    │
   • TIP = positive barrel center (+12V)                            │
   • RING = GND (barrel shell)                                      │
   • Q1 Source connects to TIP, Drain to +12V_RAW                   │
   • R_GATE pulls gate to GND → gate is LOW relative to             │
     source when +12V applied → P-FET turns ON                     │
   • If polarity reversed, gate goes HIGH relative to               │
     source → P-FET stays OFF, protecting circuit                   │
   • D1 clamps any voltage spike above ~24V                         │
                                                                   │
                                                                   │
  5V BUCK REGULATOR (TPS54302)                                     │
  ────────────────────────────                                     │
                                                                   │
         +12V_RAW ─────────────────────────────────────────────────┘
              │
              │
              │    UVLO ENABLE DIVIDER          U_REG (TPS54302, SOT-23-6)
              │    ┌──────────────────┐         ┌────────────────────────┐
              ├────┤                  │         │                        │
              │    │  R_EN1 (100k)    │    VIN ─┤pin 3              pin 2├─ SW
              │    │       │          │         │                        │
              │    │   EN node ───────┼─── EN ─┤pin 5              pin 6├─ BST
              │    │       │          │         │                        │
              │    │  R_EN2 (13.7k)   │    GND ─┤pin 1              pin 4├─ FB
              │    │       │          │         │                        │
              │    │      GND         │         └────────────────────────┘
              │    └──────────────────┘
              │
              └──── VIN (pin 3) 
                                         BST (pin 6)
                                          │
                                        C_BST
                                        100nF (0402)
                                          │
                     SW (pin 2) ──────────┘
                      │
                      │
                     L1 (10uH, SRN6045TA, 6x4.5mm)
                      │
                      │
                 +5V OUTPUT ──────────┬────────────┐
                      │               │            │
                      │             C_OUT1       C_OUT2
                      │              22uF         22uF
                      │             (0805)       (0805)
                      │               │            │
                      │              GND          GND
                      │
                   FEEDBACK NETWORK (per TI reference design)
                      │
                   R_SER
                   49.9R (0402)
                      │
                      ├──── R_FB1 (100k, 1%) ────┤├──── C_FF (75pF)
                      │         (in parallel)
                      │
                   FB node ──── FB (pin 4)
                      │
                   R_FB2
                   13.3k (1%)
                      │
                     GND

   TPS54302 Pin Mapping (SOT-23-6, DDC package):
   ┌──────────────────┐
   │ Pin 1: GND       │ ── ground
   │ Pin 2: SW        │ ── switching node → through L1 to output
   │ Pin 3: VIN       │ ── +12V_RAW input
   │ Pin 4: FB        │ ── feedback (between R_FB1 and R_FB2)
   │ Pin 5: EN        │ ── enable (UVLO divider sets turn-on threshold)
   │ Pin 6: BST       │ ── bootstrap (C_BST from BST to SW)
   └──────────────────┘

   Feedback network (per TI TPS54302EVM-716 reference design):

     The TPS54302 has a 0.6V internal reference (NOT 0.8V).

     R_SER (49.9R) ── series resistor between +5V output and top of divider.
       Filters HF noise from reaching FB. Negligible DC effect (49.9R
       vs 113.3k divider impedance).

     R_FB1 (100k) ── top feedback resistor, from R_SER output to FB node.
     C_FF  (75pF) ── feedforward capacitor, in parallel with R_FB1.
       Adds a compensation zero at ~21 kHz for better transient response.
       f_zero = 1 / (2π × 100k × 75pF) ≈ 21.2 kHz

     R_FB2 (13.3k) ── bottom feedback resistor, FB node to GND.

     Output voltage:
       VOUT = 0.6V × (1 + R_FB1 / R_FB2)
            = 0.6V × (1 + 100k / 13.3k)
            = 0.6V × 8.519
            = 5.11V

     (TI targets slightly above 5.0V to allow for load droop.)

   UVLO (under-voltage lockout) via EN divider:
     EN rising threshold = 1.2V (typ), falling = 1.0V (typ).
     R_EN1 = 100k, R_EN2 = 13.7k

     Turn-on:  VIN × 13.7k / (100k + 13.7k) = 1.2V → VIN = ~10.0V
     Turn-off: VIN × 13.7k / (100k + 13.7k) = 1.0V → VIN = ~8.3V

     System only starts when input is close to nominal 12V,
     ensuring the ±12V DC-DC module can operate.
     Shuts down cleanly if input sags below ~8.3V.

   Bootstrap cap (C_BST):
     100nF ceramic from BST (pin 6) to SW (pin 2)
     Provides charge pump for high-side FET drive


  ±12V DC-DC MODULE
  ──────────────────

         +12V_RAW
            │
            │
            │    J_DCDC (Traco TMA 1212D, SIP-6)
            │    ┌─────────────────────────────────┐
            └───>│  Pin 1: +VIN                    │
                 │  Pin 2: GND  ──── GND (input side)  │
                 │  Pin 3: N/C                          │
                 │  Pin 4: +VOUT ──── +12VA             │
                 │  Pin 5: COM  ──── GNDA (output side) │
                 │  Pin 6: -VOUT ──── -12VA             │
                 └─────────────────────────────────┘

                 Output Decoupling:

                 +12VA ────── C_12P (10uF, 0805) ────── GNDA

                 -12VA ────── C_12N (10uF, 0805) ────── GNDA


  POWER RAIL SUMMARY
  ──────────────────

   +12V_RAW ── protected input rail (after MOSFET + TVS)
   +5V      ── TPS54302 output (feeds Seed VIN, LEDs, displays, USB)
   +3V3D    ── from Daisy Seed internal regulator (via Seed VIN→3.3V)
   +12VA    ── TMA 1212D +12V isolated output (for balanced audio)
   -12VA    ── TMA 1212D -12V isolated output (for balanced audio)
   GND      ── digital ground (main ground plane)
   GNDA     ── analog ground (star-connected to GND via R_GND 0R)
```

---

## Component List (Power Section Only)

| Ref | Part | Value | Package | Notes |
|-----|------|-------|---------|-------|
| J_DC | CUI PJ-002A | - | TH | 2.1mm center-positive barrel jack |
| Q1 | SI2301CDS | - | SOT-23 | P-MOSFET reverse polarity protection |
| D1 | SMBJ15A | 15V | SMB | TVS overvoltage clamp |
| R_GATE | Resistor | 10k | 0402 | Gate pulldown for Q1 |
| C1 | Electrolytic cap | 100uF/25V | 6.3x7.7mm | Input bulk decoupling |
| C2 | Ceramic cap | 100nF | 0402 | Input HF bypass |
| U_REG | TPS54302 | - | SOT-23-6 | 5V 3A buck regulator |
| C_BST | Ceramic cap | 100nF | 0402 | Bootstrap (BST to SW) |
| L1 | SRN6045TA | 10uH | 6x4.5mm | Output inductor, 3A sat |
| R_EN1 | Resistor | 100k | 0402 | UVLO divider top (VIN to EN) |
| R_EN2 | Resistor | 13.7k | 0402 | UVLO divider bottom (EN to GND), turn-on ~10V |
| R_SER | Resistor | 49.9R | 0402 | Series filter before FB divider |
| R_FB1 | Resistor | 100k | 0402 | FB divider top, 1% |
| C_FF | Ceramic cap | 75pF | 0402 | Feedforward cap, parallel with R_FB1 |
| R_FB2 | Resistor | 13.3k | 0402 | FB divider bottom (FB to GND), 1% — VREF=0.6V |
| C_OUT1 | Ceramic cap | 22uF | 0805 | Output cap |
| C_OUT2 | Ceramic cap | 22uF | 0805 | Output cap |
| J_DCDC | TMA 1212D | - | SIP-6 | ±12V isolated DC-DC for balanced audio |

| C_12P | Ceramic cap | 10uF | 0805 | +12VA decoupling |
| C_12N | Ceramic cap | 10uF | 0805 | -12VA decoupling |

---

## CRITICAL: Feedback Network Errors in All Project Documents

The TPS54302 has a **0.6V** internal voltage reference. DESIGN.md and the BOM incorrectly assumed 0.8V and specified R_FB2 = 19.1k. The KiCad schematics used R_FB2 = 24.9k. **Both are wrong.**

| Source | R_FB2 | Assumed VREF | Resulting VOUT |
|--------|-------|-------------|----------------|
| DESIGN.md / BOM | 19.1k | 0.8V (wrong) | 3.74V with actual 0.6V ref |
| KiCad schematics | 24.9k | ??? | 3.01V with actual 0.6V ref |
| **TI reference design** | **13.3k** | **0.6V** | **5.11V** |

Additionally, the original design was missing R_SER (49.9R) and C_FF (75pF) from the TI reference design. These improve loop stability and transient response.

The BOM, DESIGN.md, and KiCad schematics all need updating to match the TI reference:
- R_FB2 = **13.3k** (not 19.1k or 24.9k)
- Add **R_SER = 49.9R** (series, between +5V and top of divider)
- Add **C_FF = 75pF** (in parallel with R_FB1)

---

## Wiring Connections Summary (net-by-net)

**+12V_RAW** connects to: Q1 drain, D1 anode, C1+, C2 (one side), U_REG VIN, R_EN1 top, J_DCDC pin 1

**GND** connects to: J_DC ring, R_GATE (bottom), D1 cathode, C1-, C2 (other side), U_REG GND, R_EN2 bottom, R_FB2 bottom, C_OUT1-, C_OUT2-, J_DCDC pins 2+5, C_12P-, C_12N-

**+5V** connects to: L1 output side, C_OUT1+, C_OUT2+, R_SER top

**SW (switching node)** connects to: U_REG SW pin, L1 input side, C_BST (one side)

**BST** connects to: U_REG BST pin, C_BST (other side)

**EN** connects to: U_REG EN pin, R_EN1/R_EN2 junction

**R_SER/R_FB1 junction** connects to: R_SER bottom, R_FB1 top, C_FF (one side)

**FB** connects to: U_REG FB pin, R_FB1 bottom, C_FF (other side), R_FB2 top

**+12VA** connects to: J_DCDC pin 4 +VOUT, C_12P+

**-12VA** connects to: J_DCDC pin 6 -VOUT, C_12N+
