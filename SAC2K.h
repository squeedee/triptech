#pragma once
// ============================================================
// SAC2K.h — Radikal Technologies SAC-2K v3 driver for Daisy Seed
// ============================================================
//
// Operates the SAC-2K in Generic Slave mode (mode 2) where
// the Daisy controls all visual feedback: LCD displays, encoder
// LED rings, button LEDs, and motor fader positions.
//
// The SAC sends encoder deltas, fader positions, button
// press/release, and fader touch events via standard MIDI CC.
//
// Usage:
//   1. Create a SAC2K instance
//   2. Call Init() with one or two MidiHandler pointers
//   3. In your main loop, call Process() to drain incoming events
//   4. Register callbacks for encoders, faders, buttons, touch
//   5. Use Set*() methods to update displays, LEDs, faders, rings
//
// See README-SAC2K.md for SAC-2K hardware configuration.
// ============================================================

#include <cstdint>
#include <cstring>

// Forward-declare the template so we don't pull in all of libDaisy
namespace daisy {
template <typename Transport> class MidiHandler;
class MidiUartTransport;
class MidiUsbTransport;
struct MidiEvent;
} // namespace daisy

namespace sac2k {

// ============================================================
// Constants
// ============================================================

static constexpr int NUM_FADERS = 9; // 8 channels + 1 master
static constexpr int NUM_ENCODERS = 12;
static constexpr int NUM_DISPLAYS = 3;
static constexpr int DISPLAY_COLS = 40;
static constexpr int DISPLAY_ROWS = 2;

// SysEx header: F0 00 01 36 2A
static constexpr uint8_t kSysExHeader[] = {0xF0, 0x00, 0x01, 0x36, 0x2A};
static constexpr uint8_t kSysExEnd = 0xF7;
static constexpr uint8_t kModelId = 0x2A;

// ============================================================
// Encoder LED ring display modes (Enhanced Encoder Control)
// ============================================================

enum class EncoderMode : uint8_t {
    Off = 0x00,
    Steps128 = 0x01,   // 128 steps, halftone dot
    Steps100 = 0x02,   // 100 steps, halftone dot
    Steps64 = 0x03,    // 64 steps, halftone dot
    Steps32 = 0x04,    // 32 steps, maps to 31 LEDs
    Steps16 = 0x05,    // 16 steps, 2 LEDs per step
    Steps8 = 0x06,     // 8 steps, 4 LEDs per step
    Steps4 = 0x07,     // 4 steps, 8 LEDs per step
    Volume128 = 0x09,  // 128 steps, band/bar display
    Volume32 = 0x0A,   // 32 steps, band/bar display
    Volume16 = 0x0B,   // 16 steps, band/bar display
    Pan128 = 0x0C,     // 128 steps, centered band
    Pan32 = 0x0D,      // 32 steps, centered band
    Pan16 = 0x0E,      // 16 steps, centered band
    Pan2 = 0x0F,       // 2 steps, left/right band
    Band128 = 0x10,    // 128 steps, centered band (V1.05)
    Band32 = 0x11,     // 32 steps, centered band (V1.05)
    Band16 = 0x12,     // 16 steps, centered band (V1.05)
    Band2 = 0x13,      // 2 steps, center dot/full (V1.05)
    Damping128 = 0x14, // 128 steps, left-turned band (V1.05)
    Damping32 = 0x15,  // 32 steps, left-turned band (V1.05)
    Damping16 = 0x16,  // 16 steps, left-turned band (V1.05)
    Damping2 = 0x17,   // 2 steps, right dot/full (V1.05)
};

// Blink modifiers — OR with EncoderMode (V1.05)
static constexpr uint8_t kEncBlink = 0x20;
static constexpr uint8_t kEncFastBlink = 0x40;
static constexpr uint8_t kEncFlash = 0x60;

// ============================================================
// Button LED states
// ============================================================

enum class LedState : uint8_t {
    Off = 0x00,
    On = 0x01,
    Blink = 0x02,
    // V1.02+ extended states (set bit 5 to use 4-bit pattern)
    ExtOff = 0x20,
    ExtOn = 0x2F,
    ExtBlink = 0x2C,
    ExtQBlink = 0x25,
    ExtFlash = 0x21,
};

// ============================================================
// Button IDs (CC data byte 2 values in slave mode)
// These are the CC number byte 2 that the SAC sends/receives
// ============================================================

enum class Button : uint8_t {
    Solo = 0x00,
    Mute1 = 0x01,
    Mute2 = 0x02,
    Mute3 = 0x03,
    Mute4 = 0x04,
    Mute5 = 0x05,
    Mute6 = 0x06,
    Mute7 = 0x07,
    Mute8 = 0x08,
    Select1 = 0x09,
    Select2 = 0x0A,
    Select3 = 0x0B,
    Select4 = 0x0C,
    Select5 = 0x0D,
    Select6 = 0x0E,
    Select7 = 0x0F,
    Select8 = 0x10,
    MasterSel = 0x11,
    InsSend = 0x12,
    Pan = 0x13,
    High = 0x14,
    Send1 = 0x15,
    HiMid = 0x16,
    Send2 = 0x17,
    LowMid = 0x18,
    Send3 = 0x19,
    Low = 0x1A,
    Send4 = 0x1B,
    Audio = 0x1C,
    Midi = 0x1D,
    Input = 0x1E,
    Inst = 0x1F,
    Bus = 0x20,
    Group = 0x21,
    Bank1_8 = 0x22,
    Bank9_16 = 0x23,
    Bank17_24 = 0x24,
    Bank25_32 = 0x25,
    System = 0x26,
    ChannelEQ = 0x27,
    InsertSends = 0x28,
    Dynamics = 0x29,
    MidiBtns = 0x2A,
    Instrument = 0x2B,
    RecallMark = 0x2C,
    JumpTo = 0x2D,
    StoreMark = 0x2E,
    JumpFrom = 0x2F,
    Scrub = 0x30,
    Rewind = 0x31,
    Forward = 0x32,
    Stop = 0x33,
    Play = 0x34,
    Record = 0x35,
    Num = 0x36,
    Enter = 0x37,
    Shift = 0x38,
    Key1 = 0x39,
    Key2 = 0x3A,
    Key3 = 0x3B,
    Key4 = 0x3C,
    Key5 = 0x3D,
    Key6 = 0x3E,
    Key7 = 0x3F,
    Key8 = 0x40,
    Key9 = 0x41,
    Key10 = 0x42,
    EncPush1 = 0x43,
    EncPush2 = 0x44,
    EncPush3 = 0x45,
    EncPush4 = 0x46,
    EncPush5 = 0x47,
    EncPush6 = 0x48,
    EncPush7 = 0x49,
    EncPush8 = 0x4A,
    EncPush9 = 0x4B,
    EncPush10 = 0x4C,
    EncPush11 = 0x4D,
    EncPush12 = 0x4E,
};

// ============================================================
// Callback signatures
// ============================================================

// Encoder rotation: encoder 0-11, delta -63..+63
using EncoderCb = void (*)(uint8_t encoder, int8_t delta, void *ctx);

// Fader moved: fader 0-8 (8=master), value 0-127
using FaderCb = void (*)(uint8_t fader, uint8_t value, void *ctx);

// Button press/release: button ID, state (0=release, 1=press, 2=shift+press)
using ButtonCb = void (*)(Button button, uint8_t state, void *ctx);

// Fader touch: fader 0-8 (8=master), touched (true/false)
using TouchCb = void (*)(uint8_t fader, bool touched, void *ctx);

// Jog wheel: delta -63..+63
using JogCb = void (*)(int8_t delta, void *ctx);

// ============================================================
// SAC2K class
// ============================================================

class SAC2K {
  public:
    SAC2K() = default;

    // ----------------------------------------------------------
    // Initialization
    // ----------------------------------------------------------

    struct Config {
        uint8_t channel = 0; // MIDI channel 0-15 (must match SAC hardware setting)
    };

    // Initialize with one or both MIDI transports.
    // Pass nullptr for either if not used.
    void Init(const Config &cfg, daisy::MidiHandler<daisy::MidiUartTransport> *trs = nullptr,
              daisy::MidiHandler<daisy::MidiUsbTransport> *usb = nullptr);

    // ----------------------------------------------------------
    // Main loop — call every iteration
    // ----------------------------------------------------------

    // Processes all pending MIDI events from both transports.
    // Dispatches registered callbacks for SAC-2K messages.
    // Non-SAC messages are ignored.
    void Process();

    // ----------------------------------------------------------
    // Callbacks
    // ----------------------------------------------------------

    void SetEncoderCallback(EncoderCb cb, void *ctx = nullptr) {
        encoder_cb_ = cb;
        encoder_ctx_ = ctx;
    }
    void SetFaderCallback(FaderCb cb, void *ctx = nullptr) {
        fader_cb_ = cb;
        fader_ctx_ = ctx;
    }
    void SetButtonCallback(ButtonCb cb, void *ctx = nullptr) {
        button_cb_ = cb;
        button_ctx_ = ctx;
    }
    void SetTouchCallback(TouchCb cb, void *ctx = nullptr) {
        touch_cb_ = cb;
        touch_ctx_ = ctx;
    }
    void SetJogCallback(JogCb cb, void *ctx = nullptr) {
        jog_cb_ = cb;
        jog_ctx_ = ctx;
    }

    // ----------------------------------------------------------
    // Motor faders — set target position (fader will move)
    // ----------------------------------------------------------

    // fader: 0-7 = channels, 8 = master. value: 0-127
    void SetFader(uint8_t fader, uint8_t value);

    // ----------------------------------------------------------
    // Encoder LED rings
    // ----------------------------------------------------------

    // Set the display mode for an encoder's LED ring.
    // encoder: 0-11
    void SetEncoderMode(uint8_t encoder, EncoderMode mode, uint8_t blinkMod = 0);

    // Set the value displayed on an encoder's LED ring.
    // encoder: 0-11. value: 0-127 (range depends on mode)
    void SetEncoderValue(uint8_t encoder, uint8_t value);

    // Convenience: set both mode and value at once
    void SetEncoder(uint8_t encoder, EncoderMode mode, uint8_t value, uint8_t blinkMod = 0);

    // Turn off all encoder LEDs for one encoder
    void SetEncoderOff(uint8_t encoder);

    // ----------------------------------------------------------
    // Button LEDs
    // ----------------------------------------------------------

    void SetButtonLed(Button button, LedState state);

    // ----------------------------------------------------------
    // LCD Displays — 3 displays, 2 lines x 40 chars each
    // ----------------------------------------------------------

    // Set cursor position on a display.
    // display: 0-2. col: 0-39. row: 0-1.
    void SetDisplayCursor(uint8_t display, uint8_t col, uint8_t row = 0);

    // Write a character at the current cursor (auto-increments)
    void WriteDisplayChar(uint8_t display, char c);

    // Clear a display
    void ClearDisplay(uint8_t display);

    // Write a null-terminated string at (col, row). Truncates at 40 chars.
    void WriteDisplayString(uint8_t display, uint8_t col, uint8_t row, const char *str);

    // Write a full line (padded/truncated to 40 chars)
    void WriteDisplayLine(uint8_t display, uint8_t row, const char *str);

    // Write text to a display via SysEx (can write across line boundary).
    // display: 0-2. col: 0-39. row: 0-1.
    void WriteDisplaySysEx(uint8_t display, uint8_t col, uint8_t row, const char *str);

    // ----------------------------------------------------------
    // Level meters (in-display, uses custom characters)
    // ----------------------------------------------------------

    // Display in-display level meter.
    // display: 0-2. position: 1-4 (meter position). value: 0-15
    void SetLevelMeter(uint8_t display, uint8_t position, uint8_t value);

    // ----------------------------------------------------------
    // Time display (7-segment LED)
    // ----------------------------------------------------------

    // Write an ASCII string to the time display (mode 2: ASCII mode).
    // Up to 10 chars, right-to-left auto-increment.
    void WriteTimeDisplay(const char *str);

    // ----------------------------------------------------------
    // System / configuration
    // ----------------------------------------------------------

    // Send the SysEx to put the SAC into slave mode (mode 2).
    // Call once at startup if the SAC isn't already configured.
    void SendSlaveMode();

    // Send application registration SysEx.
    // vendorId: 3 bytes, appId: 4 bytes (ASCII identifiers)
    void RegisterApp(const char vendorId[3], uint8_t verMajor, uint8_t verMinor,
                     const char appId[4]);

    // Request identity from SAC (Universal SysEx Identity Request)
    void RequestIdentity();

  private:
    // MIDI channel (0-15)
    uint8_t channel_ = 0;

    // Transport pointers (either or both may be null)
    daisy::MidiHandler<daisy::MidiUartTransport> *trs_ = nullptr;
    daisy::MidiHandler<daisy::MidiUsbTransport> *usb_ = nullptr;

    // Callbacks
    EncoderCb encoder_cb_ = nullptr;
    FaderCb fader_cb_ = nullptr;
    ButtonCb button_cb_ = nullptr;
    TouchCb touch_cb_ = nullptr;
    JogCb jog_cb_ = nullptr;
    void *encoder_ctx_ = nullptr;
    void *fader_ctx_ = nullptr;
    void *button_ctx_ = nullptr;
    void *touch_ctx_ = nullptr;
    void *jog_ctx_ = nullptr;

    // Internal helpers
    void HandleEvent(const daisy::MidiEvent &evt);
    void HandleCC(uint8_t cc, uint8_t val);
    void SendCC(uint8_t cc, uint8_t val);
    void SendAftertouch(uint8_t cc, uint8_t val); // Aftertouch used for Enhanced Encoder Control
    void SendRaw(const uint8_t *data, size_t len);

    // CC number mappings (slave mode)
    static constexpr uint8_t kFaderMsbBase = 0x17; // Fader#1 MSB = CC 23
    static constexpr uint8_t kFaderLsbBase = 0x37; // Fader#1 LSB = CC 55
    static constexpr uint8_t kEncoderBase = 0x53;  // Encoder#1 = CC 83
    static constexpr uint8_t kJogCC = 0x5F;        // Jog = CC 95
    static constexpr uint8_t kTouchBase = 0x77;    // Fader#1 touch = CC 119

    // Display CC bases (per display: cursor, write, special)
    static constexpr uint8_t kDispCursorCC[3] = {0x70, 0x73, 0x76};
    static constexpr uint8_t kDispWriteCC[3] = {0x71, 0x74, 0x77};
    static constexpr uint8_t kDispSpecCC[3] = {0x72, 0x75, 0x78};

    // Level meter CC per display
    static constexpr uint8_t kLevelCC[3] = {0x6D, 0x6E, 0x6F};

    // Time display CCs
    static constexpr uint8_t kTimeDigitCC = 0x67;
    static constexpr uint8_t kTimeCharCC = 0x68;
};

} // namespace sac2k
