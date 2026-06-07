// ParamMap.h — the CC map, the one authority on CC <-> live-state.
//
// A stateless behavioral module: free functions over the shared `Preset`/transport
// state (State.h). `Handle` applies an incoming Control Change; `Get` is its exact
// inverse, recovering the 7-bit value for outbound state dumps and the menu UI.
// Every parameter edit in the firmware funnels through here so the CC map stays the
// single source of truth.
#pragma once

#include "Model.h"
#include "State.h"

// Defined in other modules (Persistence / MidiOut). Declared here so Handle can
// drive save + full-state echo without depending on those headers.
namespace persist {
void SavePatch(uint8_t idx);
}
void SendAllState();

namespace parammap {

// Inverse of Handle: the current 7-bit value for a given CC, derived from the live
// preset/state. The single source of truth for both outbound state dumps
// (SendAllState) and the on-device menu UI's value display + edit base.
inline uint8_t Get(uint8_t cc) {
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
        return (uint8_t)((daisysp::fclamp(preset.bpm, 20.f, 300.f) - 20.f) / 280.f * 127.f);
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
            uint16_t v14 = (uint16_t)(
                daisysp::fclamp((cp.lfoAmount + 1.f) * 0.5f * 16383.f, 0.f, 16383.f) + 0.5f);
            return v14 >> 7;
        }
        case 12: {
            uint16_t v14 = (uint16_t)(
                daisysp::fclamp((cp.lfoAmount + 1.f) * 0.5f * 16383.f, 0.f, 16383.f) + 0.5f);
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

// Apply one incoming Control Change to the live preset/state. This is the single
// authority on the CC map: global params (delay/seq/transport) are matched first,
// then the per-channel block (kCcBase[c] + offset 0–15). Get() is its inverse.
inline void Handle(uint8_t ctrl, uint8_t val) {
    // Any edit to a stored patch parameter marks the live preset dirty (for the
    // load/save prompts). Transport (15 run / 18 bypass / 68 mute) and actions
    // (87 save / 119 sync) are not patch state, so they don't. The external clock
    // writes preset.bpm directly (not via Handle), so it never falsely dirties.
    bool patchCC = (ctrl >= kCcBase[0] && ctrl <= kCcBase[NUM_CH - 1] + 15) || ctrl == 1 ||
                   ctrl == 2 || ctrl == 3 || ctrl == 4 || ctrl == 5 || ctrl == 6 || ctrl == 14 ||
                   ctrl == 19;
    if (patchCC)
        patch_dirty = true;

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
        manual_bpm_cc = val; // remember the manual tempo for the live snapshot
        return;
    case 68: // per-channel mute bitmask
        for (int c = 0; c < NUM_CH; c++)
            ch_muted[c] = (val >> c) & 1;
        return;
    case 87: // save current preset to patch index
        persist::SavePatch(val);
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

} // namespace parammap
