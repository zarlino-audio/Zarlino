#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
namespace
{
    // dB conversion helpers
    static float dbToGain (float db) { return juce::Decibels::decibelsToGain (db, -120.0f); }

    // Simple first-order DC blocker (removes asymmetry-induced DC offset / hum).
    struct DCBlocker
    {
        float x = 0.0f, y = 0.0f;
        void reset() { x = 0.0f; y = 0.0f; }
        inline float process (float in)
        {
            const float out = in - x + 0.999f * y;
            x = in;
            y = out;
            return out;
        }
    };

    // Saturation waveshaper. Operates on a single (oversampled) sample.
    static inline float waveshape (int mode, float x, float drive, float asym)
    {
        const float g = x * (1.0f + drive * 3.0f);
        const float a = asym;
        switch (mode)
        {
            case ZScorchAudioProcessor::Tube:
            {
                const float pos = std::tanh (g * (1.0f + a * 0.5f) * 1.5f);
                const float neg = std::tanh (g * (1.0f - a * 0.5f) * 1.5f);
                return 1.2f * ((g >= 0.0f) ? pos : neg);
            }
            case ZScorchAudioProcessor::Tape:
            {
                float y = std::atan (g * 1.9f + a * 0.25f) * (2.0f / juce::MathConstants<float>::pi);
                const float g3 = g * g * g;
                y += g3 * 0.10f * (1.0f - std::abs (a));
                return 1.1f * y;
            }
            case ZScorchAudioProcessor::Transistor:
            {
                const float knee = 0.55f + a * 0.2f;
                const float absG = std::abs (g);
                if (absG < knee)
                    return 1.2f * g;
                const float sign = (g > 0.0f) ? 1.0f : -1.0f;
                const float excess = absG - knee;
                return 1.2f * sign * (knee + std::tanh (excess * 2.0f) * (1.0f - knee));
            }
            case ZScorchAudioProcessor::Germanium:
            {
                const float softClip = std::tanh (g * 1.7f + a * 0.4f);
                return 1.25f * (softClip - 0.04f * std::exp (-g * g * 3.0f));
            }
            case ZScorchAudioProcessor::Diode:
            {
                const float posThresh = 0.50f - a * 0.15f;
                const float negThresh = 0.50f + a * 0.15f;
                const float xc = juce::jlimit (-1.0f, 1.0f, g);
                if (xc > posThresh)
                    return posThresh + std::tanh ((xc - posThresh) * 3.0f) * 0.35f;
                if (xc < -negThresh)
                    return -negThresh + std::tanh ((xc + negThresh) * 3.0f) * 0.35f;
                return 1.05f * xc;
            }
            default: // Wavefold
            {
                float y = std::sin (g * juce::MathConstants<float>::pi * 1.1f) * 0.9f;
                y = y * 0.85f + g * 0.15f * (1.0f - std::abs (a));
                return y;
            }
        }
    }

    // Stereo RBJ biquad filter (used by the pre/post emphasis EQ).
    struct BiquadFilter
    {
        juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                       juce::dsp::IIR::Coefficients<float>> f;
        void prepare (const juce::dsp::ProcessSpec& spec) { f.prepare (spec); }
        void reset() { f.reset(); }
        void process (juce::AudioBuffer<float>& buf)
        {
            auto block = juce::dsp::AudioBlock<float> (buf);
            juce::dsp::ProcessContextReplacing<float> ctx (block);
            f.process (ctx);
        }
    };

    // 1 ms-lookahead ceiling limiter with a smoothed peak-hold envelope.
    // The one-pole peak-hold (fast attack ~1 ms, medium release ~80 ms) removes the
    // instantaneous per-sample gain-reduction steps that produced zipper / crackle
    // artefacts under the multiband saturation -> dry/wet -> limiter chain.
    struct CeilingLimiter
    {
        int lookaheadSamples = 48;
        juce::AudioBuffer<float> delayBuf;
        int writePos = 0;
        float env = 1.0f;
        float attackCoef = 0.2f;
        float releaseCoef = 0.01f;

        void prepare (const juce::dsp::ProcessSpec& spec)
        {
            lookaheadSamples = juce::jmax (1, (int) (spec.sampleRate * 0.001));
            // Fast attack (reach ~1ms), medium release (~80ms).
            attackCoef  = 1.0f - std::exp (-1.0f / (0.001f * (float) spec.sampleRate));
            releaseCoef = 1.0f - std::exp (-1.0f / (0.080f * (float) spec.sampleRate));
            delayBuf.setSize ((int) spec.numChannels,
                              (int) spec.maximumBlockSize + lookaheadSamples * 2,
                              false, false, true);
            delayBuf.clear ();
            writePos = 0;
            env = 1.0f;
        }
        void reset ()
        {
            delayBuf.clear ();
            writePos = 0;
            env = 1.0f;
        }

        void process (juce::AudioBuffer<float>& buffer, float ceilingDb, float& gainReduction)
        {
            const int n = buffer.getNumSamples ();
            const int chans = buffer.getNumChannels ();
            const float ceiling = dbToGain (ceilingDb);
            const int dSize = delayBuf.getNumSamples ();
            gainReduction = 0.0f;

            for (int ch = 0; ch < chans; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                auto* delay = delayBuf.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                {
                    delay[writePos] = data[i];
                    const int readPos = (writePos + lookaheadSamples) % dSize;
                    const float incoming = delay[readPos];

                    // Instantaneous required gain reduction (1.0 = none).
                    float inst = 1.0f;
                    const float a = std::abs (incoming);
                    if (a > ceiling && a > 1e-6f)
                        inst = ceiling / a;

                    // Smooth the gain-reduction envelope (fast attack, slow release).
                    if (inst < env)
                        env = env + attackCoef * (inst - env);
                    else
                        env = env + releaseCoef * (inst - env);
                    env = juce::jlimit (0.001f, 1.0f, env);

                    // Apply smoothed gain with an arctan soft-knee final safety.
                    float out = delay[readPos] * env;
                    out = std::tanh (out / (ceiling + 1e-6f)) * ceiling;
                    data[i] = out;

                    if (env < 1.0f)
                        gainReduction = juce::jmax (gainReduction, 20.0f * std::log10 (env));
                    writePos = (writePos + 1) % dSize;
                }
            }
        }
    };
}

//==============================================================================
// ZScorchAudioProcessor::Engine - the real, anti-aliased DSP implementation.
struct ZScorchAudioProcessor::Engine
{
    // Pre / post emphasis EQ stages.
    BiquadFilter preLow, preBell, preHigh;
    BiquadFilter postLow, postBell, postHigh;

    // LR4 Linkwitz-Riley crossover. Each split point = two cascaded Butterworth
    // biquads wrapped in a stereo ProcessorDuplicator (handles both channels).
    struct StereoBiquad
    {
        juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                       juce::dsp::IIR::Coefficients<float>> f;
        void prepare (const juce::dsp::ProcessSpec& spec) { f.prepare (spec); }
        void reset () { f.reset (); }
    };
    StereoBiquad lp1a, lp1b, hp1a, hp1b;
    StereoBiquad lp2a, lp2b, hp2a, hp2b;
    StereoBiquad lp3a, lp3b, hp3a, hp3b;

    // Per-band oversampled saturator with a DC blocker (stereo).
    struct BandSat
    {
        std::unique_ptr<juce::dsp::Oversampling<float>> os;
        std::array<DCBlocker, 2> dc;
        int   mode  = 0;
        float drive = 0.0f, asym = 0.0f, mix = 1.0f;
        bool  solo  = false, mute = false;
    };
    std::array<BandSat, 4> bands;

    // Lookahead limiter at the end.
    CeilingLimiter limiter;
    // Master DC blocker: removes residual DC/hum summed by the per-band
    // saturators at recombination so it never contaminates the limiter.
    std::array<DCBlocker, 2> masterDc;

    juce::AudioBuffer<float> dry;
    juce::AudioBuffer<float> rest;
    std::array<juce::AudioBuffer<float>, 4> bandBuf;
    std::array<juce::AudioBuffer<float>, 4> bandDry;

    enum { osFactor = 4 };

    void setCrossover (double sr, float f1, float f2, float f3)
    {
        auto makeLP = [] (double sr, float hz) {
            return juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, hz, 0.7071f);
        };
        auto makeHP = [] (double sr, float hz) {
            return juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, hz, 0.7071f);
        };
        *lp1a.f.state = *makeLP (sr, f1);  *lp1b.f.state = *makeLP (sr, f1);
        *hp1a.f.state = *makeHP (sr, f1);  *hp1b.f.state = *makeHP (sr, f1);
        *lp2a.f.state = *makeLP (sr, f2);  *lp2b.f.state = *makeLP (sr, f2);
        *hp2a.f.state = *makeHP (sr, f2);  *hp2b.f.state = *makeHP (sr, f2);
        *lp3a.f.state = *makeLP (sr, f3);  *lp3b.f.state = *makeLP (sr, f3);
        *hp3a.f.state = *makeHP (sr, f3);  *hp3b.f.state = *makeHP (sr, f3);
    }

    void setPreEmphasis (double sr, float lf, float lg, float lq,
                         float bf, float bg, float bq,
                         float hf, float hg, float hq)
    {
        *preLow.f.state  = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (sr, lf, lq, dbToGain (lg));
        *preBell.f.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, bf, bq, dbToGain (bg));
        *preHigh.f.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, hf, hq, dbToGain (hg));
    }

    void setPostEmphasis (double sr, float lf, float lg, float lq,
                          float bf, float bg, float bq,
                          float hf, float hg, float hq)
    {
        *postLow.f.state  = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (sr, lf, lq, dbToGain (lg));
        *postBell.f.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, bf, bq, dbToGain (bg));
        *postHigh.f.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, hf, hq, dbToGain (hg));
    }
    void reset ()
    {
        preLow.reset (); preBell.reset (); preHigh.reset ();
        postLow.reset (); postBell.reset (); postHigh.reset ();
        lp1a.reset (); lp1b.reset (); hp1a.reset (); hp1b.reset ();
        lp2a.reset (); lp2b.reset (); hp2a.reset (); hp2b.reset ();
        lp3a.reset (); lp3b.reset (); hp3a.reset (); hp3b.reset ();
        limiter.reset ();
        for (auto& d : masterDc) d.reset ();
        for (auto& b : bands)
        {
            if (b.os) b.os->reset ();
            for (auto& d : b.dc) d.reset ();
        }
        dry.clear (); rest.clear ();
        for (auto& bb : bandBuf) bb.clear ();
        for (auto& bd : bandDry) bd.clear ();
    }
    void prepare (double sampleRate, int samplesPerBlock, int numChannels)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
        spec.numChannels = (juce::uint32) numChannels;

        preLow.prepare (spec); preBell.prepare (spec); preHigh.prepare (spec);
        postLow.prepare (spec); postBell.prepare (spec); postHigh.prepare (spec);
        lp1a.prepare (spec); lp1b.prepare (spec); hp1a.prepare (spec); hp1b.prepare (spec);
        lp2a.prepare (spec); lp2b.prepare (spec); hp2a.prepare (spec); hp2b.prepare (spec);
        lp3a.prepare (spec); lp3b.prepare (spec); hp3a.prepare (spec); hp3b.prepare (spec);

        for (auto& b : bands)
        {
            b.os = std::make_unique<juce::dsp::Oversampling<float>> (
                (size_t) numChannels, (size_t) osFactor,
                juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple);
            b.os->initProcessing ((size_t) samplesPerBlock);
        }
        limiter.prepare (spec);

        dry.setSize (numChannels, samplesPerBlock, false, false, true);
        rest.setSize (numChannels, samplesPerBlock, false, false, true);
        for (auto& bb : bandBuf) bb.setSize (numChannels, samplesPerBlock, false, false, true);
        for (auto& bd : bandDry) bd.setSize (numChannels, samplesPerBlock, false, false, true);
        reset ();
    }

    void runCascade (StereoBiquad& a, StereoBiquad& b, juce::AudioBuffer<float>& buf)
    {
        auto block = juce::dsp::AudioBlock<float> (buf);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        a.f.process (ctx); b.f.process (ctx);
    }

    void splitAndSaturate (juce::AudioBuffer<float>& out, int chans, int n,
                           const juce::AudioBuffer<float>& source, bool anySolo)
    {
        // band0 = LP24(f1) on source; rest = HP24(f1) on source
        bandBuf[0].makeCopyOf (source, true);  runCascade (lp1a, lp1b, bandBuf[0]);
        rest.makeCopyOf (source, true);        runCascade (hp1a, hp1b, rest);
        // band1 = LP24(f2) on rest; band2/3 from HP24(f2) on rest
        bandBuf[1].makeCopyOf (rest, true);    runCascade (lp2a, lp2b, bandBuf[1]);
        rest.makeCopyOf (rest, true);          runCascade (hp2a, hp2b, rest);
        bandBuf[2].makeCopyOf (rest, true);    runCascade (lp3a, lp3b, bandBuf[2]);
        bandBuf[3].makeCopyOf (rest, true);    runCascade (hp3a, hp3b, bandBuf[3]);

        for (int b = 0; b < 4; ++b)
        {
            auto& sat = bands[b];
            const bool engaged = (! sat.mute);
            const bool audible = (anySolo ? sat.solo : engaged);
            bandDry[b].makeCopyOf (bandBuf[b], true);

            if (audible)
            {
                auto block = juce::dsp::AudioBlock<float> (bandBuf[b]);
                auto osBlock = sat.os->processSamplesUp (block);
                const int osN = (int) osBlock.getNumSamples ();
                for (int ch = 0; ch < chans; ++ch)
                {
                    auto* d = osBlock.getChannelPointer (ch);
                    for (int i = 0; i < osN; ++i)
                        d[i] = waveshape (sat.mode, d[i], sat.drive, sat.asym);
                }
                sat.os->processSamplesDown (osBlock);

                for (int ch = 0; ch < chans; ++ch)
                {
                    auto* w = bandBuf[b].getWritePointer (ch);
                    const auto* osSamples = osBlock.getChannelPointer (ch);
                    auto& dc = sat.dc[ch];
                    for (int i = 0; i < n; ++i)
                        w[i] = dc.process (osSamples[i]);
                    const auto* dryP = bandDry[b].getReadPointer (ch);
                    const float m = sat.mix;
                    for (int i = 0; i < n; ++i)
                        w[i] = w[i] * m + dryP[i] * (1.0f - m);
                }
            }
        }

        // Recombine -> out (wet).
        out.clear ();
        for (int b = 0; b < 4; ++b)
        {
            const auto& sat = bands[b];
            const bool engaged = (! sat.mute);
            const bool audible = (anySolo ? sat.solo : engaged);
            if (audible)
                for (int ch = 0; ch < chans; ++ch)
                    out.addFrom (ch, 0, bandBuf[b], ch, 0, n);
        }
    }
    // Full chain. `buffer` in/out.
    void process (juce::AudioBuffer<float>& buffer,
                  float dryWet, float ceilingDb, float& gainRed)
    {
        const int n = buffer.getNumSamples ();
        const int chans = buffer.getNumChannels ();

        // Pre-emphasis.
        preLow.process (buffer); preBell.process (buffer); preHigh.process (buffer);

        // Snapshot dry for the dry/wet mix.
        dry.makeCopyOf (buffer, true);

        // Crossover + oversampled saturation + recombine (wet into buffer).
        bool anySolo = false;
        for (auto& b : bands) if (b.solo) { anySolo = true; break; }
        splitAndSaturate (buffer, chans, n, buffer, anySolo);

        // Dry/wet mix (buffer currently = wet).
        for (int ch = 0; ch < chans; ++ch)
        {
            const auto* d = dry.getReadPointer (ch);
            auto* i = buffer.getWritePointer (ch);
            for (int s = 0; s < n; ++s)
                i[s] = i[s] * dryWet + d[s] * (1.0f - dryWet);
        }

        // Post-emphasis.
        postLow.process (buffer); postBell.process (buffer); postHigh.process (buffer);

        // Master DC block (kills residual hum/DC before the limiter).
        for (int ch = 0; ch < chans; ++ch)
        {
            auto* i = buffer.getWritePointer (ch);
            auto& dc = masterDc[ch];
            for (int s = 0; s < n; ++s)
                i[s] = dc.process (i[s]);
        }

        // Ceiling limiter.
        gainRed = 0.0f;
        limiter.process (buffer, ceilingDb, gainRed);
    }
};
//==============================================================================
const char* ZScorchAudioProcessor::satParamId (const char* base, int band)
{
    static char buf[32];
    snprintf (buf, sizeof (buf), "%s%s", base, bandNames[band]);
    return buf;
}

//==============================================================================
ZScorchAudioProcessor::~ZScorchAudioProcessor() = default;

ZScorchAudioProcessor::ZScorchAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "ZScorchState", createParameterLayout())
{
    engine = std::make_unique<Engine> ();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
ZScorchAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramInputGain, "Input Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f, 0.5f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPreEqLowFreq, "Pre-EQ Low Freq",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), 200.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPreEqLowGain, "Pre-EQ Low Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f, 0.5f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPreEqLowQ, "Pre-EQ Low Q",
        juce::NormalisableRange<float> (0.1f, 6.0f, 0.01f, 0.4f), 0.707f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPreEqBellFreq, "Pre-EQ Bell Freq",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), 1000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPreEqBellGain, "Pre-EQ Bell Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f, 0.5f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPreEqBellQ, "Pre-EQ Bell Q",
        juce::NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.4f), 1.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPreEqHighFreq, "Pre-EQ High Freq",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), 8000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPreEqHighGain, "Pre-EQ High Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f, 0.5f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPreEqHighQ, "Pre-EQ High Q",
        juce::NormalisableRange<float> (0.1f, 6.0f, 0.01f, 0.4f), 0.707f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramXOverLowMid, "Crossover Low-Mid",
        juce::NormalisableRange<float> (50.0f, 500.0f, 1.0f, 0.35f), 150.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramXOverMidHiMid, "Crossover Mid-HiMid",
        juce::NormalisableRange<float> (500.0f, 5000.0f, 1.0f, 0.35f), 800.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramXOverHiMidHigh, "Crossover HiMid-High",
        juce::NormalisableRange<float> (2000.0f, 12000.0f, 1.0f, 0.35f), 3500.0f));

    for (int b = 0; b < numBands; ++b)
    {
        juce::String bandLabel (bandNames[b]);
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            satParamId (satModeBase, b), "Sat " + bandLabel + " Mode",
            juce::StringArray ("Tube", "Tape", "Transistor", "Germanium", "Diode", "Wavefold"), 0));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            satParamId (satDriveBase, b), "Sat " + bandLabel + " Drive",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f, 0.5f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            satParamId (satAsymBase, b), "Sat " + bandLabel + " Asym",
            juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f, 0.5f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            satParamId (satMixBase, b), "Sat " + bandLabel + " Mix",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f, 0.5f), 100.0f));
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            satParamId (satSoloBase, b), "Sat " + bandLabel + " Solo", false));
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            satParamId (satMuteBase, b), "Sat " + bandLabel + " Mute", false));
    }
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPostEqLowFreq, "Post-EQ Low Freq",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), 200.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPostEqLowGain, "Post-EQ Low Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f, 0.5f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPostEqLowQ, "Post-EQ Low Q",
        juce::NormalisableRange<float> (0.1f, 6.0f, 0.01f, 0.4f), 0.707f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPostEqBellFreq, "Post-EQ Bell Freq",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), 1000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPostEqBellGain, "Post-EQ Bell Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f, 0.5f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPostEqBellQ, "Post-EQ Bell Q",
        juce::NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.4f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPostEqHighFreq, "Post-EQ High Freq",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), 8000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPostEqHighGain, "Post-EQ High Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f, 0.5f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPostEqHighQ, "Post-EQ High Q",
        juce::NormalisableRange<float> (0.1f, 6.0f, 0.01f, 0.4f), 0.707f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramOutputGain, "Output Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f, 0.5f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramCeiling, "Ceiling",
        juce::NormalisableRange<float> (-24.0f, 0.0f, 0.1f, 0.5f), -1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramDryWet, "Dry/Wet",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f, 0.5f), 100.0f));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        paramPreEqEnable, "Pre-EQ Enable", true));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        paramPostEqEnable, "Post-EQ Enable", true));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        paramBypass, "Bypass", false));

    return { params.begin(), params.end() };
}

//==============================================================================
float ZScorchAudioProcessor::readFloatParam (
    juce::AudioProcessorValueTreeState& state, const char* id)
{
    if (auto* param = state.getRawParameterValue (id))
        return param->load ();
    return 0.0f;
}

//==============================================================================
void ZScorchAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxSamplesPerBlock = samplesPerBlock;
    if (engine)
        engine->prepare (sampleRate, samplesPerBlock, (int) getMainBusNumOutputChannels ());
    setLatencySamples (64);
}

void ZScorchAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool ZScorchAudioProcessor::isBusesLayoutSupported (
    const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
#endif
//==============================================================================
void ZScorchAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples ();
    const auto numChannels = buffer.getNumChannels ();
    if (numSamples == 0 || numChannels == 0 || ! engine)
        return;

    const bool bypass = readFloatParam (parameters, paramBypass) > 0.5f;
    if (bypass)
        return;

    const float inputGainDb = readFloatParam (parameters, paramInputGain);

    const bool preEqEnable  = readFloatParam (parameters, paramPreEqEnable) > 0.5f;
    const bool postEqEnable = readFloatParam (parameters, paramPostEqEnable) > 0.5f;

    const float preLowFreq  = readFloatParam (parameters, paramPreEqLowFreq);
    const float preLowGain  = preEqEnable ? readFloatParam (parameters, paramPreEqLowGain) : 0.0f;
    const float preLowQ     = readFloatParam (parameters, paramPreEqLowQ);
    const float preBellFreq = readFloatParam (parameters, paramPreEqBellFreq);
    const float preBellGain = preEqEnable ? readFloatParam (parameters, paramPreEqBellGain) : 0.0f;
    const float preBellQ    = readFloatParam (parameters, paramPreEqBellQ);
    const float preHighFreq = readFloatParam (parameters, paramPreEqHighFreq);
    const float preHighGain = preEqEnable ? readFloatParam (parameters, paramPreEqHighGain) : 0.0f;
    const float preHighQ    = readFloatParam (parameters, paramPreEqHighQ);

    const float xLowMid    = readFloatParam (parameters, paramXOverLowMid);
    const float xMidHiMid  = readFloatParam (parameters, paramXOverMidHiMid);
    const float xHiMidHigh = readFloatParam (parameters, paramXOverHiMidHigh);

    const float postLowFreq  = readFloatParam (parameters, paramPostEqLowFreq);
    const float postLowGain  = postEqEnable ? readFloatParam (parameters, paramPostEqLowGain) : 0.0f;
    const float postLowQ     = readFloatParam (parameters, paramPostEqLowQ);
    const float postBellFreq = readFloatParam (parameters, paramPostEqBellFreq);
    const float postBellGain = postEqEnable ? readFloatParam (parameters, paramPostEqBellGain) : 0.0f;
    const float postBellQ    = readFloatParam (parameters, paramPostEqBellQ);
    const float postHighFreq = readFloatParam (parameters, paramPostEqHighFreq);
    const float postHighGain = postEqEnable ? readFloatParam (parameters, paramPostEqHighGain) : 0.0f;
    const float postHighQ    = readFloatParam (parameters, paramPostEqHighQ);

    const float outputGainDb = readFloatParam (parameters, paramOutputGain);
    const float ceilingDb    = readFloatParam (parameters, paramCeiling);
    const float dryWet       = readFloatParam (parameters, paramDryWet) * 0.01f;

    float maxIn = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        maxIn = juce::jmax (maxIn, buffer.getMagnitude (ch, 0, numSamples));
    inputLevelDb.store (juce::Decibels::gainToDecibels (maxIn, -120.0f));

    // Input gain.
    if (std::abs (inputGainDb) > 0.001f)
        buffer.applyGain (dbToGain (inputGainDb));

    engine->setCrossover (currentSampleRate, xLowMid, xMidHiMid, xHiMidHigh);
    engine->setPreEmphasis (currentSampleRate,
                            preLowFreq, preLowGain, preLowQ,
                            preBellFreq, preBellGain, preBellQ,
                            preHighFreq, preHighGain, preHighQ);
    engine->setPostEmphasis (currentSampleRate,
                             postLowFreq, postLowGain, postLowQ,
                             postBellFreq, postBellGain, postBellQ,
                             postHighFreq, postHighGain, postHighQ);

    for (int b = 0; b < numBands; ++b)
    {
        auto& sat = engine->bands[b];
        sat.mode  = (int) (readFloatParam (parameters, satParamId (satModeBase, b)) + 0.5f);
        sat.drive = readFloatParam (parameters, satParamId (satDriveBase, b)) * 0.01f;
        sat.asym  = readFloatParam (parameters, satParamId (satAsymBase, b)) * 0.01f;
        sat.mix   = readFloatParam (parameters, satParamId (satMixBase, b)) * 0.01f;
        sat.solo  = readFloatParam (parameters, satParamId (satSoloBase, b)) > 0.5f;
        sat.mute  = readFloatParam (parameters, satParamId (satMuteBase, b)) > 0.5f;
    }

    float gainRed = 0.0f;
    engine->process (buffer, dryWet, ceilingDb, gainRed);

    if (std::abs (outputGainDb) > 0.001f)
        buffer.applyGain (dbToGain (outputGainDb));

    float maxOut = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        maxOut = juce::jmax (maxOut, buffer.getMagnitude (ch, 0, numSamples));
    outputLevelDb.store (juce::Decibels::gainToDecibels (maxOut, -120.0f));
    gainReductionDb.store (gainRed);
}

//==============================================================================
void ZScorchAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState ();
    std::unique_ptr<juce::XmlElement> xml (state.createXml ());
    copyXmlToBinary (*xml, destData);
}

void ZScorchAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName (parameters.state.getType ()))
        parameters.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
juce::AudioProcessorEditor* ZScorchAudioProcessor::createEditor()
{
    return new ZScorchAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ZScorchAudioProcessor ();
}
