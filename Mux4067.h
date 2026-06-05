// CD74HC4067 16-channel analog/digital multiplexer driver for the Daisy.
//
// Copied from menu/firmware (encoder-test) for the Triptech menu UI. On the
// `menu` control surface the four PEC11H rotary encoders (A/B/SW each) are read
// through one 4067: 4 select lines from the Daisy pick a channel and a single
// SIG line is read back. Because the 4067 connects only one channel to the
// common SIG pin at a time, a single pull-up on SIG (here the Daisy's internal
// pull-up) serves every channel -- an open contact floats high, a closed
// contact pulls SIG to GND. So every input reads active-low.
//
// Wiring (see menu/README.md):
//   S0=D15, S1=D18, S2=D19, S3=D21, SIG=D9 (the display's unused SPI1 MISO),
//   EN=GND (always enabled), VCC=+3V3D.

#pragma once

#include "daisy_seed.h"

class Mux4067 {
  public:
    void Init() {
        using namespace daisy;

        GPIO::Config sel;
        sel.mode = GPIO::Mode::OUTPUT;
        sel.pull = GPIO::Pull::NOPULL;
        sel.speed = GPIO::Speed::VERY_HIGH;

        sel.pin = seed::D15;
        s_[0].Init(sel);
        sel.pin = seed::D18;
        s_[1].Init(sel);
        sel.pin = seed::D19;
        s_[2].Init(sel);
        sel.pin = seed::D21;
        s_[3].Init(sel);

        // SIG: input with the internal pull-up (no external resistor needed).
        GPIO::Config sig;
        sig.pin = seed::D9;
        sig.mode = GPIO::Mode::INPUT;
        sig.pull = GPIO::Pull::PULLUP;
        sig.speed = GPIO::Speed::VERY_HIGH;
        sig_.Init(sig);
    }

    // Select `ch`, let SIG settle, and return the raw level (true = high/open).
    bool ReadChannel(uint8_t ch) {
        s_[0].Write(ch & 0x1);
        s_[1].Write(ch & 0x2);
        s_[2].Write(ch & 0x4);
        s_[3].Write(ch & 0x8);
        // Let the pull-up + flying-lead capacitance settle before sampling.
        daisy::System::DelayUs(kSettleUs);
        return sig_.Read();
    }

    // Scan channels 0..count-1 into a bitmask. Bit n = channel n.
    // Inputs are active-low, so we invert: bit set => contact closed / pressed.
    uint16_t Scan(uint8_t count = 16) {
        uint16_t bits = 0;
        for (uint8_t ch = 0; ch < count; ++ch)
            if (!ReadChannel(ch))
                bits |= (uint16_t)(1u << ch);
        return bits;
    }

  private:
    static constexpr uint32_t kSettleUs = 5;

    daisy::GPIO s_[4];
    daisy::GPIO sig_;
};
