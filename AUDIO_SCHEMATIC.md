# Triptech Audio I/O — Component & Wiring Reference

## Overview

There are three functional blocks:
1. **Input stage** — TRS jacks → THAT1246 balanced receivers → Seed audio inputs
2. **Output stage** — Seed audio outputs → DRV134 balanced drivers → TRS jacks
3. **Bypass jumpers** — allow selecting balanced or direct (unbalanced) path

All four channels (L in, R in, L out, R out) are identical within their stage.
The balanced ICs are transparent analog front-ends — no firmware changes needed.
TS (unbalanced) cables are fully backward-compatible with no switching circuitry.

Power rails used: **+12VA**, **-12VA** (from TMA 1212D), **GNDA** (analog ground).
Star ground bridge R_GND (0R, 0805) connects GNDA to GND at a single point.

---

## ASCII Schematic

```
                         AUDIO I/O — TRIPTECH CARRIER BOARD
                         ===================================


  INPUT STAGE (Left channel shown — Right channel is identical)
  ─────────────────────────────────────────────────────────────

   J_IN_L (Neutrik NRJ6HF, 1/4" TRS)
   ┌──────────┐
   │  TIP  ───┼──────────────────────────────────── U_IN_L pin 2 (+IN)
   │          │
   │  RING ───┼──────────────────────────────────── U_IN_L pin 3 (-IN)
   │          │
   │  SLEEVE ─┼──── GNDA
   └──────────┘


                U_IN_L (THAT1246, SOIC-8)
                ┌────────────────────────┐
                │  Pin 1: V-  ─── -12VA  │
                │  Pin 2: +IN ─── TIP    │
                │  Pin 3: -IN ─── RING   │
                │  Pin 4: REF ─── GNDA   │
                │  Pin 5: N/C            │
                │  Pin 6: OUT ───────────┼──── JP_IN_L pin 3
                │  Pin 7: N/C            │
                │  Pin 8: V+  ─── +12VA  │
                └────────────────────────┘

                Bypass caps (close to IC):
                  +12VA ── C_IN_L_P (10uF, 0805) ── GNDA
                  +12VA ── C_IN_L_PH (100nF, 0402) ── GNDA
                  -12VA ── C_IN_L_N (10uF, 0805) ── GNDA
                  -12VA ── C_IN_L_NH (100nF, 0402) ── GNDA


   JP_IN_L (1x3 pin header — input bypass jumper)
   ┌─────────────┐
   │ Pin 1 ──────┼──── TIP (J_IN_L)         ← direct/unbalanced path
   │ Pin 2 ──────┼──── Seed Audio In 1 (header pin 16)
   │ Pin 3 ──────┼──── U_IN_L OUT (pin 6)   ← balanced path
   └─────────────┘

   Jumper position:
     Pins 1-2: TRS tip connects directly to Seed input (bypass THAT1246)
     Pins 2-3: THAT1246 output feeds Seed input (balanced mode)


  Right input channel: identical, using J_IN_R, U_IN_R, JP_IN_R,
  and Seed Audio In 2 (header pin 17).


  OUTPUT STAGE (Left channel shown — Right channel is identical)
  ──────────────────────────────────────────────────────────────

   JP_OUT_L (1x3 pin header — output bypass jumper)
   ┌─────────────┐
   │ Pin 1 ──────┼──── TIP (J_OUT_L)        ← direct/unbalanced path
   │ Pin 2 ──────┼──── Seed Audio Out 1 (header pin 18)
   │ Pin 3 ──────┼──── DRV134 OUT+ (pin 5)  ← balanced path
   └─────────────┘

   Jumper position:
     Pins 1-2: Seed output connects directly to TRS tip (bypass DRV134)
     Pins 2-3: DRV134 balanced output feeds TRS tip (balanced mode)


   Coupling cap (blocks DC offset from Seed output):

     Seed Audio Out 1 (header pin 18) ──── C_COUPLE_L (10uF, 0805) ──── U_OUT_L pin 7 (IN)


                U_OUT_L (DRV134, SOIC-8)
                ┌────────────────────────┐
                │  Pin 1: V+     ─── +12VA  │
                │  Pin 2: SENSE+ ─── pin 5  │  (tie SENSE+ to OUT+)
                │  Pin 3: GND    ─── GNDA   │
                │  Pin 4: V-     ─── -12VA  │
                │  Pin 5: OUT+   ────────┼──── JP_OUT_L pin 3 ──── TIP (J_OUT_L)
                │  Pin 6: SENSE- ─── pin 8  │  (tie SENSE- to OUT-)
                │  Pin 7: IN     ────────┼──── C_COUPLE_L (from Seed out)
                │  Pin 8: OUT-   ────────┼──── RING (J_OUT_L)
                └────────────────────────┘

                Bypass caps (close to IC):
                  +12VA ── C_OUT_L_P (10uF, 0805) ── GNDA
                  +12VA ── C_OUT_L_PH (100nF, 0402) ── GNDA
                  -12VA ── C_OUT_L_N (10uF, 0805) ── GNDA
                  -12VA ── C_OUT_L_NH (100nF, 0402) ── GNDA


   J_OUT_L (Neutrik NRJ6HF, 1/4" TRS)
   ┌──────────┐
   │  TIP  ───┼──── JP_OUT_L pin 1 (direct) or DRV134 OUT+ (via jumper)
   │          │
   │  RING ───┼──── DRV134 OUT- (pin 8)
   │          │
   │  SLEEVE ─┼──── GNDA
   └──────────┘


  Right output channel: identical, using J_OUT_R, U_OUT_R, JP_OUT_R,
  C_COUPLE_R, and Seed Audio Out 2 (header pin 19).


  GROUND TOPOLOGY
  ────────────────

   All TRS jack sleeves connect to GNDA (analog ground).
   GNDA connects to GND (digital ground) via a single 0R resistor
   (R_GND, 0805) located near the Seed module — star ground bridge.

   The ±12V supply bypass caps for all four audio ICs also return to GNDA.

   This keeps analog return currents on the analog ground plane,
   separated from digital switching noise (SPI, LEDs, buck converter).


  TS (UNBALANCED) CABLE COMPATIBILITY
  ────────────────────────────────────

   Input with TS cable:
     A TS plug shorts RING to SLEEVE (both become GND/GNDA).
     THAT1246 sees signal on +IN, ground on -IN → unity-gain buffer.
     Works transparently, no detection needed.

   Output with TS cable:
     A TS plug shorts RING to SLEEVE → OUT- (cold) is loaded to GNDA.
     DRV134 handles this gracefully — drives hot signal into TIP,
     cold output loaded to ground. No damage, no oscillation.
```

---

## THAT1246 Pinout (SOIC-8)

| Pin | Name   | Connection        | Notes                                |
|-----|--------|-------------------|--------------------------------------|
| 1   | V-     | -12VA             | Negative supply                      |
| 2   | +IN    | TRS Tip           | Non-inverting input (hot)            |
| 3   | -IN    | TRS Ring          | Inverting input (cold)               |
| 4   | REF    | GNDA              | Output ground reference              |
| 5   | N/C    | —                 | No connection                        |
| 6   | OUT    | JP_IN pin 3       | Single-ended output to Seed          |
| 7   | N/C    | —                 | No connection                        |
| 8   | V+     | +12VA             | Positive supply                      |

Key specs: unity gain, CMRR >80dB, THD+N 0.0006%, 20k input impedance per leg.
Internal precision resistors — no external gain-setting components needed.

---

## DRV134 Pinout (SOIC-8)

| Pin | Name    | Connection         | Notes                                |
|-----|---------|--------------------|--------------------------------------|
| 1   | V+      | +12VA              | Positive supply                      |
| 2   | SENSE+  | Tied to pin 5      | Feedback — tie to OUT+ for unity gain|
| 3   | GND     | GNDA               | Ground reference                     |
| 4   | V-      | -12VA              | Negative supply                      |
| 5   | OUT+    | TRS Tip (via JP)   | Non-inverting output (hot)           |
| 6   | SENSE-  | Tied to pin 8      | Feedback — tie to OUT- for unity gain|
| 7   | IN      | C_COUPLE (from Seed)| Single-ended input                  |
| 8   | OUT-    | TRS Ring           | Inverting output (cold)              |

Key specs: unity gain (differential), THD+N 0.006%, drives 600 ohm balanced loads.
SENSE pins must be tied to their respective outputs for unity-gain operation.

---

## Component List (Audio Section)

### TRS Jacks

| Ref      | Part         | Package | Notes                    |
|----------|--------------|---------|--------------------------|
| J_IN_L   | Neutrik NRJ6HF | TH   | 1/4" TRS, horizontal PCB mount |
| J_IN_R   | Neutrik NRJ6HF | TH   |                          |
| J_OUT_L  | Neutrik NRJ6HF | TH   |                          |
| J_OUT_R  | Neutrik NRJ6HF | TH   |                          |

### Balanced Input ICs

| Ref      | Part      | Package | Notes                    |
|----------|-----------|---------|--------------------------|
| U_IN_L   | THAT1246  | SOIC-8  | Balanced line receiver, L input |
| U_IN_R   | THAT1246  | SOIC-8  | Balanced line receiver, R input |

### Balanced Output ICs

| Ref      | Part      | Package | Notes                    |
|----------|-----------|---------|--------------------------|
| U_OUT_L  | DRV134UA  | SOIC-8  | Balanced line driver, L output |
| U_OUT_R  | DRV134UA  | SOIC-8  | Balanced line driver, R output |

### Bypass Jumpers

| Ref      | Part        | Package | Notes                    |
|----------|-------------|---------|--------------------------|
| JP_IN_L  | 1x3 header  | 2.54mm  | Input L: 1-2=direct, 2-3=balanced |
| JP_IN_R  | 1x3 header  | 2.54mm  | Input R: same             |
| JP_OUT_L | 1x3 header  | 2.54mm  | Output L: 1-2=direct, 2-3=balanced |
| JP_OUT_R | 1x3 header  | 2.54mm  | Output R: same            |

### Coupling Caps (Output stage input coupling)

| Ref        | Value | Package | Notes                      |
|------------|-------|---------|----------------------------|
| C_COUPLE_L | 10uF  | 0805    | Seed out → DRV134 IN, blocks DC |
| C_COUPLE_R | 10uF  | 0805    | Seed out → DRV134 IN, blocks DC |

### Supply Bypass Caps (per IC — 4 caps each, 16 total)

Each THAT1246 and DRV134 needs a 10uF + 100nF pair on both V+ and V- pins,
placed close to the IC. All bypass cap ground sides connect to GNDA.

| Ref          | Value | Package | IC       | Supply pin |
|--------------|-------|---------|----------|------------|
| C_IN_L_P     | 10uF  | 0805    | U_IN_L   | V+ (+12VA) |
| C_IN_L_PH    | 100nF | 0402    | U_IN_L   | V+ (+12VA) |
| C_IN_L_N     | 10uF  | 0805    | U_IN_L   | V- (-12VA) |
| C_IN_L_NH    | 100nF | 0402    | U_IN_L   | V- (-12VA) |
| C_IN_R_P     | 10uF  | 0805    | U_IN_R   | V+ (+12VA) |
| C_IN_R_PH    | 100nF | 0402    | U_IN_R   | V+ (+12VA) |
| C_IN_R_N     | 10uF  | 0805    | U_IN_R   | V- (-12VA) |
| C_IN_R_NH    | 100nF | 0402    | U_IN_R   | V- (-12VA) |
| C_OUT_L_P    | 10uF  | 0805    | U_OUT_L  | V+ (+12VA) |
| C_OUT_L_PH   | 100nF | 0402    | U_OUT_L  | V+ (+12VA) |
| C_OUT_L_N    | 10uF  | 0805    | U_OUT_L  | V- (-12VA) |
| C_OUT_L_NH   | 100nF | 0402    | U_OUT_L  | V- (-12VA) |
| C_OUT_R_P    | 10uF  | 0805    | U_OUT_R  | V+ (+12VA) |
| C_OUT_R_PH   | 100nF | 0402    | U_OUT_R  | V+ (+12VA) |
| C_OUT_R_N    | 10uF  | 0805    | U_OUT_R  | V- (-12VA) |
| C_OUT_R_NH   | 100nF | 0402    | U_OUT_R  | V- (-12VA) |

**NOTE:** These 16 bypass caps are missing from the current BOM
(`hardware/bom.csv`). They need to be added before ordering.

---

## Wiring Connections Summary (net-by-net)

**+12VA** connects to: U_IN_L pin 8, U_IN_R pin 8, U_OUT_L pin 1, U_OUT_R pin 1,
  C_IN_L_P+, C_IN_L_PH+, C_IN_R_P+, C_IN_R_PH+,
  C_OUT_L_P+, C_OUT_L_PH+, C_OUT_R_P+, C_OUT_R_PH+

**-12VA** connects to: U_IN_L pin 1, U_IN_R pin 1, U_OUT_L pin 4, U_OUT_R pin 4,
  C_IN_L_N+, C_IN_L_NH+, C_IN_R_N+, C_IN_R_NH+,
  C_OUT_L_N+, C_OUT_L_NH+, C_OUT_R_N+, C_OUT_R_NH+

**GNDA** connects to: all TRS jack sleeves (J_IN_L, J_IN_R, J_OUT_L, J_OUT_R),
  U_IN_L pin 4 (REF), U_IN_R pin 4 (REF),
  U_OUT_L pin 3 (GND), U_OUT_R pin 3 (GND),
  all bypass cap ground sides (16 caps),
  R_GND (one side — other side to GND)

**Seed Audio In 1** (header pin 16): JP_IN_L pin 2

**Seed Audio In 2** (header pin 17): JP_IN_R pin 2

**Seed Audio Out 1** (header pin 18): JP_OUT_L pin 2, C_COUPLE_L (one side)

**Seed Audio Out 2** (header pin 19): JP_OUT_R pin 2, C_COUPLE_R (one side)

**JP_IN_L pin 1**: J_IN_L TIP

**JP_IN_L pin 3**: U_IN_L pin 6 (OUT)

**JP_IN_R pin 1**: J_IN_R TIP

**JP_IN_R pin 3**: U_IN_R pin 6 (OUT)

**JP_OUT_L pin 1**: J_OUT_L TIP

**JP_OUT_L pin 3**: U_OUT_L pin 5 (OUT+)

**JP_OUT_R pin 1**: J_OUT_R TIP

**JP_OUT_R pin 3**: U_OUT_R pin 5 (OUT+)

**C_COUPLE_L** (other side): U_OUT_L pin 7 (IN)

**C_COUPLE_R** (other side): U_OUT_R pin 7 (IN)

**U_OUT_L pin 2** (SENSE+): tied to U_OUT_L pin 5 (OUT+)

**U_OUT_L pin 6** (SENSE-): tied to U_OUT_L pin 8 (OUT-)

**U_OUT_R pin 2** (SENSE+): tied to U_OUT_R pin 5 (OUT+)

**U_OUT_R pin 6** (SENSE-): tied to U_OUT_R pin 8 (OUT-)

**J_OUT_L RING**: U_OUT_L pin 8 (OUT-)

**J_OUT_R RING**: U_OUT_R pin 8 (OUT-)

**J_IN_L TIP**: U_IN_L pin 2 (+IN), JP_IN_L pin 1

**J_IN_L RING**: U_IN_L pin 3 (-IN)

**J_IN_R TIP**: U_IN_R pin 2 (+IN), JP_IN_R pin 1

**J_IN_R RING**: U_IN_R pin 3 (-IN)

---

## Design Notes

### Coupling caps on inputs vs. outputs

The Seed's audio **inputs** (pins 16/17) are AC-coupled on the Seed module itself,
so no additional coupling cap is needed between THAT1246 output and the Seed input.

The Seed's audio **outputs** (pins 18/19) may have a small DC offset (~few mV).
C_COUPLE_L/R (10uF) block this DC from reaching the DRV134 input. The cutoff
frequency with the DRV134's ~20k input impedance is:

  f = 1 / (2pi x 20k x 10uF) = 0.8 Hz

This is well below audio band — no signal loss.

### DRV134 SENSE pin connections

The DRV134 SENSE+ and SENSE- pins provide output feedback for the internal
amplifiers. For unity-gain operation, tie SENSE+ directly to OUT+ (pin 2 to
pin 5) and SENSE- directly to OUT- (pin 6 to pin 8). These are short traces
or direct pin-to-pin connections on the PCB — no components needed.

### Input jack RING connection

The TRS input jack RING connects to THAT1246 -IN (pin 3). When a TS
(unbalanced) cable is inserted, the plug body shorts RING to SLEEVE,
which is GNDA. This gives the THAT1246 a ground reference on -IN and
it operates as a unity-gain buffer — no signal degradation.

### Output jack RING connection

The TRS output jack RING connects directly to DRV134 OUT- (pin 8).
When a TS cable is inserted, OUT- is shorted to GNDA through the sleeve.
The DRV134 is designed to handle this — it simply drives the hot output
into TIP with the cold output loaded to ground. No damage or oscillation.

### PCB layout considerations

- Place bypass caps as close as possible to each IC's supply pins.
- Keep the analog signal path short — route THAT1246 output to jumper
  to Seed input in a tight path.
- Keep analog traces away from the SPI bus, LED data line, and buck
  converter switching node.
- All four TRS jacks mount on the rear panel. Route audio traces on
  the analog ground plane side of the board where possible.
- The star ground bridge (R_GND) should be positioned near the Seed
  module, with GNDA on the audio side and GND on the digital side.
