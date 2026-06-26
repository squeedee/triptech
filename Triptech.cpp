#include "daisysp.h"
#include "daisy_seed.h"
#include "Ili9341.h"
#include "Mux4067.h"
#include <cmath>
#include <cstring>
#include <cstdlib>

#include "Hardware.h"    // extern peripheral handles (hw, midi, usb_midi)
#include "Model.h"       // shared data model: constants, POD presets, scale helpers
#include "State.h"       // shared runtime state: preset, channels, transport
#include "ParamMap.h"    // CC map module (parammap::Handle / parammap::Get)
#include "MidiOut.h"     // outbound MIDI module (SendCC/SendNote.../SendAllState)
#include "Sequencer.h"   // step sequencer (seq::Advance / Trigger / Reset + kPatterns)
#include "ClockSource.h" // clock arbitration + tempo (clock::*)
#include "Persistence.h" // QSPI patch bank + live snapshot (persist::*)
#include "MenuUi.h"      // on-device TFT + encoder menu (ui_* state + rendering)
#include "MidiRouter.h"  // inbound MIDI dispatch (midirouter::Process)
#include "Telemetry.h"   // load meters + status echo (telemetry::* + dspLoad)

using namespace daisysp;
using namespace daisy;

// ChannelPreset / Preset / PatchStorage / PATCH_VERSION, kClockRatios +
// ClockDivIndex, and the Channel runtime struct now live in Model.h / State.h.

// ============================================================
// Globals
// ============================================================

// ============================================================
// Delay — ping-pong, clock-synced
// Max 192000 samples ≈ 4 s at 48 kHz (covers 1/2 bar at ~30 BPM)
// ============================================================

// kDivBeats (musical division table) now lives in Model.h.

static DelayLine<float, 192001> DSY_SDRAM_BSS delayL;
static DelayLine<float, 192001> DSY_SDRAM_BSS delayR;

// hw, midi, usb_midi are declared extern in Hardware.h; defined here (non-static)
// so the header modules can reach them. hw must precede anything built from hw.qspi.
DaisySeed hw;
MidiUartHandler midi; // TRS MIDI — USART1 default pins (PB6/PB7 = D13/D14)
MidiUsbHandler usb_midi;
// QSPI stores — defined here (after hw, non-static) and declared extern in
// Persistence.h. Their structs, versions and offsets live in Persistence.h.
PersistentStorage<PatchStorage> patchStorage(hw.qspi);
PersistentStorage<UiSettings> uiStore(hw.qspi);
PersistentStorage<LiveState> liveStore(hw.qspi);

// preset, ch[], cur_patch now live in State.h.
static float sample_rate; // audio sample rate in Hz, captured in main() after hw.Init

// Output soft-start: ramp the audio out from silence over ~150 ms so the engine
// starting up (and any garbage in the first DMA block) slews in instead of
// popping. soft_inc is set in main() once the sample rate is known.
static float soft_gain = 0.f;
static float soft_inc = 1.f;

// Master peak limiter: an envelope follower with instant attack + slow release.
// Feedforward (gain derived from the current sample's own peak), so it bounds every
// output sample to <= kLimCeil with no lookahead/latency and no overshoot — the
// "user never has to manage levels" safety net. lim_rel is set in main().
static float limEnv = 0.f;               // linked L/R peak envelope
static float lim_rel = 0.999f;           // release coefficient (per-sample decay)
static constexpr float kLimCeil = 0.95f; // ceiling (~-0.45 dBFS), below the soft knee
// Resonance makeup: attenuate each channel as Q rises so a high-Q peak doesn't slam
// the bus (keeps the limiter relaxed/transparent). makeup = 1/(1 + kResComp*res).
static constexpr float kResComp = 1.5f;

// Slewed delay length (samples). The synced length is derived from preset.bpm,
// which jitters when slaved to external MIDI clock; slewing absorbs that so the
// read pointer never jumps. -1 = uninitialised (seed on first audio block).
static float delaySmpsSmoothed = -1.f;

// Load meters (dspLoad + loop timing) and the BPM echo now live in Telemetry.h.
// vu_*, ui_dsp_load, ui_ctrl_load (metering) live in State.h. Sequencer/transport
// state in State.h; patch bank + live snapshot in Persistence.h; clock-detection
// window + internal clock in ClockSource.h; the shared timing timestamps
// (led2_flash_ms, g_step_ms, tap_last_ms) in State.h; bypass/mute/solo + kCcBase/
// kTrigNote in State.h / Model.h.

// ============================================================
// Per-channel LFO — phase-accumulator with duty cycle
// ============================================================

// One LFO sample for `phase` (0..1) given a waveform `shape` and `duty` skew.
// Returns a bipolar value in [-1, +1].
// `ramp` is the transition width in phase units (0..1). Only used by the
// trapezoidal "square" shape; ignored otherwise. Caller sizes it from lfoFreq
// so the slew stays roughly constant in wall-clock time across all rates.
static float LfoSample(float phase, uint8_t shape, float duty, float ramp) {
    if (duty < 0.01f)
        duty = 0.01f;
    if (duty > 0.99f)
        duty = 0.99f;
    switch (shape) {
    case 0: // Triangle — peak position at duty
        return (phase < duty) ? -1.f + 2.f * phase / duty
                              : 1.f - 2.f * (phase - duty) / (1.f - duty);
    case 1: { // Sine — skewed via phase warp, peak at duty
        float w =
            (phase < duty) ? 0.5f * phase / duty : 0.5f + 0.5f * (phase - duty) / (1.f - duty);
        return -cosf(w * 6.283185307f);
    }
    case 2: { // Trapezoid — square with slew centered on each transition
        float maxR = (duty < (1.f - duty) ? duty : (1.f - duty)) * 0.9f;
        float r = ramp > maxR ? maxR : ramp;
        float half = r * 0.5f;
        if (phase < half)
            return 2.f * phase / r; // rising tail (0 → +1)
        if (phase < duty - half)
            return 1.f; // high plateau
        if (phase < duty + half)
            return 1.f - 2.f * (phase - duty + half) / r; // falling (+1 → -1)
        if (phase < 1.f - half)
            return -1.f;                              // low plateau
        return -1.f + 2.f * (phase - 1.f + half) / r; // rising head (-1 → 0)
    }
    default:
        return 0.f;
    }
}

// ============================================================
// Default preset factory
// ============================================================

// Seed a channel's LFO fields with the factory defaults (clock-synced sine, 50% duty).
static void InitChannelLfo(ChannelPreset &cp) {
    cp.lfoParam = 55;
    cp.lfoSynced = true;
    cp.lfoShape = 1; // Sine
    cp.lfoDuty = 0.5f;
    cp.ampLfoAmount = 0.f;
}

// Build the factory default preset: three voices (Ch1 LP/centre, Ch2 BP/left,
// Ch3 HP/right) plus default global tempo, delay and pattern. Every patch slot
// is initialised from this on first boot / version bump.
static Preset DefaultPreset() {
    Preset p = {};
    p.bpm = 120.f;
    p.delayParam = 32;
    p.clockDivParam = 64; // midway = 1:1
    p.delaySynced = true;
    p.delayFeedback = 0.4f;
    p.delayWidth = 1.f;
    p.dryLevel = 0.f;
    p.pattern = 0;

    // Ch1 — LP, center
    p.ch[0].cutoff = 800.f;
    p.ch[0].resonance = 0.5f;
    p.ch[0].drive = 1.f;
    p.ch[0].attack = 0.005f;
    p.ch[0].decay = 0.2f;
    p.ch[0].level = 0.7f;
    p.ch[0].pan = 0.5f;
    p.ch[0].lfoAmount = 0.f;
    p.ch[0].delayAmount = 0.f;
    p.ch[0].filterType = 0;
    p.ch[0].filterSlope = 1;
    InitChannelLfo(p.ch[0]);

    // Ch2 — BP, left
    p.ch[1].cutoff = 1000.f;
    p.ch[1].resonance = 0.5f;
    p.ch[1].drive = 1.f;
    p.ch[1].attack = 0.005f;
    p.ch[1].decay = 0.2f;
    p.ch[1].level = 0.7f;
    p.ch[1].pan = 0.25f;
    p.ch[1].lfoAmount = 0.f;
    p.ch[1].delayAmount = 0.f;
    p.ch[1].filterType = 1;
    p.ch[1].filterSlope = 1;
    InitChannelLfo(p.ch[1]);

    // Ch3 — HP, right
    p.ch[2].cutoff = 600.f;
    p.ch[2].resonance = 0.5f;
    p.ch[2].drive = 1.f;
    p.ch[2].attack = 0.005f;
    p.ch[2].decay = 0.2f;
    p.ch[2].level = 0.7f;
    p.ch[2].pan = 0.75f;
    p.ch[2].lfoAmount = 0.f;
    p.ch[2].delayAmount = 0.f;
    p.ch[2].filterType = 2;
    p.ch[2].filterSlope = 1;
    InitChannelLfo(p.ch[2]);

    return p;
}

// ============================================================
// Helpers
// ============================================================

// CC <-> value scaling helpers (CcLin/CcLog/CcLinInv/CcLogInv) now live in Model.h.

// Final-stage soft saturator. Transparent (unity gain) below ±kSatLin, then a
// smooth knee that asymptotes to a hard ±1 ceiling — no flat-top corner like the
// old ±3 hard-clamp. The master limiter (below) holds the bus under the ceiling,
// so normal signal passes untouched; this only rounds off the rare overshoot.
static inline float softclip(float x) {
    constexpr float lin = 0.8f;       // transparent below this magnitude
    constexpr float knee = 1.f - lin; // soft-knee width up to the ±1 ceiling
    float a = fabsf(x);
    if (a <= lin)
        return x;
    float over = a - lin;
    float shaped = lin + knee * over / (over + knee); // -> 1 as over -> inf
    return x < 0.f ? -shaped : shaped;
}

// SendCC / SendProgramChange / SendNoteOn / SendNoteOff / SendAllState now live in
// MidiOut.h.

// ProcessMidi now lives in MidiRouter.h (midirouter::Process).

// ============================================================
// Audio callback — non-interleaved stereo
// ============================================================

// Peak-hold-with-decay update for the VU page, called once per audio block.
static void VuCommit(float iL, float iR, float oL, float oR) {
    const float k = 0.96f; // per-block decay -> a few hundred ms release
    vu_in_l = iL > vu_in_l ? iL : vu_in_l * k;
    vu_in_r = iR > vu_in_r ? iR : vu_in_r * k;
    vu_out_l = oL > vu_out_l ? oL : vu_out_l * k;
    vu_out_r = oR > vu_out_r ? oR : vu_out_r * k;
}

// The audio engine. Per block: update filter/delay/LFO coefficients once, then
// per sample run each channel's filter → envelope → amp → pan, sum into the
// ping-pong delay and dry path, soft-clip, and apply the startup fade. Reads the
// live `preset`/transport globals directly; must never block or call MIDI/flash.
static void AudioCallback(const float *const *in, float **out, size_t size) {
    dspLoad.OnBlockStart();

    float biL = 0.f, biR = 0.f, boL = 0.f, boR = 0.f; // block peaks for the VU

    if (bypass) {
        for (size_t i = 0; i < size; i++) {
            float l = in[0][i], r = in[1][i];
            out[0][i] = l * soft_gain;
            out[1][i] = r * soft_gain;
            if (soft_gain < 1.f)
                soft_gain = soft_gain + soft_inc > 1.f ? 1.f : soft_gain + soft_inc;
            biL = fabsf(l) > biL ? fabsf(l) : biL;
            biR = fabsf(r) > biR ? fabsf(r) : biR;
            boL = fabsf(out[0][i]) > boL ? fabsf(out[0][i]) : boL;
            boR = fabsf(out[1][i]) > boR ? fabsf(out[1][i]) : boR;
        }
        VuCommit(biL, biR, boL, boR);
        dspLoad.OnBlockEnd();
        return;
    }

    // Per-block: update filter + delay coefficients once, cache pan/mode flags.
    float panL[NUM_CH], panR[NUM_CH];
    float makeup[NUM_CH]; // resonance makeup gain per channel
    bool use6[NUM_CH], use24[NUM_CH];

    // Delay time: synced to clock divisions or free ms. The synced length is
    // derived from preset.bpm, which jitters when slaved to external MIDI clock
    // (a per-tick EMA estimate). Writing that jittery length straight to SetDelay()
    // jumps the read pointer every block — a click per block — which is what made
    // the delay sound crunchy under external clock. Slew the length toward its
    // target instead: jitter is absorbed, and an intentional tempo/division change
    // glides smoothly (tape-style) rather than stepping.
    {
        float delaySec = preset.delaySynced
                             ? kDivBeats[preset.delayParam / 16] * 60.f / preset.bpm
                             : CcLog(preset.delayParam, 0.01f, 2.f); // 10 ms – 2000 ms
        float target = fclamp(delaySec * sample_rate, 1.f, 192000.f);
        if (delaySmpsSmoothed < 0.f)
            delaySmpsSmoothed = target; // seed on first block
        // Deadband, then slew. Hold the read pointer perfectly still unless the
        // target moves by more than ~0.5% (a real tempo/division change). Without
        // this, the clock-derived length wiggles every block, and continuously
        // micro-modulating a high-feedback delay line makes it resonate into noise
        // — the difference vs. a rock-steady internal clock. A genuine change
        // exceeds the band and slews smoothly (tape-style).
        float diff = target - delaySmpsSmoothed;
        float band = delaySmpsSmoothed * 0.005f;
        if (diff > band || diff < -band)
            delaySmpsSmoothed += diff * 0.02f;
        delayL.SetDelay(delaySmpsSmoothed);
        delayR.SetDelay(delaySmpsSmoothed);
    }

    // Per-channel LFO phase increment + trapezoid slew width.
    // Slew = ~1.5 ms in wall-clock terms → lfoFreq * 0.0015 in phase units.
    float lfoInc[NUM_CH], lfoRamp[NUM_CH];
    for (int c = 0; c < NUM_CH; c++) {
        float lfoFreq;
        if (preset.ch[c].lfoSynced) {
            lfoFreq = preset.bpm / (60.f * kDivBeats[preset.ch[c].lfoParam / 16]);
        } else {
            lfoFreq = CcLog(preset.ch[c].lfoParam, 0.1f, 20.f);
        }
        lfoInc[c] = lfoFreq / sample_rate;
        lfoRamp[c] = lfoFreq * 0.0015f;
    }

    for (int c = 0; c < NUM_CH; c++) {
        // 6 dB: OnePole — LP or HP only (BP/Notch fall back to SVF 12 dB)
        use6[c] = preset.ch[c].filterSlope == 0 && preset.ch[c].filterType != 1 // not BP
                  && preset.ch[c].filterType != 3;                              // not Notch
        // 24 dB: Ladder — LP, HP, BP (Notch not available, falls back to SVF 12 dB)
        use24[c] = preset.ch[c].filterSlope == 2 && preset.ch[c].filterType != 3;

        float freq = preset.ch[c].cutoff * (1.f + preset.ch[c].lfoAmount * ch[c].lfoVal * 0.5f);
        freq = fclamp(freq, 100.f, sample_rate / 3.f - 1.f);

        if (use6[c]) {
            float normF = freq / sample_rate;
            ch[c].poleL.SetFrequency(normF);
            ch[c].poleR.SetFrequency(normF);
            OnePole::FilterMode pm = preset.ch[c].filterType == 2 ? OnePole::FILTER_MODE_HIGH_PASS
                                                                  : OnePole::FILTER_MODE_LOW_PASS;
            ch[c].poleL.SetFilterMode(pm);
            ch[c].poleR.SetFilterMode(pm);
        } else if (use24[c]) {
            LadderFilter::FilterMode lm;
            switch (preset.ch[c].filterType) {
            case 1:
                lm = LadderFilter::FilterMode::BP24;
                break;
            case 2:
                lm = LadderFilter::FilterMode::HP24;
                break;
            default:
                lm = LadderFilter::FilterMode::LP24;
                break;
            }
            ch[c].ladL.SetFilterMode(lm);
            ch[c].ladR.SetFilterMode(lm);
            ch[c].ladL.SetFreq(freq);
            ch[c].ladR.SetFreq(freq);
            ch[c].ladL.SetRes(preset.ch[c].resonance);
            ch[c].ladR.SetRes(preset.ch[c].resonance);
            ch[c].ladL.SetInputDrive(preset.ch[c].drive);
            ch[c].ladR.SetInputDrive(preset.ch[c].drive);
        } else {
            // 12 dB SVF — LP, HP, BP, or Notch; also fallback for unsupported combos
            ch[c].fltL.SetFreq(freq);
            ch[c].fltR.SetFreq(freq);
            ch[c].fltL.SetRes(preset.ch[c].resonance);
            ch[c].fltR.SetRes(preset.ch[c].resonance);
        }

        panL[c] = cosf(preset.ch[c].pan * HALF_PI);
        panR[c] = sinf(preset.ch[c].pan * HALF_PI);
        makeup[c] = 1.f / (1.f + kResComp * preset.ch[c].resonance);
    }

    for (size_t i = 0; i < size; i++) {
        // Advance per-channel LFOs
        for (int c = 0; c < NUM_CH; c++) {
            ch[c].lfoPhase += lfoInc[c];
            if (ch[c].lfoPhase >= 1.f)
                ch[c].lfoPhase -= 1.f;
            ch[c].lfoVal =
                LfoSample(ch[c].lfoPhase, preset.ch[c].lfoShape, preset.ch[c].lfoDuty, lfoRamp[c]);
        }

        float inL = in[0][i];
        float inR = in[1][i];
        biL = fabsf(inL) > biL ? fabsf(inL) : biL;
        biR = fabsf(inR) > biR ? fabsf(inR) : biR;

        // Accumulate channel mix separately — used as sidechain source
        float chanL = 0.f, chanR = 0.f, delaySend = 0.f;

        for (int c = 0; c < NUM_CH; c++) {
            float filtL, filtR;

            if (use6[c]) {
                filtL = ch[c].poleL.Process(inL * preset.ch[c].drive);
                filtR = ch[c].poleR.Process(inR * preset.ch[c].drive);
            } else if (use24[c]) {
                // drive applied via SetInputDrive() in the per-block setup above, not here
                filtL = ch[c].ladL.Process(inL);
                filtR = ch[c].ladR.Process(inR);
            } else {
                ch[c].fltL.Process(inL * preset.ch[c].drive);
                ch[c].fltR.Process(inR * preset.ch[c].drive);
                switch (preset.ch[c].filterType) {
                case 1:
                    filtL = ch[c].fltL.Band();
                    filtR = ch[c].fltR.Band();
                    break;
                case 2:
                    filtL = ch[c].fltL.High();
                    filtR = ch[c].fltR.High();
                    break;
                case 3:
                    filtL = ch[c].fltL.Notch();
                    filtR = ch[c].fltR.Notch();
                    break;
                default:
                    filtL = ch[c].fltL.Low();
                    filtR = ch[c].fltR.Low();
                    break;
                }
            }

            float env = ch[c].env.Process();
            float lvl =
                fclamp(preset.ch[c].level + preset.ch[c].ampLfoAmount * ch[c].lfoVal, 0.f, 1.f) *
                makeup[c]; // resonance makeup keeps high-Q peaks in check
            if (ch_silenced(c))
                lvl = 0.f;
            float chL = filtL * env * lvl;
            float chR = filtR * env * lvl;
            chanL += chL * panL[c];
            chanR += chR * panR[c];
            delaySend += (chL + chR) * 0.5f * preset.ch[c].delayAmount;
        }

        // Ping-pong delay: L fed by send + R×feedback; R fed by L×feedback
        float dL = delayL.Read();
        float dR = delayR.Read();
        delayL.Write(delaySend + dR * preset.delayFeedback);
        delayR.Write(dL * preset.delayFeedback);

        // Width: 0 = mono (L+R mixed to centre), 1 = full stereo ping-pong
        float wBlend = (1.f - preset.delayWidth) * 0.5f;
        float delOutL = dL * (1.f - wBlend) + dR * wBlend;
        float delOutR = dR * (1.f - wBlend) + dL * wBlend;

        // Final mix: channels + delay + dry
        float outL = chanL + delOutL + inL * preset.dryLevel;
        float outR = chanR + delOutR + inR * preset.dryLevel;

        // Master limiter: pull the linked L/R bus under the ceiling. Instant attack
        // (env jumps to a new peak) bounds this very sample; slow release avoids
        // pumping. The gain can only reduce, so it never adds level — just prevents
        // the channel sum / delay feedback (worst at high Q) from ever clipping.
        float peak = fmaxf(fabsf(outL), fabsf(outR));
        limEnv = peak > limEnv ? peak : peak + (limEnv - peak) * lim_rel;
        float limGain = limEnv > kLimCeil ? kLimCeil / limEnv : 1.f;
        outL *= limGain;
        outR *= limGain;

        // Soft clip — final smooth ceiling (transparent below the limiter's level).
        // soft_gain ramps the output up from silence at startup (anti-pop).
        out[0][i] = softclip(outL) * soft_gain;
        out[1][i] = softclip(outR) * soft_gain;
        if (soft_gain < 1.f)
            soft_gain = soft_gain + soft_inc > 1.f ? 1.f : soft_gain + soft_inc;
        boL = fabsf(out[0][i]) > boL ? fabsf(out[0][i]) : boL;
        boR = fabsf(out[1][i]) > boR ? fabsf(out[1][i]) : boR;
    }
    VuCommit(biL, biR, boL, boR);

    dspLoad.OnBlockEnd();
}

// ============================================================
// Main
// ============================================================

// Boot the hardware and run the control loop forever. Order matters: codec/MIDI
// first, then the display; the main loop services MIDI, runs the internal clock,
// scans encoders, repaints the screen, emits telemetry, and auto-snapshots state.
int main(void) {
    hw.Configure();
    hw.Init(true); // boost to 480 MHz for audio + display headroom
    hw.SetAudioBlockSize(48);
    sample_rate = hw.AudioSampleRate();
    soft_inc = 1.f / (sample_rate * 0.15f);      // ~150 ms output fade-in
    lim_rel = expf(-1.f / (0.1f * sample_rate)); // ~100 ms limiter release

    // --- Init load meters ---
    telemetry::Init(sample_rate); // 10 Hz cutoff — fast enough for 250ms windows
    ticksPerUs = System::GetTickFreq() / 1000000.f;

    // --- Init delay lines ---
    delayL.Init();
    delayR.Init();

    // --- Init DSP ---
    for (int c = 0; c < NUM_CH; c++) {
        ch[c].fltL.Init(sample_rate);
        ch[c].fltR.Init(sample_rate);
        ch[c].ladL.Init(sample_rate);
        ch[c].ladR.Init(sample_rate);
        ch[c].poleL.Init();
        ch[c].poleR.Init();
        ch[c].env.Init(sample_rate);
        ch[c].env.SetMin(0.f);
        ch[c].env.SetMax(1.f);
        ch[c].lfoPhase = 0.f;
        ch[c].lfoVal = 0.f;
        ch[c].note_active = false;
        ch[c].env_started = false;
    }

    // --- Init patch storage (QSPI flash) ---
    {
        PatchStorage defaults;
        defaults.version = PATCH_VERSION;
        Preset dp = DefaultPreset();
        for (int i = 0; i < NUM_PATCHES; i++)
            defaults.patches[i] = dp;
        patchStorage.Init(defaults);
    }
    if (patchStorage.GetSettings().version != PATCH_VERSION)
        patchStorage.RestoreDefaults();
    // Base/fallback working state (the live snapshot below overrides this).
    preset = patchStorage.GetSettings().patches[0];

    // --- Init UI settings storage (separate QSPI sector) ---
    {
        UiSettings d;
        d.version = UI_VERSION;
        for (int i = 0; i < 4; i++)
            d.color[i] = menu::kDefaultColor[i];
        d.brightness = 255;
        d.echoIface = MIDI_BOTH; // defaults: echo both ports on ch 1, listen both/omni
        d.echoChan = 0;
        d.inIface = MIDI_BOTH;
        d.inChan = 16; // OMNI
        uiStore.Init(d, UI_QSPI_OFFSET);
    }
    if (uiStore.GetSettings().version != UI_VERSION)
        uiStore.RestoreDefaults();
    {
        const UiSettings &s = uiStore.GetSettings();
        for (int i = 0; i < 4; i++)
            menu::ui_color[i] = s.color[i];
        menu::ui_brightness = s.brightness;
        echo_iface = s.echoIface;
        echo_chan = s.echoChan;
        in_iface = s.inIface;
        in_chan = s.inChan;
    }

    // --- Init "reset-resistant" live state (separate QSPI sector) ---
    {
        LiveState d;
        memset(&d, 0, sizeof(d)); // deterministic padding for the memcmp diff
        d.version = LIVE_VERSION;
        d.preset = preset; // default preset, ...
        d.preset.bpm = 120.f;
        d.run = 1; // ... sequencer running, nothing muted/soloed
        liveStore.Init(d, LIVE_QSPI_OFFSET);
    }
    if (liveStore.GetSettings().version != LIVE_VERSION)
        liveStore.RestoreDefaults();
    persist::ApplyLiveState(liveStore.GetSettings());
    persist::SeedSnapshot(); // seed change-detector so we don't re-save on boot

    // --- Init MIDI ---
    MidiUartHandler::Config midi_cfg; // defaults: USART1, RX=PB7 (D14), TX=PB6 (D13)
    midi.Init(midi_cfg);
    midi.StartReceive();

    MidiUsbHandler::Config usb_cfg;
    usb_cfg.transport_config.periph = MidiUsbTransport::Config::INTERNAL;
    usb_midi.Init(usb_cfg);
    usb_midi.StartReceive();

    // --- Start audio ---
    hw.StartAudio(AudioCallback);

    // --- Init the menu UI (TFT + encoders). After audio so its blocking
    //     panel-reset delays don't hold up codec/MIDI bring-up. ---
    menu::MenuInit();

    // Seed internal clock timer so first tick doesn't fire immediately
    clock::SeedInternal(System::GetNow());

    // --- Main loop ---
    while (true) {
        uint32_t loopStart = System::GetTick();
        uint32_t now = System::GetNow();

        // Age out stale external clock sources; reset the estimator when idle.
        bool midi_active = clock::Update(now);

        // Service MIDI (resets on UART overrun)
        midi.Listen();
        usb_midi.Listen();

        // Process events
        midirouter::Process(midi, true);
        midirouter::Process(usb_midi, false);

        // Internal clock — only when no MIDI clock is present
        if (!midi_active)
            clock::ServiceInternal(now);

        // Send NoteOff when each channel's envelope has completed
        for (int c = 0; c < NUM_CH; c++) {
            if (ch[c].note_active) {
                float v = ch[c].env.GetValue();
                if (!ch[c].env_started) {
                    if (v > 0.001f)
                        ch[c].env_started = true;
                } else if (v < 0.001f) {
                    SendNoteOff(kTrigNote[c]);
                    ch[c].note_active = false;
                }
            }
        }

        // Menu UI — encoder scan + screen redraw. Never touches audio.
        menu::MenuPoll(now);
        menu::MenuRender(now);
        menu::MenuDots(now);
        menu::tft.ServiceFlush(); // pump the background DMA flush queue

        // Load metering + status echo (loop timing this iteration; CC dump @ 250 ms).
        telemetry::RecordLoop(loopStart);
        telemetry::Service(now);

        // Auto-snapshot the live working state (full preset + transport) to QSPI so
        // a power cut restores exactly where we were (debounced — see Persistence.h).
        persist::ServiceSnapshot(now);
    }
}
