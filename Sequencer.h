// Sequencer.h — the step sequencer module.
//
// Owns the gate-pattern table and the behavior that advances the playhead and
// fires channel gates. Pure behavior over the shared state (cur_step/tick_accum in
// State.h, the active pattern in preset). Driven by both the external MIDI clock
// (MidiRouter) and the internal clock (ClockSource) — never the audio callback.
#pragma once

#include "Hardware.h" // daisy::System
#include "MidiOut.h"  // SendNoteOn / SendNoteOff
#include "Model.h"
#include "State.h"

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

namespace seq {

// Restart the playhead at step 0 (used by transport Start / RUN toggle).
inline void Reset() {
    cur_step = 0;
    tick_accum = 0.f;
}

// Fire channel `c`: re-arm its AD envelope from the preset's attack/decay and
// (re)trigger it, emitting a MIDI note so the gate is visible/playable downstream.
// Cuts any still-open note first so rapid re-triggers don't leave notes hanging.
inline void Trigger(int c) {
    ch[c].env.SetTime(daisysp::ADENV_SEG_ATTACK, preset.ch[c].attack);
    ch[c].env.SetTime(daisysp::ADENV_SEG_DECAY, preset.ch[c].decay);
    ch[c].env.Trigger();
    if (ch[c].note_active)
        SendNoteOff(kTrigNote[c]); // cut off any still-open note
    SendNoteOn(kTrigNote[c], 100);
    ch[c].note_active = true;
    ch[c].env_started = false;
}

// Advance the sequencer by one clock tick. Applies the clock divider/multiplier,
// and for every step it crosses: bumps cur_step, flashes the beat/activity dots,
// and triggers each channel whose gate is set in the active pattern. Called from
// both the external-clock path (MidiRouter) and the internal clock (ClockSource).
inline void Advance() {
    // ratio = pattern-steps consumed per incoming tick. >1 multiplies, <1 divides.
    float ratio = kClockRatios[ClockDivIndex(preset.clockDivParam)];
    tick_accum += ratio;
    while (tick_accum >= (float)TICKS_PER_STEP) {
        tick_accum -= (float)TICKS_PER_STEP;
        cur_step = (cur_step + 1) % NUM_STEPS;
        g_step_ms = daisy::System::GetNow(); // global activity-dot tick (divided rate)
        if (cur_step % 4 == 0)
            led2_flash_ms = daisy::System::GetNow();
        for (int c = 0; c < NUM_CH; c++) {
            if (kPatterns[preset.pattern][cur_step][c])
                Trigger(c);
        }
    }
}

} // namespace seq
