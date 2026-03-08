// ============================================================
// example-sac2k.cpp — Example: using SAC-2K with Triptech
// ============================================================
//
// This sketch demonstrates mapping the SAC-2K's 9 faders, 12 encoders,
// and 3 displays to Triptech's parameter set. It shows how to:
//
// - Map faders to per-channel levels + master dry
// - Map encoders to filter/envelope/delay parameters
// - Update displays with parameter names and values
// - Handle fader touch to avoid motor fighting
// - Use button LEDs for channel selection and transport
//
// This is a REFERENCE EXAMPLE — not compiled into the main firmware.
// To use: rename to main.cpp or merge the SAC integration into Triptech.cpp.
// ============================================================

#include "daisy_seed.h"
#include "daisysp.h"
#include "SAC2K.h"
#include <cstdio> // snprintf

using namespace daisy;
using namespace sac2k;

// ============================================================
// Hardware
// ============================================================

static DaisySeed seed;
static MidiUartHandler trs_midi;
static MidiUsbHandler usb_midi;
static SAC2K sac;

// ============================================================
// Parameter storage (simplified from Triptech)
// ============================================================

struct ChannelParams {
    uint8_t cutoff = 64;  // 0-127 (log: 100 Hz – 20 kHz)
    uint8_t q = 64;       // 0-127 (lin: 0 – 0.95)
    uint8_t drive = 0;    // 0-127 (lin: 1x – 4x)
    uint8_t attack = 10;  // 0-127 (log: 1 ms – 2 s)
    uint8_t decay = 40;   // 0-127 (log: 10 ms – 2 s)
    uint8_t level = 90;   // 0-127 (lin: 0 – 1)
    uint8_t pan = 64;     // 0-127 (lin: L – R, 64 = center)
    uint8_t lfoAmt = 64;  // 0-127 (bipolar: -1..+1, 64 = 0)
    uint8_t delaySnd = 0; // 0-127 (lin: 0 – 1)
};

static ChannelParams ch[3];
static uint8_t delayTime = 32;
static uint8_t delayFb = 51;   // ~40%
static uint8_t delayWid = 127; // full stereo
static uint8_t dryLevel = 0;
static uint8_t lfoRate = 55;
static uint8_t bpm = 73; // maps to 120 BPM
static uint8_t pattern = 0;
static bool running = false;
static bool bypass = false;

// Currently selected channel for encoder editing (0-2)
static uint8_t selectedCh = 0;

// Fader touch state (true = user is touching, don't send motor updates)
static bool faderTouched[9] = {};

// ============================================================
// Formatters — convert 0-127 to display strings
// ============================================================

static void FmtHz(char *buf, size_t n, uint8_t v) {
    float hz = 100.f * powf(200.f, v / 127.f);
    if (hz >= 1000.f)
        snprintf(buf, n, "%.1fkHz", hz / 1000.f);
    else
        snprintf(buf, n, "%.0f Hz", hz);
}

static void FmtQ(char *buf, size_t n, uint8_t v) { snprintf(buf, n, "%.2f", v / 127.f * 0.95f); }

static void FmtDrive(char *buf, size_t n, uint8_t v) {
    snprintf(buf, n, "%.1fx", 1.f + v / 127.f * 3.f);
}

static void FmtMs(char *buf, size_t n, uint8_t v, float lo, float hi) {
    float sec = lo * powf(hi / lo, v / 127.f);
    if (sec >= 1.f)
        snprintf(buf, n, "%.1f s", sec);
    else
        snprintf(buf, n, "%.0fms", sec * 1000.f);
}

static void FmtPct(char *buf, size_t n, uint8_t v) {
    snprintf(buf, n, "%d%%", (int)(v / 127.f * 100.f));
}

static void FmtPan(char *buf, size_t n, uint8_t v) {
    if (v < 62)
        snprintf(buf, n, "L%d", 64 - v);
    else if (v > 66)
        snprintf(buf, n, "R%d", v - 64);
    else
        snprintf(buf, n, "CTR");
}

static void FmtBipct(char *buf, size_t n, uint8_t v) {
    int pct = (int)((v - 64) / 63.f * 100.f);
    if (pct == 0)
        snprintf(buf, n, "0%%");
    else
        snprintf(buf, n, "%+d%%", pct);
}

// ============================================================
// Display layout:
//   Display 0 (left):   Ch1 params — FREQ Q DRIVE ATK
//   Display 1 (center): Ch2 params — DECAY LEVEL PAN LFOA
//   Display 2 (right):  Ch3 params — DLSND DLTIME DLFB DLWID
//
// Encoders map:
//   Row 1 (enc 0-3):  FREQ, Q, DRIVE, ATK    (active channel)
//   Row 2 (enc 4-7):  DECAY, LEVEL, PAN, LFO AMT
//   Row 3 (enc 8-11): DLY SEND, DLY TIME, DLY FB, DLY WIDTH
//
// Faders:
//   0-2: Ch1-3 level
//   3-5: Ch1-3 delay send
//   6-7: unused (or map to filter freq for Ch1/Ch2)
//   8:   dry/master level
// ============================================================

// Encoder -> parameter mapping
struct EncMapping {
    const char *name;
    uint8_t *param;
    EncoderMode mode;
    void (*fmt)(char *, size_t, uint8_t);
};

// Build encoder mappings for the selected channel
static EncMapping encMap[12];

static void BuildEncMap() {
    auto &c = ch[selectedCh];
    // Row 1: filter + envelope attack
    encMap[0] = {"FREQ", &c.cutoff, EncoderMode::Volume128, FmtHz};
    encMap[1] = {"Q", &c.q, EncoderMode::Volume128, FmtQ};
    encMap[2] = {"DRIVE", &c.drive, EncoderMode::Volume128, FmtDrive};
    encMap[3] = {"ATK", &c.attack, EncoderMode::Volume128, nullptr};
    // Row 2: envelope + modulation
    encMap[4] = {"DECAY", &c.decay, EncoderMode::Volume128, nullptr};
    encMap[5] = {"LEVEL", &c.level, EncoderMode::Volume128, FmtPct};
    encMap[6] = {"PAN", &c.pan, EncoderMode::Pan128, FmtPan};
    encMap[7] = {"LFOAMT", &c.lfoAmt, EncoderMode::Pan128, FmtBipct};
    // Row 3: delay (global, not per-channel except send)
    encMap[8] = {"DLSND", &c.delaySnd, EncoderMode::Volume128, FmtPct};
    encMap[9] = {"DLTIME", &delayTime, EncoderMode::Steps128, nullptr};
    encMap[10] = {"DLFB", &delayFb, EncoderMode::Volume128, FmtPct};
    encMap[11] = {"DLWID", &delayWid, EncoderMode::Volume128, FmtPct};
}

// ============================================================
// Refresh displays + encoder LEDs
// ============================================================

static void RefreshDisplays() {
    char line[42];

    // Display 0: encoder names (row 0) and values (row 1) for enc 0-3
    snprintf(line, sizeof(line), "  %-8s %-8s %-8s %-8s", encMap[0].name, encMap[1].name,
             encMap[2].name, encMap[3].name);
    sac.WriteDisplayLine(0, 0, line);

    char v0[10], v1[10], v2[10], v3[10];
    if (encMap[0].fmt)
        encMap[0].fmt(v0, sizeof(v0), *encMap[0].param);
    else
        FmtMs(v0, sizeof(v0), *encMap[0].param, 0.001f, 2.f);
    if (encMap[1].fmt)
        encMap[1].fmt(v1, sizeof(v1), *encMap[1].param);
    else
        FmtMs(v1, sizeof(v1), *encMap[1].param, 0.001f, 2.f);
    if (encMap[2].fmt)
        encMap[2].fmt(v2, sizeof(v2), *encMap[2].param);
    else
        FmtMs(v2, sizeof(v2), *encMap[2].param, 0.001f, 2.f);
    if (encMap[3].fmt)
        encMap[3].fmt(v3, sizeof(v3), *encMap[3].param);
    else
        FmtMs(v3, sizeof(v3), *encMap[3].param, 0.001f, 2.f);
    snprintf(line, sizeof(line), "  %-8s %-8s %-8s %-8s", v0, v1, v2, v3);
    sac.WriteDisplayLine(0, 1, line);

    // Display 1: enc 4-7
    snprintf(line, sizeof(line), "  %-8s %-8s %-8s %-8s", encMap[4].name, encMap[5].name,
             encMap[6].name, encMap[7].name);
    sac.WriteDisplayLine(1, 0, line);

    if (encMap[4].fmt)
        encMap[4].fmt(v0, sizeof(v0), *encMap[4].param);
    else
        FmtMs(v0, sizeof(v0), *encMap[4].param, 0.01f, 2.f);
    if (encMap[5].fmt)
        encMap[5].fmt(v1, sizeof(v1), *encMap[5].param);
    else
        FmtPct(v1, sizeof(v1), *encMap[5].param);
    if (encMap[6].fmt)
        encMap[6].fmt(v2, sizeof(v2), *encMap[6].param);
    else
        FmtPan(v2, sizeof(v2), *encMap[6].param);
    if (encMap[7].fmt)
        encMap[7].fmt(v3, sizeof(v3), *encMap[7].param);
    else
        FmtBipct(v3, sizeof(v3), *encMap[7].param);
    snprintf(line, sizeof(line), "  %-8s %-8s %-8s %-8s", v0, v1, v2, v3);
    sac.WriteDisplayLine(1, 1, line);

    // Display 2: enc 8-11
    snprintf(line, sizeof(line), "  %-8s %-8s %-8s %-8s", encMap[8].name, encMap[9].name,
             encMap[10].name, encMap[11].name);
    sac.WriteDisplayLine(2, 0, line);

    if (encMap[8].fmt)
        encMap[8].fmt(v0, sizeof(v0), *encMap[8].param);
    else
        FmtPct(v0, sizeof(v0), *encMap[8].param);
    if (encMap[9].fmt)
        encMap[9].fmt(v1, sizeof(v1), *encMap[9].param);
    else
        FmtPct(v1, sizeof(v1), *encMap[9].param);
    if (encMap[10].fmt)
        encMap[10].fmt(v2, sizeof(v2), *encMap[10].param);
    else
        FmtPct(v2, sizeof(v2), *encMap[10].param);
    if (encMap[11].fmt)
        encMap[11].fmt(v3, sizeof(v3), *encMap[11].param);
    else
        FmtPct(v3, sizeof(v3), *encMap[11].param);
    snprintf(line, sizeof(line), "  %-8s %-8s %-8s %-8s", v0, v1, v2, v3);
    sac.WriteDisplayLine(2, 1, line);
}

static void RefreshEncoderLeds() {
    for (int i = 0; i < 12; i++) {
        sac.SetEncoder(i, encMap[i].mode, *encMap[i].param);
    }
}

static void RefreshFaders() {
    // Only send motor commands to faders the user isn't touching
    if (!faderTouched[0])
        sac.SetFader(0, ch[0].level);
    if (!faderTouched[1])
        sac.SetFader(1, ch[1].level);
    if (!faderTouched[2])
        sac.SetFader(2, ch[2].level);
    if (!faderTouched[3])
        sac.SetFader(3, ch[0].delaySnd);
    if (!faderTouched[4])
        sac.SetFader(4, ch[1].delaySnd);
    if (!faderTouched[5])
        sac.SetFader(5, ch[2].delaySnd);
    if (!faderTouched[8])
        sac.SetFader(8, dryLevel);
}

static void RefreshChannelLeds() {
    // Select buttons show active channel
    sac.SetButtonLed(Button::Select1, selectedCh == 0 ? LedState::On : LedState::Off);
    sac.SetButtonLed(Button::Select2, selectedCh == 1 ? LedState::On : LedState::Off);
    sac.SetButtonLed(Button::Select3, selectedCh == 2 ? LedState::On : LedState::Off);

    // Transport LEDs
    sac.SetButtonLed(Button::Play, running ? LedState::On : LedState::Off);
    sac.SetButtonLed(Button::Stop, running ? LedState::Off : LedState::On);
}

// ============================================================
// Callbacks
// ============================================================

void OnEncoder(uint8_t enc, int8_t delta, void *) {
    if (enc >= 12)
        return;
    int val = (int)*encMap[enc].param + delta;
    if (val < 0)
        val = 0;
    if (val > 127)
        val = 127;
    *encMap[enc].param = (uint8_t)val;

    // Update just this encoder's LED ring immediately
    sac.SetEncoderValue(enc, (uint8_t)val);

    // Update the display value for the changed encoder
    // (in production, you'd debounce/throttle display updates)
    RefreshDisplays();
}

void OnFader(uint8_t fader, uint8_t value, void *) {
    switch (fader) {
    case 0:
        ch[0].level = value;
        break;
    case 1:
        ch[1].level = value;
        break;
    case 2:
        ch[2].level = value;
        break;
    case 3:
        ch[0].delaySnd = value;
        break;
    case 4:
        ch[1].delaySnd = value;
        break;
    case 5:
        ch[2].delaySnd = value;
        break;
    case 8:
        dryLevel = value;
        break;
    default:
        break;
    }
}

void OnButton(Button btn, uint8_t state, void *) {
    if (state == 0)
        return; // ignore release

    switch (btn) {
    case Button::Select1:
        selectedCh = 0;
        BuildEncMap();
        RefreshDisplays();
        RefreshEncoderLeds();
        RefreshChannelLeds();
        break;
    case Button::Select2:
        selectedCh = 1;
        BuildEncMap();
        RefreshDisplays();
        RefreshEncoderLeds();
        RefreshChannelLeds();
        break;
    case Button::Select3:
        selectedCh = 2;
        BuildEncMap();
        RefreshDisplays();
        RefreshEncoderLeds();
        RefreshChannelLeds();
        break;
    case Button::Play:
        running = true;
        RefreshChannelLeds();
        break;
    case Button::Stop:
        running = false;
        RefreshChannelLeds();
        break;
    default:
        break;
    }
}

void OnTouch(uint8_t fader, bool touched, void *) {
    if (fader < 9) {
        faderTouched[fader] = touched;
    }
}

// ============================================================
// Main
// ============================================================

int main() {
    seed.Init();

    // --- TRS MIDI ---
    MidiUartHandler::Config trs_cfg;
    trs_cfg.transport_config.rx = seed.GetPin(15);
    trs_cfg.transport_config.tx = seed.GetPin(14);
    trs_midi.Init(trs_cfg);
    trs_midi.StartReceive();

    // --- USB MIDI ---
    MidiUsbHandler::Config usb_cfg;
    usb_cfg.transport_config.periph = MidiUsbTransport::Config::INTERNAL;
    usb_midi.Init(usb_cfg);
    usb_midi.StartReceive();

    // --- SAC-2K ---
    SAC2K::Config sac_cfg;
    sac_cfg.channel = 0; // MIDI channel 1

    sac.Init(sac_cfg, &trs_midi, &usb_midi);
    sac.SetEncoderCallback(OnEncoder);
    sac.SetFaderCallback(OnFader);
    sac.SetButtonCallback(OnButton);
    sac.SetTouchCallback(OnTouch);

    // Configure SAC: slave mode + register
    sac.SendSlaveMode();
    sac.RegisterApp("DAI", 1, 0, "Trip");

    // Build parameter mapping and initial UI
    BuildEncMap();
    for (int d = 0; d < 3; d++)
        sac.ClearDisplay(d);
    RefreshDisplays();
    RefreshEncoderLeds();
    RefreshFaders();
    RefreshChannelLeds();

    // --- Main loop ---
    while (true) {
        trs_midi.Listen();
        usb_midi.Listen();
        sac.Process();

        // Periodically refresh fader positions (e.g., after preset load)
        // In production, only send on parameter change + not touched.
    }
}
