// Persistence.h — QSPI-backed storage module.
//
// The named patch bank, the UI-prefs sector, and the "reset-resistant" live-state
// snapshot. Owns the persistence structs and the load/save/snapshot behavior over
// the shared preset/transport state. The PersistentStorage objects themselves are
// defined in Triptech.cpp (they construct from hw.qspi) and reached via extern.
#pragma once

#include "Hardware.h" // daisy::PersistentStorage (via daisy_seed.h), hw
#include "MidiOut.h"  // SendAllState / SendProgramChange / SendCC
#include "Model.h"
#include "State.h"

// Global UI prefs persisted in QSPI, in its own sector well clear of the patch
// bank (which lives at offset 0).
struct UiSettings {
    uint32_t version;
    uint16_t color[4];
    uint8_t brightness;
    uint8_t echoIface, echoChan, inIface, inChan; // MIDI I/O routing (see State.h)
    bool operator!=(const UiSettings &o) const {
        if (version != o.version || brightness != o.brightness || echoIface != o.echoIface ||
            echoChan != o.echoChan || inIface != o.inIface || inChan != o.inChan)
            return true;
        for (int i = 0; i < 4; i++)
            if (color[i] != o.color[i])
                return true;
        return false;
    }
};
static constexpr uint32_t UI_VERSION = 4;           // bumped: added MIDI I/O routing
static constexpr uint32_t UI_QSPI_OFFSET = 0x80000; // 512 KB in

// "Reset-resistant" live working state, auto-snapshotted to QSPI (debounced) so a
// power cut during a performance restores exactly where you were — the full active
// preset plus transport (run/bypass/mute/solo). This is separate from the named
// preset bank (patchStorage): explicit Save/Load patches still work as before; this
// just remembers the *current* state regardless of which preset it came from.
struct LiveState {
    uint32_t version;
    Preset preset;    // bpm field holds the MANUAL tempo (never the external clock)
    uint8_t run;      // sequencer running 0/1
    uint8_t bypass;   // 0/1
    uint8_t muteMask; // bit c = channel c muted
    uint8_t soloMask; // bit c = channel c soloed
    bool operator!=(const LiveState &o) const { return memcmp(this, &o, sizeof(*this)) != 0; }
};
static constexpr uint32_t LIVE_VERSION = 1;
static constexpr uint32_t LIVE_QSPI_OFFSET = 0x100000; // 1 MB in (clear of patches@0, ui@0x80000)

// Defined in Triptech.cpp (constructed from hw.qspi, so they must follow hw).
extern daisy::PersistentStorage<PatchStorage> patchStorage;
extern daisy::PersistentStorage<UiSettings> uiStore;
extern daisy::PersistentStorage<LiveState> liveStore;

namespace persist {

// Copy patch slot `idx` from the QSPI bank into the live preset and broadcast it.
inline void LoadPatch(uint8_t idx) {
    if (idx >= NUM_PATCHES)
        return;
    cur_patch = idx;
    preset = patchStorage.GetSettings().patches[idx];
    patch_dirty = false; // live state now matches the loaded slot
    SendAllState();
}

// Write the live preset into patch slot `idx` and commit the bank to QSPI flash.
inline void SavePatch(uint8_t idx) {
    if (idx >= NUM_PATCHES)
        return;
    cur_patch = idx;
    PatchStorage &store = patchStorage.GetSettings();
    store.patches[idx] = preset;
    patchStorage.Save();          // brief audio glitch possible during flash write
    patch_dirty = false;          // live state now matches the saved slot
    SendProgramChange(cur_patch); // echo back saved location
    SendCC(87, idx);              // confirm save to controller
}

// Pack the current live working state into `s` for the auto-snapshot. bpm is forced
// to the manual tempo so an external clock (which drives preset.bpm live) doesn't
// churn the snapshot and starve other changes of a quiet window to save in.
inline void BuildLiveState(LiveState &s) {
    s.version = LIVE_VERSION;
    s.preset = preset;
    s.preset.bpm = 20.f + (manual_bpm_cc / 127.f) * 280.f;
    s.run = seq_running ? 1 : 0;
    s.bypass = bypass ? 1 : 0;
    uint8_t mm = 0, sm = 0;
    for (int c = 0; c < NUM_CH; c++) {
        if (ch_muted[c])
            mm |= 1 << c;
        if (ch_solo[c])
            sm |= 1 << c;
    }
    s.muteMask = mm;
    s.soloMask = sm;
}

// Apply a restored live snapshot to the running globals (boot-time).
inline void ApplyLiveState(const LiveState &s) {
    preset = s.preset;
    seq_running = s.run != 0;
    bypass = s.bypass != 0;
    for (int c = 0; c < NUM_CH; c++) {
        ch_muted[c] = (s.muteMask >> c) & 1;
        ch_solo[c] = (s.soloMask >> c) & 1;
    }
    manual_bpm_cc = (uint8_t)((daisysp::fclamp(preset.bpm, 20.f, 300.f) - 20.f) / 280.f * 127.f);
}

// --- Debounced auto-snapshot of the live working state ---
static LiveState live_build;        // rebuilt each check (static => stable padding for memcmp)
static LiveState live_last;         // value at previous check, to detect "still changing"
static uint32_t live_check_ms = 0;  // throttles how often we rebuild/compare
static uint32_t live_settle_ms = 0; // when the state last changed
static bool live_pending = false;   // a change is waiting out the debounce window

// Seed the change-detector at boot so we don't immediately re-save the restored state.
inline void SeedSnapshot() { BuildLiveState(live_last); }

// Rebuild the snapshot ~every 200 ms; once it stops changing for LIVE_DEBOUNCE_MS,
// persist it (only if it differs from flash — Save() skips the erase/write
// otherwise). The quiet-period debounce means a knob sweep writes once, after you
// settle, not continuously. Call once per main-loop iteration.
inline void ServiceSnapshot(uint32_t now) {
    static constexpr uint32_t LIVE_DEBOUNCE_MS = 1500;
    if (now - live_check_ms >= 200) {
        live_check_ms = now;
        BuildLiveState(live_build);
        if (live_build != live_last) {
            live_last = live_build;
            live_settle_ms = now;
            live_pending = true;
        } else if (live_pending && (now - live_settle_ms) >= LIVE_DEBOUNCE_MS) {
            LiveState &dst = liveStore.GetSettings();
            if (dst != live_build) {
                dst = live_build;
                liveStore.Save(); // brief audio glitch possible during flash write
            }
            live_pending = false;
        }
    }
}

} // namespace persist
