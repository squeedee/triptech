// Telemetry.h — load metering + status echo module.
//
// Measures audio-callback CPU (dspLoad) and main-loop iteration time, and once per
// 250 ms emits them as CCs (10/11 audio avg/peak, 12/13 loop avg/peak) plus a BPM
// echo (CC 19), and feeds the on-screen footer meters. dspLoad is driven by the
// audio callback (OnBlockStart/End); everything else runs from the main loop.
#pragma once

#include "Hardware.h" // daisy::CpuLoadMeter, daisy::System
#include "MenuUi.h"   // ui_cur / ui_band_dirty / ui_foot_dirty / F_BPM
#include "MidiOut.h"  // SendCC
#include "Model.h"
#include "State.h"

// Audio-callback CPU load meter — global so the audio callback drives it directly.
static daisy::CpuLoadMeter dspLoad;

namespace telemetry {

static float loopLoadAvg = 0.f;   // smoothed main-loop µs per iteration
static float loopPeak = 0.f;      // peak µs in the current 250 ms window
static uint32_t lastLoadMs = 0;   // last telemetry emit timestamp
static uint8_t last_bpm_cc = 255; // last BPM echoed as CC 19 (de-dupe)

// Initialise the audio load meter (call once, after the sample rate is known).
inline void Init(float sample_rate) {
    dspLoad.Init(sample_rate, 48, 10.f); // 10 Hz cutoff — fast enough for 250 ms windows
}

// Fold this iteration's wall-clock time into the smoothed/peak loop-load trackers.
// Call once per main-loop iteration with the tick captured at the top of the loop.
inline void RecordLoop(uint32_t loopStart) {
    float loopUs = (daisy::System::GetTick() - loopStart) / ticksPerUs;
    loopLoadAvg = 0.05f * loopUs + 0.95f * loopLoadAvg;
    if (loopUs > loopPeak)
        loopPeak = loopUs;
}

// Every 250 ms: emit the load CCs, refresh the footer meters, and echo BPM (which
// covers tempo drift from external clock + tap tempo). Call once per main-loop iter.
inline void Service(uint32_t now) {
    if ((now - lastLoadMs) < 250)
        return;
    lastLoadMs = now;
    // DSP: CC 10 = avg, CC 11 = peak (0–127 = 0–100 %)
    ui_dsp_load = dspLoad.GetAvgCpuLoad(); // captured for the on-screen meter
    SendCC(10, (uint8_t)daisysp::fclamp(ui_dsp_load * 127.f, 0.f, 127.f));
    SendCC(11, (uint8_t)daisysp::fclamp(dspLoad.GetMaxCpuLoad() * 127.f, 0.f, 127.f));
    dspLoad.Reset();
    // Loop: CC 12 = avg, CC 13 = peak (0–127 = 0–500 µs); meter vs 500 µs
    ui_ctrl_load = loopLoadAvg / 500.f;
    SendCC(12, (uint8_t)daisysp::fclamp(ui_ctrl_load * 127.f, 0.f, 127.f));
    SendCC(13, (uint8_t)daisysp::fclamp(loopPeak / 500.f * 127.f, 0.f, 127.f));
    loopPeak = 0.f;
    menu::ui_foot_dirty = true; // refresh the footer meters + BPM
    // BPM echo — covers tempo drift from external clock and tap tempo.
    uint8_t bpm_cc = (uint8_t)((daisysp::fclamp(preset.bpm, 20.f, 300.f) - 20.f) / 280.f * 127.f);
    if (bpm_cc != last_bpm_cc) {
        SendCC(19, bpm_cc);
        last_bpm_cc = bpm_cc;
        // Tempo changed without a UI edit (external clock / tap). Refresh the BPM
        // band too if it's the page on screen.
        if (!menu::ui_settings) {
            const menu::Section &s = menu::ui_cur();
            for (int i = 0; i < s.n; i++)
                if (s.bands[i].fmt == menu::F_BPM)
                    menu::ui_band_dirty[i] = true;
        }
    }
}

} // namespace telemetry
