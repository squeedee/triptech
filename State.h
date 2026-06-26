// State.h — Triptech shared runtime state.
//
// The live working `preset`, the per-channel runtime DSP instances, and the
// transport/sequencer state that the behavioral modules read and write. This is
// the mutable "shared data" the modules operate over (the parameters themselves
// live in the POD `Preset`; this adds the runtime that isn't serialized). Single
// translation unit, so these are plain file-scope globals defined once here.
#pragma once

#include "Model.h"
#include "daisysp.h"

// Per-channel runtime DSP state — the live filters/envelope/LFO instances the
// audio callback runs. Distinct from ChannelPreset (the stored parameters): the
// active filter is chosen per block from the preset's type/slope.
struct Channel {
    daisysp::Svf fltL, fltR;          // SVF: LP/HP/BP/Notch at 12 dB
    daisysp::LadderFilter ladL, ladR; // Ladder: LP/HP/BP at 12 or 24 dB
    daisysp::OnePole poleL, poleR;    // OnePole: LP/HP at 6 dB
    daisysp::AdEnv env;
    float lfoPhase;    // 0–1 phase accumulator
    float lfoVal;      // last computed LFO sample
    uint8_t lfoAmtMsb; // 14-bit MSB cache for filter lfoAmount
    bool note_active;
    bool env_started;
};

static Preset preset;              // live working preset — what the engine plays right now
static Channel ch[NUM_CH];         // per-channel runtime DSP state (parallel to preset.ch[])
static uint8_t cur_patch = 0;      // slot index of the most recently loaded/saved patch
static bool patch_dirty = false;   // live preset edited since the last patch load/save
static uint8_t manual_bpm_cc = 45; // last manually-set tempo as CC (external clock excluded)

// --- Sequencer position ---
static uint8_t cur_step = 0;    // current sequencer step, 0..NUM_STEPS-1
static float tick_accum = 0.f;  // fractional ticks carried between steps (clock div/mult)
static bool seq_running = true; // sequencer runs by default at power-on

// --- Transport ---
static bool bypass = false;
static bool ch_muted[NUM_CH] = {false, false, false};
static bool ch_solo[NUM_CH] = {false, false, false};

// A channel is silenced if it's muted, or if any channel is soloed and it isn't.
static inline bool any_solo() { return ch_solo[0] || ch_solo[1] || ch_solo[2]; }
static inline bool ch_silenced(int c) { return ch_muted[c] || (any_solo() && !ch_solo[c]); }

// --- Shared timing ---
static float ticksPerUs = 1.f;     // System::GetTick() ticks per µs (set in main, after init)
static uint32_t led2_flash_ms = 0; // timestamp of last beat flash (read by the menu UI)
static uint32_t g_step_ms = 0;     // timestamp of last sequencer step (global activity dot)
static uint32_t tap_last_ms = 0;   // timestamp of last tap-tempo tap

// --- Metering (written by the audio callback / telemetry, read by the menu UI) ---
static volatile float vu_in_l = 0.f, vu_in_r = 0.f, vu_out_l = 0.f, vu_out_r = 0.f;
static float ui_dsp_load = 0.f;  // audio DSP load 0..1 (for the footer meter)
static float ui_ctrl_load = 0.f; // control/main-loop load 0..1 (vs 500 µs)

// --- MIDI I/O routing (edited in the menu, persisted in UiSettings) ---
// Which physical port(s) a stream uses. NONE silences/ignores that direction.
enum MidiIface : uint8_t { MIDI_USB, MIDI_TRS, MIDI_BOTH, MIDI_NONE };
static uint8_t echo_iface = MIDI_BOTH; // outbound: CC echo + note triggers + telemetry
static uint8_t echo_chan = 0;          // outbound channel, 0-15 (shown as 1-16)
static uint8_t in_iface = MIDI_BOTH;   // main MIDI in (notes/CC/PC AND clock) port(s)
static uint8_t in_chan = 16;           // input channel filter: 0-15 = ch 1-16, 16 = OMNI

static inline bool echoTRS() { return echo_iface == MIDI_TRS || echo_iface == MIDI_BOTH; }
static inline bool echoUSB() { return echo_iface == MIDI_USB || echo_iface == MIDI_BOTH; }
static inline bool inTRS() { return in_iface == MIDI_TRS || in_iface == MIDI_BOTH; }
static inline bool inUSB() { return in_iface == MIDI_USB || in_iface == MIDI_BOTH; }
// True if an incoming channel-voice message on `chan` (0-15) passes the input filter.
static inline bool inChanOk(uint8_t chan) { return in_chan >= 16 || chan == in_chan; }
