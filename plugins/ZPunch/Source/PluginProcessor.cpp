#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Parameter layout
//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
ZPunchAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    using Range  = juce::NormalisableRange<float>;
    using Param  = juce::AudioParameterFloat;
    using Bool   = juce::AudioParameterBool;

    // Input Gain: -18 to +18 dB, default 0 dB
    layout.add (std::make_unique<Param> (paramInputGain, "Input Gain",
        Range { -18.0f, 18.0f, 0.1f }, 0.0f, "dB"));

    // Attack: -24 to +24 dB, default 0 dB
    layout.add (std::make_unique<Param> (paramAttack, "Attack",
        Range { -24.0f, 24.0f, 0.1f }, 0.0f, "dB"));

    // Sustain: -24 to +24 dB, default 0 dB
    layout.add (std::make_unique<Param> (paramSustain, "Sustain",
        Range { -24.0f, 24.0f, 0.1f }, 0.0f, "dB"));

    // Attack Time: 0.1 to 100 ms, log-scaled, default 5 ms
    layout.add (std::make_unique<Param> (paramAttackTime, "Attack Time",
        Range { 0.1f, 100.0f, 0.1f, 0.35f }, 5.0f, "ms"));

    // Release Time: 10 to 500 ms, log-scaled, default 100 ms
    layout.add (std::make_unique<Param> (paramReleaseTime, "Release Time",
        Range { 10.0f, 500.0f, 1.0f, 0.35f }, 100.0f, "ms"));

    // Sensitivity: 0 to 100%, default 50%
    layout.add (std::make_unique<Param> (paramSensitivity, "Sensitivity",
        Range { 0.0f, 100.0f, 0.1f }, 50.0f, "%"));

    // Saturation: 0 to 100%, default 0%
    layout.add (std::make_unique<Param> (paramSaturation, "Saturation",
        Range { 0.0f, 100.0f, 0.1f }, 0.0f, "%"));

    // Output Gain: -18 to +18 dB, default 0 dB
    layout.add (std::make_unique<Param> (paramOutputGain, "Output Gain",
        Range { -18.0f, 18.0f, 0.1f }, 0.0f, "dB"));

    // Dry/Wet: 0 to 100%, default 100%
    layout.add (std::make_unique<Param> (paramDryWet, "Mix",
        Range { 0.0f, 100.0f, 0.1f }, 100.0f, "%"));

    // Bypass
    layout.add (std::make_unique<Bool> (paramBypass, "Bypass", false));

    return layout;
}

//==============================================================================
ZPunchAudioProcessor::ZPunchAudioProcessor()
    #ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    #endif
    , parameters (*this, nullptr, "ZPunchParams", createParameterLayout())
{
}

//==============================================================================
float ZPunchAudioProcessor::readFloatParam (juce::AudioProcessorValueTreeState& state,
                                             const char* id)
{
    if (auto* p = state.getRawParameterValue (id))
        return p->load();
    return 0.0f;
}

bool ZPunchAudioProcessor::readBoolParam (juce::AudioProcessorValueTreeState& state,
                                           const char* id)
{
    if (auto* p = state.getRawParameterValue (id))
        return p->load() >= 0.5f;
    return false;
}

//==============================================================================
void ZPunchAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate  = sampleRate;
    maxSamplesPerBlock = samplesPerBlock;

    // 2x oversampler for the saturation stage — uses JUCE's default FIR design
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2,                                          // numChannels (will be processed in stereo)
        2,                                          // 2x factor
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true,                                       // normalise
        true);                                      // includeAntiImagingAfterUpsampling

    oversampler->initProcessing (static_cast<size_t> (maxSamplesPerBlock));

    for (auto& det : detectorStates)
        det.reset();

    inputPeakDb.store (-120.0f);
    outputPeakDb.store (-120.0f);
    attackAmountDb.store (0.0f);
    sustainAmountDb.store (0.0f);
}

void ZPunchAudioProcessor::releaseResources()
{
    oversampler.reset();
}

//==============================================================================
#ifndef JucePlugin_PreferredChannelConfigurations
bool ZPunchAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono())
        return false;
    return true;
}
#endif

//==============================================================================
// Envelope detector — returns a value [0..1] where:
//   > 0.5 = attack (signal above smoothed envelope)
//   < 0.5 = sustain (signal below smoothed envelope)
//
// The detector uses two RMS followers:
//   - fast RMS: tracks transients (short window)
//   - slow RMS: tracks the body/sustain level (long window)
// The smoothed envelope adapts at different rates depending on whether the
// fast RMS is above or below the slow RMS.
// Sensitivity controls the threshold offset between the two RMS values.
//==============================================================================
float ZPunchAudioProcessor::processEnvelope (float input, DetectorState& det,
                                              float attackTimeMs, float releaseTimeMs,
                                              float sensitivity, double sampleRate)
{
    const float sampleRateF = static_cast<float> (sampleRate);

    // Fast RMS (5 ms window) — tracks transients
    const float fastTau = 0.005f; // 5 ms fixed
    const float fastCoeff = std::exp (-1.0f / (fastTau * sampleRateF));
    det.rmsFast = fastCoeff * det.rmsFast + (1.0f - fastCoeff) * (input * input);

    // Slow RMS (50 ms window) — tracks body level
    const float slowTau = 0.050f; // 50 ms fixed
    const float slowCoeff = std::exp (-1.0f / (slowTau * sampleRateF));
    det.rmsSlow = slowCoeff * det.rmsSlow + (1.0f - slowCoeff) * (input * input);

    // Sensitivity: maps 0-100% to a threshold multiplier [0.1 .. 2.0]
    //   Low sensitivity = harder to detect transients (threshold multiplier > 1)
    //   High sensitivity = easier to detect transients (threshold multiplier < 1)
    const float thresholdMult = juce::jmap (sensitivity * 0.01f, 0.0f, 1.0f, 2.0f, 0.3f);

    // Compare fast RMS against slow RMS × threshold multiplier
    const bool isAttack = std::sqrt (det.rmsFast) > std::sqrt (det.rmsSlow) * thresholdMult;

    // Smooth the decision with adjustable attack/release rates
    const float target = isAttack ? 1.0f : 0.0f;

    if (target > det.smoothed)
    {
        // Attack: transition toward "is transient"
        const float attackSamples = attackTimeMs * 0.001f * sampleRateF;
        const float attackCoeff = std::exp (-1.0f / attackSamples);
        det.smoothed = attackCoeff * det.smoothed + (1.0f - attackCoeff) * target;
    }
    else
    {
        // Release: transition toward "is sustain"
        const float releaseSamples = releaseTimeMs * 0.001f * sampleRateF;
        const float releaseCoeff = std::exp (-1.0f / releaseSamples);
        det.smoothed = releaseCoeff * det.smoothed + (1.0f - releaseCoeff) * target;
    }

    return det.smoothed;
}

//==============================================================================
// Tube-style asymmetric saturation — uses a polynomial that generates even
// (2nd) and odd (3rd) harmonics. Drive 0 = clean, 1 = full saturation.
//==============================================================================
float ZPunchAudioProcessor::saturate (float x, float drive)
{
    if (drive <= 0.0f)
        return x;

    // Pre-gain before the shaper curve
    const float preGain = 1.0f + drive * 4.0f;   // drive=0→1x, drive=1→5x

    // Soft-clip: x - (x^3)/3 scaled by drive (cubic nonlinearity, mostly 3rd harmonic)
    // Plus a small asymmetric term for even harmonics (tube-like)
    float xn = x * preGain;

    // Cubic saturation: tanh approximation
    //   tanh(x) ≈ x - x³/3  for |x| < 1
    //   Adding asymmetry: multiply one polarity differently for even harmonics
    const float x2 = xn * xn;
    const float x3 = x2 * xn;

    // Symmetric cubic saturator (3rd harmonic)
    float y = xn - (x3 / 3.0f) * drive;

    // Add asymmetric term (2nd harmonic): boost positive peaks slightly more
    if (xn > 0.0f)
        y += x2 * drive * 0.15f;

    // Anti-gain to maintain unity level at moderate drive
    const float makeup = 1.0f / (1.0f + drive * 0.3f);
    y *= makeup;

    // Hard clamp as safety
    return juce::jlimit (-1.5f, 1.5f, y);
}

//==============================================================================
// Output safety clipper — tanh-based soft knee at -1 dBFS
// -1 dBFS = 10^(-1/20) ≈ 0.891 linear
//==============================================================================
float ZPunchAudioProcessor::clipOutput (float x)
{
    constexpr float ceiling = 0.891250938f; // -1 dBFS linear
    // Scale by ~1.05 to give a soft knee before hard ceiling
    return std::tanh (x * 1.05f) * ceiling / std::tanh (1.05f);
}

//==============================================================================
void ZPunchAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Read all parameters once per block (audio-rate params, not smoothed)
    const float inputGainDb   = readFloatParam (parameters, paramInputGain);
    const float attackDb      = readFloatParam (parameters, paramAttack);
    const float sustainDb     = readFloatParam (parameters, paramSustain);
    const float attackTimeMs  = readFloatParam (parameters, paramAttackTime);
    const float releaseTimeMs = readFloatParam (parameters, paramReleaseTime);
    const float sensitivity   = readFloatParam (parameters, paramSensitivity);
    const float saturationPct = readFloatParam (parameters, paramSaturation);
    const float outputGainDb  = readFloatParam (parameters, paramOutputGain);
    const float dryWetPct     = readFloatParam (parameters, paramDryWet);
    const bool  bypassed      = readBoolParam  (parameters, paramBypass);

    const float inputGainLin   = gainFromDb (inputGainDb);
    const float attackGainLin  = gainFromDb (attackDb);
    const float sustainGainLin = gainFromDb (sustainDb);
    const float outputGainLin  = gainFromDb (outputGainDb);
    const float saturationDrive = saturationPct * 0.01f;   // 0..1
    const float dryWet = dryWetPct * 0.01f;
    const float wetMix = dryWet;
    const float dryMix = 1.0f - dryWet;

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // Metering: capture input peak
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto r = buffer.findMinMax (ch, 0, numSamples);
            peak = std::max ({ peak, std::abs (r.getStart()), std::abs (r.getEnd()) });
        }
        inputPeakDb.store (dbFromGain (peak));
    }

    // Bypass: passthrough (but still meter input/output)
    if (bypassed)
    {
        outputPeakDb.store (inputPeakDb.load());
        return;
    }

    // If dry/wet = 0%, passthrough
    if (dryWet <= 0.0f)
    {
        outputPeakDb.store (inputPeakDb.load());
        return;
    }

    //==========================================================================
    // Main DSP pipeline
    //==========================================================================

    // If dry/wet < 100%, save a dry copy before processing
    juce::AudioBuffer<float> dryBuffer;
    if (dryMix > 0.0f)
    {
        dryBuffer.makeCopyOf (buffer, true);
    }

    // Stage 1: Input gain
    if (std::abs (inputGainLin - 1.0f) > 1e-6f)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                data[i] *= inputGainLin;
        }
    }

    //==========================================================================
    // Stage 2-4: Envelope detection + Attack/Sustain shaping (per-sample, per-channel)
    //==========================================================================

    // Per-channel processed buffers (we need independent detector state per channel)
    // Process in-place: shape each sample by the envelope decision
    {
        float maxAttack = 0.0f;
        float maxSustain = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& det = detectorStates[ch];
            auto* data = buffer.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
            {
                const float inputSample = data[i];

                // Stage 2: Envelope detection
                const float env = processEnvelope (inputSample, det,
                                                    attackTimeMs, releaseTimeMs,
                                                    sensitivity, currentSampleRate);

                // Stage 3+4: Apply attack and sustain shaping
                // env = 1.0 → pure attack (apply attack gain)
                // env = 0.0 → pure sustain (apply sustain gain)
                // env = 0.5 → equal blend of both
                const float attackFactor  = env;
                const float sustainFactor = 1.0f - env;

                // Crossfade between attack gain and sustain gain based on envelope
                const float shapedGain = attackGainLin * attackFactor
                                       + sustainGainLin * sustainFactor;
                data[i] = inputSample * shapedGain;

                // Track what we applied for metering
                if (attackFactor > 0.5f)
                    maxAttack = std::max (maxAttack, attackFactor);
                if (sustainFactor > 0.5f)
                    maxSustain = std::max (maxSustain, sustainFactor);
            }
        }

        attackAmountDb.store (dbFromGain (maxAttack));
        sustainAmountDb.store (dbFromGain (maxSustain));
    }

    //==========================================================================
    // Stage 5: Saturation with 2x oversampling (when drive > 0)
    //==========================================================================
    if (saturationDrive > 0.0f && oversampler != nullptr)
    {
        // Create an audio block view for the oversampler
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::AudioBlock<float> osBlock = oversampler->processSamplesUp (block);

        // Apply saturation to the oversampled data
        const int osChannels = static_cast<int> (osBlock.getNumChannels());
        const int osSamples  = static_cast<int> (osBlock.getNumSamples());

        for (int ch = 0; ch < osChannels; ++ch)
        {
            auto* data = osBlock.getChannelPointer (static_cast<size_t> (ch));
            for (int i = 0; i < osSamples; ++i)
                data[i] = saturate (data[i], saturationDrive);
        }

        // Downsample back
        oversampler->processSamplesDown (block);
    }

    //==========================================================================
    // Stage 6: Output clipper (-1 dBFS)
    //==========================================================================
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                data[i] = clipOutput (data[i]);
        }
    }

    //==========================================================================
    // Stage 7: Output gain
    //==========================================================================
    if (std::abs (outputGainLin - 1.0f) > 1e-6f)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                data[i] *= outputGainLin;
        }
    }

    //==========================================================================
    // Stage 8: Dry/Wet mix
    //==========================================================================
    if (dryMix > 0.0f)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* wetData = buffer.getWritePointer (ch);
            const auto* dryData = dryBuffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                wetData[i] = wetData[i] * wetMix + dryData[i] * dryMix;
        }
    }

    // Metering: capture output peak
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto r = buffer.findMinMax (ch, 0, numSamples);
            peak = std::max ({ peak, std::abs (r.getStart()), std::abs (r.getEnd()) });
        }
        outputPeakDb.store (dbFromGain (peak));
    }
}

//==============================================================================
juce::AudioProcessorEditor* ZPunchAudioProcessor::createEditor()
{
    return new ZPunchAudioProcessorEditor (*this);
}

//==============================================================================
void ZPunchAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void ZPunchAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (parameters.state.getType()))
        parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ZPunchAudioProcessor();
}
