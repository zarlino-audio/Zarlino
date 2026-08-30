#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
namespace
{
    // dB conversion helpers
    static float dbToGain (float db) { return juce::Decibels::decibelsToGain (db, -120.0f); }

    //==============================================================================
    // RBJ Biquad filter — 3-band EQ stage (Low Shelf + Bell + High Shelf)
    struct EQStage
    {
        juce::dsp::IIR::Filter<float> lowShelf;
        juce::dsp::IIR::Filter<float> bell;
        juce::dsp::IIR::Filter<float> highShelf;

        void prepare (const juce::dsp::ProcessSpec& spec)
        {
            lowShelf.prepare (spec);
            bell.prepare (spec);
            highShelf.prepare (spec);
        }

        void reset()
        {
            lowShelf.reset();
            bell.reset();
            highShelf.reset();
        }

        void updateCoefficients (double sampleRate,
                                 float lowFreq, float lowGainDb, float lowQ,
                                 float bellFreq, float bellGainDb, float bellQ,
                                 float highFreq, float highGainDb, float highQ)
        {
            *lowShelf.coefficients  = *juce::dsp::IIR::Coefficients<float>::makeLowShelf  (sampleRate, lowFreq,  lowQ,  dbToGain (lowGainDb));
            *bell.coefficients      = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (sampleRate, bellFreq, bellQ, dbToGain (bellGainDb));
            *highShelf.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (sampleRate, highFreq, highQ, dbToGain (highGainDb));
        }

        void processBlock (juce::AudioBuffer<float>& buffer)
        {
            auto block = juce::dsp::AudioBlock<float> (buffer);
            juce::dsp::ProcessContextReplacing<float> ctx (block);
            lowShelf.process (ctx);
            bell.process (ctx);
            highShelf.process (ctx);
        }
    };

    //==============================================================================
    // Saturation engine: 6 character modes
    // All functions operate on a single sample, clamped to [-1, 1] input range.
    // drive: 0..1 mapped to internal gain range
    // asym: -1..1 asymmetry (positive = push positive peaks, negative = push negative)

    static float satTube (float x, float drive, float asym)
    {
        const float g = 1.0f + drive * 20.0f;  // gain: 1x to 21x
        const float a = asym * 0.5f;
        x = x * g * 0.3f;  // scale input

        // Asymmetric transfer: shift the operating point
        float pos = std::tanh (x * (1.0f + a) * 1.5f);
        float neg = std::tanh (x * (1.0f - a) * 1.5f);
        float y = (x >= 0.0f) ? pos : neg;

        return y * 1.5f;
    }

    static float satTape (float x, float drive, float asym)
    {
        const float g = 1.0f + drive * 15.0f;
        const float a = asym * 0.4f;

        // Soft saturation + hysteresis-like asymmetry
        x = x * g * 0.25f;

        // Asymmetric arctan with bias
        float bias = a * 0.3f;
        float y = std::atan (x * 2.5f + bias) * (2.0f / juce::MathConstants<float>::pi);

        // Add slight 3rd harmonic
        float x3 = x * x * x;
        y += x3 * 0.12f * (1.0f - std::abs (a));

        return y * 1.3f;
    }

    static float satTransistor (float x, float drive, float asym)
    {
        const float g = 1.0f + drive * 25.0f;
        const float a = asym * 0.5f;

        x = x * g * 0.2f;

        // Hard-clip style with soft knee
        float absX = std::abs (x);
        float knee = 0.5f + a * 0.3f;
        float y;

        if (absX < knee)
        {
            y = x * 1.2f;
        }
        else
        {
            float sign = (x > 0.0f) ? 1.0f : -1.0f;
            float excess = absX - knee;
            y = sign * (knee * 1.2f + std::tanh (excess * 2.0f) * (1.0f - knee));
        }

        return y * 1.2f;
    }

    static float satGermanium (float x, float drive, float asym)
    {
        const float g = 1.0f + drive * 18.0f;
        const float a = asym * 0.5f;

        x = x * g * 0.3f;

        // Soft, rounded clipping with asymmetric crossover distortion
        float softClip = std::tanh (x * 1.8f);

        // Add germanium-style crossover notch near zero
        float notch = x * (0.05f + a * 0.03f);
        float y = softClip - notch * std::exp (-x * x * 4.0f);

        return y * 1.4f;
    }

    static float satDiode (float x, float drive, float asym)
    {
        const float g = 1.0f + drive * 22.0f;
        const float a = asym * 0.6f;

        x = x * g * 0.25f;

        // Diode ladder: hard clipping with different thresholds for +/-
        float posThresh = 0.6f - a * 0.2f;
        float negThresh = 0.6f + a * 0.2f;

        float xc = juce::jlimit (-1.0f, 1.0f, x);
        float y;
        if (xc > posThresh)
            y = posThresh + std::tanh ((xc - posThresh) * 3.0f) * 0.3f;
        else if (xc < -negThresh)
            y = -negThresh + std::tanh ((xc + negThresh) * 3.0f) * 0.3f;
        else
            y = xc;

        return y * 1.3f;
    }

    static float satWavefold (float x, float drive, float asym)
    {
        const float g = 1.0f + drive * 12.0f;
        const float a = asym * 0.4f;

        x = x * g * 0.35f;

        // Phase shift for asymmetry
        x += a * 0.15f;

        // Wavefolding: sin-based fold
        float y = std::sin (x * juce::MathConstants<float>::pi * 1.5f) * 0.8f;

        // Add a small amount of dry signal for body
        y = y * 0.85f + x * 0.15f * (1.0f - std::abs (a));

        return y * 1.2f;
    }

    using SatFunc = float (*)(float, float, float);
    static constexpr SatFunc satFunctions[] = {
        satTube, satTape, satTransistor, satGermanium, satDiode, satWavefold
    };

    //==============================================================================
    // LR4 Crossover (24 dB/oct Linkwitz-Riley)
    // Implemented as two cascaded 12 dB/oct Butterworth filters per band
    // This is a placeholder — the full implementation is in Crossover.h
    struct LR4Crossover
    {
        // Per-band lowpass/highpass filter pairs
        struct BandFilter
        {
            juce::dsp::IIR::Filter<float> lp1, lp2;  // cascaded 12 dB/oct
            juce::dsp::IIR::Filter<float> hp1, hp2;

            void prepare (const juce::dsp::ProcessSpec& spec)
            {
                lp1.prepare (spec); lp2.prepare (spec);
                hp1.prepare (spec); hp2.prepare (spec);
            }

            void reset()
            {
                lp1.reset(); lp2.reset();
                hp1.reset(); hp2.reset();
            }
        };

        std::array<BandFilter, 4> bands;    // [Low, LoMid, HiMid, High]
        std::array<juce::AudioBuffer<float>, 4> bandBuffers;
        std::array<bool, 4> bandActive;

        void prepare (const juce::dsp::ProcessSpec& spec)
        {
            for (auto& bf : bands)
                bf.prepare (spec);

            for (auto& buf : bandBuffers)
                buf.setSize (2, spec.maximumBlockSize, false, false, true);
        }

        void reset()
        {
            for (auto& bf : bands)
                bf.reset();
            for (auto& buf : bandBuffers)
                buf.clear();
        }

        void updateCoefficients (double sampleRate,
                                 float freqLoMid, float freqMidHiMid, float freqHiMidHigh)
        {
            // Low band: LP12 cascaded = LP24
            auto lpLo = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, freqLoMid, 0.7071f);
            *bands[0].lp1.coefficients = *lpLo;
            *bands[0].lp2.coefficients = *lpLo;

            // Lo-Mid band: HP12 + LP12 cascaded
            auto hpLoMid = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, freqLoMid, 0.7071f);
            auto lpLoMid = juce::dsp::IIR::Coefficients<float>::makeLowPass  (sampleRate, freqMidHiMid, 0.7071f);
            *bands[1].hp1.coefficients = *hpLoMid;
            *bands[1].hp2.coefficients = *hpLoMid;
            *bands[1].lp1.coefficients = *lpLoMid;
            *bands[1].lp2.coefficients = *lpLoMid;

            // Hi-Mid band: HP12 + LP12 cascaded
            auto hpHiMid = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, freqMidHiMid, 0.7071f);
            auto lpHiMid = juce::dsp::IIR::Coefficients<float>::makeLowPass  (sampleRate, freqHiMidHigh, 0.7071f);
            *bands[2].hp1.coefficients = *hpHiMid;
            *bands[2].hp2.coefficients = *hpHiMid;
            *bands[2].lp1.coefficients = *lpHiMid;
            *bands[2].lp2.coefficients = *lpHiMid;

            // High band: HP12 cascaded = HP24
            auto hpHi = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, freqHiMidHigh, 0.7071f);
            *bands[3].hp1.coefficients = *hpHi;
            *bands[3].hp2.coefficients = *hpHi;
        }

        void split (const juce::AudioBuffer<float>& input)
        {
            const auto numSamples = input.getNumSamples();

            // Process each band
            for (int b = 0; b < 4; ++b)
            {
                bandBuffers[b].clear();
                bandBuffers[b].makeCopyOf (input, true);

                auto block = juce::dsp::AudioBlock<float> (bandBuffers[b]);
                juce::dsp::ProcessContextReplacing<float> ctx (block);

                switch (b)
                {
                    case 0: // Low: LP24 only
                        bands[b].lp1.process (ctx);
                        bands[b].lp2.process (ctx);
                        break;
                    case 1: // Lo-Mid: HP24 + LP24
                        bands[b].hp1.process (ctx);
                        bands[b].hp2.process (ctx);
                        bands[b].lp1.process (ctx);
                        bands[b].lp2.process (ctx);
                        break;
                    case 2: // Hi-Mid: HP24 + LP24
                        bands[b].hp1.process (ctx);
                        bands[b].hp2.process (ctx);
                        bands[b].lp1.process (ctx);
                        bands[b].lp2.process (ctx);
                        break;
                    case 3: // High: HP24 only
                        bands[b].hp1.process (ctx);
                        bands[b].hp2.process (ctx);
                        break;
                }
            }
        }

        void recombine (juce::AudioBuffer<float>& output)
        {
            output.clear();
            for (int b = 0; b < 4; ++b)
            {
                for (int ch = 0; ch < output.getNumChannels(); ++ch)
                    output.addFrom (ch, 0, bandBuffers[b], ch, 0, output.getNumSamples());
            }
        }
    };

    //==============================================================================
    // Ceiling Limiter with 1ms lookahead
    struct CeilingLimiter
    {
        static constexpr int lookaheadMs = 1;
        int lookaheadSamples = 48;  // at 48 kHz

        void prepare (const juce::dsp::ProcessSpec& spec)
        {
            lookaheadSamples = static_cast<int> (spec.sampleRate * lookaheadMs / 1000.0);
            delayBuffer.setSize (2, spec.maximumBlockSize + lookaheadSamples * 2, false, false, true);
            delayWritePos = 0;
        }

        void reset()
        {
            delayBuffer.clear();
            delayWritePos = 0;
        }

        void processBlock (juce::AudioBuffer<float>& buffer, float ceilingDb)
        {
            const auto numSamples = buffer.getNumSamples();
            const auto numChannels = buffer.getNumChannels();
            const float ceiling = dbToGain (ceilingDb);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                auto* delayData = delayBuffer.getWritePointer (ch);
                const int dSize = delayBuffer.getNumSamples();

                for (int i = 0; i < numSamples; ++i)
                {
                    // Write to delay line
                    delayData[delayWritePos] = data[i];

                    // Read lookahead
                    int readPos = (delayWritePos + lookaheadSamples) % dSize;
                    float peak = std::abs (delayData[readPos]);

                    // Simple arctan soft-clip limiter
                    float gain = 1.0f;
                    if (peak > ceiling)
                        gain = ceiling / peak;

                    // Smooth gain reduction with arctan knee
                    float overshoot = peak - ceiling;
                    if (overshoot > 0.0f)
                    {
                        // arctan soft clip applies at the sample level
                        float sign = (data[i] >= 0.0f) ? 1.0f : -1.0f;
                        data[i] = sign * ceiling * std::atan (std::abs (data[i]) / ceiling * 1.5f) * (2.0f / juce::MathConstants<float>::pi);
                    }

                    // Advance write position
                    delayWritePos = (delayWritePos + 1) % dSize;
                }
            }
        }

        juce::AudioBuffer<float> delayBuffer;
        int delayWritePos = 0;
    };

} // anonymous namespace

//==============================================================================
const char* ZScorchAudioProcessor::satParamId (const char* base, int band)
{
    static char buf[32];
    snprintf (buf, sizeof (buf), "%s%s", base, bandNames[band]);
    return buf;
}

//==============================================================================
ZScorchAudioProcessor::ZScorchAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "ZScorchState", createParameterLayout())
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
ZScorchAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // --- Input Gain (±24 dB) ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramInputGain, "Input Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f, 0.5f), 0.0f));

    // --- Pre-EQ: Low Shelf (Freq, Gain, Q) ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPreEqLowFreq, "Pre-EQ Low Freq",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), 200.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPreEqLowGain, "Pre-EQ Low Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f, 0.5f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPreEqLowQ, "Pre-EQ Low Q",
        juce::NormalisableRange<float> (0.1f, 6.0f, 0.01f, 0.4f), 0.707f));

    // --- Pre-EQ: Bell (Freq, Gain, Q) ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPreEqBellFreq, "Pre-EQ Bell Freq",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), 1000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPreEqBellGain, "Pre-EQ Bell Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f, 0.5f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPreEqBellQ, "Pre-EQ Bell Q",
        juce::NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.4f), 1.0f));

    // --- Pre-EQ: High Shelf (Freq, Gain, Q) ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPreEqHighFreq, "Pre-EQ High Freq",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), 8000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPreEqHighGain, "Pre-EQ High Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f, 0.5f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPreEqHighQ, "Pre-EQ High Q",
        juce::NormalisableRange<float> (0.1f, 6.0f, 0.01f, 0.4f), 0.707f));

    // --- Crossover Frequencies ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramXOverLowMid, "Crossover Low-Mid",
        juce::NormalisableRange<float> (50.0f, 500.0f, 1.0f, 0.35f), 150.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramXOverMidHiMid, "Crossover Mid-HiMid",
        juce::NormalisableRange<float> (500.0f, 5000.0f, 1.0f, 0.35f), 800.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramXOverHiMidHigh, "Crossover HiMid-High",
        juce::NormalisableRange<float> (2000.0f, 12000.0f, 1.0f, 0.35f), 3500.0f));

    // --- Saturation: 4 bands × (Mode, Drive, Asymmetry, Mix, Solo, Mute) = 24 params ---
    for (int b = 0; b < numBands; ++b)
    {
        juce::String bandLabel (bandNames[b]);

        // Mode (choice: Tube, Tape, Transistor, Germanium, Diode, Wavefold)
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            satParamId (satModeBase, b), "Sat " + bandLabel + " Mode",
            juce::StringArray ("Tube", "Tape", "Transistor", "Germanium", "Diode", "Wavefold"), 0));

        // Drive (0–100%)
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            satParamId (satDriveBase, b), "Sat " + bandLabel + " Drive",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f, 0.5f), 0.0f));

        // Asymmetry (-100 to +100%)
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            satParamId (satAsymBase, b), "Sat " + bandLabel + " Asym",
            juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f, 0.5f), 0.0f));

        // Mix (0–100%)
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            satParamId (satMixBase, b), "Sat " + bandLabel + " Mix",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f, 0.5f), 100.0f));

        // Solo
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            satParamId (satSoloBase, b), "Sat " + bandLabel + " Solo", false));

        // Mute
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            satParamId (satMuteBase, b), "Sat " + bandLabel + " Mute", false));
    }

    // --- Post-EQ: Low Shelf (Freq, Gain, Q) ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPostEqLowFreq, "Post-EQ Low Freq",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), 200.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPostEqLowGain, "Post-EQ Low Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f, 0.5f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPostEqLowQ, "Post-EQ Low Q",
        juce::NormalisableRange<float> (0.1f, 6.0f, 0.01f, 0.4f), 0.707f));

    // --- Post-EQ: Bell (Freq, Gain, Q) ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPostEqBellFreq, "Post-EQ Bell Freq",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), 1000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPostEqBellGain, "Post-EQ Bell Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f, 0.5f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPostEqBellQ, "Post-EQ Bell Q",
        juce::NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.4f), 1.0f));

    // --- Post-EQ: High Shelf (Freq, Gain, Q) ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPostEqHighFreq, "Post-EQ High Freq",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), 8000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPostEqHighGain, "Post-EQ High Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f, 0.5f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramPostEqHighQ, "Post-EQ High Q",
        juce::NormalisableRange<float> (0.1f, 6.0f, 0.01f, 0.4f), 0.707f));

    // --- Output Gain + Ceiling ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramOutputGain, "Output Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f, 0.5f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramCeiling, "Ceiling",
        juce::NormalisableRange<float> (-24.0f, 0.0f, 0.1f, 0.5f), -1.0f));

    // --- Dry/Wet Mix ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramDryWet, "Dry/Wet",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f, 0.5f), 100.0f));

    // --- Master toggles ---
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
        return param->load();
    return 0.0f;
}

//==============================================================================
void ZScorchAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (sampleRate, samplesPerBlock);

    currentSampleRate = sampleRate;
    maxSamplesPerBlock = samplesPerBlock;

    // Report latency: ~132 samples at 48 kHz
    //   - 65-tap FIR: 32 samples group delay
    //   - 1ms lookahead: 48 samples at 48kHz
    //   - LR4 crossover: ~4 samples
    setLatencySamples (132);
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

    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    if (numSamples == 0 || numChannels == 0)
        return;

    // Read master bypass
    const bool bypass = readFloatParam (parameters, paramBypass) > 0.5f;
    if (bypass)
    {
        // Pass through unmodified
        return;
    }

    // --- Read all parameters ---
    const float inputGainDb = readFloatParam (parameters, paramInputGain);

    // Pre-EQ
    const bool preEqEnable = readFloatParam (parameters, paramPreEqEnable) > 0.5f;
    const float preLowFreq  = readFloatParam (parameters, paramPreEqLowFreq);
    const float preLowGain  = readFloatParam (parameters, paramPreEqLowGain);
    const float preLowQ     = readFloatParam (parameters, paramPreEqLowQ);
    const float preBellFreq = readFloatParam (parameters, paramPreEqBellFreq);
    const float preBellGain = readFloatParam (parameters, paramPreEqBellGain);
    const float preBellQ    = readFloatParam (parameters, paramPreEqBellQ);
    const float preHighFreq = readFloatParam (parameters, paramPreEqHighFreq);
    const float preHighGain = readFloatParam (parameters, paramPreEqHighGain);
    const float preHighQ    = readFloatParam (parameters, paramPreEqHighQ);

    // Crossover
    const float xLowMid    = readFloatParam (parameters, paramXOverLowMid);
    const float xMidHiMid  = readFloatParam (parameters, paramXOverMidHiMid);
    const float xHiMidHigh = readFloatParam (parameters, paramXOverHiMidHigh);

    // Post-EQ
    const bool postEqEnable = readFloatParam (parameters, paramPostEqEnable) > 0.5f;
    const float postLowFreq  = readFloatParam (parameters, paramPostEqLowFreq);
    const float postLowGain  = readFloatParam (parameters, paramPostEqLowGain);
    const float postLowQ     = readFloatParam (parameters, paramPostEqLowQ);
    const float postBellFreq = readFloatParam (parameters, paramPostEqBellFreq);
    const float postBellGain = readFloatParam (parameters, paramPostEqBellGain);
    const float postBellQ    = readFloatParam (parameters, paramPostEqBellQ);
    const float postHighFreq = readFloatParam (parameters, paramPostEqHighFreq);
    const float postHighGain = readFloatParam (parameters, paramPostEqHighGain);
    const float postHighQ    = readFloatParam (parameters, paramPostEqHighQ);

    // Output
    const float outputGainDb = readFloatParam (parameters, paramOutputGain);
    const float ceilingDb    = readFloatParam (parameters, paramCeiling);
    const float dryWet       = readFloatParam (parameters, paramDryWet) * 0.01f;

    // --- Stage 1: Input Gain ---
    if (std::abs (inputGainDb) > 0.001f)
    {
        buffer.applyGain (dbToGain (inputGainDb));
    }

    // --- Stage 2: Pre-Emphasis EQ ---
    // (Placeholder — will be wired with EQStage in step 7)

    // --- Stage 3: LR4 Crossover Split ---
    // (Placeholder — will be wired with LR4Crossover in step 7)

    // --- Stage 4: Saturation Engine × 4 bands ---
    // (Placeholder — will be wired in step 7)

    // --- Stage 5: LR4 Crossover Recombine ---
    // (Placeholder)

    // --- Stage 6: Post-Emphasis EQ ---
    // (Placeholder)

    // --- Stage 7: Output Gain ---
    if (std::abs (outputGainDb) > 0.001f)
    {
        buffer.applyGain (dbToGain (outputGainDb));
    }

    // --- Stage 8: Dry/Wet Mix ---
    // (Placeholder — will be wired in step 7)

    // Update meters
    float maxIn = buffer.getMagnitude (0, 0, numSamples);
    for (int ch = 1; ch < numChannels; ++ch)
        maxIn = juce::jmax (maxIn, buffer.getMagnitude (ch, 0, numSamples));
    inputLevelDb.store (juce::Decibels::gainToDecibels (maxIn, -120.0f));
    outputLevelDb.store (inputLevelDb.load());
    gainReductionDb.store (0.0f);
}

//==============================================================================
void ZScorchAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void ZScorchAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr)
    {
        if (xmlState->hasTagName (parameters.state.getType()))
        {
            parameters.replaceState (juce::ValueTree::fromXml (*xmlState));
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* ZScorchAudioProcessor::createEditor()
{
    return new ZScorchAudioProcessorEditor (*this);
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ZScorchAudioProcessor();
}