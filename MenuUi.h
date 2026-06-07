// MenuUi.h — on-device menu UI module (240x320 ILI9341 TFT + 4 encoders).
//
// The entire menu surface: the parameter/band model, encoder input handling, and
// framebuffer rendering. Every edit routes through parammap::Handle()/SendCC() so
// the CC map stays the single source of truth. Runs from the main loop only — never
// the audio callback. Behavior over the shared preset/transport state + UI prefs.
#pragma once

#include "Hardware.h"
#include "Ili9341.h"
#include "MidiOut.h"
#include "Model.h"
#include "Mux4067.h"
#include "ParamMap.h"
#include "Persistence.h"
#include "State.h"

namespace menu {
using namespace daisy;
using namespace daisysp;

// --- Menu UI state (240x320 TFT + 4 encoders). Driven from the main loop
//     only — see the "Menu UI" section below. Other modules (MidiRouter,
//     Telemetry) reach in via menu:: to flag the screen dirty. ---
static const char *const CH_NAME[NUM_CH] = {"CH1", "CH2", "CH3"};
static uint8_t ui_ctx = 0;         // 0..2 = channel, 3 = global
static uint8_t ui_sec = 0;         // section within the current context
static uint8_t ui_patch_sel = 0;   // patch slot highlighted on the PATCH page
static bool ui_settings = false;   // hidden settings mode (hold NAV + click E1)
static bool ui_vu = false;         // hidden I/O VU meter (hold NAV + click E2)
static bool ui_mix = false;        // mixer page: level/mute/solo (hold NAV + click E3)
static bool ui_full_dirty = true;  // full-screen redraw pending
static bool ui_foot_dirty = false; // footer (BPM) redraw pending
static bool ui_band_dirty[3] = {true, true, true};

// ============================================================
// Menu UI — 240x320 ILI9341 TFT + 4 PEC11H encoders (CD74HC4067 mux)
//
// A port of menu-controller.html to the device. Layout: a top bar, three
// horizontal "bands", and a footer. The NAV encoder (below the screen) selects
// the context (CH1/CH2/CH3/GLOBAL) and the section within it; the three
// right-hand encoders (E1/E2/E3) each edit the parameter shown in their band.
//
// IMPORTANT: everything here runs from the main loop, never the audio callback.
// Every edit routes through parammap::Handle()/SendCC(), so the MIDI map stays the one
// source of truth and external controllers stay in sync.
//
// Pin map comes from the `menu` board (Seed GPIO: display on D7/D8/D9/D10/D17/
// D20, mux on D9/D15/D18/D19/D21). The firmware targets a bare DaisySeed (Daisy
// Studio carrier), so nothing else claims these pins. (It is NOT DaisyPod-
// compatible: the Pod uses D17-D21 for its LEDs/pots, which would fight the
// display and mux.)
// ============================================================

static Ili9341 tft;
static Mux4067 mux;

// --- Quadrature decode + button debounce (from menu/firmware encoder-test) ---
static const int8_t kQuadLut[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

// Quadrature decoder for one encoder. Feed the raw A/B levels each scan; Update
// returns +1/-1 per detent (4 quarter-steps) or 0 between detents.
struct Quad {
    uint8_t prev = 0;
    int8_t accum = 0;
    int Update(uint8_t a, uint8_t b) {
        uint8_t s = (uint8_t)((a << 1) | b);
        accum += kQuadLut[(prev << 2) | s];
        prev = s;
        if (accum >= 4) {
            accum -= 4;
            return +1;
        }
        if (accum <= -4) {
            accum += 4;
            return -1;
        }
        return 0;
    }
};

// Debounce for one encoder push-button. Update returns true once on a confirmed
// press edge, after the raw level has held steady for kStable scans.
struct EncBtn {
    static constexpr uint8_t kStable = 5; // ~5 ms at a 1 kHz scan
    bool state = false;
    uint8_t cnt = 0;
    bool Update(bool raw) { // returns true on a press edge
        if (raw == state) {
            cnt = 0;
            return false;
        }
        if (++cnt >= kStable) {
            state = raw;
            cnt = 0;
            return state;
        }
        return false;
    }
};

static Quad q_enc[4];
static EncBtn b_enc[4];

// Physical mux-encoder index per role. Mux channels: A=3i, B=3i+1, SW=3i+2.
// The three value encoders are the right-hand column (mux 0/1/2, top to bottom);
// the NAV/menu encoder sits below the screen and is the last group (mux 3).
enum { ENC_E1 = 0, ENC_E2 = 1, ENC_E3 = 2, ENC_NAV = 3 };

// --- Parameter model (mirrors the ROWS table in menu-controller.html) ---
// B_COL_* / B_BRIGHT are settings-mode bands; for those `cc` holds the colour
// index (0..3 = CH1/CH2/CH3/GLOBAL) rather than a MIDI CC.
enum BandKind : uint8_t {
    B_CONT,
    B_ENUM,
    B_TOGGLE,
    B_PATIDX,
    B_PATCH,
    B_ACTION,
    B_COL_R,
    B_COL_G,
    B_COL_B,
    B_BRIGHT,
};
enum Fmt : uint8_t {
    F_NONE,
    F_CUTOFF,
    F_RES,
    F_DRIVE,
    F_LEVEL,
    F_ATTACK,
    F_DECAY,
    F_PAN,
    F_LFORATE,
    F_PCT,
    F_BIPCT,
    F_DELAYTIME,
    F_CLOCKDIV,
    F_BPM
};
enum Push : uint8_t {
    P_NONE,
    P_MUTE,
    P_LFOSYNC,
    P_DELAYSYNC,
    P_RUN,
    P_TAP,
    P_BYPASS,
    P_LOAD,
    P_SAVE,
    P_SYNC,
    P_RESET // click resets the band to a value held in its muteCh field
};

struct Band {
    const char *label;
    uint8_t kind;
    uint8_t cc;     // absolute CC, or a per-channel offset when chRel != 0
    uint8_t chRel;  // 1 = cc is an offset added to kCcBase[ctx]
    uint8_t fmt;    // value formatter (B_CONT)
    uint8_t bip;    // bipolar bar (centred)
    uint8_t push;   // encoder-press action
    uint8_t lsbOff; // 14-bit LSB offset (255 = none)
    uint8_t muteCh; // channel index for P_MUTE bands
    const char *opts[4];
    uint8_t optVal[4];
    uint8_t nOpt;
};
struct Section {
    const char *name;
    const Band *bands;
    uint8_t n;
};

// label,kind,cc,chRel,fmt,bip,push,lsbOff,muteCh,opts,optVal,nOpt
static const Band kFilter[] = {
    {"TYPE",
     B_ENUM,
     9,
     1,
     F_NONE,
     0,
     P_NONE,
     255,
     0,
     {"LP", "BP", "HP", "NOTCH"},
     {0, 32, 64, 96},
     4},
    {"SLOPE",
     B_ENUM,
     10,
     1,
     F_NONE,
     0,
     P_NONE,
     255,
     0,
     {"6DB", "12DB", "24DB", 0},
     {0, 63, 126, 0},
     3},
    {"FREQ", B_CONT, 0, 1, F_CUTOFF, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kTone[] = {
    {"Q", B_CONT, 1, 1, F_RES, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"DRIVE", B_CONT, 2, 1, F_DRIVE, 0, P_RESET, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"LFO>FILT", B_CONT, 8, 1, F_BIPCT, 1, P_RESET, 12, 64, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kEnv[] = {
    {"ATTACK", B_CONT, 4, 1, F_ATTACK, 0, P_RESET, 255, 27, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"DECAY", B_CONT, 5, 1, F_DECAY, 0, P_RESET, 255, 63, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"LEVEL/MUTE", B_CONT, 6, 1, F_LEVEL, 0, P_MUTE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kLfo[] = {
    {"SHAPE",
     B_ENUM,
     11,
     1,
     F_NONE,
     0,
     P_LFOSYNC,
     255,
     0,
     {"TRI", "SIN", "SQ", 0},
     {0, 21, 42, 0},
     3},
    {"RATE", B_CONT, 3, 1, F_LFORATE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"DUTY", B_CONT, 14, 1, F_PCT, 0, P_RESET, 255, 64, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kOut[] = {
    {"PAN", B_CONT, 7, 1, F_PAN, 0, P_RESET, 255, 64, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"AMP MOD", B_CONT, 15, 1, F_BIPCT, 1, P_RESET, 255, 64, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"DLY SEND", B_CONT, 13, 1, F_LEVEL, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Section kChSections[] = {
    {"FILTER", kFilter, 3}, {"TONE", kTone, 3}, {"ENV", kEnv, 3},
    {"LFO", kLfo, 3},       {"OUT", kOut, 3},
};

static const Band kSeq[] = {
    {"PATTERN", B_PATIDX, 14, 0, F_NONE, 0, P_RUN, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"CLK DIV", B_CONT, 5, 0, F_CLOCKDIV, 0, P_RESET, 255, 64, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"BPM", B_CONT, 19, 0, F_BPM, 0, P_TAP, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kDelay[] = {
    {"TIME", B_CONT, 1, 0, F_DELAYTIME, 0, P_DELAYSYNC, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"FDBK", B_CONT, 2, 0, F_LEVEL, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"WIDTH", B_CONT, 3, 0, F_LEVEL, 0, P_RESET, 255, 127, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
// Mixer page bands: per-channel level (turn), mute (click), solo (NAV + click).
// Reached via NAV-hold + click E3; not part of the GLOBAL section list.
static const Band kMix[] = {
    {"CH1 LVL", B_CONT, 26, 0, F_LEVEL, 0, P_MUTE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"CH2 LVL", B_CONT, 42, 0, F_LEVEL, 0, P_MUTE, 255, 1, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"CH3 LVL", B_CONT, 58, 0, F_LEVEL, 0, P_MUTE, 255, 2, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Section kMixSections[] = {
    {"MIXER", kMix, 3},
};
static const Band kMaster[] = {
    {"DRY", B_CONT, 4, 0, F_LEVEL, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"BYPASS", B_TOGGLE, 18, 0, F_NONE, 0, P_BYPASS, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"RUN", B_TOGGLE, 15, 0, F_NONE, 0, P_RUN, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kPatch[] = {
    {"PATCH", B_PATCH, 0, 0, F_NONE, 0, P_LOAD, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"SAVE", B_ACTION, 0, 0, F_NONE, 0, P_SAVE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"STATE", B_ACTION, 0, 0, F_NONE, 0, P_SYNC, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Section kGlSections[] = {
    {"SEQ", kSeq, 3},
    {"DELAY", kDelay, 3},
    {"MASTER", kMaster, 3},
    {"PATCH", kPatch, 3},
};

// Hidden settings mode. Colour pages carry the colour index in `cc` (0..3).
static const Band kColCh1[] = {
    {"RED", B_COL_R, 0, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"GREEN", B_COL_G, 0, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"BLUE", B_COL_B, 0, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kColCh2[] = {
    {"RED", B_COL_R, 1, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"GREEN", B_COL_G, 1, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"BLUE", B_COL_B, 1, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kColCh3[] = {
    {"RED", B_COL_R, 2, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"GREEN", B_COL_G, 2, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"BLUE", B_COL_B, 2, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kColGbl[] = {
    {"RED", B_COL_R, 3, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"GREEN", B_COL_G, 3, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
    {"BLUE", B_COL_B, 3, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Band kDisplay[] = {
    {"BRIGHT", B_BRIGHT, 0, 0, F_NONE, 0, P_NONE, 255, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
};
static const Section kSetSections[] = {
    {"CH1 COL", kColCh1, 3}, {"CH2 COL", kColCh2, 3},  {"CH3 COL", kColCh3, 3},
    {"GBL COL", kColGbl, 3}, {"DISPLAY", kDisplay, 1},
};

// Section counts per page table (channels/settings have 5, global 4, mixer 1).
static constexpr uint8_t kNChSec = sizeof(kChSections) / sizeof(kChSections[0]);
static constexpr uint8_t kNGlSec = sizeof(kGlSections) / sizeof(kGlSections[0]);
static constexpr uint8_t kNSetSec = sizeof(kSetSections) / sizeof(kSetSections[0]);
static constexpr uint8_t kNMixSec = sizeof(kMixSections) / sizeof(kMixSections[0]);

static const char *const kDivName[8] = {"1/2", "1/2T", "1/4",  "1/4T",
                                        "1/8", "1/8T", "1/16", "1/16T"};
static const char *const kClockName[9] = {"/4", "/3", "/2", "/1.5", "X1", "X1.5", "X2", "X3", "X4"};

// --- colours (RGB565) ---
static constexpr uint16_t kAccent = 0x4C7F; // fixed accent (sync badges etc.)
static constexpr uint16_t kPanel = 0x1082;
static constexpr uint16_t kPanel2 = 0x0841;
static constexpr uint16_t kHdrBg = 0x2104;
static constexpr uint16_t kFootBg = 0x0841;
static constexpr uint16_t kDimCol = 0x52AA;
static constexpr uint16_t kTxt = 0xFFFF;

// Editable channel/global colours (CH1=green, CH2=yellow, CH3=purple,
// GLOBAL=blue) + backlight level. Persisted in QSPI (see uiStore).
static const uint16_t kDefaultColor[4] = {0x07E0, 0xFFE0, 0x801F, 0x4C7F};
static uint16_t ui_color[4] = {0x07E0, 0xFFE0, 0x801F, 0x4C7F};
static uint8_t ui_brightness = 255;

// --- Current-page accessors. The visible page is (mode, context, section):
//     settings mode picks kSetSections; otherwise channels 0–2 use kChSections
//     and context 3 (GLOBAL) uses kGlSections. ---

// The section table for the active mode/context.
static const Section *ui_sectionTbl() {
    if (ui_mix)
        return kMixSections;
    return ui_settings ? kSetSections : (ui_ctx < 3 ? kChSections : kGlSections);
}
// How many sections the active table has (global has one fewer than the channels).
static uint8_t ui_numSec() {
    if (ui_mix)
        return kNMixSec;
    if (ui_settings)
        return kNSetSec;
    return ui_ctx < 3 ? kNChSec : kNGlSec;
}
// The section currently on screen.
static const Section &ui_cur() { return ui_sectionTbl()[ui_sec]; }
static uint16_t ui_col() {
    if (ui_mix)
        return kAccent;
    if (ui_settings) {
        const Band &b0 = ui_cur().bands[0];
        if (b0.kind == B_COL_R) // colour page: preview the colour being edited
            return ui_color[b0.cc];
        return kAccent;
    }
    return ui_color[ui_ctx < 3 ? ui_ctx : 3];
}
// Resolve a band's CC to an absolute number: per-channel bands (chRel) add the
// current context's kCcBase; global bands use b.cc verbatim.
static uint8_t ui_absCC(const Band &b) {
    return b.chRel ? (uint8_t)(kCcBase[ui_ctx] + b.cc) : b.cc;
}

// For an enum band, pick the option index whose optVal is closest to the live CC.
static uint8_t ui_enumIdx(const Band &b) {
    int raw = parammap::Get(ui_absCC(b));
    if (b.push == P_LFOSYNC)
        raw &= 0x3F; // mask the sync bit out of the shape byte
    uint8_t best = 0;
    int bd = 1000;
    for (uint8_t i = 0; i < b.nOpt; i++) {
        int d = raw - (int)b.optVal[i];
        if (d < 0)
            d = -d;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

// --- tiny string builders. Hand-rolled to avoid linking newlib's printf,
//     which alone overflows the 128 KB internal flash. Glyphs are limited to
//     what the 5x7 font provides (uppercase, digits, . - : / % + >). ---
// Append string `s`; returns the new write cursor.
static char *ui_puts(char *p, const char *s) {
    while (*s)
        *p++ = *s++;
    return p;
}
// Append signed integer `v` in base 10; returns the new write cursor.
static char *ui_putl(char *p, long v) {
    if (v < 0) {
        *p++ = '-';
        v = -v;
    }
    char tmp[12];
    int n = 0;
    if (v == 0)
        tmp[n++] = '0';
    while (v) {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (n)
        *p++ = tmp[--n];
    return p;
}
// `v` rounded to `dec` decimal places.
static char *ui_putf(char *p, float v, int dec) {
    if (v < 0.f) {
        *p++ = '-';
        v = -v;
    }
    long scale = 1;
    for (int i = 0; i < dec; i++)
        scale *= 10;
    long n = (long)(v * scale + 0.5f);
    p = ui_putl(p, n / scale);
    if (dec > 0) {
        *p++ = '.';
        for (long div = scale / 10; div > 0; div /= 10)
            *p++ = (char)('0' + (n / div) % 10);
    }
    return p;
}

// Render a band's current value into `out` as a display string, applying the
// band's formatter (Hz/kHz, ms/s, %, BPM, pan, clock division, …).
static void ui_text(const Band &b, char *out) {
    char *p = out;
    if (b.kind == B_ENUM) {
        const char *s = b.opts[ui_enumIdx(b)];
        p = ui_puts(p, s ? s : "");
        *p = 0;
        return;
    }
    if (b.kind == B_TOGGLE) {
        p = ui_puts(p, parammap::Get(b.cc) >= 64 ? "ON" : "OFF");
        *p = 0;
        return;
    }
    if (b.kind == B_PATIDX) {
        p = ui_puts(p, "PAT ");
        p = ui_putl(p, preset.pattern + 1);
        *p++ = '/';
        p = ui_putl(p, NUM_PATTERNS);
        *p = 0;
        return;
    }
    if (b.kind == B_PATCH) {
        *p++ = 'P';
        if (ui_patch_sel + 1 < 10)
            *p++ = '0';
        p = ui_putl(p, ui_patch_sel + 1);
        *p = 0;
        return;
    }
    if (b.kind == B_ACTION) {
        *p++ = '-';
        *p = 0;
        return;
    }
    uint8_t raw = parammap::Get(ui_absCC(b));
    switch (b.fmt) {
    case F_CUTOFF: {
        float v = CcLog(raw, 100.f, 20000.f);
        if (v >= 1000.f) {
            p = ui_putf(p, v / 1000.f, 1);
            p = ui_puts(p, "KHZ");
        } else {
            p = ui_putf(p, v, 0);
            p = ui_puts(p, "HZ");
        }
        break;
    }
    case F_RES:
    case F_LEVEL:
        p = ui_putf(p, raw / 127.f, 2);
        break;
    case F_DRIVE:
        *p++ = 'X';
        p = ui_putf(p, CcLin(raw, 1.f, 4.f), 2);
        break;
    case F_ATTACK: {
        float v = CcLog(raw, 0.001f, 2.f);
        if (v < 1.f) {
            p = ui_putf(p, v * 1000.f, 0);
            p = ui_puts(p, "MS");
        } else {
            p = ui_putf(p, v, 2);
            *p++ = 'S';
        }
        break;
    }
    case F_DECAY: {
        float v = CcLog(raw, 0.01f, 2.f);
        if (v < 1.f) {
            p = ui_putf(p, v * 1000.f, 0);
            p = ui_puts(p, "MS");
        } else {
            p = ui_putf(p, v, 2);
            *p++ = 'S';
        }
        break;
    }
    case F_PAN: {
        int d = (int)raw - 64;
        if (d > -4 && d < 4)
            p = ui_puts(p, "CTR");
        else {
            float a = raw > 63.5f ? raw - 63.5f : 63.5f - raw;
            *p++ = d < 0 ? 'L' : 'R';
            p = ui_putl(p, (long)(a / 63.5f * 100.f + 0.5f));
        }
        break;
    }
    case F_LFORATE: {
        if (preset.ch[ui_ctx].lfoSynced) {
            int i = raw >> 4;
            p = ui_puts(p, kDivName[i > 7 ? 7 : i]);
        } else {
            p = ui_putf(p, CcLog(raw, 0.1f, 20.f), 1);
            p = ui_puts(p, "HZ");
        }
        break;
    }
    case F_PCT:
        p = ui_putl(p, (long)(raw / 127.f * 100.f + 0.5f));
        *p++ = '%';
        break;
    case F_BIPCT: {
        int q = (int)((raw - 64) / 63.f * 100.f + (raw >= 64 ? 0.5f : -0.5f));
        if (q > 100)
            q = 100;
        if (q < -100)
            q = -100;
        if (q >= 0)
            *p++ = '+';
        p = ui_putl(p, q);
        *p++ = '%';
        break;
    }
    case F_DELAYTIME: {
        if (preset.delaySynced) {
            int i = raw >> 4;
            p = ui_puts(p, kDivName[i > 7 ? 7 : i]);
        } else {
            float v = CcLog(raw, 0.01f, 2.f);
            if (v < 1.f) {
                p = ui_putf(p, v * 1000.f, 0);
                p = ui_puts(p, "MS");
            } else {
                p = ui_putf(p, v, 2);
                *p++ = 'S';
            }
        }
        break;
    }
    case F_CLOCKDIV: {
        int i = (raw * 9) >> 7;
        p = ui_puts(p, kClockName[i > 8 ? 8 : i]);
        break;
    }
    case F_BPM:
        p = ui_putl(p, (long)(20.f + raw / 127.f * 280.f + 0.5f));
        p = ui_puts(p, "BPM");
        break;
    default:
        p = ui_putl(p, raw);
        break;
    }
    *p = 0;
}

// Shift + click on a mute band solos its channel (toggle).
static void ui_bandSolo(const Band &b) {
    int sc = b.chRel ? ui_ctx : b.muteCh;
    ch_solo[sc] = !ch_solo[sc];
}

// --- edits: all route through parammap::Handle()+SendCC() ---
static void ui_bandPush(const Band &b) {
    switch (b.push) {
    case P_MUTE: {
        int mc = b.chRel ? ui_ctx : b.muteCh; // per-channel band mutes the current channel
        ch_muted[mc] = !ch_muted[mc];
        SendCC(68, (ch_muted[0] ? 1 : 0) | (ch_muted[1] ? 2 : 0) | (ch_muted[2] ? 4 : 0));
        break;
    }
    case P_LFOSYNC: {
        preset.ch[ui_ctx].lfoSynced = !preset.ch[ui_ctx].lfoSynced;
        uint8_t v = preset.ch[ui_ctx].lfoShape * 21 + (preset.ch[ui_ctx].lfoSynced ? 64 : 0);
        parammap::Handle(kCcBase[ui_ctx] + 11, v);
        SendCC(kCcBase[ui_ctx] + 11, v);
        break;
    }
    case P_DELAYSYNC:
        preset.delaySynced = !preset.delaySynced;
        SendCC(6, preset.delaySynced ? 127 : 0);
        break;
    case P_RUN:
        seq_running = !seq_running;
        if (!seq_running) {
            cur_step = 0;
            tick_accum = 0.f;
        }
        SendCC(15, seq_running ? 127 : 0);
        break;
    case P_TAP: {
        uint32_t now = System::GetNow();
        uint32_t gap = now - tap_last_ms;
        if (gap > 0 && gap < 2000)
            preset.bpm = fclamp(60000.f / (float)gap, 20.f, 300.f);
        tap_last_ms = now;
        manual_bpm_cc = (uint8_t)((fclamp(preset.bpm, 20.f, 300.f) - 20.f) / 280.f * 127.f);
        SendCC(19, manual_bpm_cc);
        break;
    }
    case P_BYPASS:
        bypass = !bypass;
        SendCC(18, bypass ? 127 : 0);
        break;
    case P_LOAD:
        persist::LoadPatch(ui_patch_sel);
        break;
    case P_SAVE:
        persist::SavePatch(ui_patch_sel);
        break;
    case P_SYNC:
        SendAllState();
        break;
    case P_RESET: {
        uint8_t acc = ui_absCC(b);
        parammap::Handle(acc, b.muteCh);
        SendCC(acc, b.muteCh);
        if (b.lsbOff != 255) { // 14-bit pair: zero the LSB
            uint8_t lcc = (uint8_t)(kCcBase[ui_ctx] + b.lsbOff);
            parammap::Handle(lcc, 0);
            SendCC(lcc, 0);
        }
        break;
    }
    default:
        break;
    }
}

// Encoder turn on a value band (dir = ±1). Continuous bands step the CC; enum
// bands cycle options; pattern/patch bands index their list; colour/brightness
// bands edit the UI prefs directly. Everything else routes through HandleCC+SendCC.
static void ui_bandTurn(const Band &b, int dir) {
    uint8_t acc = ui_absCC(b);
    switch (b.kind) {
    case B_ENUM: {
        uint8_t i = (uint8_t)((ui_enumIdx(b) + (dir > 0 ? 1 : b.nOpt - 1)) % b.nOpt);
        uint8_t v = b.optVal[i];
        if (b.push == P_LFOSYNC)
            v += (preset.ch[ui_ctx].lfoSynced ? 64 : 0);
        parammap::Handle(acc, v);
        SendCC(acc, v);
        break;
    }
    case B_TOGGLE:
        ui_bandPush(b);
        break;
    case B_PATIDX: {
        int i = (preset.pattern + dir + NUM_PATTERNS) % NUM_PATTERNS;
        uint8_t v = (uint8_t)(i * 8);
        parammap::Handle(14, v);
        SendCC(14, v);
        break;
    }
    case B_PATCH: {
        int p = ui_patch_sel + dir;
        if (p < 0)
            p = 0;
        if (p >= NUM_PATCHES)
            p = NUM_PATCHES - 1;
        ui_patch_sel = (uint8_t)p;
        break;
    }
    case B_ACTION:
        break;
    case B_COL_R: {
        uint16_t &c = ui_color[b.cc];
        int r = (c >> 11) & 0x1F;
        r += dir;
        r = r < 0 ? 0 : r > 31 ? 31 : r;
        c = (uint16_t)((c & ~0xF800) | (r << 11));
        ui_full_dirty = true;
        break;
    }
    case B_COL_G: {
        uint16_t &c = ui_color[b.cc];
        int g = (c >> 5) & 0x3F;
        g += dir;
        g = g < 0 ? 0 : g > 63 ? 63 : g;
        c = (uint16_t)((c & ~0x07E0) | (g << 5));
        ui_full_dirty = true;
        break;
    }
    case B_COL_B: {
        uint16_t &c = ui_color[b.cc];
        int bl = c & 0x1F;
        bl += dir;
        bl = bl < 0 ? 0 : bl > 31 ? 31 : bl;
        c = (uint16_t)((c & ~0x001F) | bl);
        ui_full_dirty = true;
        break;
    }
    case B_BRIGHT: {
        int v = (int)ui_brightness + dir * 8;
        v = v < 8 ? 8 : v > 255 ? 255 : v; // keep some backlight on
        ui_brightness = (uint8_t)v;
        tft.SetBrightness(ui_brightness / 255.f);
        break;
    }
    default: {
        int v = (int)parammap::Get(acc) + dir * 2;
        if (v < 0)
            v = 0;
        if (v > 127)
            v = 127;
        parammap::Handle(acc, (uint8_t)v);
        SendCC(acc, (uint8_t)v);
        if (b.lsbOff != 255) { // 14-bit pair: keep the LSB at 0
            uint8_t lcc = (uint8_t)(kCcBase[ui_ctx] + b.lsbOff);
            parammap::Handle(lcc, 0);
            SendCC(lcc, 0);
        }
        break;
    }
    }
}

// NAV encoder: move between sections within the current context (wraps).
static void ui_navTurn(int dir) {
    uint8_t n = ui_numSec();
    ui_sec = (uint8_t)((ui_sec + dir + n) % n);
    ui_full_dirty = true;
}
// NAV encoder (shifted): cycle the context CH1/CH2/CH3/GLOBAL (wraps).
static void ui_navCtx(int dir) {
    ui_ctx = (uint8_t)((ui_ctx + dir + 4) % 4);
    if (ui_sec >= ui_numSec()) // global has fewer sections than the channels
        ui_sec = ui_numSec() - 1;
    ui_full_dirty = true;
}
// Commit colours + brightness to QSPI (no-op if unchanged). Called on exit so we
// write at most once per settings session, not on every encoder tick.
static void ui_settingsSave() {
    UiSettings &s = uiStore.GetSettings();
    s.version = UI_VERSION;
    for (int i = 0; i < 4; i++)
        s.color[i] = ui_color[i];
    s.brightness = ui_brightness;
    uiStore.Save();
}
static void ui_settingsToggle() {
    if (ui_settings)
        ui_settingsSave(); // leaving settings → persist
    ui_settings = !ui_settings;
    if (ui_settings)
        ui_vu = ui_mix = false; // modes are exclusive
    ui_sec = 0;
    ui_full_dirty = true;
}
static void ui_vuToggle() {
    ui_vu = !ui_vu;
    if (ui_vu)
        ui_settings = ui_mix = false; // modes are exclusive
    ui_full_dirty = true;
}
// Mixer page (level/mute/solo for all three channels). Entered/exited like the
// other hidden modes; NAV-click exits it (see MenuPoll).
static void ui_mixToggle() {
    ui_mix = !ui_mix;
    if (ui_mix)
        ui_settings = ui_vu = false; // modes are exclusive
    ui_sec = 0;
    ui_full_dirty = true;
}

// --- rendering ---
// Pixel width of string `s` at text scale `size` (6px advance per glyph).
static int ui_strw(const char *s, uint8_t size) {
    int n = 0;
    while (s[n])
        n++;
    return n * 6 * size;
}

// --- activity dots (centre top). Dots 0-2 track each channel's AD envelope
//     (trigger flashes, fades with the gate); dot 3 is GLOBAL and ticks on each
//     divided sequencer step. A box around a dot marks the selected context. ---
static const int kDotCx[4] = {106, 120, 134, 148};
static constexpr int kDotN = 4;
static constexpr int kDotY = 8;
static constexpr int kDotSz = 9;
static constexpr int kDotRowY = 5; // flush region (covers the selection box)
static constexpr int kDotRowH = 16;

// Scale an RGB565 colour's brightness by `f` (0..1), per channel. Used to fade
// the activity dots with their envelope level.
static uint16_t ui_scale(uint16_t c, float f) {
    if (f < 0.f)
        f = 0.f;
    if (f > 1.f)
        f = 1.f;
    int r = (int)(((c >> 11) & 0x1F) * f);
    int g = (int)(((c >> 5) & 0x3F) * f);
    int b = (int)((c & 0x1F) * f);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// Which dot the selection box belongs to (0..3, or -1 for none).
static int ui_selDot() {
    if (ui_mix)
        return -1; // mixer shows all channels — no single-context box
    if (ui_settings)
        return ui_sec < 4 ? ui_sec : -1; // colour pages map onto the dots
    return ui_ctx;                       // 0..2 channels, 3 global
}

static void ui_drawDots() {
    uint32_t now = System::GetNow();
    int sel = ui_selDot();
    for (int c = 0; c < kDotN; c++) {
        float e;
        uint16_t col;
        bool muted = false;
        if (c < 3) {
            muted = ch_silenced(c); // muted, or silenced by another channel's solo
            e = muted ? 0.f : ch[c].env.GetValue();
            col = ui_color[c];
        } else {
            uint32_t dt = now - g_step_ms; // global step tick (~120 ms flash)
            e = (seq_running && dt < 120) ? 1.f - dt / 120.f : 0.f;
            col = ui_color[3];
        }
        int bx = kDotCx[c] - 6;
        tft.FillRect(bx, kDotY - 2, 13, 13, kHdrBg); // clear dot + box area
        int x = kDotCx[c] - kDotSz / 2;
        tft.DrawRect(x, kDotY, kDotSz, kDotSz, kPanel2); // idle ring
        tft.FillRect(x + 1, kDotY + 1, kDotSz - 2, kDotSz - 2, ui_scale(col, muted ? 0.2f : e));
        if (muted) { // cross it out
            for (int i = 0; i < kDotSz; i++) {
                tft.FillRect(x + i, kDotY + i, 1, 1, Ili9341::kRed);
                tft.FillRect(x + (kDotSz - 1 - i), kDotY + i, 1, 1, Ili9341::kRed);
            }
        }
        if (c == sel)
            tft.DrawRect(bx, kDotY - 2, 13, 13, col); // selected-context box
    }
}

// Draw the top bar: context badge (CH1/CH2/CH3/GLBL/SET), section name, the
// section-position dots, and the channel activity dots.
static void ui_drawHeader() {
    tft.FillRect(0, 0, 240, 28, kHdrBg);
    uint16_t col = ui_col();
    tft.FillRect(4, 5, 46, 18, col);
    const char *cn = ui_mix ? "MIX" : ui_settings ? "SET" : (ui_ctx < 3 ? CH_NAME[ui_ctx] : "GLBL");
    tft.DrawString(4 + (46 - ui_strw(cn, 1)) / 2, 8, cn, Ili9341::kBlack, col, 1);
    tft.DrawString(54, 10, ui_cur().name, kTxt, kHdrBg, 1); // size 1, left of the dots
    uint8_t ns = ui_numSec();
    int dx = 240 - 6 - ns * 8;
    for (int i = 0; i < ns; i++)
        tft.FillRect(dx + i * 8, 12, 5, 5, i == ui_sec ? col : kDimCol);
    ui_drawDots();
}

// One labelled CPU meter: a letter + a bar that goes green -> yellow -> red as
// the load climbs. `load` is 0..1.
static void ui_drawMeter(int x, int y, char label, float load) {
    if (load < 0.f)
        load = 0.f;
    if (load > 1.f)
        load = 1.f;
    char l[2] = {label, 0};
    tft.DrawString(x, y, l, kDimCol, kFootBg, 1);
    const int bx = x + 8, bw = 64;
    tft.FillRect(bx, y, bw, 7, kPanel2);
    uint16_t c = load < 0.7f ? 0x07E0 : load < 0.9f ? 0xFFE0 : 0xF800;
    int w = (int)(load * bw + 0.5f);
    if (w > 0)
        tft.FillRect(bx, y, w, 7, c);
}

// Draw the bottom bar: audio (A) + control (C) CPU meters on the left, BPM right.
static void ui_drawFooter() {
    tft.FillRect(0, 300, 240, 20, kFootBg);
    // bottom-left: audio (A) and control (C) CPU meters
    ui_drawMeter(4, 302, 'A', ui_dsp_load);
    ui_drawMeter(4, 311, 'C', ui_ctrl_load);
    // bottom-right: BPM
    char t[16];
    char *q = ui_putl(t, (long)(20.f + parammap::Get(19) / 127.f * 280.f + 0.5f));
    q = ui_puts(q, "BPM");
    *q = 0;
    tft.DrawString(236 - ui_strw(t, 1), 306, t, kDimCol, kFootBg, 1);
}

// Draw band `i` (0–2) of the current section: label, status badge, the value, and
// the kind-specific widget (bar / enum chips / toggle / colour or brightness bar).
static void ui_drawBand(int i) {
    const Section &s = ui_cur();
    int y = 30 + i * 90; // 30, 120, 210
    int h = 86;
    uint16_t col = ui_col();
    tft.FillRect(2, y, 236, h, kPanel);
    if (i >= s.n)
        return;
    const Band &b = s.bands[i];
    tft.FillRect(2, y, 3, h, col); // accent stripe
    tft.DrawString(8, y + 5, b.label, col, kPanel, 1);

    // status badge (solo / mute / lfo sync / delay sync)
    const char *badge = nullptr;
    uint16_t bcol = Ili9341::kRed;
    if (b.push == P_MUTE && ch_solo[b.chRel ? ui_ctx : b.muteCh]) {
        badge = "SOLO";
        bcol = Ili9341::kYellow;
    } else if (b.push == P_MUTE && ch_muted[b.chRel ? ui_ctx : b.muteCh]) {
        badge = "MUTE";
        bcol = Ili9341::kRed;
    } else if (b.push == P_LFOSYNC) {
        badge = preset.ch[ui_ctx].lfoSynced ? "SYNC" : "FREE";
        bcol = preset.ch[ui_ctx].lfoSynced ? kAccent : kDimCol;
    } else if (b.push == P_DELAYSYNC) {
        badge = preset.delaySynced ? "SYNC" : "FREE";
        bcol = preset.delaySynced ? kAccent : kDimCol;
    }
    if (badge) {
        int bw = ui_strw(badge, 1) + 6;
        tft.FillRect(120, y + 4, bw, 11, bcol);
        tft.DrawString(123, y + 5, badge, Ili9341::kBlack, bcol, 1);
    }

    if (b.kind == B_COL_R || b.kind == B_COL_G || b.kind == B_COL_B) {
        uint16_t c = ui_color[b.cc];
        int comp, maxv;
        uint16_t barcol;
        if (b.kind == B_COL_R) {
            comp = (c >> 11) & 0x1F;
            maxv = 31;
            barcol = 0xF800;
        } else if (b.kind == B_COL_G) {
            comp = (c >> 5) & 0x3F;
            maxv = 63;
            barcol = 0x07E0;
        } else {
            comp = c & 0x1F;
            maxv = 31;
            barcol = 0x001F;
        }
        char v[8];
        char *p = ui_putl(v, comp);
        *p = 0;
        tft.DrawString(232 - ui_strw(v, 2), y + 22, v, kTxt, kPanel, 2);
        tft.FillRect(8, y + 62, 222, 12, kPanel2);
        int w = comp * 222 / maxv;
        tft.FillRect(8, y + 62, w < 2 ? 2 : w, 12, barcol);
        return;
    }
    if (b.kind == B_BRIGHT) {
        int pct = ui_brightness * 100 / 255;
        char v[8];
        char *p = ui_putl(v, pct);
        *p++ = '%';
        *p = 0;
        tft.DrawString(232 - ui_strw(v, 2), y + 22, v, kTxt, kPanel, 2);
        tft.FillRect(8, y + 62, 222, 12, kPanel2);
        int w = ui_brightness * 222 / 255;
        tft.FillRect(8, y + 62, w < 2 ? 2 : w, 12, col);
        return;
    }

    if (b.kind == B_ENUM) {
        uint8_t cur = ui_enumIdx(b);
        int n = b.nOpt, gap = 4, cw = (222 - (n - 1) * gap) / n;
        for (int k = 0; k < n; k++) {
            int cx = 8 + k * (cw + gap);
            uint16_t f = (k == cur) ? col : kPanel2;
            tft.FillRect(cx, y + 40, cw, 22, f);
            int tw = ui_strw(b.opts[k], 1);
            tft.DrawString(cx + (cw - tw) / 2, y + 47, b.opts[k],
                           k == cur ? Ili9341::kBlack : kDimCol, f, 1);
        }
        return;
    }
    if (b.kind == B_TOGGLE) {
        bool on = parammap::Get(b.cc) >= 64;
        uint16_t f = on ? col : kPanel2;
        tft.FillRect(8, y + 40, 222, 24, f);
        const char *tx = on ? "ON" : "OFF";
        tft.DrawString(8 + (222 - ui_strw(tx, 2)) / 2, y + 45, tx, on ? Ili9341::kBlack : kDimCol,
                       f, 2);
        return;
    }

    char val[20];
    ui_text(b, val);
    tft.DrawString(232 - ui_strw(val, 2), y + 22, val, kTxt, kPanel, 2);
    if (b.kind == B_CONT) {
        int raw = parammap::Get(ui_absCC(b));
        float frac = raw / 127.f;
        tft.FillRect(8, y + 62, 222, 12, kPanel2);
        if (b.bip) {
            int cx = 8 + 111;
            int w = (int)((frac > 0.5f ? frac - 0.5f : 0.5f - frac) * 222.f);
            if (w < 2)
                w = 2;
            tft.FillRect(frac >= 0.5f ? cx : cx - w, y + 62, w, 12, col);
            tft.FillRect(cx, y + 62, 1, 12, Ili9341::kBlack);
        } else {
            int w = (int)(frac * 222.f);
            if (w < 2)
                w = 2;
            tft.FillRect(8, y + 62, w, 12, col);
        }
    } else {
        const char *hint =
            b.kind == B_PATCH
                ? "PUSH:LOAD"
                : b.push == P_SAVE ? "PUSH:SAVE" : b.push == P_SYNC ? "PUSH:SYNC" : "";
        if (hint[0])
            tft.DrawString(8, y + 62, hint, kDimCol, kPanel, 1);
    }
}

// Off-screen RGB565 framebuffer in SDRAM (150 KB). Drawing writes here; pixels
// reach the panel via background DMA. 32-byte aligned for cache-clean ops.
static uint16_t menu_fb[Ili9341::kWidth * Ili9341::kHeight] __attribute__((aligned(32)))
DSY_SDRAM_BSS;

// Bring up the display + encoder mux and point drawing at the framebuffer.
// Called from main() after audio is running (see the call site for why).
static void MenuInit() {
    tft.Init();                  // SPI1 first ...
    mux.Init();                  // ... then mux, so D9 ends configured as the mux SIG input
    tft.SetFramebuffer(menu_fb); // all drawing now targets the framebuffer
    tft.SetBrightness(ui_brightness / 255.f);
    ui_full_dirty = true;
}

static uint32_t ui_next_scan_ms = 0;
static uint32_t ui_last_render_ms = 0;

// Read the encoders (~1 kHz) and dispatch turns/presses. Main loop only.
static void MenuPoll(uint32_t now) {
    if (now < ui_next_scan_ms)
        return;
    ui_next_scan_ms = now + 1;

    // NAV: turn = section; hold + turn = cycle context; click (press+release with
    // no turn) = next context. Tracks whether a turn happened during the hold so
    // the release doesn't also register as a click.
    static bool nav_turned_while_held = false;

    uint16_t bits = mux.Scan(12);
    for (int i = 0; i < 4; i++) {
        uint8_t a = (bits >> (3 * i + 0)) & 1;
        uint8_t bbit = (bits >> (3 * i + 1)) & 1;
        bool sw = (bits >> (3 * i + 2)) & 1;
        int d = q_enc[i].Update(a, bbit);
        bool prevPressed = b_enc[i].state;
        b_enc[i].Update(sw);
        bool held = b_enc[i].state;
        bool pressEdge = !prevPressed && held;
        bool releaseEdge = prevPressed && !held;

        if (i == ENC_NAV) {
            if (pressEdge)
                nav_turned_while_held = false;
            if (d != 0 && !ui_vu && !ui_mix) {
                if (ui_settings) {
                    ui_navTurn(d); // settings: turn = page
                } else if (held) {
                    ui_navCtx(d); // shifted: cycle CH1/CH2/CH3/GLOBAL
                    nav_turned_while_held = true;
                } else {
                    ui_navTurn(d); // section
                }
            }
            if (releaseEdge && !nav_turned_while_held) {
                if (ui_vu)
                    ui_vuToggle(); // click exits VU
                else if (ui_mix)
                    ui_mixToggle(); // click exits the mixer
                else if (ui_settings)
                    ui_settingsToggle(); // click exits settings
                else
                    ui_navCtx(1); // plain click: next context (channel)
            }
            continue;
        }

        int idx = (i == ENC_E1) ? 0 : (i == ENC_E2) ? 1 : 2;

        // Shift (NAV held) + click. From a normal page E1/E2/E3 open the hidden
        // modes (settings / VU / mixer). Inside the mixer, shift-click instead
        // solos the channel under that encoder.
        if (pressEdge && b_enc[ENC_NAV].state) {
            if (ui_mix) {
                if (idx < ui_cur().n && ui_cur().bands[idx].push == P_MUTE) {
                    ui_bandSolo(ui_cur().bands[idx]);
                    ui_full_dirty = true;
                }
            } else if (i == ENC_E1) {
                ui_settingsToggle();
            } else if (i == ENC_E2) {
                ui_vuToggle();
            } else if (i == ENC_E3) {
                ui_mixToggle();
            }
            nav_turned_while_held = true; // suppress NAV's own click on release
            continue;
        }

        if (ui_vu)
            continue; // VU page ignores the value encoders

        if (d != 0 && idx < ui_cur().n) {
            ui_bandTurn(ui_cur().bands[idx], d);
            ui_band_dirty[idx] = true;
        }
        if (pressEdge && idx < ui_cur().n) {
            ui_bandPush(ui_cur().bands[idx]);
            ui_full_dirty = true;
        }
    }
}

// --- I/O VU page (hidden: hold NAV + click E2) ---
static const int kVuBarX = 56;
static const int kVuBarW = 176;
static const int kVuBarH = 26;
static const int kVuY[4] = {54, 92, 168, 206};
static const char *const kVuLab[4] = {"IN L", "IN R", "OUT L", "OUT R"};

// Paint the VU page's fixed chrome (header, labels, bar outlines). Drawn once;
// only the bars themselves refresh after this.
static void ui_drawVuStatic() {
    tft.FillScreen(Ili9341::kBlack);
    tft.FillRect(0, 0, 240, 28, kHdrBg);
    tft.FillRect(4, 5, 46, 18, kAccent);
    tft.DrawString(4 + (46 - ui_strw("VU", 1)) / 2, 8, "VU", Ili9341::kBlack, kAccent, 1);
    tft.DrawString(56, 10, "I/O METER", kTxt, kHdrBg, 1);
    tft.DrawString(8, 36, "INPUT", kDimCol, Ili9341::kBlack, 1);
    tft.DrawString(8, 150, "OUTPUT", kDimCol, Ili9341::kBlack, 1);
    for (int i = 0; i < 4; i++) {
        tft.DrawString(8, kVuY[i] + kVuBarH / 2 - 3, kVuLab[i], kTxt, Ili9341::kBlack, 1);
        tft.DrawRect(kVuBarX - 1, kVuY[i] - 1, kVuBarW + 2, kVuBarH + 2, kPanel2);
    }
    tft.FillRect(0, 300, 240, 20, kFootBg);
    tft.DrawString(4, 306, "NAV CLICK = EXIT", kDimCol, kFootBg, 1);
}

// Redraw the four I/O level bars from the latest peak-hold values, scaled
// -60 dB..0 dB with a green/yellow/red gradient.
static void ui_drawVuBars() {
    const float lv[4] = {vu_in_l, vu_in_r, vu_out_l, vu_out_r};
    for (int i = 0; i < 4; i++) {
        tft.FillRect(kVuBarX, kVuY[i], kVuBarW, kVuBarH, kPanel2);
        float level = lv[i];
        float db = level > 1e-4f ? 20.f * log10f(level) : -80.f;
        float frac = (db + 60.f) / 60.f; // -60 dB .. 0 dB full scale
        if (frac < 0.f)
            frac = 0.f;
        if (frac > 1.f)
            frac = 1.f;
        int w = (int)(frac * kVuBarW);
        uint16_t c = frac < 0.8f ? 0x07E0 : frac < 0.95f ? 0xFFE0 : 0xF800;
        if (w > 0)
            tft.FillRect(kVuBarX, kVuY[i], w, kVuBarH, c);
    }
}

// Redraw whatever is dirty, batched to ~50 Hz. Main loop only.
static void MenuRender(uint32_t now) {
    if (ui_vu) { // VU page: static layout once, bars refreshed ~30 Hz
        if (!ui_full_dirty && now - ui_last_render_ms < 33)
            return;
        if (tft.FlushBusy())
            return;
        ui_last_render_ms = now;
        if (ui_full_dirty) {
            ui_drawVuStatic();
            ui_full_dirty = false;
            tft.FlushRows(0, Ili9341::kHeight);
        } else {
            ui_drawVuBars();
            tft.FlushRows(50, 70);  // input bars
            tft.FlushRows(164, 72); // output bars
        }
        return;
    }
    bool any =
        ui_full_dirty || ui_foot_dirty || ui_band_dirty[0] || ui_band_dirty[1] || ui_band_dirty[2];
    if (!any)
        return;
    // Single framebuffer: don't redraw into it while a previous flush is still
    // streaming it out over DMA. Drains first, which also coalesces fast turns.
    if (tft.FlushBusy())
        return;
    if (now - ui_last_render_ms < 20)
        return;
    ui_last_render_ms = now;

    if (ui_full_dirty) {
        tft.FillScreen(Ili9341::kBlack);
        ui_drawHeader();
        for (int i = 0; i < 3; i++)
            ui_drawBand(i);
        ui_drawFooter();
        ui_full_dirty = false;
        ui_foot_dirty = false;
        ui_band_dirty[0] = ui_band_dirty[1] = ui_band_dirty[2] = false;
        tft.FlushRows(0, Ili9341::kHeight);
    } else {
        for (int i = 0; i < 3; i++)
            if (ui_band_dirty[i]) {
                ui_drawBand(i);
                ui_band_dirty[i] = false;
                tft.FlushRows(30 + i * 90, 90);
            }
        ui_drawFooter();
        ui_foot_dirty = false;
        tft.FlushRows(300, 20);
    }
}

// Refresh the channel activity dots (~30 Hz) without a full header redraw.
static uint32_t ui_dots_ms = 0;
static void MenuDots(uint32_t now) {
    if (ui_vu || ui_settings)
        return; // dots live in the normal header only
    if (now - ui_dots_ms < 33)
        return;
    if (tft.FlushBusy())
        return;
    ui_dots_ms = now;
    ui_drawDots();
    tft.FlushRows(kDotRowY, kDotRowH);
}

} // namespace menu
