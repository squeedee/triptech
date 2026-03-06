#include "daisysp.h"
#include "daisy_pod.h"
#include <cmath>
#include <cstdlib>

using namespace daisysp;
using namespace daisy;

// ============================================================
// Constants
// ============================================================

static constexpr int      NUM_CH         = 3;   // Ch1=0, Ch2=1, Ch3=2
static constexpr int      NUM_STEPS      = 16;
static constexpr int      NUM_PATTERNS   = 16;
static constexpr int      TICKS_PER_STEP = 6;   // 24 PPQN → 6 ticks per 16th note
static constexpr uint32_t TRS_TIMEOUT_MS = 500;
static constexpr float    HALF_PI        = 1.5707963268f;

// ============================================================
// Gate Patterns [pattern][step][channel]   (0=Ch1, 1=Ch2, 2=Ch3)
// ============================================================

static const bool kPatterns[NUM_PATTERNS][NUM_STEPS][NUM_CH] = {
    // 0: Open LP — LP every step
    {{1,0,0},{1,0,0},{1,0,0},{1,0,0},{1,0,0},{1,0,0},{1,0,0},{1,0,0},
     {1,0,0},{1,0,0},{1,0,0},{1,0,0},{1,0,0},{1,0,0},{1,0,0},{1,0,0}},

    // 1: Classic — LP quarters / HP 8th-off / BP 16th-fill
    {{1,0,0},{0,1,0},{0,0,1},{0,1,0},{1,0,0},{0,1,0},{0,0,1},{0,1,0},
     {1,0,0},{0,1,0},{0,0,1},{0,1,0},{1,0,0},{0,1,0},{0,0,1},{0,1,0}},

    // 2: LP/HP alternating
    {{1,0,0},{0,0,1},{1,0,0},{0,0,1},{1,0,0},{0,0,1},{1,0,0},{0,0,1},
     {1,0,0},{0,0,1},{1,0,0},{0,0,1},{1,0,0},{0,0,1},{1,0,0},{0,0,1}},

    // 3: Even split — LP/BP/HP/rest cycling
    {{1,0,0},{0,1,0},{0,0,1},{0,0,0},{1,0,0},{0,1,0},{0,0,1},{0,0,0},
     {1,0,0},{0,1,0},{0,0,1},{0,0,0},{1,0,0},{0,1,0},{0,0,1},{0,0,0}},

    // 4: LP first half, BP second half
    {{1,0,0},{1,0,0},{1,0,0},{1,0,0},{1,0,0},{1,0,0},{1,0,0},{1,0,0},
     {0,1,0},{0,1,0},{0,1,0},{0,1,0},{0,1,0},{0,1,0},{0,1,0},{0,1,0}},

    // 5: Quarter rotation — LP / BP / HP / LP
    {{1,0,0},{0,0,0},{0,0,0},{0,0,0},{0,1,0},{0,0,0},{0,0,0},{0,0,0},
     {0,0,1},{0,0,0},{0,0,0},{0,0,0},{1,0,0},{0,0,0},{0,0,0},{0,0,0}},

    // 6: Open HP — HP every step
    {{0,0,1},{0,0,1},{0,0,1},{0,0,1},{0,0,1},{0,0,1},{0,0,1},{0,0,1},
     {0,0,1},{0,0,1},{0,0,1},{0,0,1},{0,0,1},{0,0,1},{0,0,1},{0,0,1}},

    // 7: LP→BP→HP rotation every step
    {{1,0,0},{0,1,0},{0,0,1},{1,0,0},{0,1,0},{0,0,1},{1,0,0},{0,1,0},
     {0,0,1},{1,0,0},{0,1,0},{0,0,1},{1,0,0},{0,1,0},{0,0,1},{1,0,0}},

    // 8: LP quarters + BP 8th-offbeats
    {{1,0,0},{0,0,0},{0,1,0},{0,0,0},{1,0,0},{0,0,0},{0,1,0},{0,0,0},
     {1,0,0},{0,0,0},{0,1,0},{0,0,0},{1,0,0},{0,0,0},{0,1,0},{0,0,0}},

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

    // 14: Dotted-8th LP (0,3,6,9,12) + HP fills (2,5,8,11,14)
    {{1,0,0},{0,0,0},{0,0,1},{1,0,0},{0,0,0},{0,0,1},{1,0,0},{0,0,0},
     {0,0,1},{1,0,0},{0,0,0},{0,0,1},{1,0,0},{0,0,0},{0,0,1},{0,0,0}},

    // 15: Complex syncopated — LP:0,2,9,13 / BP:3,7,11,15 / HP:5,6,12
    {{1,0,0},{0,0,0},{1,0,0},{0,1,0},{0,0,0},{0,0,1},{0,0,1},{0,1,0},
     {0,0,0},{1,0,0},{0,0,0},{0,1,0},{0,0,1},{1,0,0},{0,0,0},{0,1,0}},
};

// ============================================================
// Per-channel state
// ============================================================

struct ChannelPreset
{
    float   cutoff;       // Hz
    float   resonance;    // 0–0.95
    float   drive;        // 1–4 (pre-filter gain)
    float   envAmount;    // 0–1
    float   attack;       // seconds
    float   decay;        // seconds
    float   level;        // 0–1
    float   pan;          // 0=left, 0.5=center, 1=right
    float   lfoAmount;    // -1 to +1 (14-bit bipolar)
    float   delayAmount;  // 0–1 (post-amp send to delay)
    uint8_t filterType;   // 0=LP, 1=BP, 2=HP, 3=Notch
    uint8_t filterSlope;  // 0=6dB, 1=12dB, 2=24dB
};

struct Preset
{
    ChannelPreset ch[NUM_CH];
    float    bpm;           // 20–300
    float    delayFeedback; // 0–0.95
    float    delayWidth;    // 0=mono, 1=full ping-pong
    float    dryLevel;      // 0–1
    uint8_t  pattern;       // 0–15
    uint8_t  delayParam;    // CC 1 raw value
    uint8_t  lfoParam;      // CC 16 raw value
    bool     delaySynced;   // true = clock-synced divisions, false = free ms
    bool     lfoSynced;     // true = clock-synced, false = free Hz
};

struct Channel
{
    Svf          fltL, fltR;       // SVF: LP/HP/BP/Notch at 12 dB
    LadderFilter ladL, ladR;       // Ladder: LP/HP/BP at 12 or 24 dB
    OnePole      poleL, poleR;     // OnePole: LP/HP at 6 dB
    AdEnv        env;
    uint8_t      lfoAmtMsb;        // 14-bit MSB cache for lfoAmount
    bool         note_active;
    bool         env_started;
};

// ============================================================
// Globals
// ============================================================

// ============================================================
// Delay — ping-pong, clock-synced
// Max 192000 samples ≈ 4 s at 48 kHz (covers 1/2 bar at ~30 BPM)
// ============================================================

// Musical division table: beats per division (1 beat = 1 quarter note)
static const float kDivBeats[8] = {
    2.f,        // 1/2
    4.f/3.f,    // 1/2T
    1.f,        // 1/4
    2.f/3.f,    // 1/4T
    0.5f,       // 1/8
    1.f/3.f,    // 1/8T
    0.25f,      // 1/16
    1.f/6.f,    // 1/16T
};

static DelayLine<float, 192001> DSY_SDRAM_BSS delayL;
static DelayLine<float, 192001> DSY_SDRAM_BSS delayR;

static DaisyPod       pod;
static MidiUsbHandler usb_midi;
static Preset         preset;
static Channel        ch[NUM_CH];
static Oscillator     lfo;
static float          sample_rate;

static CpuLoadMeter   dspLoad;
static float          loopLoadAvg   = 0.f;   // smoothed main-loop µs per iteration
static float          loopPeak      = 0.f;   // peak µs in current 250ms window
static uint32_t       lastLoadMs    = 0;
static float          ticksPerUs    = 1.f;    // populated after init

static uint8_t  cur_step    = 0;
static int      tick_count  = 0;
static bool     seq_running = false;

static bool     trs_active       = false;
static uint32_t trs_last_ms      = 0;
static bool     usb_clock_active = false;
static uint32_t usb_last_ms      = 0;

// Internal clock
static float    lfo_rate      = 1.f;
static uint32_t int_tick_ms   = 0;   // timestamp of last internal tick
static uint32_t led2_flash_ms = 0;   // timestamp of last beat flash
static uint32_t tap_last_ms   = 0;   // timestamp of last tap-tempo tap
static bool     bypass        = false;

// MIDI CC base per channel — 16 CC slots each (offsets 0-12 used)
static constexpr int     kCcBase[NUM_CH]   = {20, 36, 52};
// Note triggers: all on MIDI channel 1 — C4, C#4, D4
static constexpr uint8_t kTrigNote[NUM_CH] = {60, 61, 62};

// ============================================================
// Helpers
// ============================================================

static float CcLin(uint8_t v, float lo, float hi)
{
    return lo + (v / 127.f) * (hi - lo);
}

static float CcLog(uint8_t v, float lo, float hi)
{
    return lo * powf(hi / lo, v / 127.f);
}

static uint8_t CcLinInv(float val, float lo, float hi)
{
    return (uint8_t)fclamp((val - lo) / (hi - lo) * 127.f, 0.f, 127.f);
}

static uint8_t CcLogInv(float val, float lo, float hi)
{
    return (uint8_t)fclamp(logf(val / lo) / logf(hi / lo) * 127.f, 0.f, 127.f);
}

static inline float fasttanh(float x)
{
    if(x >  3.f) return  1.f;
    if(x < -3.f) return -1.f;
    float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

static void SendCC(uint8_t cc, uint8_t val)
{
    uint8_t msg[3] = { 0xB0, cc, val };
    pod.midi.SendMessage(msg, 3);
    usb_midi.SendMessage(msg, 3);
}

static void SendNoteOn(uint8_t note, uint8_t vel)
{
    uint8_t msg[3] = { 0x90, note, vel };
    pod.midi.SendMessage(msg, 3);
    usb_midi.SendMessage(msg, 3);
}

static void SendNoteOff(uint8_t note)
{
    uint8_t msg[3] = { 0x80, note, 0 };
    pod.midi.SendMessage(msg, 3);
    usb_midi.SendMessage(msg, 3);
}

static void SendAllState()
{
    SendCC(1,  preset.delayParam);
    SendCC(2,  CcLinInv(preset.delayFeedback, 0.f, 0.95f));
    SendCC(3,  CcLinInv(preset.delayWidth,    0.f, 1.f));
    SendCC(4,  CcLinInv(preset.dryLevel,      0.f, 1.f));
    SendCC(6,  preset.delaySynced ? 127 : 0);
    SendCC(7,  preset.lfoSynced   ? 127 : 0);
    SendCC(16, preset.lfoParam);
    SendCC(14, preset.pattern * 8);
    SendCC(15, seq_running ? 127 : 0);
    SendCC(18, bypass ? 127 : 0);
    SendCC(19, (uint8_t)((fclamp(preset.bpm, 20.f, 300.f) - 20.f) / 280.f * 127.f));
    for(int c = 0; c < NUM_CH; c++)
    {
        int base = kCcBase[c];
        SendCC(base + 0, CcLogInv(preset.ch[c].cutoff,    100.f,   20000.f));
        SendCC(base + 1, CcLinInv(preset.ch[c].resonance, 0.f,     0.95f));
        SendCC(base + 2, CcLinInv(preset.ch[c].drive,     1.f,     4.f));
        SendCC(base + 3,  CcLinInv(preset.ch[c].envAmount, 0.f, 1.f));
        SendCC(base + 4, CcLogInv(preset.ch[c].attack,    0.001f,  2.f));
        SendCC(base + 5, CcLogInv(preset.ch[c].decay,     0.01f,   4.f));
        SendCC(base + 6, CcLinInv(preset.ch[c].level,     0.f,     1.f));
        SendCC(base + 7, CcLinInv(preset.ch[c].pan,       0.f,     1.f));
        { uint16_t v14 = (uint16_t)fclamp((preset.ch[c].lfoAmount + 1.f) * 0.5f * 16383.f, 0.f, 16383.f);
          SendCC(base + 8,  v14 >> 7);
          SendCC(base + 12, v14 & 0x7F); }
        SendCC(base + 9,  preset.ch[c].filterType  * 32);
        SendCC(base + 10, preset.ch[c].filterSlope * 63);
        SendCC(base + 13, CcLinInv(preset.ch[c].delayAmount, 0.f, 1.f));
    }
}

// ============================================================
// Gate trigger
// ============================================================

static void TriggerGate(int c)
{
    ch[c].env.SetTime(ADENV_SEG_ATTACK, preset.ch[c].attack);
    ch[c].env.SetTime(ADENV_SEG_DECAY,  preset.ch[c].decay);
    ch[c].env.Trigger();
    if(ch[c].note_active)
        SendNoteOff(kTrigNote[c]);  // cut off any still-open note
    SendNoteOn(kTrigNote[c], 100);
    ch[c].note_active = true;
    ch[c].env_started = false;
}

// ============================================================
// Sequencer step (called every TICKS_PER_STEP MIDI clocks)
// ============================================================

static void AdvanceClock()
{
    tick_count++;
    if(tick_count >= TICKS_PER_STEP)
    {
        tick_count = 0;
        cur_step   = (cur_step + 1) % NUM_STEPS;
        if(cur_step % 4 == 0)
            led2_flash_ms = System::GetNow();
        for(int c = 0; c < NUM_CH; c++)
        {
            if(kPatterns[preset.pattern][cur_step][c])
                TriggerGate(c);
        }
    }
}

// ============================================================
// MIDI CC handler
// ============================================================

static void HandleCC(uint8_t ctrl, uint8_t val)
{
    switch(ctrl)
    {
        case 1:  preset.delayParam    = val;                         return;
        case 2:  preset.delayFeedback = CcLin(val, 0.f, 0.95f);    return;
        case 3:  preset.delayWidth    = CcLin(val, 0.f, 1.f);      return;
        case 4:  preset.dryLevel      = CcLin(val, 0.f, 1.f);      return;
        case 6:  preset.delaySynced   = (val >= 64);                return;
        case 7:  preset.lfoSynced     = (val >= 64);                return;
        case 14: preset.pattern = (val * NUM_PATTERNS) / 128;   return;
        case 15: seq_running = (val >= 64);                return;
        case 16: preset.lfoParam = val;
                 if(!preset.lfoSynced) { lfo_rate = CcLog(val, 0.1f, 20.f); lfo.SetFreq(lfo_rate); }
                 return;
        case 18: bypass = (val >= 64);                     return;
        case 19: preset.bpm = 20.f + (val / 127.f) * 280.f;     return;
        case 119: SendAllState();                          return;
        default: break;
    }

    for(int c = 0; c < NUM_CH; c++)
    {
        int offset = (int)ctrl - kCcBase[c];
        if(offset < 0 || offset > 13) continue;
        switch(offset)
        {
            case 0: preset.ch[c].cutoff    = CcLog(val, 100.f, 20000.f); break;
            case 1: preset.ch[c].resonance = CcLin(val, 0.f, 0.95f);     break;
            case 2: preset.ch[c].drive     = CcLin(val, 1.f, 4.f);       break;
            case 3: preset.ch[c].envAmount = CcLin(val, 0.f, 1.f); break;
            case 4: preset.ch[c].attack    = CcLog(val, 0.001f, 2.f);    break;
            case 5: preset.ch[c].decay     = CcLog(val, 0.01f, 4.f);     break;
            case 6: preset.ch[c].level     = CcLin(val, 0.f, 1.f);       break;
            case 7: preset.ch[c].pan       = CcLin(val, 0.f, 1.f);       break;
            case 8: // lfo amount MSB
                ch[c].lfoAmtMsb = val;
                preset.ch[c].lfoAmount = ((uint16_t)val << 7) / 16383.5f * 2.f - 1.f;
                break;
            case 9:  preset.ch[c].filterType  = val < 32 ? 0 : val < 64 ? 1 : val < 96 ? 2 : 3; break;
            case 10: preset.ch[c].filterSlope = val < 43 ? 0 : val < 85 ? 1 : 2; break;
            case 12: { // lfo amount LSB
                uint16_t v14 = ((uint16_t)ch[c].lfoAmtMsb << 7) | val;
                preset.ch[c].lfoAmount = v14 / 16383.5f * 2.f - 1.f;
                break;
            }
            case 13: preset.ch[c].delayAmount = CcLin(val, 0.f, 1.f); break;
        }
    }
}

// ============================================================
// Drain events from either MIDI handler
// TRS clock is authoritative; USB clock is ignored while TRS is active
// ============================================================

template <typename Handler>
static void ProcessMidi(Handler& midi, bool from_trs)
{
    while(midi.HasEvents())
    {
        MidiEvent msg      = midi.PopEvent();
        bool allow_trans   = from_trs || !trs_active;

        if(msg.type == SystemRealTime)
        {
            switch(msg.srt_type)
            {
                case TimingClock:
                    if(from_trs)
                    {
                        trs_active  = true;
                        trs_last_ms = System::GetNow();
                    }
                    else
                    {
                        usb_clock_active = true;
                        usb_last_ms      = System::GetNow();
                    }
                    if(allow_trans && seq_running)
                        AdvanceClock();
                    break;

                case Start:
                    if(allow_trans)
                    {
                        seq_running = true;
                        cur_step    = 0;
                        tick_count  = 0;
                    }
                    break;

                case Stop:
                    if(allow_trans)
                        seq_running = false;
                    break;

                case Continue:
                    if(allow_trans)
                        seq_running = true;
                    break;

                default: break;
            }
        }
        else if(msg.type == ControlChange)
        {
            auto cc = msg.AsControlChange();
            HandleCC(cc.control_number, cc.value);
        }
        else if(msg.type == NoteOn)
        {
            auto note = msg.AsNoteOn();
            if(note.velocity > 0 && msg.channel == 0)
            {
                for(int c = 0; c < NUM_CH; c++)
                {
                    if(note.note == kTrigNote[c])
                    {
                        TriggerGate(c);
                        break;
                    }
                }
            }
        }
    }
}

// ============================================================
// Audio callback — non-interleaved stereo
// ============================================================

static void AudioCallback(const float* const* in, float** out, size_t size)
{
    dspLoad.OnBlockStart();

    if(bypass)
    {
        for(size_t i = 0; i < size; i++)
        {
            out[0][i] = in[0][i];
            out[1][i] = in[1][i];
        }
        dspLoad.OnBlockEnd();
        return;
    }

    // Per-block: update filter + delay coefficients once, cache pan/mode flags.
    static float last_lfo_val = 0.f;
    float panL[NUM_CH], panR[NUM_CH];
    bool  use6[NUM_CH], use24[NUM_CH];

    // Delay time: synced to clock divisions or free ms
    {
        float delaySec = preset.delaySynced
            ? kDivBeats[preset.delayParam / 16] * 60.f / preset.bpm
            : CcLog(preset.delayParam, 0.01f, 2.f);          // 10 ms – 2000 ms
        size_t delaySmps = (size_t)fclamp(delaySec * sample_rate, 1.f, 192000.f);
        delayL.SetDelay(delaySmps);
        delayR.SetDelay(delaySmps);
    }

    // LFO freq: synced to clock divisions or free Hz
    if(preset.lfoSynced)
    {
        float lfoFreq = preset.bpm / (60.f * kDivBeats[preset.lfoParam / 16]);
        lfo.SetFreq(lfoFreq);
    }

    for(int c = 0; c < NUM_CH; c++)
    {
        // 6 dB: OnePole — LP or HP only (BP/Notch fall back to SVF 12 dB)
        use6[c]  = preset.ch[c].filterSlope == 0
                   && preset.ch[c].filterType != 1   // not BP
                   && preset.ch[c].filterType != 3;  // not Notch
        // 24 dB: Ladder — LP, HP, BP (Notch not available, falls back to SVF 12 dB)
        use24[c] = preset.ch[c].filterSlope == 2 && preset.ch[c].filterType != 3;

        float freq = preset.ch[c].cutoff * (1.f + preset.ch[c].lfoAmount * last_lfo_val * 0.5f);
        freq = fclamp(freq, 100.f, sample_rate / 3.f - 1.f);

        if(use6[c])
        {
            float normF = freq / sample_rate;
            ch[c].poleL.SetFrequency(normF);
            ch[c].poleR.SetFrequency(normF);
            OnePole::FilterMode pm = preset.ch[c].filterType == 2
                ? OnePole::FILTER_MODE_HIGH_PASS : OnePole::FILTER_MODE_LOW_PASS;
            ch[c].poleL.SetFilterMode(pm);
            ch[c].poleR.SetFilterMode(pm);
        }
        else if(use24[c])
        {
            LadderFilter::FilterMode lm;
            switch(preset.ch[c].filterType)
            {
                case 1:  lm = LadderFilter::FilterMode::BP24; break;
                case 2:  lm = LadderFilter::FilterMode::HP24; break;
                default: lm = LadderFilter::FilterMode::LP24; break;
            }
            ch[c].ladL.SetFilterMode(lm);
            ch[c].ladR.SetFilterMode(lm);
            ch[c].ladL.SetFreq(freq);
            ch[c].ladR.SetFreq(freq);
            ch[c].ladL.SetRes(preset.ch[c].resonance);
            ch[c].ladR.SetRes(preset.ch[c].resonance);
            ch[c].ladL.SetInputDrive(preset.ch[c].drive);
            ch[c].ladR.SetInputDrive(preset.ch[c].drive);
        }
        else
        {
            // 12 dB SVF — LP, HP, BP, or Notch; also fallback for unsupported combos
            ch[c].fltL.SetFreq(freq);
            ch[c].fltR.SetFreq(freq);
            ch[c].fltL.SetRes(preset.ch[c].resonance);
            ch[c].fltR.SetRes(preset.ch[c].resonance);
        }

        panL[c] = cosf(preset.ch[c].pan * HALF_PI);
        panR[c] = sinf(preset.ch[c].pan * HALF_PI);
    }

    for(size_t i = 0; i < size; i++)
    {
        last_lfo_val = lfo.Process();   // advance LFO every sample; value used next block
        float inL = in[0][i];
        float inR = in[1][i];

        // Accumulate channel mix separately — used as sidechain source
        float chanL = 0.f, chanR = 0.f, delaySend = 0.f;

        for(int c = 0; c < NUM_CH; c++)
        {
            float filtL, filtR;

            if(use6[c])
            {
                filtL = ch[c].poleL.Process(inL * preset.ch[c].drive);
                filtR = ch[c].poleR.Process(inR * preset.ch[c].drive);
            }
            else if(use24[c])
            {
                // LadderFilter applies drive internally via SetInputDrive
                filtL = ch[c].ladL.Process(inL);
                filtR = ch[c].ladR.Process(inR);
            }
            else
            {
                ch[c].fltL.Process(inL * preset.ch[c].drive);
                ch[c].fltR.Process(inR * preset.ch[c].drive);
                switch(preset.ch[c].filterType)
                {
                    case 1:  filtL = ch[c].fltL.Band();  filtR = ch[c].fltR.Band();  break;
                    case 2:  filtL = ch[c].fltL.High();  filtR = ch[c].fltR.High();  break;
                    case 3:  filtL = ch[c].fltL.Notch(); filtR = ch[c].fltR.Notch(); break;
                    default: filtL = ch[c].fltL.Low();   filtR = ch[c].fltR.Low();   break;
                }
            }

            float env  = ch[c].env.Process() * preset.ch[c].envAmount;
            float chL  = filtL * env * preset.ch[c].level;
            float chR  = filtR * env * preset.ch[c].level;
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

        // Soft clip — handles summing of multiple channels gracefully
        out[0][i] = fasttanh(outL);
        out[1][i] = fasttanh(outR);
    }

    dspLoad.OnBlockEnd();
}

// ============================================================
// Main
// ============================================================

int main(void)
{
    pod.Init();
    pod.SetAudioBlockSize(48);
    sample_rate = pod.AudioSampleRate();

    // --- Init load meters ---
    dspLoad.Init(sample_rate, 48, 10.f);  // 10 Hz cutoff — fast enough for 250ms windows
    ticksPerUs = System::GetTickFreq() / 1000000.f;

    // --- Init delay lines ---
    delayL.Init();
    delayR.Init();

    // --- Init DSP ---
    for(int c = 0; c < NUM_CH; c++)
    {
        ch[c].fltL.Init(sample_rate);
        ch[c].fltR.Init(sample_rate);
        ch[c].ladL.Init(sample_rate);
        ch[c].ladR.Init(sample_rate);
        ch[c].poleL.Init();
        ch[c].poleR.Init();
        ch[c].env.Init(sample_rate);
        ch[c].env.SetMin(0.f);
        ch[c].env.SetMax(1.f);
        ch[c].note_active = false;
        ch[c].env_started = false;
    }

    // --- Default preset ---
    preset.bpm = 120.f;
    preset.delayParam = 32; preset.delaySynced = true;
    preset.delayFeedback = 0.4f; preset.delayWidth = 1.f;
    preset.lfoParam = 55; preset.lfoSynced = false;
    preset.dryLevel = 0.f;
    preset.pattern = 0;

    // LP defaults — panned center
    preset.ch[0].cutoff = 800.f;  preset.ch[0].resonance = 0.5f; preset.ch[0].drive = 1.f;
    preset.ch[0].envAmount = 1.f; preset.ch[0].attack = 0.005f;  preset.ch[0].decay = 0.2f;
    preset.ch[0].level = 0.7f;    preset.ch[0].pan = 0.5f;       preset.ch[0].lfoAmount = 0.f;
    preset.ch[0].delayAmount = 0.f;
    preset.ch[0].filterType = 0;  preset.ch[0].filterSlope = 1;  // LP, 12 dB

    // Ch2 defaults — panned left
    preset.ch[1].cutoff = 2000.f; preset.ch[1].resonance = 0.5f; preset.ch[1].drive = 1.f;
    preset.ch[1].envAmount = 1.f; preset.ch[1].attack = 0.005f;  preset.ch[1].decay = 0.2f;
    preset.ch[1].level = 0.7f;    preset.ch[1].pan = 0.25f;      preset.ch[1].lfoAmount = 0.f;
    preset.ch[1].delayAmount = 0.f;
    preset.ch[1].filterType = 1;  preset.ch[1].filterSlope = 1;  // BP, 12 dB

    // Ch3 defaults — panned right
    preset.ch[2].cutoff = 1200.f; preset.ch[2].resonance = 0.5f; preset.ch[2].drive = 1.f;
    preset.ch[2].envAmount = 1.f; preset.ch[2].attack = 0.005f;  preset.ch[2].decay = 0.2f;
    preset.ch[2].level = 0.7f;    preset.ch[2].pan = 0.75f;      preset.ch[2].lfoAmount = 0.f;
    preset.ch[2].delayAmount = 0.f;
    preset.ch[2].filterType = 2;  preset.ch[2].filterSlope = 1;  // HP, 12 dB

    // --- Init LFO ---
    lfo.Init(sample_rate);
    lfo.SetWaveform(Oscillator::WAVE_SIN);
    lfo.SetFreq(1.f);
    lfo.SetAmp(1.f);

    // --- Init MIDI ---
    pod.midi.StartReceive();

    MidiUsbHandler::Config usb_cfg;
    usb_cfg.transport_config.periph = MidiUsbTransport::Config::INTERNAL;
    usb_midi.Init(usb_cfg);
    usb_midi.StartReceive();

    // --- Start audio ---
    pod.StartAudio(AudioCallback);

    // Seed internal clock timer so first tick doesn't fire immediately
    int_tick_ms = System::GetNow();

    // --- Main loop ---
    while(true)
    {
        uint32_t loopStart = System::GetTick();
        uint32_t now       = System::GetNow();

        // Clock source timeouts
        if(trs_active       && (now - trs_last_ms) > TRS_TIMEOUT_MS)
            trs_active = false;
        if(usb_clock_active && (now - usb_last_ms) > TRS_TIMEOUT_MS)
            usb_clock_active = false;

        bool midi_active = trs_active || usb_clock_active;

        // Service MIDI (resets on UART overrun)
        pod.midi.Listen();
        usb_midi.Listen();

        // Process events
        ProcessMidi(pod.midi,  true);
        ProcessMidi(usb_midi, false);

        // Internal clock — only when no MIDI clock is present
        if(!midi_active)
        {
            uint32_t interval = (uint32_t)(60000.f / (preset.bpm * 24.f));
            if(seq_running)
            {
                if((now - int_tick_ms) >= interval)
                {
                    int_tick_ms += interval;
                    AdvanceClock();
                }
            }
            else
            {
                int_tick_ms = now;  // keep fresh so start is immediate
            }
        }

        // Send NoteOff when each channel's envelope has completed
        for(int c = 0; c < NUM_CH; c++)
        {
            if(ch[c].note_active)
            {
                float v = ch[c].env.GetValue();
                if(!ch[c].env_started)
                {
                    if(v > 0.001f) ch[c].env_started = true;
                }
                else if(v < 0.001f)
                {
                    SendNoteOff(kTrigNote[c]);
                    ch[c].note_active = false;
                }
            }
        }

        // Physical controls
        pod.ProcessDigitalControls();

        // Button 1: toggle start / stop (stop resets to step 0)
        if(pod.button1.RisingEdge())
        {
            seq_running = !seq_running;
            if(!seq_running)
            {
                cur_step   = 0;
                tick_count = 0;
            }
            SendCC(15, seq_running ? 127 : 0);
        }

        // Button 2: tap tempo — sets BPM from interval between taps (< 2 s apart)
        if(pod.button2.RisingEdge())
        {
            uint32_t gap = now - tap_last_ms;
            if(gap > 0 && gap < 2000)
                preset.bpm = fclamp(60000.f / (float)gap, 20.f, 300.f);
            tap_last_ms = now;
            uint8_t bpm_cc = (uint8_t)((fclamp(preset.bpm, 20.f, 300.f) - 20.f) / 280.f * 127.f);
            SendCC(19, bpm_cc);
        }

        // Encoder click: toggle bypass
        if(pod.encoder.RisingEdge())
        {
            bypass = !bypass;
            SendCC(18, bypass ? 127 : 0);
        }

        // Encoder turn: select pattern
        int enc = pod.encoder.Increment();
        if(enc != 0)
        {
            preset.pattern = (uint8_t)((preset.pattern + NUM_PATTERNS + enc) % NUM_PATTERNS);
            SendCC(14, preset.pattern * 8);
        }

        // LED 1: envelope brightness per channel (LP=blue, BP=green, HP=red)
        pod.led1.Set(
            ch[2].env.GetValue(),   // red   = HP
            ch[1].env.GetValue(),   // green = BP
            ch[0].env.GetValue()    // blue  = LP
        );

        // LED 2:
        //   Bypass              → solid white
        //   MIDI clock active   → solid green while running, off while stopped
        //   Internal clock mode → red beat-pulse while running, dim red while stopped
        if(bypass)
        {
            pod.led2.Set(1.f, 1.f, 1.f);
        }
        else if(midi_active)
        {
            pod.led2.Set(0.f, seq_running ? 1.f : 0.f, 0.f);
        }
        else
        {
            if(seq_running)
            {
                uint32_t elapsed  = now - led2_flash_ms;
                float    red      = (elapsed < 100) ? (1.f - elapsed / 100.f) : 0.f;
                pod.led2.Set(red, 0.f, 0.f);
            }
            else
            {
                pod.led2.Set(0.15f, 0.f, 0.f);  // dim red = internal clock mode, stopped
            }
        }

        pod.UpdateLeds();

        // Smooth main-loop iteration time (µs) and track per-window peak
        float loopUs = (System::GetTick() - loopStart) / ticksPerUs;
        loopLoadAvg  = 0.05f * loopUs + 0.95f * loopLoadAvg;
        if(loopUs > loopPeak) loopPeak = loopUs;

        if((now - lastLoadMs) >= 250)
        {
            lastLoadMs = now;
            // DSP: CC 10 = avg, CC 11 = peak (0–127 = 0–100 %)
            SendCC(10, (uint8_t)fclamp(dspLoad.GetAvgCpuLoad() * 127.f, 0.f, 127.f));
            SendCC(11, (uint8_t)fclamp(dspLoad.GetMaxCpuLoad() * 127.f, 0.f, 127.f));
            dspLoad.Reset();
            // Loop: CC 12 = avg, CC 13 = peak (0–127 = 0–500 µs)
            SendCC(12, (uint8_t)fclamp(loopLoadAvg / 500.f * 127.f, 0.f, 127.f));
            SendCC(13, (uint8_t)fclamp(loopPeak    / 500.f * 127.f, 0.f, 127.f));
            loopPeak = 0.f;
        }
    }
}
