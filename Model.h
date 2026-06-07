// Model.h — Triptech shared data model.
//
// The POD parameter structs (the serialized flash format), the global constants,
// the small lookup tables, and the CC<->value scaling helpers. This is pure data
// plus pure functions: no runtime state, no hardware, no behavior. Every
// behavioral module reads/writes a `Preset` and uses these helpers, so this is the
// one header they all share. See ~/.claude/plans/can-we-carve-out-async-elephant.md.
#pragma once

#include "daisysp.h" // fclamp (used by the inverse scale helpers)
#include <cmath>
#include <cstdint>
#include <cstring>

// ============================================================
// Constants
// ============================================================

static constexpr int NUM_CH = 3; // Ch1=0, Ch2=1, Ch3=2
static constexpr int NUM_STEPS = 16;
static constexpr int NUM_PATTERNS = 16;
static constexpr int TICKS_PER_STEP = 6; // 24 PPQN → 6 ticks per 16th note
static constexpr int NUM_PATCHES = 64;
static constexpr uint32_t TRS_TIMEOUT_MS = 500;
static constexpr float HALF_PI = 1.5707963268f;

// ============================================================
// Per-channel state
// ============================================================

// One channel's full voice definition: filter + envelope + LFO + output routing.
// Stored values are in engineering units (Hz, seconds, 0–1), not raw CC bytes —
// HandleCC()/CcGet() convert between the two.
struct ChannelPreset {
    float cutoff;        // Hz
    float resonance;     // 0–0.95
    float drive;         // 1–4 (pre-filter gain)
    float attack;        // seconds
    float decay;         // seconds
    float level;         // 0–1
    float pan;           // 0=left, 0.5=center, 1=right
    float lfoAmount;     // -1 to +1 (14-bit bipolar, filter mod)
    float delayAmount;   // 0–1 (post-amp send to delay)
    float ampLfoAmount;  // -1 to +1 (amp mod depth)
    float lfoDuty;       // 0–1 (waveform duty cycle)
    uint8_t filterType;  // 0=LP, 1=BP, 2=HP, 3=Notch
    uint8_t filterSlope; // 0=6dB, 1=12dB, 2=24dB
    uint8_t lfoShape;    // 0=Saw, 1=RevSaw, 2=Tri, 3=Sin, 4=Sq
    uint8_t lfoParam;    // CC raw value for rate
    bool lfoSynced;      // true = clock-synced, false = free Hz
};

// A complete patch: the three channel voices plus the global sequencer, delay
// and mix settings. This is the unit that gets saved/loaded as a "patch" and is
// also what `preset` (the live working state) holds.
struct Preset {
    ChannelPreset ch[NUM_CH];
    float bpm;             // 20–300
    float delayFeedback;   // 0–0.95
    float delayWidth;      // 0=mono, 1=full ping-pong
    float dryLevel;        // 0–1
    uint8_t pattern;       // 0–15
    uint8_t delayParam;    // CC 1 raw value
    uint8_t clockDivParam; // CC 5 raw value — pattern clock divider/multiplier
    bool delaySynced;      // true = clock-synced divisions, false = free ms
};

// The whole named-patch bank as it lives in QSPI flash. `version` gates a
// RestoreDefaults() migration when the struct layout changes; operator!= lets
// PersistentStorage skip the flash write when nothing actually changed.
struct PatchStorage {
    uint32_t version;
    Preset patches[NUM_PATCHES];
    bool operator!=(const PatchStorage &o) const { return memcmp(this, &o, sizeof(*this)) != 0; }
};

static constexpr uint32_t PATCH_VERSION = 4;

// ============================================================
// Lookup tables
// ============================================================

// Pattern clock divider/multiplier — steps-per-tick scaler.
// CC byte 0–127 binned into 9 zones; midway (56–71, includes 64) = 1:1.
static const float kClockRatios[9] = {
    0.25f,     // /4    (slower)
    1.f / 3.f, // /3
    0.5f,      // /2
    2.f / 3.f, // /1.5  (dotted slow)
    1.f,       // 1:1   (midway)
    1.5f,      // ×1.5  (dotted fast)
    2.f,       // ×2    (faster)
    3.f,       // ×3
    4.f,       // ×4
};

// Bin a 0–127 CC byte into one of the 9 kClockRatios zones (64 → index 4 = 1:1).
static inline uint8_t ClockDivIndex(uint8_t cc) {
    uint8_t idx = cc * 9u / 128u;
    return idx > 8 ? 8 : idx;
}

// Musical division table: beats per division (1 beat = 1 quarter note).
static const float kDivBeats[8] = {
    2.f,       // 1/2
    4.f / 3.f, // 1/2T
    1.f,       // 1/4
    2.f / 3.f, // 1/4T
    0.5f,      // 1/8
    1.f / 3.f, // 1/8T
    0.25f,     // 1/16
    1.f / 6.f, // 1/16T
};

// MIDI CC base per channel — 16 CC slots each (offsets 0-15 used).
static constexpr int kCcBase[NUM_CH] = {20, 36, 52};
// Note triggers: all on MIDI channel 1 — C4, C#4, D4.
static constexpr uint8_t kTrigNote[NUM_CH] = {60, 61, 62};

// ============================================================
// CC <-> value scaling
// ============================================================

// CcLin/CcLog map a 0–127 CC into [lo,hi] linearly / logarithmically (log is the
// musical taper for frequency and time); the *Inv variants are the exact inverses
// used by CcGet() to recover the CC for state dumps and the menu display.

static inline float CcLin(uint8_t v, float lo, float hi) { return lo + (v / 127.f) * (hi - lo); }

static inline float CcLog(uint8_t v, float lo, float hi) { return lo * powf(hi / lo, v / 127.f); }

static inline uint8_t CcLinInv(float val, float lo, float hi) {
    return (uint8_t)(daisysp::fclamp((val - lo) / (hi - lo) * 127.f, 0.f, 127.f) + 0.5f);
}

static inline uint8_t CcLogInv(float val, float lo, float hi) {
    return (uint8_t)(daisysp::fclamp(logf(val / lo) / logf(hi / lo) * 127.f, 0.f, 127.f) + 0.5f);
}
