// MidiOut.h — outbound MIDI module.
//
// Free functions that emit MIDI on BOTH transports (TRS + USB) so an attached
// controller and a host DAW stay in sync regardless of which one is driving us.
// `SendAllState` walks the CC map (parammap::Get) to mirror the full live state to
// a freshly-connected controller. Behavior over the shared state + hardware handles.
#pragma once

#include "Hardware.h"
#include "Model.h"
#include "ParamMap.h"
#include "State.h"

inline void SendCC(uint8_t cc, uint8_t val) {
    uint8_t msg[3] = {0xB0, cc, val};
    midi.SendMessage(msg, 3);
    usb_midi.SendMessage(msg, 3);
}

inline void SendProgramChange(uint8_t prog) {
    uint8_t msg[2] = {0xC0, prog};
    midi.SendMessage(msg, 2);
    usb_midi.SendMessage(msg, 2);
}

inline void SendNoteOn(uint8_t note, uint8_t vel) {
    uint8_t msg[3] = {0x90, note, vel};
    midi.SendMessage(msg, 3);
    usb_midi.SendMessage(msg, 3);
}

inline void SendNoteOff(uint8_t note) {
    uint8_t msg[3] = {0x80, note, 0};
    midi.SendMessage(msg, 3);
    usb_midi.SendMessage(msg, 3);
}

// Broadcast the entire live state — every global CC, the program-change for the
// current patch, and all 16 per-channel CCs × NUM_CH — so a freshly-connected
// controller can mirror the device. Triggered by CC 119 or the PATCH "STATE" action.
inline void SendAllState() {
    static const uint8_t kGlobalCc[] = {1, 2, 3, 4, 5, 6, 14, 15, 18, 19, 68};
    for (uint8_t cc : kGlobalCc)
        SendCC(cc, parammap::Get(cc));
    SendProgramChange(cur_patch);
    for (int c = 0; c < NUM_CH; c++) {
        int base = kCcBase[c];
        for (int off = 0; off <= 15; off++)
            SendCC(base + off, parammap::Get(base + off));
    }
}
