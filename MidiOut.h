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

// Emit a channel-voice message (high nibble `statusHi`, e.g. 0xB0, + 1 or 2 data
// bytes) on the enabled echo port(s), on the echo channel — or duplicated across
// all 16 channels when the feedback channel is set to OMNI (echo_chan >= 16).
inline void EchoSend(uint8_t statusHi, uint8_t d1, uint8_t d2, size_t ndata) {
    if (echo_iface == MIDI_NONE)
        return;
    uint8_t first = echo_chan >= 16 ? 0 : echo_chan;
    uint8_t last = echo_chan >= 16 ? 15 : echo_chan;
    uint8_t msg[3] = {0, d1, d2};
    size_t n = ndata + 1;
    for (uint8_t ch = first; ch <= last; ch++) {
        msg[0] = (uint8_t)(statusHi | ch);
        if (echoTRS())
            midi.SendMessage(msg, n);
        if (echoUSB())
            usb_midi.SendMessage(msg, n);
    }
}

inline void SendCC(uint8_t cc, uint8_t val) { EchoSend(0xB0, cc, val, 2); }
inline void SendProgramChange(uint8_t prog) { EchoSend(0xC0, prog, 0, 1); }
inline void SendNoteOn(uint8_t note, uint8_t vel) { EchoSend(0x90, note, vel, 2); }
inline void SendNoteOff(uint8_t note) { EchoSend(0x80, note, 0, 2); }

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
