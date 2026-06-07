// Hardware.h — handles to the physical peripherals, shared across modules.
//
// The Daisy Seed and the two MIDI transports are defined once in Triptech.cpp
// (the composition root) and declared here so behavioral modules — MIDI output,
// persistence, the clock — can reach them without owning them. Construction order
// still lives in Triptech.cpp (hw before anything built from hw.qspi).
#pragma once

#include "daisy_seed.h"

extern daisy::DaisySeed hw;
extern daisy::MidiUartHandler midi;    // TRS MIDI — USART1 (PB6/PB7 = D13/D14)
extern daisy::MidiUsbHandler usb_midi; // USB MIDI — internal full-speed port
