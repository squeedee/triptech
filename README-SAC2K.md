# SAC-2K Driver Library for Daisy Seed

A C++ library for using the Radikal Technologies SAC-2K v3.0.3 MIDI controller
as a prototyping surface for Daisy Seed synthesizer/effects projects.

## Overview

The SAC-2K is a professional MIDI controller surface with:
- 9 motorized 100mm faders (touch-sensitive)
- 12 rotary encoders with push buttons and 11-LED rings
- 3 LCD displays (2 lines x 40 characters each)
- 70+ buttons with LED indicators
- Jog wheel with LED ring
- 7-segment time display

This library runs on the Daisy Seed side and provides a clean API to:
- Receive encoder turns, fader movements, button presses, and touch events
- Control motor fader positions (faders physically move to target)
- Set encoder LED ring patterns (dot, bar, centered, panorama, etc.)
- Write text to LCD displays (both CC-based and SysEx methods)
- Control button LEDs (on/off/blink/flash patterns)
- Drive in-display level meters
- Write to the 7-segment time display

## Hardware Setup

### MIDI Connection

Connect the SAC-2K to the Daisy Seed via standard 5-pin DIN MIDI:
- SAC-2K MIDI OUT -> Daisy Seed MIDI IN (TRS or via MIDI breakout board)
- Daisy Seed MIDI OUT -> SAC-2K MIDI IN

Alternatively, use USB MIDI if your SAC-2K has the USB option installed.

### SAC-2K Configuration

The SAC-2K must be set to **Generic Slave Mode (mode 2)** for this library
to work correctly. In slave mode, the host (Daisy Seed) controls all visual
feedback, and the SAC sends raw control events.

#### Setting Slave Mode via the SAC-2K front panel:

1. Hold the **SYSTEM** button
2. While held, press **digit 2** on the numeric keypad
3. The SAC enters Generic Slave mode

#### Setting the MIDI channel:

The SAC-2K and the Daisy Seed must use the same MIDI channel.

1. Hold the **SYSTEM** button
2. Use the numeric keypad to enter the channel number
3. The default is channel 16 (0x0F); this library defaults to channel 1 (0x00)

**Important:** Match the `Config::channel` value (0-15) to the SAC's setting.

#### Programmatic configuration:

You can also send the slave mode command from the Daisy at startup:

```cpp
sac.SendSlaveMode();  // Sends SysEx to put SAC into mode 2
```

## Files

| File | Purpose |
|------|---------|
| `SAC2K.h` | Header — all types, enums, API declarations |
| `SAC2K.cpp` | Implementation |

## Quick Start

```cpp
#include "daisy_seed.h"
#include "daisysp.h"
#include "SAC2K.h"

using namespace daisy;

static DaisySeed seed;
static MidiUartHandler trs_midi;
static sac2k::SAC2K sac;

// Fader 0-8 moved
void OnFader(uint8_t fader, uint8_t value, void* ctx) {
    // value is 0-127
    float level = value / 127.f;
    // ... apply to your parameter
}

// Encoder 0-11 turned
void OnEncoder(uint8_t enc, int8_t delta, void* ctx) {
    // delta is -63..+63 (negative = CCW, positive = CW)
    // ... adjust your parameter
}

// Button pressed/released
void OnButton(sac2k::Button btn, uint8_t state, void* ctx) {
    // state: 0 = released, 1 = pressed, 2 = shift+pressed
    if (btn == sac2k::Button::Play && state == 1) {
        // ... start sequencer
    }
}

int main() {
    seed.Init();

    // Init TRS MIDI
    MidiUartHandler::Config midi_cfg;
    midi_cfg.transport_config.rx = seed.GetPin(15); // adjust for your wiring
    midi_cfg.transport_config.tx = seed.GetPin(14);
    trs_midi.Init(midi_cfg);
    trs_midi.StartReceive();

    // Init SAC-2K driver
    sac2k::SAC2K::Config sac_cfg;
    sac_cfg.channel = 0; // MIDI channel 1

    sac.Init(sac_cfg, &trs_midi, nullptr); // TRS only, no USB
    sac.SetEncoderCallback(OnEncoder);
    sac.SetFaderCallback(OnFader);
    sac.SetButtonCallback(OnButton);

    // Put SAC into slave mode (optional if already configured)
    sac.SendSlaveMode();

    // Register our application
    sac.RegisterApp("DAI", 1, 0, "Trip");

    // Set up initial display
    sac.ClearDisplay(0);
    sac.ClearDisplay(1);
    sac.ClearDisplay(2);
    sac.WriteDisplayLine(0, 0, "  FREQ     Q      DRIVE    ATK  ");
    sac.WriteDisplayLine(0, 1, " 800 Hz   0.50    1.0x    5 ms  ");

    // Set encoder LED modes
    sac.SetEncoder(0, sac2k::EncoderMode::Volume128, 64);  // bar display
    sac.SetEncoder(1, sac2k::EncoderMode::Steps128,  64);  // dot display
    sac.SetEncoder(2, sac2k::EncoderMode::Pan128,    64);  // centered bar

    // Set initial fader positions (motors will move)
    sac.SetFader(0, 90);  // channel 1 at ~70%
    sac.SetFader(1, 90);
    sac.SetFader(8, 100); // master

    // Main loop
    while (true) {
        trs_midi.Listen();
        sac.Process();  // drains MIDI events, calls callbacks

        // ... your DSP/UI logic
    }
}
```

## API Reference

### Initialization

```cpp
void Init(const Config& cfg,
          MidiHandler<MidiUartTransport>* trs = nullptr,
          MidiHandler<MidiUsbTransport>*  usb = nullptr);
```

Pass one or both MIDI handlers. The SAC-2K events will be read from whichever
transport(s) you provide. Outgoing feedback is sent to both.

### Process Loop

```cpp
void Process();
```

Call every main loop iteration. Drains all pending MIDI events and dispatches
your registered callbacks.

### Callbacks

| Setter | Signature | Description |
|--------|-----------|-------------|
| `SetEncoderCallback` | `void(uint8_t enc, int8_t delta, void* ctx)` | Encoder 0-11 turned, delta -63..+63 |
| `SetFaderCallback` | `void(uint8_t fader, uint8_t value, void* ctx)` | Fader 0-8 moved (8=master), value 0-127 |
| `SetButtonCallback` | `void(Button btn, uint8_t state, void* ctx)` | Button press/release (0/1/2) |
| `SetTouchCallback` | `void(uint8_t fader, bool touched, void* ctx)` | Fader touch sensor |
| `SetJogCallback` | `void(int8_t delta, void* ctx)` | Jog wheel turned |

All callbacks receive an optional `void* ctx` for passing user data.

### Motor Faders

```cpp
void SetFader(uint8_t fader, uint8_t value);  // fader 0-8, value 0-127
```

Sends a CC that causes the SAC's motor fader to physically move to the position.

### Encoder LED Rings

```cpp
void SetEncoderMode(uint8_t enc, EncoderMode mode, uint8_t blinkMod = 0);
void SetEncoderValue(uint8_t enc, uint8_t value);
void SetEncoder(uint8_t enc, EncoderMode mode, uint8_t value, uint8_t blinkMod = 0);
void SetEncoderOff(uint8_t enc);
```

**EncoderMode options:**

| Mode | Display Style |
|------|--------------|
| `Steps128` | Single dot, 128 positions |
| `Volume128` | Bar from left, 128 positions |
| `Pan128` | Centered bar, 128 positions |
| `Band128` | Centered bar (V1.05) |
| `Damping128` | Bar from right, 128 positions |

Each also has 32-step and 16-step variants. Blink modifiers:
- `kEncBlink` (0x20) — normal blink
- `kEncFastBlink` (0x40) — fast blink
- `kEncFlash` (0x60) — flash

### Button LEDs

```cpp
void SetButtonLed(Button btn, LedState state);
```

| State | Effect |
|-------|--------|
| `Off` | LED off |
| `On` | LED on |
| `Blink` | LED blinking |
| `ExtBlink` | 4-bit pattern blink (V1.02+) |
| `ExtQBlink` | Fast blink |
| `ExtFlash` | Flash |

### LCD Displays

```cpp
// CC-based (one char at a time — simple, slightly slower for long strings)
void SetDisplayCursor(uint8_t display, uint8_t col, uint8_t row);
void WriteDisplayChar(uint8_t display, char c);
void ClearDisplay(uint8_t display);
void WriteDisplayString(uint8_t display, uint8_t col, uint8_t row, const char* str);
void WriteDisplayLine(uint8_t display, uint8_t row, const char* str);

// SysEx-based (sends entire string in one message — more efficient)
void WriteDisplaySysEx(uint8_t display, uint8_t col, uint8_t row, const char* str);
```

Display 0 is the left LCD, display 1 is center, display 2 is right.
Each has 2 rows of 40 characters.

### Level Meters

```cpp
void SetLevelMeter(uint8_t display, uint8_t position, uint8_t value);
```

Drives the in-display level meters (uses custom LCD characters).
Position: 1-4, Value: 0-15. Note: level meters and custom characters
share the same LCD character slots, so you can't use both simultaneously
on the same display.

### Time Display

```cpp
void WriteTimeDisplay(const char* str);
```

Writes ASCII characters to the 7-segment time display (requires ASCII mode 2).

### System

```cpp
void SendSlaveMode();                          // Set SAC to Generic Slave mode
void RegisterApp(const char[3], uint8_t, uint8_t, const char[4]);  // Register app
void RequestIdentity();                        // Request SAC identity SysEx
```

## Physical Layout Reference

The SAC-2K's physical layout from left to right:

```
[Transport]  [Faders 1-8]  [Master]  [Encoders 1-12 in 3 rows of 4]
                                      [LCD 1]  [LCD 2]  [LCD 3]
```

- **Faders 1-8**: Channel faders (index 0-7)
- **Master fader**: Index 8
- **Encoders 1-12**: Arranged in rows, each with push button and LED ring
- **Mute/Solo/Select buttons**: Below each channel fader
- **Transport**: Play, Stop, Record, Rewind, Forward, Scrub

## Building

Add `SAC2K.cpp` to your Makefile's `CPP_SOURCES`:

```makefile
CPP_SOURCES = Triptech.cpp SAC2K.cpp
```

The library depends only on libDaisy headers (`daisy_seed.h` for MIDI types).

## Notes

- The SAC-2K encoders send **relative** values (signed delta), not absolute
  positions. Your code must maintain the current parameter value and apply
  the delta.
- Motor faders have touch sensors. When a user touches a fader, you should
  stop sending position updates to avoid fighting the user's hand. Use the
  touch callback for this.
- The SysEx display write method is more efficient for updating multi-character
  strings, but the CC method works reliably for single-character updates.
- In slave mode, the SAC's internal memory/presets are inactive — the host
  controls everything.
