#include "daisysp.h"
#include "daisy_seed.h"
#include "Ili9341.h"
#include "Mux4067.h"
#include <cmath>
#include <cstring>
#include <cstdlib>

using namespace daisysp;
using namespace daisy;

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
// Gate Patterns [pattern][step][channel]   (0=Ch1, 1=Ch2, 2=Ch3)
// ============================================================

// clang-format off
static const bool kPatterns[NUM_PATTERNS][NUM_STEPS][NUM_CH] = {
    // 0: Cascade — LP→BP→HP sweep with gaps
    {{1,0,0},{0,1,0},{0,0,1},{0,0,0},{0,0,0},{1,0,0},{0,1,0},{0,0,1},
     {1,0,0},{0,1,0},{0,0,1},{0,0,0},{0,0,0},{1,0,0},{0,1,0},{0,0,1}},

    // 1: Classic — LP quarters / HP 8th-off / BP 16th-fill
    {{1,0,0},{0,1,0},{0,0,1},{0,1,0},{1,0,0},{0,1,0},{0,0,1},{0,1,0},
     {1,0,0},{0,1,0},{0,0,1},{0,1,0},{1,0,0},{0,1,0},{0,0,1},{0,1,0}},

    // 2: Tresillo — 3+3+2 Latin feel / LP accent / BP offbeat / HP running
    {{1,0,0},{0,0,0},{0,0,1},{0,1,0},{0,0,0},{0,0,1},{1,0,0},{0,0,0},
     {0,0,1},{0,1,0},{0,0,0},{0,0,1},{1,0,0},{0,0,0},{0,0,1},{0,1,0}},

    // 3: Even split — LP/BP/HP/rest cycling
    {{1,0,0},{0,1,0},{0,0,1},{0,0,0},{1,0,0},{0,1,0},{0,0,1},{0,0,0},
     {1,0,0},{0,1,0},{0,0,1},{0,0,0},{1,0,0},{0,1,0},{0,0,1},{0,0,0}},

    // 4: Polymetric — LP quarters / BP ~5ths / HP ~3rds (overlaps on 8, 11)
    {{1,0,0},{0,1,0},{0,0,1},{0,0,0},{1,0,0},{0,0,1},{0,1,0},{0,0,0},
     {1,0,1},{0,0,0},{0,0,0},{0,1,1},{1,0,0},{0,0,0},{0,0,1},{0,0,0}},

    // 5: Quarter rotation — LP / BP / HP / LP
    {{1,0,0},{0,0,0},{0,0,0},{0,0,0},{0,1,0},{0,0,0},{0,0,0},{0,0,0},
     {0,0,1},{0,0,0},{0,0,0},{0,0,0},{1,0,0},{0,0,0},{0,0,0},{0,0,0}},

    // 6: Dense stack — layered with simultaneous triggers
    {{1,0,1},{0,1,0},{0,0,1},{0,1,0},{1,0,0},{0,0,1},{1,1,0},{0,0,1},
     {1,0,0},{0,1,0},{0,0,1},{0,1,0},{1,0,0},{0,0,1},{1,1,0},{0,0,1}},

    // 7: LP→BP→HP rotation every step
    {{1,0,0},{0,1,0},{0,0,1},{1,0,0},{0,1,0},{0,0,1},{1,0,0},{0,1,0},
     {0,0,1},{1,0,0},{0,1,0},{0,0,1},{1,0,0},{0,1,0},{0,0,1},{1,0,0}},

    // 8: Shuffle — swing-like paired hits
    {{1,0,0},{0,1,0},{0,0,1},{1,0,0},{0,0,1},{0,1,0},{0,0,0},{0,0,1},
     {1,0,0},{0,1,0},{0,0,1},{1,0,0},{0,0,1},{0,1,0},{0,0,0},{0,0,1}},

    // 9: Triplet rotation — LP on 0,3,6,9,12,15 / HP on 1,4,7,10,13 / BP on 2,5,8,11,14
    {{1,0,0},{0,0,1},{0,1,0},{1,0,0},{0,0,1},{0,1,0},{1,0,0},{0,0,1},
     {0,1,0},{1,0,0},{0,0,1},{0,1,0},{1,0,0},{0,0,1},{0,1,0},{1,0,0}},

    // 10: Syncopated three-way — LP:0,3,7,10 / BP:1,5,9,13 / HP:2,6,11,14
    {{1,0,0},{0,1,0},{0,0,1},{1,0,0},{0,0,0},{0,1,0},{0,0,1},{1,0,0},
     {0,0,0},{0,1,0},{1,0,0},{0,0,1},{0,0,0},{0,1,0},{0,0,1},{0,0,0}},

    // 11: Channel blocks — two hits each in sequence, then one each
    {{1,0,0},{1,0,0},{0,0,0},{0,0,0},{0,1,0},{0,1,0},{0,0,0},{0,0,0},
     {0,0,1},{0,0,1},{0,0,0},{0,0,0},{1,0,0},{0,1,0},{0,0,1},{0,0,0}},

    // 12: Clave — LP clave (son 3+3+2) / HP fills / BP sparse
    {{1,0,0},{0,0,1},{0,0,0},{1,0,0},{0,0,1},{0,0,0},{1,0,0},{0,1,0},
     {0,0,1},{0,0,0},{1,0,0},{0,0,1},{1,0,0},{0,0,0},{0,0,1},{0,1,0}},

    // 13: Half-time — LP:0,2 / BP:6 / HP:8,12
    {{1,0,0},{0,0,0},{1,0,0},{0,0,0},{0,0,0},{0,0,0},{0,1,0},{0,0,0},
     {0,0,1},{0,0,0},{0,0,0},{0,0,0},{0,0,1},{0,0,0},{0,0,0},{0,0,0}},

    // 14: Call/response — LP burst / BP bridge / HP answer
    {{1,0,0},{0,0,0},{1,0,0},{0,0,0},{1,0,0},{0,1,0},{0,0,0},{0,1,0},
     {0,0,1},{0,0,0},{0,0,1},{0,0,0},{0,0,1},{0,1,0},{0,0,0},{0,0,1}},

    // 15: Complex syncopated — LP:0,2,9,13 / BP:3,7,11,15 / HP:5,6,12
    {{1,0,0},{0,0,0},{1,0,0},{0,1,0},{0,0,0},{0,0,1},{0,0,1},{0,1,0},
     {0,0,0},{1,0,0},{0,0,0},{0,1,0},{0,0,1},{1,0,0},{0,0,0},{0,1,0}},
};
// clang-format on

// ============================================================
// Per-channel state
// ============================================================

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

struct PatchStorage {
    uint32_t version;
    Preset patches[NUM_PATCHES];
    bool operator!=(const PatchStorage &o) const { return memcmp(this, &o, sizeof(*this)) != 0; }
};

static constexpr uint32_t PATCH_VERSION = 4;

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

static inline uint8_t ClockDivIndex(uint8_t cc) {
    uint8_t idx = cc * 9u / 128u;
    return idx > 8 ? 8 : idx;
}

struct Channel {
    Svf fltL, fltR;          // SVF: LP/HP/BP/Notch at 12 dB
    LadderFilter ladL, ladR; // Ladder: LP/HP/BP at 12 or 24 dB
    OnePole poleL, poleR;    // OnePole: LP/HP at 6 dB
    AdEnv env;
    float lfoPhase;    // 0–1 phase accumulator
    float lfoVal;      // last computed LFO sample
    uint8_t lfoAmtMsb; // 14-bit MSB cache for filter lfoAmount
    bool note_active;
    bool env_started;
};

// ============================================================
// Globals
// ============================================================

// ============================================================
// Delay — ping-pong, clock-synced
// Max 192000 samples ≈ 4 s at 48 kHz (covers 1/2 bar at ~30 BPM)
// ============================================================

// Musical division table: beats per division (1 beat = 1 quarter note)
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

static DelayLine<float, 192001> DSY_SDRAM_BSS delayL;
static DelayLine<float, 192001> DSY_SDRAM_BSS delayR;

static DaisySeed hw;
static MidiUartHandler midi; // TRS MIDI — USART1 default pins (PB6/PB7 = D13/D14)
static MidiUsbHandler usb_midi;
static PersistentStorage<PatchStorage> patchStorage(hw.qspi);

// Global UI prefs persisted in QSPI, in its own sector well clear of the patch
// bank (which lives at offset 0).
struct UiSettings {
    uint32_t version;
    uint16_t color[4];
    uint8_t brightness;
    // Persisted transport/global state (restored on boot).
    uint8_t run;    // sequencer running 0/1
    uint8_t bypass; // 0/1
    uint8_t dry;    // dry level as CC 0..127
    uint8_t bpm;    // manual BPM as CC 0..127
    bool operator!=(const UiSettings &o) const {
        if (version != o.version || brightness != o.brightness || run != o.run ||
            bypass != o.bypass || dry != o.dry || bpm != o.bpm)
            return true;
        for (int i = 0; i < 4; i++)
            if (color[i] != o.color[i])
                return true;
        return false;
    }
};
static constexpr uint32_t UI_VERSION = 2;
static constexpr uint32_t UI_QSPI_OFFSET = 0x80000; // 512 KB in
static PersistentStorage<UiSettings> uiStore(hw.qspi);

static Preset preset;
static Channel ch[NUM_CH];
static float sample_rate;
static uint8_t cur_patch = 0;

// Output soft-start: ramp the audio out from silence over ~150 ms so the engine
// starting up (and any garbage in the first DMA block) slews in instead of
// popping. soft_inc is set in main() once the sample rate is known.
static float soft_gain = 0.f;
static float soft_inc = 1.f;

static CpuLoadMeter dspLoad;
static float loopLoadAvg = 0.f; // smoothed main-loop µs per iteration
static float loopPeak = 0.f;    // peak µs in current 250ms window
static uint32_t lastLoadMs = 0;
static float ticksPerUs = 1.f;   // populated after init
static float ui_dsp_load = 0.f;  // audio DSP load 0..1 (for the footer meter)
static float ui_ctrl_load = 0.f; // control/main-loop load 0..1 (vs 500 µs)
// Peak in/out levels for the VU page (written by the audio callback, decaying).
static volatile float vu_in_l = 0.f, vu_in_r = 0.f, vu_out_l = 0.f, vu_out_r = 0.f;
// Debounced persistence of transport/global state (run/bypass/dry/bpm) to QSPI.
static uint32_t ui_persist_ms = 0;
static uint8_t ui_seen_run = 0xFF, ui_seen_byp = 0xFF, ui_seen_dry = 0xFF, ui_seen_bpm = 0xFF;

static uint8_t cur_step = 0;
static float tick_accum = 0.f;
static bool seq_running = true; // sequencer runs by default at power-on

// Incoming-clock BPM detection (updates preset.bpm when external clock is active)
static uint32_t last_clock_us = 0;
static float clock_bpm_ema = 0.f;
static uint8_t last_bpm_cc = 255;

static bool trs_active = false;
static uint32_t trs_last_ms = 0;
static bool usb_clock_active = false;
static uint32_t usb_last_ms = 0;

// Internal clock
static uint32_t int_tick_ms = 0;   // timestamp of last internal tick
static uint32_t led2_flash_ms = 0; // timestamp of last beat flash
static uint32_t g_step_ms = 0;     // timestamp of last sequencer step (global dot tick)
static uint32_t tap_last_ms = 0;   // timestamp of last tap-tempo tap
static bool bypass = false;
static bool ch_muted[NUM_CH] = {false, false, false};
static bool ch_solo[NUM_CH] = {false, false, false};

// A channel is silenced if it's muted, or if any channel is soloed and it isn't.
static inline bool any_solo() { return ch_solo[0] || ch_solo[1] || ch_solo[2]; }
static inline bool ch_silenced(int c) { return ch_muted[c] || (any_solo() && !ch_solo[c]); }

// MIDI CC base per channel — 16 CC slots each (offsets 0-15 used)
static constexpr int kCcBase[NUM_CH] = {20, 36, 52};
// Note triggers: all on MIDI channel 1 — C4, C#4, D4
static constexpr uint8_t kTrigNote[NUM_CH] = {60, 61, 62};

// --- Menu UI state (240x320 TFT + 4 encoders). Driven from the main loop
//     only — see the "Menu UI" section below. Declared here so ProcessMidi and
//     the telemetry block can flag the screen dirty. ---
static const char *const CH_NAME[NUM_CH] = {"CH1", "CH2", "CH3"};
static uint8_t ui_ctx = 0;         // 0..2 = channel, 3 = global
static uint8_t ui_sec = 0;         // section within the current context
static uint8_t ui_patch_sel = 0;   // patch slot highlighted on the PATCH page
static bool ui_settings = false;   // hidden settings mode (hold NAV + click E1)
static bool ui_vu = false;         // hidden I/O VU meter (hold NAV + click E2)
static bool ui_full_dirty = true;  // full-screen redraw pending
static bool ui_foot_dirty = false; // footer (BPM) redraw pending
static bool ui_band_dirty[3] = {true, true, true};

// ============================================================
// Per-channel LFO — phase-accumulator with duty cycle
// ============================================================

// `ramp` is the transition width in phase units (0..1). Only used by the
// trapezoidal "square" shape; ignored otherwise. Caller sizes it from lfoFreq
// so the slew stays roughly constant in wall-clock time across all rates.
static float LfoSample(float phase, uint8_t shape, float duty, float ramp) {
    if (duty < 0.01f)
        duty = 0.01f;
    if (duty > 0.99f)
        duty = 0.99f;
    switch (shape) {
    case 0: // Triangle — peak position at duty
        return (phase < duty) ? -1.f + 2.f * phase / duty
                              : 1.f - 2.f * (phase - duty) / (1.f - duty);
    case 1: { // Sine — skewed via phase warp, peak at duty
        float w =
            (phase < duty) ? 0.5f * phase / duty : 0.5f + 0.5f * (phase - duty) / (1.f - duty);
        return -cosf(w * 6.283185307f);
    }
    case 2: { // Trapezoid — square with slew centered on each transition
        float maxR = (duty < (1.f - duty) ? duty : (1.f - duty)) * 0.9f;
        float r = ramp > maxR ? maxR : ramp;
        float half = r * 0.5f;
        if (phase < half)
            return 2.f * phase / r; // rising tail (0 → +1)
        if (phase < duty - half)
            return 1.f; // high plateau
        if (phase < duty + half)
            return 1.f - 2.f * (phase - duty + half) / r; // falling (+1 → -1)
        if (phase < 1.f - half)
            return -1.f;                              // low plateau
        return -1.f + 2.f * (phase - 1.f + half) / r; // rising head (-1 → 0)
    }
    default:
        return 0.f;
    }
}

// ============================================================
// Default preset factory
// ============================================================

static void InitChannelLfo(ChannelPreset &cp) {
    cp.lfoParam = 55;
    cp.lfoSynced = true;
    cp.lfoShape = 1; // Sine
    cp.lfoDuty = 0.5f;
    cp.ampLfoAmount = 0.f;
}

static Preset DefaultPreset() {
    Preset p = {};
    p.bpm = 120.f;
    p.delayParam = 32;
    p.clockDivParam = 64; // midway = 1:1
    p.delaySynced = true;
    p.delayFeedback = 0.4f;
    p.delayWidth = 1.f;
    p.dryLevel = 0.f;
    p.pattern = 0;

    // Ch1 — LP, center
    p.ch[0].cutoff = 800.f;
    p.ch[0].resonance = 0.5f;
    p.ch[0].drive = 1.f;
    p.ch[0].attack = 0.005f;
    p.ch[0].decay = 0.2f;
    p.ch[0].level = 0.7f;
    p.ch[0].pan = 0.5f;
    p.ch[0].lfoAmount = 0.f;
    p.ch[0].delayAmount = 0.f;
    p.ch[0].filterType = 0;
    p.ch[0].filterSlope = 1;
    InitChannelLfo(p.ch[0]);

    // Ch2 — BP, left
    p.ch[1].cutoff = 1000.f;
    p.ch[1].resonance = 0.5f;
    p.ch[1].drive = 1.f;
    p.ch[1].attack = 0.005f;
    p.ch[1].decay = 0.2f;
    p.ch[1].level = 0.7f;
    p.ch[1].pan = 0.25f;
    p.ch[1].lfoAmount = 0.f;
    p.ch[1].delayAmount = 0.f;
    p.ch[1].filterType = 1;
    p.ch[1].filterSlope = 1;
    InitChannelLfo(p.ch[1]);

    // Ch3 — HP, right
    p.ch[2].cutoff = 600.f;
    p.ch[2].resonance = 0.5f;
    p.ch[2].drive = 1.f;
    p.ch[2].attack = 0.005f;
    p.ch[2].decay = 0.2f;
    p.ch[2].level = 0.7f;
    p.ch[2].pan = 0.75f;
    p.ch[2].lfoAmount = 0.f;
    p.ch[2].delayAmount = 0.f;
    p.ch[2].filterType = 2;
    p.ch[2].filterSlope = 1;
    InitChannelLfo(p.ch[2]);

    return p;
}

// ============================================================
// Helpers
// ============================================================

static float CcLin(uint8_t v, float lo, float hi) { return lo + (v / 127.f) * (hi - lo); }

static float CcLog(uint8_t v, float lo, float hi) { return lo * powf(hi / lo, v / 127.f); }

static uint8_t CcLinInv(float val, float lo, float hi) {
    return (uint8_t)(fclamp((val - lo) / (hi - lo) * 127.f, 0.f, 127.f) + 0.5f);
}

static uint8_t CcLogInv(float val, float lo, float hi) {
    return (uint8_t)(fclamp(logf(val / lo) / logf(hi / lo) * 127.f, 0.f, 127.f) + 0.5f);
}

static inline float fasttanh(float x) {
    if (x > 3.f)
        return 1.f;
    if (x < -3.f)
        return -1.f;
    float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

static void SendCC(uint8_t cc, uint8_t val) {
    uint8_t msg[3] = {0xB0, cc, val};
    midi.SendMessage(msg, 3);
    usb_midi.SendMessage(msg, 3);
}

static void SendProgramChange(uint8_t prog) {
    uint8_t msg[2] = {0xC0, prog};
    midi.SendMessage(msg, 2);
    usb_midi.SendMessage(msg, 2);
}

static void SendNoteOn(uint8_t note, uint8_t vel) {
    uint8_t msg[3] = {0x90, note, vel};
    midi.SendMessage(msg, 3);
    usb_midi.SendMessage(msg, 3);
}

static void SendNoteOff(uint8_t note) {
    uint8_t msg[3] = {0x80, note, 0};
    midi.SendMessage(msg, 3);
    usb_midi.SendMessage(msg, 3);
}

// Inverse of HandleCC: the current 7-bit value for a given CC, derived from the
// live preset/state. This is the single source of truth for both outbound state
// dumps (SendAllState) and the on-device menu UI's value display + edit base.
static uint8_t CcGet(uint8_t cc) {
    switch (cc) {
    case 1:
        return preset.delayParam;
    case 2:
        return CcLinInv(preset.delayFeedback, 0.f, 0.95f);
    case 3:
        return CcLinInv(preset.delayWidth, 0.f, 1.f);
    case 4:
        return CcLinInv(preset.dryLevel, 0.f, 1.f);
    case 5:
        return preset.clockDivParam;
    case 6:
        return preset.delaySynced ? 127 : 0;
    case 14:
        return preset.pattern * 8;
    case 15:
        return seq_running ? 127 : 0;
    case 18:
        return bypass ? 127 : 0;
    case 19:
        return (uint8_t)((fclamp(preset.bpm, 20.f, 300.f) - 20.f) / 280.f * 127.f);
    case 68:
        return (ch_muted[0] ? 1 : 0) | (ch_muted[1] ? 2 : 0) | (ch_muted[2] ? 4 : 0);
    default:
        break;
    }
    for (int c = 0; c < NUM_CH; c++) {
        int off = (int)cc - kCcBase[c];
        if (off < 0 || off > 15)
            continue;
        const ChannelPreset &cp = preset.ch[c];
        switch (off) {
        case 0:
            return CcLogInv(cp.cutoff, 100.f, 20000.f);
        case 1:
            return CcLinInv(cp.resonance, 0.f, 0.95f);
        case 2:
            return CcLinInv(cp.drive, 1.f, 4.f);
        case 3:
            return cp.lfoParam;
        case 4:
            return CcLogInv(cp.attack, 0.001f, 2.f);
        case 5:
            return CcLogInv(cp.decay, 0.01f, 2.f);
        case 6:
            return CcLinInv(cp.level, 0.f, 1.f);
        case 7:
            return CcLinInv(cp.pan, 0.f, 1.f);
        case 8: {
            uint16_t v14 =
                (uint16_t)(fclamp((cp.lfoAmount + 1.f) * 0.5f * 16383.f, 0.f, 16383.f) + 0.5f);
            return v14 >> 7;
        }
        case 12: {
            uint16_t v14 =
                (uint16_t)(fclamp((cp.lfoAmount + 1.f) * 0.5f * 16383.f, 0.f, 16383.f) + 0.5f);
            return v14 & 0x7F;
        }
        case 9:
            return cp.filterType * 32;
        case 10:
            return cp.filterSlope * 63;
        case 11:
            return cp.lfoShape * 21 + (cp.lfoSynced ? 64 : 0);
        case 13:
            return CcLinInv(cp.delayAmount, 0.f, 1.f);
        case 14:
            return CcLinInv(cp.lfoDuty, 0.f, 1.f);
        case 15:
            return CcLinInv(cp.ampLfoAmount, -1.f, 1.f);
        }
    }
    return 0;
}

static void SendAllState() {
    static const uint8_t kGlobalCc[] = {1, 2, 3, 4, 5, 6, 14, 15, 18, 19, 68};
    for (uint8_t cc : kGlobalCc)
        SendCC(cc, CcGet(cc));
    SendProgramChange(cur_patch);
    for (int c = 0; c < NUM_CH; c++) {
        int base = kCcBase[c];
        for (int off = 0; off <= 15; off++)
            SendCC(base + off, CcGet(base + off));
    }
}

// ============================================================
// Patch management
// ============================================================

static void LoadPatch(uint8_t idx) {
    if (idx >= NUM_PATCHES)
        return;
    cur_patch = idx;
    preset = patchStorage.GetSettings().patches[idx];
    SendAllState();
}

static void SavePatch(uint8_t idx) {
    if (idx >= NUM_PATCHES)
        return;
    cur_patch = idx;
    PatchStorage &store = patchStorage.GetSettings();
    store.patches[idx] = preset;
    patchStorage.Save();          // brief audio glitch possible during flash write
    SendProgramChange(cur_patch); // echo back saved location
    SendCC(87, idx);              // confirm save to controller
}

// ============================================================
// Gate trigger
// ============================================================

static void TriggerGate(int c) {
    ch[c].env.SetTime(ADENV_SEG_ATTACK, preset.ch[c].attack);
    ch[c].env.SetTime(ADENV_SEG_DECAY, preset.ch[c].decay);
    ch[c].env.Trigger();
    if (ch[c].note_active)
        SendNoteOff(kTrigNote[c]); // cut off any still-open note
    SendNoteOn(kTrigNote[c], 100);
    ch[c].note_active = true;
    ch[c].env_started = false;
}

// ============================================================
// Sequencer step (called every TICKS_PER_STEP MIDI clocks)
// ============================================================

static void AdvanceClock() {
    // ratio = pattern-steps consumed per incoming tick. >1 multiplies, <1 divides.
    float ratio = kClockRatios[ClockDivIndex(preset.clockDivParam)];
    tick_accum += ratio;
    while (tick_accum >= (float)TICKS_PER_STEP) {
        tick_accum -= (float)TICKS_PER_STEP;
        cur_step = (cur_step + 1) % NUM_STEPS;
        g_step_ms = System::GetNow(); // global activity-dot tick (divided rate)
        if (cur_step % 4 == 0)
            led2_flash_ms = System::GetNow();
        for (int c = 0; c < NUM_CH; c++) {
            if (kPatterns[preset.pattern][cur_step][c])
                TriggerGate(c);
        }
    }
}

// ============================================================
// MIDI CC handler
// ============================================================

static void HandleCC(uint8_t ctrl, uint8_t val) {
    switch (ctrl) {
    case 1:
        preset.delayParam = val;
        return;
    case 2:
        preset.delayFeedback = CcLin(val, 0.f, 0.95f);
        return;
    case 3:
        preset.delayWidth = CcLin(val, 0.f, 1.f);
        return;
    case 4:
        preset.dryLevel = CcLin(val, 0.f, 1.f);
        return;
    case 5:
        preset.clockDivParam = val;
        return;
    case 6:
        preset.delaySynced = (val >= 64);
        return;
    case 14:
        preset.pattern = (val * NUM_PATTERNS) / 128;
        return;
    case 15:
        seq_running = (val >= 64);
        return;
    case 18:
        bypass = (val >= 64);
        return;
    case 19:
        preset.bpm = 20.f + (val / 127.f) * 280.f;
        return;
    case 68: // per-channel mute bitmask
        for (int c = 0; c < NUM_CH; c++)
            ch_muted[c] = (val >> c) & 1;
        return;
    case 87: // save current preset to patch index
        SavePatch(val);
        return;
    case 119:
        SendAllState();
        return;
    default:
        break;
    }

    for (int c = 0; c < NUM_CH; c++) {
        int offset = (int)ctrl - kCcBase[c];
        if (offset < 0 || offset > 15)
            continue;
        switch (offset) {
        case 0:
            preset.ch[c].cutoff = CcLog(val, 100.f, 20000.f);
            break;
        case 1:
            preset.ch[c].resonance = CcLin(val, 0.f, 0.95f);
            break;
        case 2:
            preset.ch[c].drive = CcLin(val, 1.f, 4.f);
            break;
        case 3:
            preset.ch[c].lfoParam = val;
            break;
        case 4:
            preset.ch[c].attack = CcLog(val, 0.001f, 2.f);
            break;
        case 5:
            preset.ch[c].decay = CcLog(val, 0.01f, 2.f);
            break;
        case 6:
            preset.ch[c].level = CcLin(val, 0.f, 1.f);
            break;
        case 7:
            preset.ch[c].pan = CcLin(val, 0.f, 1.f);
            break;
        case 8: // lfo amount MSB
            ch[c].lfoAmtMsb = val;
            preset.ch[c].lfoAmount = ((uint16_t)val << 7) / 16383.5f * 2.f - 1.f;
            break;
        case 9:
            preset.ch[c].filterType = val < 32 ? 0 : val < 64 ? 1 : val < 96 ? 2 : 3;
            break;
        case 10:
            preset.ch[c].filterSlope = val < 43 ? 0 : val < 85 ? 1 : 2;
            break;
        case 12: { // lfo amount LSB
            uint16_t v14 = ((uint16_t)ch[c].lfoAmtMsb << 7) | val;
            preset.ch[c].lfoAmount = v14 / 16383.5f * 2.f - 1.f;
            break;
        }
        case 11: {
            preset.ch[c].lfoSynced = (val >= 64);
            uint8_t raw = val & 0x3F;
            preset.ch[c].lfoShape = raw / 21 > 2 ? 2 : raw / 21;
            break;
        }
        case 13:
            preset.ch[c].delayAmount = CcLin(val, 0.f, 1.f);
            break;
        case 14:
            preset.ch[c].lfoDuty = CcLin(val, 0.f, 1.f);
            break;
        case 15:
            preset.ch[c].ampLfoAmount = CcLin(val, -1.f, 1.f);
            break;
        }
    }
}

// ============================================================
// Drain events from either MIDI handler
// TRS clock is authoritative; USB clock is ignored while TRS is active
// ============================================================

template <typename Handler> static void ProcessMidi(Handler &midi, bool from_trs) {
    while (midi.HasEvents()) {
        MidiEvent msg = midi.PopEvent();
        bool allow_trans = from_trs || !trs_active;

        if (msg.type == SystemRealTime) {
            switch (msg.srt_type) {
            case TimingClock:
                if (from_trs) {
                    trs_active = true;
                    trs_last_ms = System::GetNow();
                } else {
                    usb_clock_active = true;
                    usb_last_ms = System::GetNow();
                }
                if (allow_trans) {
                    // BPM estimate from inter-tick interval (24 PPQN), EMA-smoothed.
                    uint32_t now_us = System::GetUs();
                    if (last_clock_us != 0) {
                        uint32_t dt = now_us - last_clock_us;
                        if (dt > 1000u && dt < 200000u) {
                            float inst = 60000000.f / ((float)dt * 24.f);
                            clock_bpm_ema = (clock_bpm_ema == 0.f)
                                                ? inst
                                                : (clock_bpm_ema * 0.8f + inst * 0.2f);
                            preset.bpm = fclamp(clock_bpm_ema, 20.f, 300.f);
                        }
                    }
                    last_clock_us = now_us;
                    if (seq_running)
                        AdvanceClock();
                }
                break;

            case Start:
                if (allow_trans) {
                    seq_running = true;
                    cur_step = 0;
                    tick_accum = 0.f;
                }
                break;

            case Stop:
                if (allow_trans)
                    seq_running = false;
                break;

            case Continue:
                if (allow_trans)
                    seq_running = true;
                break;

            default:
                break;
            }
        } else if (msg.type == ControlChange) {
            auto cc = msg.AsControlChange();
            HandleCC(cc.control_number, cc.value);
            ui_full_dirty = true; // reflect external edits on the screen
        } else if (msg.type == ProgramChange) {
            auto pc = msg.AsProgramChange();
            if (pc.program < NUM_PATCHES) {
                LoadPatch(pc.program);
                ui_patch_sel = pc.program;
                ui_full_dirty = true;
            }
        } else if (msg.type == NoteOn) {
            auto note = msg.AsNoteOn();
            if (note.velocity > 0 && msg.channel == 0) {
                for (int c = 0; c < NUM_CH; c++) {
                    if (note.note == kTrigNote[c]) {
                        TriggerGate(c);
                        break;
                    }
                }
            }
        }
    }
}

// ============================================================
// Menu UI — 240x320 ILI9341 TFT + 4 PEC11H encoders (CD74HC4067 mux)
//
// A port of menu-controller.html to the device. Layout: a top bar, three
// horizontal "bands", and a footer. The NAV encoder (below the screen) selects
// the context (CH1/CH2/CH3/GLOBAL) and the section within it; the three
// right-hand encoders (E1/E2/E3) each edit the parameter shown in their band.
//
// IMPORTANT: everything here runs from the main loop, never the audio callback.
// Every edit routes through HandleCC()/SendCC(), so the MIDI map stays the one
// source of truth and external controllers stay in sync.
//
// Pin map comes from the `menu` board (Seed GPIO: display on D7/D8/D9/D10/D17/
// D20, mux on D9/D15/D18/D19/D21). The firmware targets a bare DaisySeed (Daisy
// Studio carrier), so nothing else claims these pins. (It is NOT DaisyPod-
// compatible: the Pod uses D17-D21 for its LEDs/pots, which would fight the
// display and mux.)
// ============================================================

static Ili9341 tft;
static Mux4067 mux;

// --- Quadrature decode + button debounce (from menu/firmware encoder-test) ---
static const int8_t kQuadLut[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

struct Quad {
    uint8_t prev = 0;
    int8_t accum = 0;
    int Update(uint8_t a, uint8_t b) {
        uint8_t s = (uint8_t)((a << 1) | b);
        accum += kQuadLut[(prev << 2) | s];
        prev = s;
        if (accum >= 4) {
            accum -= 4;
            return +1;
        }
        if (accum <= -4) {
            accum += 4;
            return -1;
        }
        return 0;
    }
};

struct EncBtn {
    static constexpr uint8_t kStable = 5; // ~5 ms at a 1 kHz scan
    bool state = false;
    uint8_t cnt = 0;
    bool Update(bool raw) { // returns true on a press edge
        if (raw == state) {
            cnt = 0;
            return false;
        }
        if (++cnt >= kStable) {
            state = raw;
            cnt = 0;
            return state;
        }
        return false;
    }
};

static Quad q_enc[4];
static EncBtn b_enc[4];

// Physical mux-encoder index per role. Mux channels: A=3i, B=3i+1, SW=3i+2.
// The three value encoders are the right-hand column (mux 0/1/2, top to bottom);
// the NAV/menu encoder sits below the screen and is the last group (mux 3).
enum { ENC_E1 = 0, ENC_E2 = 1, ENC_E3 = 2, ENC_NAV = 3 };

// --- Parameter model (mirrors the ROWS table in menu-controller.html) ---
// B_COL_* / B_BRIGHT are settings-mode bands; for those `cc` holds the colour
// index (0..3 = CH1/CH2/CH3/GLOBAL) rather than a MIDI CC.
enum BandKind : uint8_t {
    B_CONT,
    B_ENUM,
    B_TOGGLE,
    B_PATIDX,
    B_PATCH,
    B_ACTION,
    B_COL_R,
    B_COL_G,
    B_COL_B,
    B_BRIGHT,
};
enum Fmt : uint8_t {
    F_NONE,
    F_CUTOFF,
    F_RES,
    F_DRIVE,
    F_LEVEL,
    F_ATTACK,
    F_DECAY,
    F_PAN,
    F_LFORATE,
    F_PCT,
    F_BIPCT,
    F_DELAYTIME,
    F_CLOCKDIV,
    F_BPM
};
enum Push : uint8_t {
    P_NONE,
    P_MUTE,
    P_LFOSYNC,
    P_DELAYSYNC,
    P_RUN,
    P_TAP,
    P_BYPASS,
    P_LOAD,
    P_SAVE,
    P_SYNC
};

struct Band {
    const char *label;
    uint8_t kind;
    uint8_t cc;     // absolute CC, or a per-channel offset when chRel != 0
    uint8_t chRel;  // 1 = cc is an offset added to kCcBase[ctx]
    uint8_t fmt;    // value formatter (B_CONT)
    uint8_t bip;    // bipolar bar (centred)
    uint8_t push;   // encoder-press action
    uint8_t lsbOff; // 14-bit LSB offset (255 = none)
    uint8_t muteCh; // channel index for P_MUTE bands
    const char *opts[4];
    uint8_t optVal[4];
    uint8_t nOpt;
};
struct Section {
    const char *name;
    const Band *bands;
    uint8_t n;
};

// label,kind,cc,chRel,fmt,bip,push,lsbOff,muteCh,opts,optVal,nOpt
static const Band kFilter[] = {
    {"TYPE",
     B_ENUM,
     9,
     1,
     F_NONE,
     0,
     P_NONE,
     255,
     0,
     {"LP", "BP", "HP", "NOTCH"},
     {0, 32, 64, 96},
     4},
    {"SLOPE",
     B_ENUM,
     10,
     1,
     F_NONE,
     0,
     P_NONE,
     255,
     0,
     {"6DB", "12DB", "24DB", 0},
     {0, 63, 126, 0},
     3},
    {"FREQ", B_CONT, 0, 1, F_CUTOFF, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kTone[] = {
    {"Q", B_CONT, 1, 1, F_RES, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"DRIVE", B_CONT, 2, 1, F_DRIVE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"LFO>FILT", B_CONT, 8, 1, F_BIPCT, 1, P_NONE, 12, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kEnv[] = {
    {"ATTACK", B_CONT, 4, 1, F_ATTACK, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"DECAY", B_CONT, 5, 1, F_DECAY, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"LEVEL/MUTE", B_CONT, 6, 1, F_LEVEL, 0, P_MUTE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kLfo[] = {
    {"SHAPE",
     B_ENUM,
     11,
     1,
     F_NONE,
     0,
     P_LFOSYNC,
     255,
     0,
     {"TRI", "SIN", "SQ", 0},
     {0, 21, 42, 0},
     3},
    {"RATE", B_CONT, 3, 1, F_LFORATE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"DUTY", B_CONT, 14, 1, F_PCT, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kOut[] = {
    {"PAN", B_CONT, 7, 1, F_PAN, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"AMP MOD", B_CONT, 15, 1, F_BIPCT, 1, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"DLY SEND", B_CONT, 13, 1, F_LEVEL, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Section kChSections[] = {
    {"FILTER", kFilter, 3}, {"TONE", kTone, 3}, {"ENV", kEnv, 3},
    {"LFO", kLfo, 3},       {"OUT", kOut, 3},
};

static const Band kSeq[] = {
    {"PATTERN", B_PATIDX, 14, 0, F_NONE, 0, P_RUN, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"CLK DIV", B_CONT, 5, 0, F_CLOCKDIV, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"BPM", B_CONT, 19, 0, F_BPM, 0, P_TAP, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kDelay[] = {
    {"TIME", B_CONT, 1, 0, F_DELAYTIME, 0, P_DELAYSYNC, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"FDBK", B_CONT, 2, 0, F_LEVEL, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"WIDTH", B_CONT, 3, 0, F_LEVEL, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kMix[] = {
    {"CH1 LVL", B_CONT, 26, 0, F_LEVEL, 0, P_MUTE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"CH2 LVL", B_CONT, 42, 0, F_LEVEL, 0, P_MUTE, 255, 1, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"CH3 LVL", B_CONT, 58, 0, F_LEVEL, 0, P_MUTE, 255, 2, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kMaster[] = {
    {"DRY", B_CONT, 4, 0, F_LEVEL, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"BYPASS", B_TOGGLE, 18, 0, F_NONE, 0, P_BYPASS, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"RUN", B_TOGGLE, 15, 0, F_NONE, 0, P_RUN, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kPatch[] = {
    {"PATCH", B_PATCH, 0, 0, F_NONE, 0, P_LOAD, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"SAVE", B_ACTION, 0, 0, F_NONE, 0, P_SAVE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"STATE", B_ACTION, 0, 0, F_NONE, 0, P_SYNC, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Section kGlSections[] = {
    {"SEQ", kSeq, 3},       {"DELAY", kDelay, 3}, {"MIX", kMix, 3},
    {"MASTER", kMaster, 3}, {"PATCH", kPatch, 3},
};

// Hidden settings mode. Colour pages carry the colour index in `cc` (0..3).
static const Band kColCh1[] = {
    {"RED", B_COL_R, 0, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"GREEN", B_COL_G, 0, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"BLUE", B_COL_B, 0, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kColCh2[] = {
    {"RED", B_COL_R, 1, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"GREEN", B_COL_G, 1, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"BLUE", B_COL_B, 1, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kColCh3[] = {
    {"RED", B_COL_R, 2, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"GREEN", B_COL_G, 2, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"BLUE", B_COL_B, 2, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kColGbl[] = {
    {"RED", B_COL_R, 3, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"GREEN", B_COL_G, 3, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"BLUE", B_COL_B, 3, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kDisplay[] = {
    {"BRIGHT", B_BRIGHT, 0, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Section kSetSections[] = {
    {"CH1 COL", kColCh1, 3}, {"CH2 COL", kColCh2, 3},  {"CH3 COL", kColCh3, 3},
    {"GBL COL", kColGbl, 3}, {"DISPLAY", kDisplay, 1},
};

static constexpr uint8_t kNumSec = 5; // both normal and settings modes have 5

static const char *const kDivName[8] = {"1/2", "1/2T", "1/4",  "1/4T",
                                        "1/8", "1/8T", "1/16", "1/16T"};
static const char *const kClockName[9] = {"/4", "/3", "/2", "/1.5", "X1", "X1.5", "X2", "X3", "X4"};

// --- colours (RGB565) ---
static constexpr uint16_t kAccent = 0x4C7F; // fixed accent (sync badges etc.)
static constexpr uint16_t kPanel = 0x1082;
static constexpr uint16_t kPanel2 = 0x0841;
static constexpr uint16_t kHdrBg = 0x2104;
static constexpr uint16_t kFootBg = 0x0841;
static constexpr uint16_t kDimCol = 0x52AA;
static constexpr uint16_t kTxt = 0xFFFF;

// Editable channel/global colours (CH1=green, CH2=yellow, CH3=purple,
// GLOBAL=blue) + backlight level. Persisted in QSPI (see uiStore).
static const uint16_t kDefaultColor[4] = {0x07E0, 0xFFE0, 0x801F, 0x4C7F};
static uint16_t ui_color[4] = {0x07E0, 0xFFE0, 0x801F, 0x4C7F};
static uint8_t ui_brightness = 255;

static const Section *ui_sectionTbl() {
    return ui_settings ? kSetSections : (ui_ctx < 3 ? kChSections : kGlSections);
}
static const Section &ui_cur() { return ui_sectionTbl()[ui_sec]; }
static uint16_t ui_col() {
    if (ui_settings) {
        const Band &b0 = ui_cur().bands[0];
        if (b0.kind == B_COL_R) // colour page: preview the colour being edited
            return ui_color[b0.cc];
        return kAccent;
    }
    return ui_color[ui_ctx < 3 ? ui_ctx : 3];
}
static uint8_t ui_absCC(const Band &b) {
    return b.chRel ? (uint8_t)(kCcBase[ui_ctx] + b.cc) : b.cc;
}

static uint8_t ui_enumIdx(const Band &b) {
    int raw = CcGet(ui_absCC(b));
    if (b.push == P_LFOSYNC)
        raw &= 0x3F; // mask the sync bit out of the shape byte
    uint8_t best = 0;
    int bd = 1000;
    for (uint8_t i = 0; i < b.nOpt; i++) {
        int d = raw - (int)b.optVal[i];
        if (d < 0)
            d = -d;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

// --- tiny string builders. Hand-rolled to avoid linking newlib's printf,
//     which alone overflows the 128 KB internal flash. Glyphs are limited to
//     what the 5x7 font provides (uppercase, digits, . - : / % + >). ---
static char *ui_puts(char *p, const char *s) {
    while (*s)
        *p++ = *s++;
    return p;
}
static char *ui_putl(char *p, long v) {
    if (v < 0) {
        *p++ = '-';
        v = -v;
    }
    char tmp[12];
    int n = 0;
    if (v == 0)
        tmp[n++] = '0';
    while (v) {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (n)
        *p++ = tmp[--n];
    return p;
}
// `v` rounded to `dec` decimal places.
static char *ui_putf(char *p, float v, int dec) {
    if (v < 0.f) {
        *p++ = '-';
        v = -v;
    }
    long scale = 1;
    for (int i = 0; i < dec; i++)
        scale *= 10;
    long n = (long)(v * scale + 0.5f);
    p = ui_putl(p, n / scale);
    if (dec > 0) {
        *p++ = '.';
        for (long div = scale / 10; div > 0; div /= 10)
            *p++ = (char)('0' + (n / div) % 10);
    }
    return p;
}

static void ui_text(const Band &b, char *out) {
    char *p = out;
    if (b.kind == B_ENUM) {
        const char *s = b.opts[ui_enumIdx(b)];
        p = ui_puts(p, s ? s : "");
        *p = 0;
        return;
    }
    if (b.kind == B_TOGGLE) {
        p = ui_puts(p, CcGet(b.cc) >= 64 ? "ON" : "OFF");
        *p = 0;
        return;
    }
    if (b.kind == B_PATIDX) {
        p = ui_puts(p, "PAT ");
        p = ui_putl(p, preset.pattern + 1);
        *p++ = '/';
        p = ui_putl(p, NUM_PATTERNS);
        *p = 0;
        return;
    }
    if (b.kind == B_PATCH) {
        *p++ = 'P';
        if (ui_patch_sel + 1 < 10)
            *p++ = '0';
        p = ui_putl(p, ui_patch_sel + 1);
        *p = 0;
        return;
    }
    if (b.kind == B_ACTION) {
        *p++ = '-';
        *p = 0;
        return;
    }
    uint8_t raw = CcGet(ui_absCC(b));
    switch (b.fmt) {
    case F_CUTOFF: {
        float v = CcLog(raw, 100.f, 20000.f);
        if (v >= 1000.f) {
            p = ui_putf(p, v / 1000.f, 1);
            p = ui_puts(p, "KHZ");
        } else {
            p = ui_putf(p, v, 0);
            p = ui_puts(p, "HZ");
        }
        break;
    }
    case F_RES:
    case F_LEVEL:
        p = ui_putf(p, raw / 127.f, 2);
        break;
    case F_DRIVE:
        *p++ = 'X';
        p = ui_putf(p, CcLin(raw, 1.f, 4.f), 2);
        break;
    case F_ATTACK: {
        float v = CcLog(raw, 0.001f, 2.f);
        if (v < 1.f) {
            p = ui_putf(p, v * 1000.f, 0);
            p = ui_puts(p, "MS");
        } else {
            p = ui_putf(p, v, 2);
            *p++ = 'S';
        }
        break;
    }
    case F_DECAY: {
        float v = CcLog(raw, 0.01f, 2.f);
        if (v < 1.f) {
            p = ui_putf(p, v * 1000.f, 0);
            p = ui_puts(p, "MS");
        } else {
            p = ui_putf(p, v, 2);
            *p++ = 'S';
        }
        break;
    }
    case F_PAN: {
        int d = (int)raw - 64;
        if (d > -4 && d < 4)
            p = ui_puts(p, "CTR");
        else {
            float a = raw > 63.5f ? raw - 63.5f : 63.5f - raw;
            *p++ = d < 0 ? 'L' : 'R';
            p = ui_putl(p, (long)(a / 63.5f * 100.f + 0.5f));
        }
        break;
    }
    case F_LFORATE: {
        if (preset.ch[ui_ctx].lfoSynced) {
            int i = raw >> 4;
            p = ui_puts(p, kDivName[i > 7 ? 7 : i]);
        } else {
            p = ui_putf(p, CcLog(raw, 0.1f, 20.f), 1);
            p = ui_puts(p, "HZ");
        }
        break;
    }
    case F_PCT:
        p = ui_putl(p, (long)(raw / 127.f * 100.f + 0.5f));
        *p++ = '%';
        break;
    case F_BIPCT: {
        int q = (int)((raw - 64) / 63.f * 100.f + (raw >= 64 ? 0.5f : -0.5f));
        if (q > 100)
            q = 100;
        if (q < -100)
            q = -100;
        if (q >= 0)
            *p++ = '+';
        p = ui_putl(p, q);
        *p++ = '%';
        break;
    }
    case F_DELAYTIME: {
        if (preset.delaySynced) {
            int i = raw >> 4;
            p = ui_puts(p, kDivName[i > 7 ? 7 : i]);
        } else {
            float v = CcLog(raw, 0.01f, 2.f);
            if (v < 1.f) {
                p = ui_putf(p, v * 1000.f, 0);
                p = ui_puts(p, "MS");
            } else {
                p = ui_putf(p, v, 2);
                *p++ = 'S';
            }
        }
        break;
    }
    case F_CLOCKDIV: {
        int i = (raw * 9) >> 7;
        p = ui_puts(p, kClockName[i > 8 ? 8 : i]);
        break;
    }
    case F_BPM:
        p = ui_putl(p, (long)(20.f + raw / 127.f * 280.f + 0.5f));
        p = ui_puts(p, "BPM");
        break;
    default:
        p = ui_putl(p, raw);
        break;
    }
    *p = 0;
}

// Shift + click on a mute band solos its channel (toggle).
static void ui_bandSolo(const Band &b) {
    int sc = b.chRel ? ui_ctx : b.muteCh;
    ch_solo[sc] = !ch_solo[sc];
}

// --- edits: all route through HandleCC()+SendCC() ---
static void ui_bandPush(const Band &b) {
    switch (b.push) {
    case P_MUTE: {
        int mc = b.chRel ? ui_ctx : b.muteCh; // per-channel band mutes the current channel
        ch_muted[mc] = !ch_muted[mc];
        SendCC(68, (ch_muted[0] ? 1 : 0) | (ch_muted[1] ? 2 : 0) | (ch_muted[2] ? 4 : 0));
        break;
    }
    case P_LFOSYNC: {
        preset.ch[ui_ctx].lfoSynced = !preset.ch[ui_ctx].lfoSynced;
        uint8_t v = preset.ch[ui_ctx].lfoShape * 21 + (preset.ch[ui_ctx].lfoSynced ? 64 : 0);
        HandleCC(kCcBase[ui_ctx] + 11, v);
        SendCC(kCcBase[ui_ctx] + 11, v);
        break;
    }
    case P_DELAYSYNC:
        preset.delaySynced = !preset.delaySynced;
        SendCC(6, preset.delaySynced ? 127 : 0);
        break;
    case P_RUN:
        seq_running = !seq_running;
        if (!seq_running) {
            cur_step = 0;
            tick_accum = 0.f;
        }
        SendCC(15, seq_running ? 127 : 0);
        break;
    case P_TAP: {
        uint32_t now = System::GetNow();
        uint32_t gap = now - tap_last_ms;
        if (gap > 0 && gap < 2000)
            preset.bpm = fclamp(60000.f / (float)gap, 20.f, 300.f);
        tap_last_ms = now;
        SendCC(19, (uint8_t)((fclamp(preset.bpm, 20.f, 300.f) - 20.f) / 280.f * 127.f));
        break;
    }
    case P_BYPASS:
        bypass = !bypass;
        SendCC(18, bypass ? 127 : 0);
        break;
    case P_LOAD:
        LoadPatch(ui_patch_sel);
        break;
    case P_SAVE:
        SavePatch(ui_patch_sel);
        break;
    case P_SYNC:
        SendAllState();
        break;
    default:
        break;
    }
}

static void ui_bandTurn(const Band &b, int dir) {
    uint8_t acc = ui_absCC(b);
    switch (b.kind) {
    case B_ENUM: {
        uint8_t i = (uint8_t)((ui_enumIdx(b) + (dir > 0 ? 1 : b.nOpt - 1)) % b.nOpt);
        uint8_t v = b.optVal[i];
        if (b.push == P_LFOSYNC)
            v += (preset.ch[ui_ctx].lfoSynced ? 64 : 0);
        HandleCC(acc, v);
        SendCC(acc, v);
        break;
    }
    case B_TOGGLE:
        ui_bandPush(b);
        break;
    case B_PATIDX: {
        int i = (preset.pattern + dir + NUM_PATTERNS) % NUM_PATTERNS;
        uint8_t v = (uint8_t)(i * 8);
        HandleCC(14, v);
        SendCC(14, v);
        break;
    }
    case B_PATCH: {
        int p = ui_patch_sel + dir;
        if (p < 0)
            p = 0;
        if (p >= NUM_PATCHES)
            p = NUM_PATCHES - 1;
        ui_patch_sel = (uint8_t)p;
        break;
    }
    case B_ACTION:
        break;
    case B_COL_R: {
        uint16_t &c = ui_color[b.cc];
        int r = (c >> 11) & 0x1F;
        r += dir;
        r = r < 0 ? 0 : r > 31 ? 31 : r;
        c = (uint16_t)((c & ~0xF800) | (r << 11));
        ui_full_dirty = true;
        break;
    }
    case B_COL_G: {
        uint16_t &c = ui_color[b.cc];
        int g = (c >> 5) & 0x3F;
        g += dir;
        g = g < 0 ? 0 : g > 63 ? 63 : g;
        c = (uint16_t)((c & ~0x07E0) | (g << 5));
        ui_full_dirty = true;
        break;
    }
    case B_COL_B: {
        uint16_t &c = ui_color[b.cc];
        int bl = c & 0x1F;
        bl += dir;
        bl = bl < 0 ? 0 : bl > 31 ? 31 : bl;
        c = (uint16_t)((c & ~0x001F) | bl);
        ui_full_dirty = true;
        break;
    }
    case B_BRIGHT: {
        int v = (int)ui_brightness + dir * 8;
        v = v < 8 ? 8 : v > 255 ? 255 : v; // keep some backlight on
        ui_brightness = (uint8_t)v;
        tft.SetBrightness(ui_brightness / 255.f);
        break;
    }
    default: {
        int v = (int)CcGet(acc) + dir * 2;
        if (v < 0)
            v = 0;
        if (v > 127)
            v = 127;
        HandleCC(acc, (uint8_t)v);
        SendCC(acc, (uint8_t)v);
        if (b.lsbOff != 255) { // 14-bit pair: keep the LSB at 0
            uint8_t lcc = (uint8_t)(kCcBase[ui_ctx] + b.lsbOff);
            HandleCC(lcc, 0);
            SendCC(lcc, 0);
        }
        break;
    }
    }
}

static void ui_navTurn(int dir) {
    ui_sec = (uint8_t)((ui_sec + dir + kNumSec) % kNumSec);
    ui_full_dirty = true;
}
static void ui_navCtx(int dir) {
    ui_ctx = (uint8_t)((ui_ctx + dir + 4) % 4);
    if (ui_sec >= kNumSec)
        ui_sec = kNumSec - 1;
    ui_full_dirty = true;
}
// Commit colours + brightness to QSPI (no-op if unchanged). Called on exit so we
// write at most once per settings session, not on every encoder tick.
static void ui_settingsSave() {
    UiSettings &s = uiStore.GetSettings();
    s.version = UI_VERSION;
    for (int i = 0; i < 4; i++)
        s.color[i] = ui_color[i];
    s.brightness = ui_brightness;
    uiStore.Save();
}
static void ui_settingsToggle() {
    if (ui_settings)
        ui_settingsSave(); // leaving settings → persist
    ui_settings = !ui_settings;
    if (ui_settings)
        ui_vu = false; // modes are exclusive
    ui_sec = 0;
    ui_full_dirty = true;
}
static void ui_vuToggle() {
    ui_vu = !ui_vu;
    if (ui_vu)
        ui_settings = false; // modes are exclusive
    ui_full_dirty = true;
}

// --- rendering ---
static int ui_strw(const char *s, uint8_t size) {
    int n = 0;
    while (s[n])
        n++;
    return n * 6 * size;
}

// --- activity dots (centre top). Dots 0-2 track each channel's AD envelope
//     (trigger flashes, fades with the gate); dot 3 is GLOBAL and ticks on each
//     divided sequencer step. A box around a dot marks the selected context. ---
static const int kDotCx[4] = {106, 120, 134, 148};
static constexpr int kDotN = 4;
static constexpr int kDotY = 8;
static constexpr int kDotSz = 9;
static constexpr int kDotRowY = 5; // flush region (covers the selection box)
static constexpr int kDotRowH = 16;

static uint16_t ui_scale(uint16_t c, float f) {
    if (f < 0.f)
        f = 0.f;
    if (f > 1.f)
        f = 1.f;
    int r = (int)(((c >> 11) & 0x1F) * f);
    int g = (int)(((c >> 5) & 0x3F) * f);
    int b = (int)((c & 0x1F) * f);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// Which dot the selection box belongs to (0..3, or -1 for none).
static int ui_selDot() {
    if (ui_settings)
        return ui_sec < 4 ? ui_sec : -1; // colour pages map onto the dots
    return ui_ctx;                       // 0..2 channels, 3 global
}

static void ui_drawDots() {
    uint32_t now = System::GetNow();
    int sel = ui_selDot();
    for (int c = 0; c < kDotN; c++) {
        float e;
        uint16_t col;
        bool muted = false;
        if (c < 3) {
            muted = ch_silenced(c); // muted, or silenced by another channel's solo
            e = muted ? 0.f : ch[c].env.GetValue();
            col = ui_color[c];
        } else {
            uint32_t dt = now - g_step_ms; // global step tick (~120 ms flash)
            e = (seq_running && dt < 120) ? 1.f - dt / 120.f : 0.f;
            col = ui_color[3];
        }
        int bx = kDotCx[c] - 6;
        tft.FillRect(bx, kDotY - 2, 13, 13, kHdrBg); // clear dot + box area
        int x = kDotCx[c] - kDotSz / 2;
        tft.DrawRect(x, kDotY, kDotSz, kDotSz, kPanel2); // idle ring
        tft.FillRect(x + 1, kDotY + 1, kDotSz - 2, kDotSz - 2, ui_scale(col, muted ? 0.2f : e));
        if (muted) { // cross it out
            for (int i = 0; i < kDotSz; i++) {
                tft.FillRect(x + i, kDotY + i, 1, 1, Ili9341::kRed);
                tft.FillRect(x + (kDotSz - 1 - i), kDotY + i, 1, 1, Ili9341::kRed);
            }
        }
        if (c == sel)
            tft.DrawRect(bx, kDotY - 2, 13, 13, col); // selected-context box
    }
}

static void ui_drawHeader() {
    tft.FillRect(0, 0, 240, 28, kHdrBg);
    uint16_t col = ui_col();
    tft.FillRect(4, 5, 46, 18, col);
    const char *cn = ui_settings ? "SET" : (ui_ctx < 3 ? CH_NAME[ui_ctx] : "GLBL");
    tft.DrawString(4 + (46 - ui_strw(cn, 1)) / 2, 8, cn, Ili9341::kBlack, col, 1);
    tft.DrawString(54, 10, ui_cur().name, kTxt, kHdrBg, 1); // size 1, left of the dots
    int dx = 240 - 6 - kNumSec * 8;
    for (int i = 0; i < kNumSec; i++)
        tft.FillRect(dx + i * 8, 12, 5, 5, i == ui_sec ? col : kDimCol);
    ui_drawDots();
}

// One labelled CPU meter: a letter + a bar that goes green -> yellow -> red as
// the load climbs. `load` is 0..1.
static void ui_drawMeter(int x, int y, char label, float load) {
    if (load < 0.f)
        load = 0.f;
    if (load > 1.f)
        load = 1.f;
    char l[2] = {label, 0};
    tft.DrawString(x, y, l, kDimCol, kFootBg, 1);
    const int bx = x + 8, bw = 64;
    tft.FillRect(bx, y, bw, 7, kPanel2);
    uint16_t c = load < 0.7f ? 0x07E0 : load < 0.9f ? 0xFFE0 : 0xF800;
    int w = (int)(load * bw + 0.5f);
    if (w > 0)
        tft.FillRect(bx, y, w, 7, c);
}

static void ui_drawFooter() {
    tft.FillRect(0, 300, 240, 20, kFootBg);
    // bottom-left: audio (A) and control (C) CPU meters
    ui_drawMeter(4, 302, 'A', ui_dsp_load);
    ui_drawMeter(4, 311, 'C', ui_ctrl_load);
    // bottom-right: BPM
    char t[16];
    char *q = ui_putl(t, (long)(20.f + CcGet(19) / 127.f * 280.f + 0.5f));
    q = ui_puts(q, "BPM");
    *q = 0;
    tft.DrawString(236 - ui_strw(t, 1), 306, t, kDimCol, kFootBg, 1);
}

static void ui_drawBand(int i) {
    const Section &s = ui_cur();
    int y = 30 + i * 90; // 30, 120, 210
    int h = 86;
    uint16_t col = ui_col();
    tft.FillRect(2, y, 236, h, kPanel);
    if (i >= s.n)
        return;
    const Band &b = s.bands[i];
    tft.FillRect(2, y, 3, h, col); // accent stripe
    tft.DrawString(8, y + 5, b.label, col, kPanel, 1);

    // status badge (solo / mute / lfo sync / delay sync)
    const char *badge = nullptr;
    uint16_t bcol = Ili9341::kRed;
    if (b.push == P_MUTE && ch_solo[b.chRel ? ui_ctx : b.muteCh]) {
        badge = "SOLO";
        bcol = Ili9341::kYellow;
    } else if (b.push == P_MUTE && ch_muted[b.chRel ? ui_ctx : b.muteCh]) {
        badge = "MUTE";
        bcol = Ili9341::kRed;
    } else if (b.push == P_LFOSYNC) {
        badge = preset.ch[ui_ctx].lfoSynced ? "SYNC" : "FREE";
        bcol = preset.ch[ui_ctx].lfoSynced ? kAccent : kDimCol;
    } else if (b.push == P_DELAYSYNC) {
        badge = preset.delaySynced ? "SYNC" : "FREE";
        bcol = preset.delaySynced ? kAccent : kDimCol;
    }
    if (badge) {
        int bw = ui_strw(badge, 1) + 6;
        tft.FillRect(120, y + 4, bw, 11, bcol);
        tft.DrawString(123, y + 5, badge, Ili9341::kBlack, bcol, 1);
    }

    if (b.kind == B_COL_R || b.kind == B_COL_G || b.kind == B_COL_B) {
        uint16_t c = ui_color[b.cc];
        int comp, maxv;
        uint16_t barcol;
        if (b.kind == B_COL_R) {
            comp = (c >> 11) & 0x1F;
            maxv = 31;
            barcol = 0xF800;
        } else if (b.kind == B_COL_G) {
            comp = (c >> 5) & 0x3F;
            maxv = 63;
            barcol = 0x07E0;
        } else {
            comp = c & 0x1F;
            maxv = 31;
            barcol = 0x001F;
        }
        char v[8];
        char *p = ui_putl(v, comp);
        *p = 0;
        tft.DrawString(232 - ui_strw(v, 2), y + 22, v, kTxt, kPanel, 2);
        tft.FillRect(8, y + 62, 222, 12, kPanel2);
        int w = comp * 222 / maxv;
        tft.FillRect(8, y + 62, w < 2 ? 2 : w, 12, barcol);
        return;
    }
    if (b.kind == B_BRIGHT) {
        int pct = ui_brightness * 100 / 255;
        char v[8];
        char *p = ui_putl(v, pct);
        *p++ = '%';
        *p = 0;
        tft.DrawString(232 - ui_strw(v, 2), y + 22, v, kTxt, kPanel, 2);
        tft.FillRect(8, y + 62, 222, 12, kPanel2);
        int w = ui_brightness * 222 / 255;
        tft.FillRect(8, y + 62, w < 2 ? 2 : w, 12, col);
        return;
    }

    if (b.kind == B_ENUM) {
        uint8_t cur = ui_enumIdx(b);
        int n = b.nOpt, gap = 4, cw = (222 - (n - 1) * gap) / n;
        for (int k = 0; k < n; k++) {
            int cx = 8 + k * (cw + gap);
            uint16_t f = (k == cur) ? col : kPanel2;
            tft.FillRect(cx, y + 40, cw, 22, f);
            int tw = ui_strw(b.opts[k], 1);
            tft.DrawString(cx + (cw - tw) / 2, y + 47, b.opts[k],
                           k == cur ? Ili9341::kBlack : kDimCol, f, 1);
        }
        return;
    }
    if (b.kind == B_TOGGLE) {
        bool on = CcGet(b.cc) >= 64;
        uint16_t f = on ? col : kPanel2;
        tft.FillRect(8, y + 40, 222, 24, f);
        const char *tx = on ? "ON" : "OFF";
        tft.DrawString(8 + (222 - ui_strw(tx, 2)) / 2, y + 45, tx, on ? Ili9341::kBlack : kDimCol,
                       f, 2);
        return;
    }

    char val[20];
    ui_text(b, val);
    tft.DrawString(232 - ui_strw(val, 2), y + 22, val, kTxt, kPanel, 2);
    if (b.kind == B_CONT) {
        int raw = CcGet(ui_absCC(b));
        float frac = raw / 127.f;
        tft.FillRect(8, y + 62, 222, 12, kPanel2);
        if (b.bip) {
            int cx = 8 + 111;
            int w = (int)((frac > 0.5f ? frac - 0.5f : 0.5f - frac) * 222.f);
            if (w < 2)
                w = 2;
            tft.FillRect(frac >= 0.5f ? cx : cx - w, y + 62, w, 12, col);
            tft.FillRect(cx, y + 62, 1, 12, Ili9341::kBlack);
        } else {
            int w = (int)(frac * 222.f);
            if (w < 2)
                w = 2;
            tft.FillRect(8, y + 62, w, 12, col);
        }
    } else {
        const char *hint =
            b.kind == B_PATCH
                ? "PUSH:LOAD"
                : b.push == P_SAVE ? "PUSH:SAVE" : b.push == P_SYNC ? "PUSH:SYNC" : "";
        if (hint[0])
            tft.DrawString(8, y + 62, hint, kDimCol, kPanel, 1);
    }
}

// Off-screen RGB565 framebuffer in SDRAM (150 KB). Drawing writes here; pixels
// reach the panel via background DMA. 32-byte aligned for cache-clean ops.
static uint16_t menu_fb[Ili9341::kWidth * Ili9341::kHeight] __attribute__((aligned(32)))
DSY_SDRAM_BSS;

static void MenuInit() {
    tft.Init();                  // SPI1 first ...
    mux.Init();                  // ... then mux, so D9 ends configured as the mux SIG input
    tft.SetFramebuffer(menu_fb); // all drawing now targets the framebuffer
    tft.SetBrightness(ui_brightness / 255.f);
    ui_full_dirty = true;
}

static uint32_t ui_next_scan_ms = 0;
static uint32_t ui_last_render_ms = 0;

// Read the encoders (~1 kHz) and dispatch turns/presses. Main loop only.
static void MenuPoll(uint32_t now) {
    if (now < ui_next_scan_ms)
        return;
    ui_next_scan_ms = now + 1;

    // NAV: turn = section; hold + turn = cycle context; click (press+release with
    // no turn) = next context. Tracks whether a turn happened during the hold so
    // the release doesn't also register as a click.
    static bool nav_turned_while_held = false;

    uint16_t bits = mux.Scan(12);
    for (int i = 0; i < 4; i++) {
        uint8_t a = (bits >> (3 * i + 0)) & 1;
        uint8_t bbit = (bits >> (3 * i + 1)) & 1;
        bool sw = (bits >> (3 * i + 2)) & 1;
        int d = q_enc[i].Update(a, bbit);
        bool prevPressed = b_enc[i].state;
        b_enc[i].Update(sw);
        bool held = b_enc[i].state;
        bool pressEdge = !prevPressed && held;
        bool releaseEdge = prevPressed && !held;

        if (i == ENC_NAV) {
            if (pressEdge)
                nav_turned_while_held = false;
            if (d != 0 && !ui_vu) {
                if (ui_settings) {
                    ui_navTurn(d); // settings: turn = page
                } else if (held) {
                    ui_navCtx(d); // shifted: cycle CH1/CH2/CH3/GLOBAL
                    nav_turned_while_held = true;
                } else {
                    ui_navTurn(d); // section
                }
            }
            if (releaseEdge && !nav_turned_while_held) {
                if (ui_vu)
                    ui_vuToggle(); // click exits VU
                else if (ui_settings)
                    ui_settingsToggle(); // click exits settings
                else
                    ui_navCtx(1); // plain click: next context (channel)
            }
            continue;
        }

        int idx = (i == ENC_E1) ? 0 : (i == ENC_E2) ? 1 : 2;

        // Shift (NAV held) + click. E1/E2 are the hidden mode toggles (settings /
        // VU); on any other encoder, shift-click on a mute band solos it instead.
        if (pressEdge && b_enc[ENC_NAV].state) {
            if (i == ENC_E1)
                ui_settingsToggle();
            else if (i == ENC_E2)
                ui_vuToggle();
            else if (idx >= 0 && idx < ui_cur().n && ui_cur().bands[idx].push == P_MUTE) {
                ui_bandSolo(ui_cur().bands[idx]);
                ui_full_dirty = true;
            }
            nav_turned_while_held = true; // suppress NAV's own click on release
            continue;
        }

        if (ui_vu)
            continue; // VU page ignores the value encoders

        if (d != 0 && idx < ui_cur().n) {
            ui_bandTurn(ui_cur().bands[idx], d);
            ui_band_dirty[idx] = true;
        }
        if (pressEdge && idx < ui_cur().n) {
            ui_bandPush(ui_cur().bands[idx]);
            ui_full_dirty = true;
        }
    }
}

// --- I/O VU page (hidden: hold NAV + click E2) ---
static const int kVuBarX = 56;
static const int kVuBarW = 176;
static const int kVuBarH = 26;
static const int kVuY[4] = {54, 92, 168, 206};
static const char *const kVuLab[4] = {"IN L", "IN R", "OUT L", "OUT R"};

static void ui_drawVuStatic() {
    tft.FillScreen(Ili9341::kBlack);
    tft.FillRect(0, 0, 240, 28, kHdrBg);
    tft.FillRect(4, 5, 46, 18, kAccent);
    tft.DrawString(4 + (46 - ui_strw("VU", 1)) / 2, 8, "VU", Ili9341::kBlack, kAccent, 1);
    tft.DrawString(56, 10, "I/O METER", kTxt, kHdrBg, 1);
    tft.DrawString(8, 36, "INPUT", kDimCol, Ili9341::kBlack, 1);
    tft.DrawString(8, 150, "OUTPUT", kDimCol, Ili9341::kBlack, 1);
    for (int i = 0; i < 4; i++) {
        tft.DrawString(8, kVuY[i] + kVuBarH / 2 - 3, kVuLab[i], kTxt, Ili9341::kBlack, 1);
        tft.DrawRect(kVuBarX - 1, kVuY[i] - 1, kVuBarW + 2, kVuBarH + 2, kPanel2);
    }
    tft.FillRect(0, 300, 240, 20, kFootBg);
    tft.DrawString(4, 306, "NAV CLICK = EXIT", kDimCol, kFootBg, 1);
}

static void ui_drawVuBars() {
    const float lv[4] = {vu_in_l, vu_in_r, vu_out_l, vu_out_r};
    for (int i = 0; i < 4; i++) {
        tft.FillRect(kVuBarX, kVuY[i], kVuBarW, kVuBarH, kPanel2);
        float level = lv[i];
        float db = level > 1e-4f ? 20.f * log10f(level) : -80.f;
        float frac = (db + 60.f) / 60.f; // -60 dB .. 0 dB full scale
        if (frac < 0.f)
            frac = 0.f;
        if (frac > 1.f)
            frac = 1.f;
        int w = (int)(frac * kVuBarW);
        uint16_t c = frac < 0.8f ? 0x07E0 : frac < 0.95f ? 0xFFE0 : 0xF800;
        if (w > 0)
            tft.FillRect(kVuBarX, kVuY[i], w, kVuBarH, c);
    }
}

// Redraw whatever is dirty, batched to ~50 Hz. Main loop only.
static void MenuRender(uint32_t now) {
    if (ui_vu) { // VU page: static layout once, bars refreshed ~30 Hz
        if (!ui_full_dirty && now - ui_last_render_ms < 33)
            return;
        if (tft.FlushBusy())
            return;
        ui_last_render_ms = now;
        if (ui_full_dirty) {
            ui_drawVuStatic();
            ui_full_dirty = false;
            tft.FlushRows(0, Ili9341::kHeight);
        } else {
            ui_drawVuBars();
            tft.FlushRows(50, 70);  // input bars
            tft.FlushRows(164, 72); // output bars
        }
        return;
    }
    bool any =
        ui_full_dirty || ui_foot_dirty || ui_band_dirty[0] || ui_band_dirty[1] || ui_band_dirty[2];
    if (!any)
        return;
    // Single framebuffer: don't redraw into it while a previous flush is still
    // streaming it out over DMA. Drains first, which also coalesces fast turns.
    if (tft.FlushBusy())
        return;
    if (now - ui_last_render_ms < 20)
        return;
    ui_last_render_ms = now;

    if (ui_full_dirty) {
        tft.FillScreen(Ili9341::kBlack);
        ui_drawHeader();
        for (int i = 0; i < 3; i++)
            ui_drawBand(i);
        ui_drawFooter();
        ui_full_dirty = false;
        ui_foot_dirty = false;
        ui_band_dirty[0] = ui_band_dirty[1] = ui_band_dirty[2] = false;
        tft.FlushRows(0, Ili9341::kHeight);
    } else {
        for (int i = 0; i < 3; i++)
            if (ui_band_dirty[i]) {
                ui_drawBand(i);
                ui_band_dirty[i] = false;
                tft.FlushRows(30 + i * 90, 90);
            }
        ui_drawFooter();
        ui_foot_dirty = false;
        tft.FlushRows(300, 20);
    }
}

// Refresh the channel activity dots (~30 Hz) without a full header redraw.
static uint32_t ui_dots_ms = 0;
static void MenuDots(uint32_t now) {
    if (ui_vu || ui_settings)
        return; // dots live in the normal header only
    if (now - ui_dots_ms < 33)
        return;
    if (tft.FlushBusy())
        return;
    ui_dots_ms = now;
    ui_drawDots();
    tft.FlushRows(kDotRowY, kDotRowH);
}

// ============================================================
// Audio callback — non-interleaved stereo
// ============================================================

// Peak-hold-with-decay update for the VU page, called once per audio block.
static void VuCommit(float iL, float iR, float oL, float oR) {
    const float k = 0.96f; // per-block decay -> a few hundred ms release
    vu_in_l = iL > vu_in_l ? iL : vu_in_l * k;
    vu_in_r = iR > vu_in_r ? iR : vu_in_r * k;
    vu_out_l = oL > vu_out_l ? oL : vu_out_l * k;
    vu_out_r = oR > vu_out_r ? oR : vu_out_r * k;
}

static void AudioCallback(const float *const *in, float **out, size_t size) {
    dspLoad.OnBlockStart();

    float biL = 0.f, biR = 0.f, boL = 0.f, boR = 0.f; // block peaks for the VU

    if (bypass) {
        for (size_t i = 0; i < size; i++) {
            float l = in[0][i], r = in[1][i];
            out[0][i] = l * soft_gain;
            out[1][i] = r * soft_gain;
            if (soft_gain < 1.f)
                soft_gain = soft_gain + soft_inc > 1.f ? 1.f : soft_gain + soft_inc;
            biL = fabsf(l) > biL ? fabsf(l) : biL;
            biR = fabsf(r) > biR ? fabsf(r) : biR;
            boL = fabsf(out[0][i]) > boL ? fabsf(out[0][i]) : boL;
            boR = fabsf(out[1][i]) > boR ? fabsf(out[1][i]) : boR;
        }
        VuCommit(biL, biR, boL, boR);
        dspLoad.OnBlockEnd();
        return;
    }

    // Per-block: update filter + delay coefficients once, cache pan/mode flags.
    float panL[NUM_CH], panR[NUM_CH];
    bool use6[NUM_CH], use24[NUM_CH];

    // Delay time: synced to clock divisions or free ms
    {
        float delaySec = preset.delaySynced
                             ? kDivBeats[preset.delayParam / 16] * 60.f / preset.bpm
                             : CcLog(preset.delayParam, 0.01f, 2.f); // 10 ms – 2000 ms
        size_t delaySmps = (size_t)fclamp(delaySec * sample_rate, 1.f, 192000.f);
        delayL.SetDelay(delaySmps);
        delayR.SetDelay(delaySmps);
    }

    // Per-channel LFO phase increment + trapezoid slew width.
    // Slew = ~1.5 ms in wall-clock terms → lfoFreq * 0.0015 in phase units.
    float lfoInc[NUM_CH], lfoRamp[NUM_CH];
    for (int c = 0; c < NUM_CH; c++) {
        float lfoFreq;
        if (preset.ch[c].lfoSynced) {
            lfoFreq = preset.bpm / (60.f * kDivBeats[preset.ch[c].lfoParam / 16]);
        } else {
            lfoFreq = CcLog(preset.ch[c].lfoParam, 0.1f, 20.f);
        }
        lfoInc[c] = lfoFreq / sample_rate;
        lfoRamp[c] = lfoFreq * 0.0015f;
    }

    for (int c = 0; c < NUM_CH; c++) {
        // 6 dB: OnePole — LP or HP only (BP/Notch fall back to SVF 12 dB)
        use6[c] = preset.ch[c].filterSlope == 0 && preset.ch[c].filterType != 1 // not BP
                  && preset.ch[c].filterType != 3;                              // not Notch
        // 24 dB: Ladder — LP, HP, BP (Notch not available, falls back to SVF 12 dB)
        use24[c] = preset.ch[c].filterSlope == 2 && preset.ch[c].filterType != 3;

        float freq = preset.ch[c].cutoff * (1.f + preset.ch[c].lfoAmount * ch[c].lfoVal * 0.5f);
        freq = fclamp(freq, 100.f, sample_rate / 3.f - 1.f);

        if (use6[c]) {
            float normF = freq / sample_rate;
            ch[c].poleL.SetFrequency(normF);
            ch[c].poleR.SetFrequency(normF);
            OnePole::FilterMode pm = preset.ch[c].filterType == 2 ? OnePole::FILTER_MODE_HIGH_PASS
                                                                  : OnePole::FILTER_MODE_LOW_PASS;
            ch[c].poleL.SetFilterMode(pm);
            ch[c].poleR.SetFilterMode(pm);
        } else if (use24[c]) {
            LadderFilter::FilterMode lm;
            switch (preset.ch[c].filterType) {
            case 1:
                lm = LadderFilter::FilterMode::BP24;
                break;
            case 2:
                lm = LadderFilter::FilterMode::HP24;
                break;
            default:
                lm = LadderFilter::FilterMode::LP24;
                break;
            }
            ch[c].ladL.SetFilterMode(lm);
            ch[c].ladR.SetFilterMode(lm);
            ch[c].ladL.SetFreq(freq);
            ch[c].ladR.SetFreq(freq);
            ch[c].ladL.SetRes(preset.ch[c].resonance);
            ch[c].ladR.SetRes(preset.ch[c].resonance);
            ch[c].ladL.SetInputDrive(preset.ch[c].drive);
            ch[c].ladR.SetInputDrive(preset.ch[c].drive);
        } else {
            // 12 dB SVF — LP, HP, BP, or Notch; also fallback for unsupported combos
            ch[c].fltL.SetFreq(freq);
            ch[c].fltR.SetFreq(freq);
            ch[c].fltL.SetRes(preset.ch[c].resonance);
            ch[c].fltR.SetRes(preset.ch[c].resonance);
        }

        panL[c] = cosf(preset.ch[c].pan * HALF_PI);
        panR[c] = sinf(preset.ch[c].pan * HALF_PI);
    }

    for (size_t i = 0; i < size; i++) {
        // Advance per-channel LFOs
        for (int c = 0; c < NUM_CH; c++) {
            ch[c].lfoPhase += lfoInc[c];
            if (ch[c].lfoPhase >= 1.f)
                ch[c].lfoPhase -= 1.f;
            ch[c].lfoVal =
                LfoSample(ch[c].lfoPhase, preset.ch[c].lfoShape, preset.ch[c].lfoDuty, lfoRamp[c]);
        }

        float inL = in[0][i];
        float inR = in[1][i];
        biL = fabsf(inL) > biL ? fabsf(inL) : biL;
        biR = fabsf(inR) > biR ? fabsf(inR) : biR;

        // Accumulate channel mix separately — used as sidechain source
        float chanL = 0.f, chanR = 0.f, delaySend = 0.f;

        for (int c = 0; c < NUM_CH; c++) {
            float filtL, filtR;

            if (use6[c]) {
                filtL = ch[c].poleL.Process(inL * preset.ch[c].drive);
                filtR = ch[c].poleR.Process(inR * preset.ch[c].drive);
            } else if (use24[c]) {
                // drive applied via SetInputDrive() in the per-block setup above, not here
                filtL = ch[c].ladL.Process(inL);
                filtR = ch[c].ladR.Process(inR);
            } else {
                ch[c].fltL.Process(inL * preset.ch[c].drive);
                ch[c].fltR.Process(inR * preset.ch[c].drive);
                switch (preset.ch[c].filterType) {
                case 1:
                    filtL = ch[c].fltL.Band();
                    filtR = ch[c].fltR.Band();
                    break;
                case 2:
                    filtL = ch[c].fltL.High();
                    filtR = ch[c].fltR.High();
                    break;
                case 3:
                    filtL = ch[c].fltL.Notch();
                    filtR = ch[c].fltR.Notch();
                    break;
                default:
                    filtL = ch[c].fltL.Low();
                    filtR = ch[c].fltR.Low();
                    break;
                }
            }

            float env = ch[c].env.Process();
            float lvl =
                fclamp(preset.ch[c].level + preset.ch[c].ampLfoAmount * ch[c].lfoVal, 0.f, 1.f);
            if (ch_silenced(c))
                lvl = 0.f;
            float chL = filtL * env * lvl;
            float chR = filtR * env * lvl;
            chanL += chL * panL[c];
            chanR += chR * panR[c];
            delaySend += (chL + chR) * 0.5f * preset.ch[c].delayAmount;
        }

        // Ping-pong delay: L fed by send + R×feedback; R fed by L×feedback
        float dL = delayL.Read();
        float dR = delayR.Read();
        delayL.Write(delaySend + dR * preset.delayFeedback);
        delayR.Write(dL * preset.delayFeedback);

        // Width: 0 = mono (L+R mixed to centre), 1 = full stereo ping-pong
        float wBlend = (1.f - preset.delayWidth) * 0.5f;
        float delOutL = dL * (1.f - wBlend) + dR * wBlend;
        float delOutR = dR * (1.f - wBlend) + dL * wBlend;

        // Final mix: channels + delay + dry
        float outL = chanL + delOutL + inL * preset.dryLevel;
        float outR = chanR + delOutR + inR * preset.dryLevel;

        // Soft clip — handles summing of multiple channels gracefully.
        // soft_gain ramps the output up from silence at startup (anti-pop).
        out[0][i] = fasttanh(outL) * soft_gain;
        out[1][i] = fasttanh(outR) * soft_gain;
        if (soft_gain < 1.f)
            soft_gain = soft_gain + soft_inc > 1.f ? 1.f : soft_gain + soft_inc;
        boL = fabsf(out[0][i]) > boL ? fabsf(out[0][i]) : boL;
        boR = fabsf(out[1][i]) > boR ? fabsf(out[1][i]) : boR;
    }
    VuCommit(biL, biR, boL, boR);

    dspLoad.OnBlockEnd();
}

// ============================================================
// Main
// ============================================================

int main(void) {
    hw.Configure();
    hw.Init(true); // boost to 480 MHz for audio + display headroom
    hw.SetAudioBlockSize(48);
    sample_rate = hw.AudioSampleRate();
    soft_inc = 1.f / (sample_rate * 0.15f); // ~150 ms output fade-in

    // --- Init load meters ---
    dspLoad.Init(sample_rate, 48, 10.f); // 10 Hz cutoff — fast enough for 250ms windows
    ticksPerUs = System::GetTickFreq() / 1000000.f;

    // --- Init delay lines ---
    delayL.Init();
    delayR.Init();

    // --- Init DSP ---
    for (int c = 0; c < NUM_CH; c++) {
        ch[c].fltL.Init(sample_rate);
        ch[c].fltR.Init(sample_rate);
        ch[c].ladL.Init(sample_rate);
        ch[c].ladR.Init(sample_rate);
        ch[c].poleL.Init();
        ch[c].poleR.Init();
        ch[c].env.Init(sample_rate);
        ch[c].env.SetMin(0.f);
        ch[c].env.SetMax(1.f);
        ch[c].lfoPhase = 0.f;
        ch[c].lfoVal = 0.f;
        ch[c].note_active = false;
        ch[c].env_started = false;
    }

    // --- Init patch storage (QSPI flash) ---
    {
        PatchStorage defaults;
        defaults.version = PATCH_VERSION;
        Preset dp = DefaultPreset();
        for (int i = 0; i < NUM_PATCHES; i++)
            defaults.patches[i] = dp;
        patchStorage.Init(defaults);
    }
    if (patchStorage.GetSettings().version != PATCH_VERSION)
        patchStorage.RestoreDefaults();
    // Load patch 0
    preset = patchStorage.GetSettings().patches[0];

    // --- Init UI settings storage (separate QSPI sector) ---
    {
        UiSettings d;
        d.version = UI_VERSION;
        for (int i = 0; i < 4; i++)
            d.color[i] = kDefaultColor[i];
        d.brightness = 255;
        d.run = 1; // sequencer running
        d.bypass = 0;
        d.dry = 0;  // dry level 0
        d.bpm = 45; // ~120 BPM
        uiStore.Init(d, UI_QSPI_OFFSET);
    }
    if (uiStore.GetSettings().version != UI_VERSION)
        uiStore.RestoreDefaults();
    {
        const UiSettings &s = uiStore.GetSettings();
        for (int i = 0; i < 4; i++)
            ui_color[i] = s.color[i];
        ui_brightness = s.brightness;
        // Restore persisted transport/global state (override patch 0's values).
        seq_running = s.run != 0;
        bypass = s.bypass != 0;
        HandleCC(4, s.dry);  // dry level
        HandleCC(19, s.bpm); // manual BPM
    }

    // --- Init MIDI ---
    MidiUartHandler::Config midi_cfg; // defaults: USART1, RX=PB7 (D14), TX=PB6 (D13)
    midi.Init(midi_cfg);
    midi.StartReceive();

    MidiUsbHandler::Config usb_cfg;
    usb_cfg.transport_config.periph = MidiUsbTransport::Config::INTERNAL;
    usb_midi.Init(usb_cfg);
    usb_midi.StartReceive();

    // --- Start audio ---
    hw.StartAudio(AudioCallback);

    // --- Init the menu UI (TFT + encoders). After audio so its blocking
    //     panel-reset delays don't hold up codec/MIDI bring-up. ---
    MenuInit();

    // Seed internal clock timer so first tick doesn't fire immediately
    int_tick_ms = System::GetNow();

    // --- Main loop ---
    while (true) {
        uint32_t loopStart = System::GetTick();
        uint32_t now = System::GetNow();

        // Clock source timeouts
        if (trs_active && (now - trs_last_ms) > TRS_TIMEOUT_MS)
            trs_active = false;
        if (usb_clock_active && (now - usb_last_ms) > TRS_TIMEOUT_MS)
            usb_clock_active = false;

        bool midi_active = trs_active || usb_clock_active;

        // Drop tempo estimator when no external clock is streaming, so resumes
        // don't average across a long silence.
        if (!midi_active) {
            last_clock_us = 0;
            clock_bpm_ema = 0.f;
        }

        // Service MIDI (resets on UART overrun)
        midi.Listen();
        usb_midi.Listen();

        // Process events
        ProcessMidi(midi, true);
        ProcessMidi(usb_midi, false);

        // Internal clock — only when no MIDI clock is present
        if (!midi_active) {
            uint32_t interval = (uint32_t)(60000.f / (preset.bpm * 24.f));
            if (seq_running) {
                if ((now - int_tick_ms) >= interval) {
                    int_tick_ms += interval;
                    AdvanceClock();
                }
            } else {
                int_tick_ms = now; // keep fresh so start is immediate
            }
        }

        // Send NoteOff when each channel's envelope has completed
        for (int c = 0; c < NUM_CH; c++) {
            if (ch[c].note_active) {
                float v = ch[c].env.GetValue();
                if (!ch[c].env_started) {
                    if (v > 0.001f)
                        ch[c].env_started = true;
                } else if (v < 0.001f) {
                    SendNoteOff(kTrigNote[c]);
                    ch[c].note_active = false;
                }
            }
        }

        // Physical controls (run/tap/bypass/pattern) and status LEDs are all
        // handled by the menu UI now. The old DaisyPod button/encoder/LED code
        // is gone: those pins (D17-D21) are the display + mux, not Pod LEDs/pots.

        // Menu UI — encoder scan + screen redraw. Never touches audio.
        MenuPoll(now);
        MenuRender(now);
        MenuDots(now);
        tft.ServiceFlush(); // pump the background DMA flush queue

        // Smooth main-loop iteration time (µs) and track per-window peak
        float loopUs = (System::GetTick() - loopStart) / ticksPerUs;
        loopLoadAvg = 0.05f * loopUs + 0.95f * loopLoadAvg;
        if (loopUs > loopPeak)
            loopPeak = loopUs;

        if ((now - lastLoadMs) >= 250) {
            lastLoadMs = now;
            // DSP: CC 10 = avg, CC 11 = peak (0–127 = 0–100 %)
            ui_dsp_load = dspLoad.GetAvgCpuLoad(); // captured for the on-screen meter
            SendCC(10, (uint8_t)fclamp(ui_dsp_load * 127.f, 0.f, 127.f));
            SendCC(11, (uint8_t)fclamp(dspLoad.GetMaxCpuLoad() * 127.f, 0.f, 127.f));
            dspLoad.Reset();
            // Loop: CC 12 = avg, CC 13 = peak (0–127 = 0–500 µs); meter vs 500 µs
            ui_ctrl_load = loopLoadAvg / 500.f;
            SendCC(12, (uint8_t)fclamp(ui_ctrl_load * 127.f, 0.f, 127.f));
            SendCC(13, (uint8_t)fclamp(loopPeak / 500.f * 127.f, 0.f, 127.f));
            loopPeak = 0.f;
            ui_foot_dirty = true; // refresh the footer meters + BPM
            // BPM echo — covers tempo drift from external clock and tap tempo.
            uint8_t bpm_cc = (uint8_t)((fclamp(preset.bpm, 20.f, 300.f) - 20.f) / 280.f * 127.f);
            if (bpm_cc != last_bpm_cc) {
                SendCC(19, bpm_cc);
                last_bpm_cc = bpm_cc;
                // Tempo changed without a UI edit (external clock / tap). Refresh
                // the BPM band too if it's the page on screen.
                if (!ui_settings) {
                    const Section &s = ui_cur();
                    for (int i = 0; i < s.n; i++)
                        if (s.bands[i].fmt == F_BPM)
                            ui_band_dirty[i] = true;
                }
            }
        }

        // Persist transport/global state (run/bypass/dry/manual bpm) to QSPI so a
        // power cycle restores it. Only when a value has been stable for one ~1 s
        // tick (so we don't write mid-tweak) and actually differs from flash —
        // PersistentStorage.Save() skips the erase/write if nothing changed.
        // BPM under external clock is left alone (only the manual tempo persists).
        if (now - ui_persist_ms >= 1000) {
            ui_persist_ms = now;
            UiSettings &s = uiStore.GetSettings();
            uint8_t run = seq_running ? 1 : 0;
            uint8_t byp = bypass ? 1 : 0;
            uint8_t dry = CcGet(4);
            uint8_t bpmv = midi_active ? s.bpm : CcGet(19);
            if (run == ui_seen_run && byp == ui_seen_byp && dry == ui_seen_dry &&
                bpmv == ui_seen_bpm) {
                if (s.run != run || s.bypass != byp || s.dry != dry || s.bpm != bpmv) {
                    s.run = run;
                    s.bypass = byp;
                    s.dry = dry;
                    s.bpm = bpmv;
                    uiStore.Save();
                }
            }
            ui_seen_run = run;
            ui_seen_byp = byp;
            ui_seen_dry = dry;
            ui_seen_bpm = bpmv;
        }
    }
}
