// MidiRouter.h — inbound MIDI dispatch module.
//
// Drains events from either transport and routes them: realtime clock/transport to
// ClockSource + the sequencer, Control Changes to the CC map, Program Changes to the
// patch bank, and trigger notes to the sequencer. TRS clock is authoritative — USB
// clock is ignored while TRS is live. Runs from the main loop only.
#pragma once

#include "ClockSource.h"
#include "Hardware.h"
#include "MenuUi.h" // ui_full_dirty / ui_patch_sel
#include "Model.h"
#include "ParamMap.h"
#include "Persistence.h"
#include "Sequencer.h"
#include "State.h"

namespace midirouter {
using namespace daisy;

template <typename Handler> void Process(Handler &m, bool from_trs) {
    while (m.HasEvents()) {
        MidiEvent msg = m.PopEvent();
        bool allow_trans = from_trs || !clock::TrsActive();

        if (msg.type == SystemRealTime) {
            switch (msg.srt_type) {
            case TimingClock:
                clock::OnExternalTick(from_trs, allow_trans); // tempo + advance
                break;

            case Start:
                if (allow_trans) {
                    seq_running = true;
                    seq::Reset();
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
            parammap::Handle(cc.control_number, cc.value);
            menu::ui_full_dirty = true; // reflect external edits on the screen
        } else if (msg.type == ProgramChange) {
            auto pc = msg.AsProgramChange();
            if (pc.program < NUM_PATCHES) {
                persist::LoadPatch(pc.program);
                menu::ui_patch_sel = pc.program;
                menu::ui_full_dirty = true;
            }
        } else if (msg.type == NoteOn) {
            auto note = msg.AsNoteOn();
            if (note.velocity > 0 && msg.channel == 0) {
                for (int c = 0; c < NUM_CH; c++) {
                    if (note.note == kTrigNote[c]) {
                        seq::Trigger(c);
                        break;
                    }
                }
            }
        }
    }
}

} // namespace midirouter
