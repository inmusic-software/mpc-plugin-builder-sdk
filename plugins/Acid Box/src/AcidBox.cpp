// Acid Box - an MPC instrument plugin by RS
//
// A monophonic acid bass synth in the spirit of the classic 303-style bass line
// machines: one band-limited saw/square oscillator into a resonant 18 dB/oct
// ladder low-pass filter, a decaying filter envelope, accent and slide.
//
// Signal flow:
//
//   MIDI -> note stack (last note wins) -> pitch (with slide glide + pitch bend)
//        -> PolyBLEP saw / square oscillator
//        -> 4-pole ladder filter, output tapped after pole 3 (18 dB/oct), with
//           resonance feedback. Cutoff = Cutoff * 2^(filterEnv * EnvMod * 5 oct)
//        -> amp envelope (fast attack, gate sustain, short release) * accent boost
//        -> Drive (soft saturation) -> Volume -> stereo out
//
// Playing it:
//   * Velocity >= "Acc Thresh" makes a note ACCENTED: louder, deeper filter sweep,
//     shorter decay. Consecutive accented notes build up extra emphasis.
//   * Slide: when a new note starts while the previous one is still held
//     (legato), the pitch glides to the new note over "Slide" ms and the envelopes
//     are NOT retriggered. Turn the Slide switch off to always retrigger.
//
// Rules of the road: nothing in process() allocates, locks or does I/O. Parameter 0
// (Volume) is harmless when nudged by the host.

#include "mpc_plugin.h"
#include "dsp/math.h"
#include "dsp/oscillator.h"

#include <cmath>

namespace {

// ---------------------------------------------------------------------------
// Parameter indices. Order here == order of addParam() in the constructor.
// IDs and order are permanent once users have projects: only append at the end.
// ---------------------------------------------------------------------------
enum ParamIndex
{
    kVolume = 0,   // dB, master volume (harmless when nudged by the host)
    kWave,         // choice: Saw / Square
    kCutoff,       // Hz, base filter cutoff
    kResonance,    // 0..100 %, filter feedback
    kEnvMod,       // 0..100 %, how far the filter envelope opens the cutoff
    kDecay,        // ms, filter envelope decay
    kAccent,       // 0..100 %, how strong accented notes are
    kDrive,        // 0..100 %, output saturation
    kSlideTime,    // ms, glide time for slid notes
    kSlide,        // bool: overlapping notes glide instead of retriggering
    kTune,         // cents, -100..100
    kOctave,       // octaves, -2..2
    kAccThresh,    // velocity at/above which a note is accented
    kGateRelease,  // ms, amp release after note off
    kNumParams
};

constexpr int   kMaxHeld   = 16;
constexpr int   kCtrlRate  = 8;       // filter coefficients are updated every 8 samples
constexpr float kEnvOctaves = 5.0f;   // cutoff sweep range at EnvMod = 100 %

// 4-pole zero-delay-feedback ladder low-pass. Output is tapped after pole 3
// (18 dB/oct). Resonance feeds pole 4 back to the input through a tanh.
struct Ladder
{
    float s[4] = {0, 0, 0, 0};
    float G = 0.1f;       // g / (1 + g)
    float k = 0.0f;       // feedback amount 0..~4.2

    void reset() { for (float& x : s) x = 0.0f; }

    void setCoeffs(float cutoffHz, float resonance01, float sampleRate)
    {
        const float fc = mpc::clamp(cutoffHz, 20.0f, 0.32f * sampleRate);
        const float g = std::tan(mpc::dsp::kPi * fc / sampleRate);
        G = g / (1.0f + g);
        k = 4.2f * mpc::clamp(resonance01, 0.0f, 1.0f);
    }

    float process(float x)
    {
        // Unit-delay feedback from the last pole (cheap, stable with the tanh).
        const float fb = k * s[3] * 0.5f;           // s[3] approximates pole-4 output
        float in = mpc::dsp::fastTanh(x - fb);
        float y3 = 0.0f;
        for (int i = 0; i < 4; ++i)
        {
            const float v = (in - s[i]) * G;
            const float lp = v + s[i];
            s[i] = lp + v;
            in = lp;
            if (i == 2) y3 = lp;
        }
        // Make up some of the low-frequency level that resonance removes.
        return y3 * (1.0f + 0.35f * k);
    }
};

} // namespace

class AcidBox : public mpc::Plugin
{
public:
    AcidBox() : mpc::Plugin(makeInfo())
    {
        addParam(mpc::floatParam("volume",    "Volume",    -40.0f, 6.0f,   -6.0f,  "dB", 1));
        addParam(mpc::choiceParam("wave",     "Wave",      {"Saw", "Square"}, 0));
        addParam(mpc::logParam  ("cutoff",    "Cutoff",    60.0f, 8000.0f, 500.0f, "Hz", 0));
        addParam(mpc::floatParam("resonance", "Reso",      0.0f, 100.0f,   60.0f,  "%",  0));
        addParam(mpc::floatParam("envmod",    "Env Mod",   0.0f, 100.0f,   60.0f,  "%",  0));
        addParam(mpc::logParam  ("decay",     "Decay",     30.0f, 2500.0f, 350.0f, "ms", 0));
        addParam(mpc::floatParam("accent",    "Accent",    0.0f, 100.0f,   70.0f,  "%",  0));
        addParam(mpc::floatParam("drive",     "Drive",     0.0f, 100.0f,   20.0f,  "%",  0));
        addParam(mpc::logParam  ("slidetime", "Slide Time", 5.0f, 500.0f,  60.0f,  "ms", 0));
        addParam(mpc::boolParam ("slide",     "Slide",     true));
        addParam(mpc::floatParam("tune",      "Tune",      -100.0f, 100.0f, 0.0f,  "ct", 0));
        addParam(mpc::intParam  ("octave",    "Octave",    -2, 2, 0, "oct"));
        addParam(mpc::intParam  ("accthresh", "Acc Thresh", 1, 127, 100, ""));
        addParam(mpc::logParam  ("gaterel",   "Gate Rel",  3.0f, 500.0f,   15.0f,  "ms", 0));

        // Program values, in parameter order:
        //  Vol  Wave Cutoff Reso EnvMod Decay Accent Drive SlideT Slide Tune Oct AccTh GateRel
        addProgram("Init Acid",    {-6, 0,  500, 60, 60,  350, 70, 20,  60, 1, 0, 0, 100, 15});
        addProgram("Classic Line", {-6, 0,  350, 75, 70,  300, 80, 25,  60, 1, 0, 0, 100, 12});
        addProgram("Squelch",      {-7, 0,  250, 90, 85,  220, 90, 35,  50, 1, 0, 0, 90,  12});
        addProgram("Square Bass",  {-6, 1,  420, 55, 55,  400, 60, 15,  70, 1, 0, -1, 100, 20});
        addProgram("Sub Pump",     {-5, 1,  180, 35, 40,  500, 50, 10,  40, 0, 0, -1, 110, 30});
        addProgram("Wide Open",    {-8, 0, 1200, 50, 40,  900, 60, 30,  90, 1, 0, 0, 100, 40});
        addProgram("Zap",          {-7, 0,  900, 85, 95,   80, 100, 40, 30, 0, 0, 1, 80,  8});
        addProgram("Slidey",       {-6, 0,  400, 70, 65,  600, 75, 25, 180, 1, 0, 0, 100, 25});
    }

    void prepare(double sampleRate, int maxBlockSize) override
    {
        (void)maxBlockSize;
        sampleRate_ = (float)sampleRate;
        osc_.setSampleRate(sampleRate);
        reset();
    }

    void reset() override
    {
        filter_.reset();
        numHeld_ = 0;
        gate_ = false;
        ampLevel_ = 0.0f;
        filtEnv_ = 0.0f;
        accentEnv_ = 0.0f;
        accentCap_ = 0.0f;
        curNote_ = 48.0f;
        targetNote_ = 48.0f;
        bend_ = 0.0f;
        accented_ = false;
        smoothedGain_ = mpc::dsp::dbToGain(get(kVolume));
        osc_.resetPhase(0.0f);
        ctrlCount_ = 0;
    }

    void process(mpc::ProcessContext& ctx) override
    {
        const int   numFrames = ctx.numFrames;
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;

        // ---- parameters, once per block --------------------------------------
        osc_.setWave(getInt(kWave) == 0 ? mpc::dsp::Wave::Saw : mpc::dsp::Wave::Square);
        const float baseCutoff = get(kCutoff);
        const float reso       = get(kResonance) * 0.01f;
        const float envMod     = get(kEnvMod) * 0.01f;
        const float decayMs    = get(kDecay);
        const float accentAmt  = get(kAccent) * 0.01f;
        const float driveAmt   = get(kDrive) * 0.01f;
        const float slideMs    = get(kSlideTime);
        const bool  slideOn    = getBool(kSlide);
        const float tuneSemi   = get(kTune) * 0.01f + 12.0f * (float)getInt(kOctave);
        const int   accThresh  = getInt(kAccThresh);
        const float gateRelMs  = get(kGateRelease);
        const float targetGain = mpc::dsp::dbToGain(get(kVolume));

        // Per-sample coefficients derived from times in ms.
        const float slideCoef  = 1.0f - std::exp(-1.0f / std::fmax(1.0f, slideMs * 0.001f * sampleRate_));
        const float attackInc  = 1.0f / (0.002f * sampleRate_);
        const float releaseCoef = 1.0f - std::exp(-1.0f / std::fmax(1.0f, gateRelMs * 0.001f * sampleRate_));
        const float accentDecayCoef = 1.0f - std::exp(-1.0f / (0.30f * sampleRate_));   // accent build-up fades in ~300 ms
        const float driveGain  = 1.0f + 7.0f * driveAmt;
        const float driveNorm  = 1.0f / mpc::dsp::fastTanh(driveGain);

        int midiIndex = 0;
        for (int i = 0; i < numFrames; ++i)
        {
            // ---- MIDI at exact sample positions ----
            while (midiIndex < ctx.numMidiEvents && ctx.midi[midiIndex].sampleOffset <= i)
            {
                handleMidi(ctx.midi[midiIndex], slideOn, accThresh);
                ++midiIndex;
            }

            // Filter-envelope decay time depends on accent: accented notes decay faster.
            const float effDecayMs = accented_ ? std::fmin(decayMs, 200.0f) : decayMs;
            const float filtDecayCoef = 1.0f - std::exp(-1.0f / std::fmax(1.0f, effDecayMs * 0.001f * sampleRate_ * 0.35f));

            // ---- pitch glide ----
            curNote_ += slideCoef * (targetNote_ - curNote_);

            // ---- envelopes ----
            if (gate_) { ampLevel_ += attackInc; if (ampLevel_ > 1.0f) ampLevel_ = 1.0f; }
            else       { ampLevel_ -= releaseCoef * ampLevel_; if (ampLevel_ < 1.0e-5f) ampLevel_ = 0.0f; }
            filtEnv_  -= filtDecayCoef * filtEnv_;
            accentCap_ -= accentDecayCoef * accentCap_;

            // ---- control-rate filter update ----
            if (ctrlCount_-- <= 0)
            {
                ctrlCount_ = kCtrlRate - 1;
                const float accentNow = accented_ ? accentAmt * (0.7f + 0.6f * accentCap_) : 0.0f;
                const float modDepth  = mpc::clamp(envMod + 0.6f * accentNow, 0.0f, 1.6f);
                const float cutoff    = baseCutoff * std::exp2(filtEnv_ * modDepth * kEnvOctaves);
                const float resoNow   = mpc::clamp(reso + 0.15f * accentNow, 0.0f, 1.0f);
                filter_.setCoeffs(cutoff, resoNow, sampleRate_);
                accentGainNow_ = 1.0f + 0.7f * accentNow;
                oscHz_ = mpc::noteToHz(curNote_ + tuneSemi + bend_ * 2.0f);
                osc_.setFrequency(oscHz_);
            }

            // ---- sound ----
            float x = osc_.process() * 0.7f;
            float y = filter_.process(x);
            y *= ampLevel_ * accentGainNow_;
            y = mpc::dsp::fastTanh(y * driveGain) * driveNorm;
            y = mpc::dsp::sanitize(mpc::dsp::flushDenormal(y));

            smoothedGain_ += 0.002f * (targetGain - smoothedGain_);
            const float out = y * smoothedGain_ * 0.8f;
            outL[i] = out;
            if (outR) outR[i] = out;
        }
    }

    void parameterChanged(int index, float plainValue) override
    {
        (void)index; (void)plainValue;
    }

private:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name         = "Acid Box";
        info.vendor       = "RS";   // must match plugin.json "manufacturer"
        info.uniqueId     = MPC_FOURCC(kUid[0], kUid[1], kUid[2], kUid[3]);   // must match plugin.json "uid"
        info.version      = 1000;
        info.isInstrument = true;
        info.numInputs    = 0;
        info.numOutputs   = 2;
        info.tailSamples  = 0;
        return info;
    }

    static constexpr char kUid[5] = "R7y3";

    // Last-note-priority note stack.
    void pushNote(int note)
    {
        removeNote(note);
        if (numHeld_ == kMaxHeld)
        {
            for (int i = 1; i < kMaxHeld; ++i) heldNotes_[i - 1] = heldNotes_[i];
            --numHeld_;
        }
        heldNotes_[numHeld_++] = note;
    }

    void removeNote(int note)
    {
        for (int i = 0; i < numHeld_; ++i)
        {
            if (heldNotes_[i] == note)
            {
                for (int j = i + 1; j < numHeld_; ++j) heldNotes_[j - 1] = heldNotes_[j];
                --numHeld_;
                return;
            }
        }
    }

    void handleMidi(const mpc::MidiEvent& e, bool slideOn, int accThresh)
    {
        if (e.isNoteOn())
        {
            const bool legato = gate_ && numHeld_ > 0;
            pushNote(e.note());
            targetNote_ = (float)e.note();
            const bool accented = e.velocity() >= accThresh;

            if (legato && slideOn)
            {
                // Slide: keep envelopes running, just glide the pitch. The accent
                // flag of the new note still applies to the sound.
                accented_ = accented;
            }
            else
            {
                curNote_ = targetNote_;       // no glide: jump straight to the pitch
                gate_ = true;
                filtEnv_ = 1.0f;
                accented_ = accented;
                if (accented) accentCap_ = accentCap_ + 0.4f * (1.0f - accentCap_);
                if (!legato) ampLevel_ = 0.0f;   // fresh start for a clean attack
            }
        }
        else if (e.isNoteOff())
        {
            removeNote(e.note());
            if (numHeld_ > 0)
                targetNote_ = (float)heldNotes_[numHeld_ - 1];   // fall back to the previous held note
            else
                gate_ = false;
        }
        else if (e.isAllNotesOff())
        {
            numHeld_ = 0;
            gate_ = false;
        }
        else if (e.isPitchBend())
        {
            bend_ = (float)e.pitchBend() / 8192.0f;   // -1..1, scaled to +-2 semitones
        }
    }

    mpc::dsp::PolyBlepOsc osc_;
    Ladder filter_;

    float sampleRate_ = 44100.0f;
    int   heldNotes_[kMaxHeld] = {};
    int   numHeld_ = 0;
    bool  gate_ = false;
    bool  accented_ = false;

    float ampLevel_ = 0.0f, filtEnv_ = 0.0f, accentEnv_ = 0.0f, accentCap_ = 0.0f;
    float curNote_ = 48.0f, targetNote_ = 48.0f, bend_ = 0.0f, oscHz_ = 130.0f;
    float accentGainNow_ = 1.0f;
    float smoothedGain_ = 0.5f;
    int   ctrlCount_ = 0;
};

// This macro creates the exported VSTPluginMain entry point. Keep it last.
MPC_PLUGIN_ENTRY(AcidBox)
