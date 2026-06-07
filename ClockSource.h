// ClockSource.h — clock arbitration + tempo, the module.
//
// Owns the external-clock detection state (the sliding BPM-estimate window and the
// per-transport active flags) and the internal free-running clock. Decides which
// clock drives the sequencer and derives preset.bpm from an external source.
// Behavior over the shared preset/transport state; runs from the main loop only.
#pragma once

#include "Hardware.h"  // daisy::System
#include "Model.h"     // CLOCK_TIMEOUT_MS, scaling
#include "Sequencer.h" // seq::Advance
#include "State.h"

namespace clock {

// Incoming-clock BPM detection (updates preset.bpm when external clock is active).
// Ticks are timestamped when drained in the main loop, not on arrival, so a busy
// main loop (display render) makes them bunch up. A single inter-tick interval is
// therefore far too noisy; estimate over a sliding window of ticks instead so the
// burst-clustering between the endpoints cancels and only the (well-separated)
// endpoints set the tempo.
static constexpr int kClkWin = 24; // window length in ticks (24 PPQN => 1 quarter)
static uint32_t clk_ts[kClkWin] = {0};
static uint8_t clk_idx = 0;
static uint32_t clk_count = 0;
static float clock_bpm_ema = 0.f;

static bool trs_active = false;
static uint32_t trs_last_ms = 0;
static bool usb_clock_active = false;
static uint32_t usb_last_ms = 0;
static uint32_t int_tick_ms = 0; // timestamp of last internal tick

// TRS clock is authoritative; USB clock is ignored while TRS is active.
inline bool TrsActive() { return trs_active; }

// Handle one incoming MIDI timing-clock tick. `allow_trans` is false when this
// transport is being ignored (USB while TRS is live). Always refreshes the
// source's liveness; only the authoritative source updates tempo + advances.
inline void OnExternalTick(bool from_trs, bool allow_trans) {
    if (from_trs) {
        trs_active = true;
        trs_last_ms = daisy::System::GetNow();
    } else {
        usb_clock_active = true;
        usb_last_ms = daisy::System::GetNow();
    }
    if (!allow_trans)
        return;
    // BPM from a sliding kClkWin-tick window, lightly EMA-smoothed. Timestamp with
    // the RAW 32-bit tick counter, not GetUs(): the system timer runs at 240 MHz,
    // so GetUs() (= CNT / 240) wraps every 2^32/240e6 ≈ 17.9 s — and a span
    // straddling that wrap underflowed, spiking the tempo and jolting the delay
    // length once every 17.9 s. Raw CNT wraps cleanly at 2^32, so the unsigned
    // delta is correct across the wrap. kClkWin ticks = kClkWin/24 quarters.
    uint32_t now_tick = daisy::System::GetTick();
    clk_ts[clk_idx] = now_tick;
    clk_idx = (clk_idx + 1) % kClkWin;
    clk_count++;
    if (clk_count > (uint32_t)kClkWin) {
        uint32_t span_ticks = now_tick - clk_ts[clk_idx]; // wrap-safe (clean 2^32)
        float span_us = span_ticks / ticksPerUs;
        if (span_us > 0.f) {
            float bpm = (float)kClkWin / 24.f * 60000000.f / span_us;
            // Ignore implausible spans (wrap leftovers, dropped/double ticks): hold
            // the last good tempo rather than lurch.
            if (bpm >= 20.f && bpm <= 300.f) {
                clock_bpm_ema = (clock_bpm_ema == 0.f) ? bpm : (clock_bpm_ema * 0.7f + bpm * 0.3f);
                preset.bpm = daisysp::fclamp(clock_bpm_ema, 20.f, 300.f);
            }
        }
    }
    if (seq_running)
        seq::Advance();
}

// Age out stale external sources and, when none is live, drop the tempo estimator
// so a resume doesn't average across a long silence. Returns true while an
// external clock is driving us. Call once per main-loop iteration.
inline bool Update(uint32_t now) {
    if (trs_active && (now - trs_last_ms) > CLOCK_TIMEOUT_MS)
        trs_active = false;
    if (usb_clock_active && (now - usb_last_ms) > CLOCK_TIMEOUT_MS)
        usb_clock_active = false;
    bool active = trs_active || usb_clock_active;
    if (!active) {
        clk_count = 0;
        clk_idx = 0;
        clock_bpm_ema = 0.f;
    }
    return active;
}

// Seed the internal clock so the first tick doesn't fire immediately (boot-time).
inline void SeedInternal(uint32_t now) { int_tick_ms = now; }

// Run the internal clock from preset.bpm. Call only when no external clock is
// active. Holds the phase fresh while stopped so Start is immediate.
inline void ServiceInternal(uint32_t now) {
    uint32_t interval = (uint32_t)(60000.f / (preset.bpm * 24.f));
    if (seq_running) {
        if ((now - int_tick_ms) >= interval) {
            int_tick_ms += interval;
            seq::Advance();
        }
    } else {
        int_tick_ms = now; // keep fresh so start is immediate
    }
}

} // namespace clock
