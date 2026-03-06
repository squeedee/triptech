# Triptech

## Author

Rasheed Abdul-Aziz

## Description

Three-channel filtered gate effect for Daisy Pod. Stereo audio input is split into three parallel
filter channels, each with its own gate driven by an AD envelope. A 16-step sequencer opens the
gates rhythmically, clocked from MIDI or an internal clock. All parameters are controlled via MIDI
CC. A browser-based controller (`controller.html`) provides a full GUI with bidirectional MIDI CC
sync.

Each channel has a selectable filter type (LP / BP / HP / Notch) and slope (6 / 12 / 24 dB/oct).
Envelope amount and LFO amount are bipolar (±100%) with 14-bit resolution.

Button 1 starts and stops the sequencer. Button 2 sets the internal BPM via tap tempo.
The encoder selects patterns. Clicking the encoder toggles bypass.

## Building

```bash
git submodule update --init --recursive
make all
```

Flash with the Daisy web programmer or `make program-dfu`.

## Clock Sources

TRS MIDI in is the authoritative clock source. USB MIDI provides clock only when no TRS clock has
been received in the last 500 ms. When neither source is active, the internal clock runs at the
BPM set by tap tempo.

MIDI Start / Stop / Continue are handled from the active clock source.

## Pod Controls

| Control       | Function                                                                                                                  |
|---------------|---------------------------------------------------------------------------------------------------------------------------|
| Button 1      | Start / stop sequencer (stop resets to step 0)                                                                            |
| Button 2      | Tap tempo — sets internal BPM from tap interval (taps > 2 s apart reset the measurement)                                 |
| Encoder click | Toggle bypass (straight-through audio)                                                                                    |
| Encoder turn  | Select pattern (wraps 0–15)                                                                                               |
| LED 1         | Gate activity: blue = Ch1, green = Ch2, red = Ch3                                                                         |
| LED 2         | Bypass: solid white. MIDI clock: solid green = running. Internal clock: red beat-pulse = running, dim red = stopped       |

## MIDI Note → Gate

NoteOn messages on MIDI channel 1 trigger channel gates directly, regardless of sequencer run state:

| Note          | MIDI # | Gate    |
|---------------|--------|---------|
| C4 (middle C) | 60     | Ch 1    |
| C#4           | 61     | Ch 2    |
| D4            | 62     | Ch 3    |

The device echoes each NoteOn back and sends NoteOff when the channel's envelope finishes.

## MIDI CC Map

### Global

| CC  | Parameter      | Range                           | Default |
|-----|----------------|---------------------------------|---------|
| 14  | Pattern select | 0–127 → patterns 0–15           | 0       |
| 15  | Run / Stop     | ≥ 64 = run, < 64 = stop         | stopped |
| 16  | LFO rate       | 0–127 → 0.1–20 Hz (log)         | 1 Hz    |
| 17  | Chance         | 0–127 → 0–100% gate probability | 100%    |
| 18  | Bypass         | ≥ 64 = bypass on, < 64 = off    | off     |
| 19  | BPM            | 0–127 → 20–300 BPM              | 120 BPM |
| 119 | State dump     | any → device sends all CC state | —       |

### Per Channel  (Ch1 base = CC 20, Ch2 base = CC 36, Ch3 base = CC 52)

| Offset | Parameter               | Range                      | Ch1 Default | Ch2 Default | Ch3 Default |
|--------|-------------------------|----------------------------|-------------|-------------|-------------|
| +0     | Cutoff                  | 100–20000 Hz (log)         | 800 Hz      | 2000 Hz     | 1200 Hz     |
| +1     | Resonance               | 0–0.95                     | 0.5         | 0.5         | 0.5         |
| +2     | Drive (pre-filter gain) | ×1–×4                      | ×1          | ×1          | ×1          |
| +3     | Env amount MSB          | 14-bit bipolar, −1 to +1   | +100%       | +100%       | +100%       |
| +4     | Attack                  | 1 ms–2 s (log)             | 5 ms        | 5 ms        | 5 ms        |
| +5     | Decay                   | 10 ms–4 s (log)            | 200 ms      | 200 ms      | 200 ms      |
| +6     | Level                   | 0–1                        | 0.7         | 0.7         | 0.7         |
| +7     | Pan                     | full left–full right       | center      | 25% L       | 25% R       |
| +8     | LFO amount MSB          | 14-bit bipolar, −1 to +1   | 0%          | 0%          | 0%          |
| +9     | Filter type             | 0=LP, 32=BP, 64=HP, 96=Notch | LP        | BP          | HP          |
| +10    | Filter slope            | 0=6 dB, 63=12 dB, 126=24 dB | 12 dB      | 12 dB       | 12 dB       |
| +11    | Env amount LSB          | 14-bit pair with +3        | —           | —           | —           |
| +12    | LFO amount LSB          | 14-bit pair with +8        | —           | —           | —           |

Offsets +3/+11 form a 14-bit pair (MSB sent first, then LSB). Same for +8/+12.

Examples: Ch1 cutoff = CC 20, Ch2 pan = CC 43, Ch3 LFO amount = CC 60.

## MIDI Feedback (Bidirectional Sync)

The device sends CC back on both TRS and USB MIDI whenever state changes via physical controls:

| Physical control | CC | Value                     |
|------------------|----|---------------------------|
| Encoder turn     | 14 | pattern × 8               |
| Button 1         | 15 | 127 = run, 0 = stop       |
| Encoder click    | 18 | 127 = bypass on, 0 = off  |
| Button 2 (tap)   | 19 | BPM scaled 0–127          |

The device also sends DSP and main-loop load every 250 ms:

| CC | Parameter     | Range              |
|----|---------------|--------------------|
| 10 | DSP avg load  | 0–127 = 0–100%     |
| 11 | DSP peak load | 0–127 = 0–100%     |
| 12 | Loop avg time | 0–127 = 0–500 µs   |
| 13 | Loop peak time| 0–127 = 0–500 µs   |

Send CC 119 (any value) to request a full state dump of all parameters.

## Web Controller

Open `controller.html` in a browser with Web MIDI support (Chrome / Edge). Select the Daisy as
both MIDI In and MIDI Out — the page auto-selects it on load. Click **Sync** to pull current
device state into the UI at any time.

### Controller Keyboard Shortcuts

| Key          | Action                        |
|--------------|-------------------------------|
| Space        | Run / Stop                    |
| B            | Bypass                        |
| 1 / 2 / 3   | Trigger Ch 1 / Ch 2 / Ch 3   |
| Shift 1/2/3  | Mute Ch 1 / Ch 2 / Ch 3      |
| Ctrl 1/2/3   | Solo Ch 1 / Ch 2 / Ch 3      |
| ?            | Show help                     |

## Patterns

Each step triggers at most one channel. Patterns marked ✦ use all three channels.

| #  | Name                                              |
|----|---------------------------------------------------|
| 0  | Open Ch1 — Ch1 every 16th                         |
| 1  | Classic — Ch1 quarters / Ch3 8th-off / Ch2 fill ✦ |
| 2  | Ch1/Ch3 alternating                               |
| 3  | Even split — Ch1 / Ch2 / Ch3 / rest cycling     ✦ |
| 4  | Ch1 first half, Ch2 second half                   |
| 5  | Quarter rotation — Ch1 / Ch2 / Ch3 / Ch1        ✦ |
| 6  | Open Ch3 — Ch3 every 16th                         |
| 7  | Ch1→Ch2→Ch3 rotation every step                 ✦ |
| 8  | Ch1 quarters + Ch2 8th-offbeats                   |
| 9  | Triplet rotation                                ✦ |
| 10 | Syncopated three-way                            ✦ |
| 11 | Channel blocks                                  ✦ |
| 12 | Clave (son 3+3+2) with Ch3 fills and Ch2 sparse ✦ |
| 13 | Half-time sparse                                ✦ |
| 14 | Dotted-8th Ch1 + Ch3 fills                        |
| 15 | Complex syncopated                              ✦ |
