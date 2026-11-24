#include "mpc_plugin.h"
#include "KenzflxPresets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

static inline float clamp01(float x)
{
    return std::clamp(x, 0.0f, 1.0f);
}

static inline float softClip(float x)
{
    return std::tanh(x * 1.5f);
}

struct StereoDelay
{
    std::vector<float> buffer;
    size_t writeIndex = 0;
    size_t maxSamples = 0;

    void prepare(size_t maxSamples_)
    {
        maxSamples = std::max<size_t>(2, maxSamples_);
        buffer.assign(maxSamples, 0.0f);
        writeIndex = 0;
    }

    void reset()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    float process(float input, float delayMs, float feedback, float wet, float sampleRate)
    {
        if (buffer.empty())
            return input;

        const size_t delaySamples = std::clamp(
            static_cast<size_t>(delayMs * 0.001f * sampleRate),
            size_t{1},
            maxSamples - 1);

        const size_t readIndex = (writeIndex + maxSamples - delaySamples) % maxSamples;
        const float delayed = buffer[readIndex];

        buffer[writeIndex] = input + delayed * feedback;
        writeIndex = (writeIndex + 1) % maxSamples;

        return input * (1.0f - wet) + delayed * wet;
    }
};

struct StereoChorus
{
    StereoDelay left;
    StereoDelay right;
    float phase = 0.0f;
    float sampleRate = 44100.0f;

    void prepare(float rate)
    {
        sampleRate = std::max(1.0f, rate);
        left.prepare(static_cast<size_t>(sampleRate * 0.35f));
        right.prepare(static_cast<size_t>(sampleRate * 0.35f));
        phase = 0.0f;
    }

    void reset()
    {
        left.reset();
        right.reset();
        phase = 0.0f;
    }

    void process(float inL, float inR, float amount, float rateHz, float& outL, float& outR)
    {
        const float lfo = std::sin(phase * kTwoPi);
        const float modL = 4.0f + amount * 16.0f + lfo * (4.0f + amount * 14.0f);
        const float modR = 6.0f + amount * 18.0f - lfo * (4.0f + amount * 12.0f);

        phase += rateHz / sampleRate;
        if (phase >= 1.0f)
            phase -= 1.0f;

        outL = left.process(inL, modL, 0.34f + amount * 0.4f, 0.45f + amount * 0.45f, sampleRate);
        outR = right.process(inR, modR, 0.32f + amount * 0.38f, 0.45f + amount * 0.45f, sampleRate);
    }
};

struct StereoReverb
{
    StereoDelay d1;
    StereoDelay d2;
    StereoDelay d3;
    StereoDelay d4;
    float sampleRate = 44100.0f;

    void prepare(float rate)
    {
        sampleRate = std::max(1.0f, rate);
        d1.prepare(static_cast<size_t>(sampleRate * 0.12f));
        d2.prepare(static_cast<size_t>(sampleRate * 0.18f));
        d3.prepare(static_cast<size_t>(sampleRate * 0.25f));
        d4.prepare(static_cast<size_t>(sampleRate * 0.33f));
    }

    void reset()
    {
        d1.reset();
        d2.reset();
        d3.reset();
        d4.reset();
    }

    void process(float inL, float inR, float amount, float damp, float& outL, float& outR)
    {
        const float w = amount * 0.8f;

        const float aL = d1.process(inL, 32.0f + amount * 80.0f, 0.72f, w, sampleRate);
        const float aR = d2.process(inR, 40.0f + amount * 90.0f, 0.70f, w, sampleRate);

        const float bL = d3.process(aL, 72.0f + amount * 130.0f, 0.68f, w * 0.7f, sampleRate);
        const float bR = d4.process(aR, 84.0f + amount * 140.0f, 0.66f, w * 0.7f, sampleRate);

        const float lp = 0.2f + damp * 0.7f;
        outL = aL * (1.0f - lp) + bL * lp;
        outR = aR * (1.0f - lp) + bR * lp;
    }
};

enum class TimeMode
{
    Bypass = 0,
    HalfTime = 1,
    FlexBeat = 2,
    Reverse = 3
};

struct TempoSliceEngine
{
    std::vector<float> historyL;
    std::vector<float> historyR;
    size_t historySize = 0;
    size_t writeIndex = 0;

    void prepare(float sampleRate)
    {
        historySize = std::max<size_t>(4096, static_cast<size_t>(sampleRate * 2.5f));
        historyL.assign(historySize, 0.0f);
        historyR.assign(historySize, 0.0f);
        writeIndex = 0;
    }

    void reset()
    {
        std::fill(historyL.begin(), historyL.end(), 0.0f);
        std::fill(historyR.begin(), historyR.end(), 0.0f);
        writeIndex = 0;
    }

    static inline float crossFade(float a, float b, float x)
    {
        const float t = std::clamp(x, 0.0f, 1.0f);
        return a * (1.0f - t) + b * t;
    }

    void process(float inL, float inR,
                 float reverseValue,
                 double bpm,
                 double ppqPos,
                 int numerator,
                 int denominator,
                 float sampleRate,
                 float& outL,
                 float& outR)
    {
        if (historySize == 0)
        {
            outL = inL;
            outR = inR;
            return;
        }

        historyL[writeIndex] = inL;
        historyR[writeIndex] = inR;
        writeIndex = (writeIndex + 1) % historySize;

        const float rv = clamp01(reverseValue);
        if (rv < 0.01f)
        {
            outL = inL;
            outR = inR;
            return;
        }

        const TimeMode mode = (rv <= 0.25f) ? TimeMode::HalfTime :
                              (rv <= 0.75f) ? TimeMode::FlexBeat : TimeMode::Reverse;

        const double safeBpm = std::max(1.0, bpm);
        const int numBeats = std::max(1, numerator);
        const int den = std::max(1, denominator);
        const double barSeconds = (60.0 / safeBpm) * (4.0 * static_cast<double>(numBeats) / static_cast<double>(den));
        const size_t barSamples = std::clamp(
            static_cast<size_t>(barSeconds * sampleRate),
            size_t{1024},
            historySize - 1);
        const size_t sliceSamples = std::max<size_t>(64, barSamples / 8);

        const float fadeWidth = std::clamp(0.005f * sampleRate, 32.0f, 128.0f);
        const float fadeAlpha = std::clamp(fadeWidth / std::max(1.0f, static_cast<float>(sliceSamples)), 0.0f, 1.0f);

        float wetL = inL;
        float wetR = inR;

        switch (mode)
        {
            case TimeMode::HalfTime:
            {
                const size_t delaySamples = std::max<size_t>(64, barSamples / 2);
                const size_t readIndex = (writeIndex + historySize - delaySamples) % historySize;

                const float delayedL = historyL[readIndex];
                const float delayedR = historyR[readIndex];
                const float mix = 0.5f + 0.5f * std::sin(static_cast<float>(ppqPos) * 0.25f);
                wetL = crossFade(inL, delayedL, mix);
                wetR = crossFade(inR, delayedR, mix);

                const float a = std::clamp((rv - 0.01f) / 0.24f, 0.0f, 1.0f);
                wetL = crossFade(inL, wetL, a * (1.0f - fadeAlpha));
                wetR = crossFade(inR, wetR, a * (1.0f - fadeAlpha));
                break;
            }

            case TimeMode::FlexBeat:
            {
                const size_t local = writeIndex % barSamples;
                const size_t sliceIndex = local / sliceSamples;
                const size_t patternIndex = static_cast<size_t>((static_cast<float>(sliceIndex + 1) * (0.15f + rv * 1.2f))) % 6u;

                static constexpr std::array<std::array<int, 8>, 6> patterns = {{
                    {{0, 1, 2, 3, 4, 5, 6, 7}},
                    {{0, 2, 1, 3, 5, 4, 6, 7}},
                    {{0, 4, 1, 5, 2, 6, 3, 7}},
                    {{0, 3, 6, 1, 4, 7, 2, 5}},
                    {{0, 5, 2, 7, 1, 6, 3, 4}},
                    {{0, 1, 4, 7, 2, 5, 6, 3}}
                }};

                const size_t targetSlice = static_cast<size_t>(patterns[patternIndex][sliceIndex % 8]);
                const size_t targetOffset = std::min(targetSlice * sliceSamples + (local % sliceSamples), historySize - 1);

                const size_t readIndex = (writeIndex + historySize - targetOffset - 1) % historySize;
                const size_t readIndex2 = (readIndex + historySize - 1) % historySize;

                const float l0 = historyL[readIndex];
                const float r0 = historyR[readIndex];
                const float l1 = historyL[readIndex2];
                const float r1 = historyR[readIndex2];

                const float alpha = std::clamp((static_cast<float>(local % sliceSamples)) / static_cast<float>(sliceSamples), 0.0f, 1.0f);
                wetL = crossFade(l0, l1, alpha);
                wetR = crossFade(r0, r1, alpha);

                const float floorMix = std::clamp((rv - 0.26f) / 0.49f, 0.0f, 1.0f);
                wetL = crossFade(inL, wetL, floorMix * (1.0f - fadeAlpha));
                wetR = crossFade(inR, wetR, floorMix * (1.0f - fadeAlpha));
                break;
            }

            case TimeMode::Reverse:
            default:
            {
                const size_t bounceSamples = std::min(std::max<size_t>(128, barSamples / 2), historySize - 1);
                const size_t readIndex = (writeIndex + historySize - bounceSamples - 1) % historySize;
                const size_t readIndex2 = (readIndex + historySize - 1) % historySize;

                const float reverseL = historyL[readIndex];
                const float reverseR = historyR[readIndex];
                const float reverseL2 = historyL[readIndex2];
                const float reverseR2 = historyR[readIndex2];

                const float alpha = std::clamp((rv - 0.76f) / 0.24f, 0.0f, 1.0f);
                wetL = crossFade(inL, reverseL, alpha);
                wetR = crossFade(inR, reverseR, alpha);

                wetL = crossFade(wetL, reverseL2, alpha * fadeAlpha);
                wetR = crossFade(wetR, reverseR2, alpha * fadeAlpha);
                break;
            }
        }

        outL = wetL;
        outR = wetR;
    }
};

struct KenzFlxDSP
{
    float output = 1.0f;
    float drive = 0.45f;
    float warmth = 0.50f;
    float tone = 0.50f;
    float chorus = 0.25f;
    float delay = 0.35f;
    float reverb = 0.30f;
    float crush = 0.10f;
    float mix = 0.70f;
    float reverse = 0.0f;
    float inputGainDb = 0.0f;

    float sampleRate = 44100.0f;
    double bpm = 120.0;
    double ppqPos = 0.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;

    StereoChorus chorusStage;
    StereoDelay delayStageL;
    StereoDelay delayStageR;
    StereoReverb reverbStage;
    TempoSliceEngine timeEngine;

    float lpL = 0.0f;
    float lpR = 0.0f;
    float hpL = 0.0f;
    float hpR = 0.0f;
    float hpInputL = 0.0f;
    float hpInputR = 0.0f;

    void prepare(double sr)
    {
        sampleRate = std::max(1.0f, static_cast<float>(sr));
        chorusStage.prepare(sampleRate);
        delayStageL.prepare(static_cast<size_t>(sampleRate * 0.8f));
        delayStageR.prepare(static_cast<size_t>(sampleRate * 0.8f));
        reverbStage.prepare(sampleRate);
        timeEngine.prepare(sampleRate);
        lpL = lpR = hpL = hpR = hpInputL = hpInputR = 0.0f;
    }

    void reset()
    {
        lpL = lpR = hpL = hpR = hpInputL = hpInputR = 0.0f;
        chorusStage.reset();
        delayStageL.reset();
        delayStageR.reset();
        reverbStage.reset();
        timeEngine.reset();
    }

    void processBlock(float* leftChannel, float* rightChannel, int numSamples)
    {
        if (numSamples <= 0)
            return;

        const float inputGainLinear = std::pow(10.0f, inputGainDb / 20.0f);
        const float steps = std::max(8.0f, 256.0f * (1.0f - crush * 0.9f));

        for (int i = 0; i < numSamples; ++i)
        {
            const float inL = leftChannel[i] * inputGainLinear;
            const float inR = rightChannel[i] * inputGainLinear;

            float sL = softClip(inL * (1.0f + drive * 3.5f));
            float sR = softClip(inR * (1.0f + drive * 3.5f));

            const float toneNorm = std::clamp(tone, 0.0f, 1.0f);
            float lowAlpha = 0.0f;
            float highAlpha = 0.0f;

            if (toneNorm < 0.45f)
            {
                const float r = toneNorm / 0.45f;
                const float cutoff = 180.0f + r * r * 16000.0f;
                const float rc = 1.0f / (kTwoPi * cutoff);
                lowAlpha = (1.0f / sampleRate) / (rc + 1.0f / sampleRate);
            }
            else if (toneNorm > 0.55f)
            {
                const float r = (toneNorm - 0.55f) / 0.45f;
                const float cutoff = 45.0f + r * r * 6000.0f;
                const float rc = 1.0f / (kTwoPi * cutoff);
                highAlpha = rc / (rc + 1.0f / sampleRate);
            }

            if (lowAlpha > 0.0f)
            {
                lpL += lowAlpha * (sL - lpL);
                lpR += lowAlpha * (sR - lpR);
                sL = lpL;
                sR = lpR;
            }
            else if (highAlpha > 0.0f)
            {
                hpL = highAlpha * (hpL + sL - hpInputL);
                hpR = highAlpha * (hpR + sR - hpInputR);
                hpInputL = sL;
                hpInputR = sR;
                sL = hpL;
                sR = hpR;
            }
            else
            {
                hpInputL = sL;
                hpInputR = sR;
            }

            float timeL = sL;
            float timeR = sR;
            float tempL = sL;
            float tempR = sR;
            timeEngine.process(sL, sR, reverse, bpm, ppqPos, timeSigNumerator,
                               timeSigDenominator, sampleRate, tempL, tempR);
            if (reverse > 0.01f)
            {
                const float blend = std::clamp((reverse - 0.01f) / 0.99f, 0.0f, 1.0f);
                timeL = sL * (1.0f - blend) + tempL * blend;
                timeR = sR * (1.0f - blend) + tempR * blend;
            }

            float chorusL = 0.0f;
            float chorusR = 0.0f;
            chorusStage.process(timeL, timeR, chorus, 0.25f + chorus * 1.6f, chorusL, chorusR);

            const float delayMs = 30.0f + delay * 220.0f;
            const float delayWet = 0.2f + delay * 0.7f;
            const float delayFeedback = 0.30f + delay * 0.45f;

            const float dL = delayStageL.process(chorusL, delayMs, delayFeedback, delayWet, sampleRate);
            const float dR = delayStageR.process(chorusR, delayMs * 1.1f, delayFeedback, delayWet, sampleRate);

            float rvL = 0.0f;
            float rvR = 0.0f;
            reverbStage.process(chorusL + dL, chorusR + dR, reverb, 0.3f + warmth * 0.5f, rvL, rvR);

            float crushL = timeL + dL * 0.8f;
            float crushR = timeR + dR * 0.8f;

            if (crush > 0.01f)
            {
                crushL = std::round(crushL * steps) / steps;
                crushR = std::round(crushR * steps) / steps;
            }

            const float finalMix = std::clamp(mix, 0.0f, 1.0f);
            float outL = inL * (1.0f - finalMix) +
                         ((crushL * (1.0f - reverb * 0.5f)) + rvL * reverb * 0.6f + dL * delay * 0.5f) * finalMix;
            float outR = inR * (1.0f - finalMix) +
                         ((crushR * (1.0f - reverb * 0.5f)) + rvR * reverb * 0.6f + dR * delay * 0.5f) * finalMix;

            outL = softClip(outL * (1.0f + warmth * 1.3f)) * output;
            outR = softClip(outR * (1.0f + warmth * 1.3f)) * output;

            leftChannel[i] = outL;
            rightChannel[i] = outR;
        }
    }
};

} // namespace

class KENZFLX : public mpc::Plugin
{
public:
    enum Params
    {
        kOutput = 0,
        kDrive,
        kWarmth,
        kTone,
        kChorus,
        kDelay,
        kReverb,
        kCrush,
        kMix,
        kReverse,
        kInputGain,
        kNumParams
    };

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name = "KENZFLX";
        info.vendor = "KENZ";
        info.uniqueId = MPC_FOURCC('K', 'F', 'V', 'B');
        info.version = 1000;
        info.isInstrument = false;
        info.numInputs = 2;
        info.numOutputs = 2;
        info.tailSamples = 0;
        return info;
    }

    KENZFLX() : mpc::Plugin(makeInfo())
    {
        addParam(mpc::floatParam("output", "Output", 0.0f, 1.0f, 1.0f, "", 2));
        addParam(mpc::floatParam("drive", "Drive", 0.0f, 1.0f, 0.45f, "", 2));
        addParam(mpc::floatParam("warmth", "Warmth", 0.0f, 1.0f, 0.50f, "", 2));
        addParam(mpc::floatParam("tone", "Tone", 0.0f, 1.0f, 0.50f, "", 2));
        addParam(mpc::floatParam("chorus", "Chorus", 0.0f, 1.0f, 0.25f, "", 2));
        addParam(mpc::floatParam("delay", "Delay", 0.0f, 1.0f, 0.35f, "", 2));
        addParam(mpc::floatParam("reverb", "Reverb", 0.0f, 1.0f, 0.30f, "", 2));
        addParam(mpc::floatParam("crush", "Crush", 0.0f, 1.0f, 0.10f, "", 2));
        addParam(mpc::floatParam("mix", "Mix", 0.0f, 1.0f, 0.70f, "", 2));
        addParam(mpc::floatParam("reverse", "Reverse", 0.0f, 1.0f, 0.0f, "", 2));
        addParam(mpc::floatParam("inputGain", "Input Gain", -24.0f, 0.0f, 0.0f, "dB", 1));

        for (int i = 0; i < kNumPresets; ++i)
        {
            const KenzflxPreset& p = kKenzflxPresets[i];
            addProgram(p.name, {
                1.0f,
                p.drive,
                p.warmth,
                p.tone,
                p.chorus,
                p.delay,
                p.reverb,
                p.crush,
                p.mix,
                p.reverse,
                0.0f
            });
        }

        setProgram(0);
    }

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        dsp_.prepare(sampleRate);
    }

    void reset() override
    {
        dsp_.reset();
    }

    void process(mpc::ProcessContext& ctx) override
    {
        if (ctx.numInputs < 1 || ctx.numOutputs < 1 ||
            ctx.inputs == nullptr || ctx.outputs == nullptr ||
            ctx.inputs[0] == nullptr || ctx.outputs[0] == nullptr)
            return;

        const float* inputL = ctx.inputs[0];
        const float* inputR =
            (ctx.numInputs > 1 && ctx.inputs[1] != nullptr) ? ctx.inputs[1] : ctx.inputs[0];

        float* outputL = ctx.outputs[0];
        float* outputR =
            (ctx.numOutputs > 1 && ctx.outputs[1] != nullptr) ? ctx.outputs[1] : ctx.outputs[0];

        dsp_.output = get(kOutput);
        dsp_.drive = get(kDrive);
        dsp_.warmth = get(kWarmth);
        dsp_.tone = get(kTone);
        dsp_.chorus = get(kChorus);
        dsp_.delay = get(kDelay);
        dsp_.reverb = get(kReverb);
        dsp_.crush = get(kCrush);
        dsp_.mix = get(kMix);
        dsp_.reverse = get(kReverse);
        dsp_.inputGainDb = get(kInputGain);

        if (ctx.transport.valid)
        {
            dsp_.bpm = ctx.transport.tempoValid ? ctx.transport.bpm : 120.0;
            dsp_.ppqPos = ctx.transport.ppqValid ? ctx.transport.ppqPos : 0.0;
            dsp_.timeSigNumerator = ctx.transport.timeSigNumerator > 0 ? ctx.transport.timeSigNumerator : 4;
            dsp_.timeSigDenominator = ctx.transport.timeSigDenominator > 0 ? ctx.transport.timeSigDenominator : 4;
        }

        if (inputL != outputL)
            std::copy(inputL, inputL + ctx.numFrames, outputL);

        if (outputR != outputL && inputR != outputR)
            std::copy(inputR, inputR + ctx.numFrames, outputR);

        if (outputR != outputL)
            dsp_.processBlock(outputL, outputR, ctx.numFrames);
        else
            dsp_.processBlock(outputL, outputL, ctx.numFrames);
    }

private:
    KenzFlxDSP dsp_;
};

MPC_PLUGIN_ENTRY(KENZFLX)
