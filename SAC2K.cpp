// ============================================================
// SAC2K.cpp — Radikal Technologies SAC-2K v3 driver for Daisy Seed
// ============================================================

#include "SAC2K.h"
#include "daisy_seed.h" // pulls in MidiHandler, MidiEvent, etc.

using namespace daisy;

namespace sac2k {

// Static member definitions
constexpr uint8_t SAC2K::kDispCursorCC[3];
constexpr uint8_t SAC2K::kDispWriteCC[3];
constexpr uint8_t SAC2K::kDispSpecCC[3];
constexpr uint8_t SAC2K::kLevelCC[3];

// ============================================================
// Init
// ============================================================

void SAC2K::Init(const Config &cfg, MidiHandler<MidiUartTransport> *trs,
                 MidiHandler<MidiUsbTransport> *usb) {
    channel_ = cfg.channel & 0x0F;
    trs_ = trs;
    usb_ = usb;
}

// ============================================================
// Process — drain events from both transports
// ============================================================

void SAC2K::Process() {
    if (trs_) {
        while (trs_->HasEvents()) {
            MidiEvent evt = trs_->PopEvent();
            HandleEvent(evt);
        }
    }
    if (usb_) {
        while (usb_->HasEvents()) {
            MidiEvent evt = usb_->PopEvent();
            HandleEvent(evt);
        }
    }
}

// ============================================================
// Event dispatcher
// ============================================================

void SAC2K::HandleEvent(const MidiEvent &evt) {
    // Only handle messages on our channel
    if (evt.channel != channel_)
        return;

    if (evt.type == MidiMessageType::ControlChange) {
        HandleCC(evt.data[0], evt.data[1]);
    }
    // Aftertouch (channel pressure) messages from SAC are not expected
    // in slave mode — we only send them for Enhanced Encoder Control.
}

// ============================================================
// CC handler — decode SAC-2K slave mode messages
// ============================================================

void SAC2K::HandleCC(uint8_t cc, uint8_t val) {
    // --- Buttons (CC 0x00–0x4E, value = press state) ---
    // The SAC sends CC with the button ID as the CC number
    // and the press state as the value.
    // Button range: 0x00 (Solo) through 0x4E (EncPush12)
    if (cc <= 0x4E) {
        if (button_cb_) {
            button_cb_(static_cast<Button>(cc), val, button_ctx_);
        }
        return;
    }

    // --- Encoder rotation (CC 0x53–0x5E) ---
    if (cc >= kEncoderBase && cc <= (kEncoderBase + NUM_ENCODERS - 1)) {
        uint8_t enc = cc - kEncoderBase;
        int8_t delta;
        if (val <= 0x3F) {
            delta = static_cast<int8_t>(val); // CW: 1..63
        } else {
            delta = static_cast<int8_t>(val) - 128; // CCW: -1..-63
        }
        if (encoder_cb_) {
            encoder_cb_(enc, delta, encoder_ctx_);
        }
        return;
    }

    // --- Jog wheel (CC 0x5F) ---
    if (cc == kJogCC) {
        int8_t delta;
        if (val <= 0x3F) {
            delta = static_cast<int8_t>(val);
        } else {
            delta = static_cast<int8_t>(val) - 128;
        }
        if (jog_cb_) {
            jog_cb_(delta, jog_ctx_);
        }
        return;
    }

    // --- Fader touch (CC 0x77–0x7F) ---
    if (cc >= kTouchBase && cc <= (kTouchBase + NUM_FADERS - 1)) {
        uint8_t fader = cc - kTouchBase;
        bool touched = (val >= 1);
        if (touch_cb_) {
            touch_cb_(fader, touched, touch_ctx_);
        }
        return;
    }

    // --- Fader position MSB (CC 0x17–0x1F) ---
    if (cc >= kFaderMsbBase && cc <= (kFaderMsbBase + NUM_FADERS - 1)) {
        uint8_t fader = cc - kFaderMsbBase;
        if (fader_cb_) {
            fader_cb_(fader, val, fader_ctx_);
        }
        return;
    }

    // --- Fader position LSB (CC 0x37–0x3F) --- reserved, currently ignored
    // If 8-bit fader mode is enabled, LSB would come here.
    // For now we only use 7-bit.
}

// ============================================================
// Motor faders
// ============================================================

void SAC2K::SetFader(uint8_t fader, uint8_t value) {
    if (fader >= NUM_FADERS)
        return;
    SendCC(kFaderMsbBase + fader, value & 0x7F);
}

// ============================================================
// Encoder LED rings
// ============================================================

void SAC2K::SetEncoderMode(uint8_t encoder, EncoderMode mode, uint8_t blinkMod) {
    if (encoder >= NUM_ENCODERS)
        return;
    // Enhanced Encoder Control uses Aftertouch (0xAx) message:
    //   Ax 43+enc mode
    uint8_t modeVal = static_cast<uint8_t>(mode) | (blinkMod & 0x60);
    SendAftertouch(0x43 + encoder, modeVal);
}

void SAC2K::SetEncoderValue(uint8_t encoder, uint8_t value) {
    if (encoder >= NUM_ENCODERS)
        return;
    // Encoder value uses Aftertouch (0xAx):
    //   Ax 53+enc value
    SendAftertouch(kEncoderBase + encoder, value & 0x7F);
}

void SAC2K::SetEncoder(uint8_t encoder, EncoderMode mode, uint8_t value, uint8_t blinkMod) {
    SetEncoderMode(encoder, mode, blinkMod);
    SetEncoderValue(encoder, value);
}

void SAC2K::SetEncoderOff(uint8_t encoder) { SetEncoderMode(encoder, EncoderMode::Off); }

// ============================================================
// Button LEDs
// ============================================================

void SAC2K::SetButtonLed(Button button, LedState state) {
    // Sending a CC back with button ID sets the LED.
    // CC number = button ID, value = LED state
    SendCC(static_cast<uint8_t>(button), static_cast<uint8_t>(state));
}

// ============================================================
// LCD Displays — CC-based character-at-a-time interface
// ============================================================

void SAC2K::SetDisplayCursor(uint8_t display, uint8_t col, uint8_t row) {
    if (display >= NUM_DISPLAYS || col >= DISPLAY_COLS || row > 1)
        return;
    uint8_t cp = (row == 0) ? col : (0x40 + col);
    SendCC(kDispCursorCC[display], cp);
}

void SAC2K::WriteDisplayChar(uint8_t display, char c) {
    if (display >= NUM_DISPLAYS)
        return;
    SendCC(kDispWriteCC[display], static_cast<uint8_t>(c) & 0x7F);
}

void SAC2K::ClearDisplay(uint8_t display) {
    if (display >= NUM_DISPLAYS)
        return;
    SendCC(kDispSpecCC[display], 0x01);
}

void SAC2K::WriteDisplayString(uint8_t display, uint8_t col, uint8_t row, const char *str) {
    if (!str || display >= NUM_DISPLAYS)
        return;
    SetDisplayCursor(display, col, row);
    uint8_t pos = col;
    while (*str && pos < DISPLAY_COLS) {
        WriteDisplayChar(display, *str++);
        pos++;
    }
}

void SAC2K::WriteDisplayLine(uint8_t display, uint8_t row, const char *str) {
    if (!str || display >= NUM_DISPLAYS || row > 1)
        return;
    SetDisplayCursor(display, 0, row);
    uint8_t pos = 0;
    while (*str && pos < DISPLAY_COLS) {
        WriteDisplayChar(display, *str++);
        pos++;
    }
    // Pad with spaces
    while (pos < DISPLAY_COLS) {
        WriteDisplayChar(display, ' ');
        pos++;
    }
}

// ============================================================
// LCD Displays — SysEx-based (more efficient for multi-char writes)
// ============================================================

void SAC2K::WriteDisplaySysEx(uint8_t display, uint8_t col, uint8_t row, const char *str) {
    if (!str || display >= NUM_DISPLAYS || col >= DISPLAY_COLS || row > 1)
        return;

    // SysEx format: F0 00 01 36 2A dv 44 70 dn cp dt... F7
    // dn = display number, cp = cursor position, dt = text ASCII
    uint8_t cp = (row == 0) ? col : (0x40 + col);

    // Calculate string length (max chars that fit in the line)
    size_t maxLen = DISPLAY_COLS - col;
    size_t len = 0;
    while (str[len] && len < maxLen)
        len++;

    // Build SysEx: header(5) + dv(1) + cmd(1) + addr(3) + data(len) + F7(1)
    // Using the Send Display Text format:
    // F0 00 01 36 2A dv 44 70 dn cp dt... F7
    uint8_t buf[64]; // 40 chars max + 10 header bytes = 50 max
    size_t pos = 0;
    buf[pos++] = 0xF0;
    buf[pos++] = 0x00;
    buf[pos++] = 0x01;
    buf[pos++] = 0x36;
    buf[pos++] = kModelId;
    buf[pos++] = channel_;
    buf[pos++] = 0x44; // Dump command
    buf[pos++] = 0x70; // Display text address high
    buf[pos++] = display;
    buf[pos++] = cp;
    for (size_t i = 0; i < len; i++) {
        buf[pos++] = static_cast<uint8_t>(str[i]) & 0x7F;
    }
    buf[pos++] = 0xF7;

    SendRaw(buf, pos);
}

// ============================================================
// Level meters
// ============================================================

void SAC2K::SetLevelMeter(uint8_t display, uint8_t position, uint8_t value) {
    if (display >= NUM_DISPLAYS || position < 1 || position > 4 || value > 0x0F)
        return;
    // CC format: Bx 6D+display (position<<4 | value)
    uint8_t nv = ((position & 0x0F) << 4) | (value & 0x0F);
    SendCC(kLevelCC[display], nv);
}

// ============================================================
// Time display
// ============================================================

void SAC2K::WriteTimeDisplay(const char *str) {
    if (!str)
        return;
    // Set digit cursor to rightmost (0)
    SendCC(kTimeDigitCC, 0x00);
    // Write chars right-to-left (auto-increment moves left)
    // First, find string length
    size_t len = 0;
    while (str[len])
        len++;
    // Write from the end of the string (rightmost first)
    for (int i = (int)len - 1; i >= 0; i--) {
        SendCC(kTimeCharCC, static_cast<uint8_t>(str[i]) & 0x7F);
    }
}

// ============================================================
// System / configuration
// ============================================================

void SAC2K::SendSlaveMode() {
    // Set Global System Configuration to mode 2 (Generic Slave)
    // F0 00 01 36 2A dv 44 00 00 00 md [ch] [tr] [mo] F7
    // md: bits 5-6 = mode low 2 bits = 2 → 0b10 << 5 = 0x40
    // ch: system channel in bits 0-3, bit 6 = mode bit 2 = 0
    // mo: 0x02 = Generic Slave mode
    uint8_t buf[] = {0xF0,
                     0x00,
                     0x01,
                     0x36,
                     kModelId,
                     channel_, // dv
                     0x44,     // Dump command
                     0x00,
                     0x00,
                     0x00,                       // address: global config
                     0x40,                       // md: mode bits 5-6 = 2
                     (uint8_t)(channel_ & 0x0F), // ch: system channel
                     0x00,                       // tr: touch response default
                     0x02,                       // mo: Generic Slave mode
                     0xF7};
    SendRaw(buf, sizeof(buf));
}

void SAC2K::RegisterApp(const char vendorId[3], uint8_t verMajor, uint8_t verMinor,
                        const char appId[4]) {
    // F0 00 01 36 2A dv 41 V V V Major Minor SacIfVer State Id Id Id Id F7
    uint8_t buf[] = {0xF0,
                     0x00,
                     0x01,
                     0x36,
                     kModelId,
                     channel_,
                     0x41, // Register Application
                     static_cast<uint8_t>(vendorId[0]),
                     static_cast<uint8_t>(vendorId[1]),
                     static_cast<uint8_t>(vendorId[2]),
                     verMajor,
                     verMinor,
                     0x16, // SAC Interface Version 1.06
                     0x01, // State: Startup
                     static_cast<uint8_t>(appId[0]),
                     static_cast<uint8_t>(appId[1]),
                     static_cast<uint8_t>(appId[2]),
                     static_cast<uint8_t>(appId[3]),
                     0xF7};
    SendRaw(buf, sizeof(buf));
}

void SAC2K::RequestIdentity() {
    // Universal SysEx Identity Request
    // F0 7E dv 06 01 F7
    uint8_t buf[] = {0xF0, 0x7E, channel_, 0x06, 0x01, 0xF7};
    SendRaw(buf, sizeof(buf));
}

// ============================================================
// Low-level MIDI send helpers
// ============================================================

void SAC2K::SendCC(uint8_t cc, uint8_t val) {
    uint8_t msg[3] = {static_cast<uint8_t>(0xB0 | (channel_ & 0x0F)),
                      static_cast<uint8_t>(cc & 0x7F), static_cast<uint8_t>(val & 0x7F)};
    SendRaw(msg, 3);
}

void SAC2K::SendAftertouch(uint8_t cc, uint8_t val) {
    // Polyphonic Aftertouch (0xAx) is used for Enhanced Encoder Control
    uint8_t msg[3] = {static_cast<uint8_t>(0xA0 | (channel_ & 0x0F)),
                      static_cast<uint8_t>(cc & 0x7F), static_cast<uint8_t>(val & 0x7F)};
    SendRaw(msg, 3);
}

void SAC2K::SendRaw(const uint8_t *data, size_t len) {
    if (trs_)
        trs_->SendMessage(const_cast<uint8_t *>(data), len);
    if (usb_)
        usb_->SendMessage(const_cast<uint8_t *>(data), len);
}

} // namespace sac2k
