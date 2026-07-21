#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr const char* masterId = "master";
    constexpr const char* inputId = "inputGain";
    constexpr const char* outputId = "outputGain";
    constexpr const char* presenceDelayId = "presencePreDelay";
    constexpr const char* memoryDelayId = "memoryPostDelay";
    constexpr const char* airSpeedId = "airSpeed";
    constexpr const char* widthId = "globalWidth";
    constexpr const char* identityLockId = "identityLock";

    float readParam (juce::AudioProcessorValueTreeState& state, const char* id)
    {
        return state.getRawParameterValue (id)->load();
    }

    float cubicEase (float x)
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        return x * x * (3.0f - 2.0f * x);
    }
}

PresenceFieldAudioProcessor::PresenceFieldAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PresenceFieldState", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout PresenceFieldAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (masterId, "Presence Field",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f, 1.0f), 50.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (inputId, "Input Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (outputId, "Output Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (presenceDelayId, "Presence Pre-Delay",
        juce::NormalisableRange<float> (-15.0f, -5.0f, 0.01f), -9.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (memoryDelayId, "Memory Post-Delay",
        juce::NormalisableRange<float> (10.0f, 40.0f, 0.01f), 25.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (airSpeedId, "Air Drift Speed",
        juce::NormalisableRange<float> (0.5f, 1.5f, 0.01f), 0.8f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (widthId, "Stereo Width",
        juce::NormalisableRange<float> (50.0f, 200.0f, 0.01f), 120.0f));
    params.push_back (std::make_unique<juce::AudioParameterBool> (identityLockId, "Identity Lock", false));

    return { params.begin(), params.end() };
}

void PresenceFieldAudioProcessor::DelayLine::prepare (int maxDelaySamples)
{
    buffer.assign (juce::jmax (1, maxDelaySamples + 1), 0.0f);
    writeIndex = 0;
}

float PresenceFieldAudioProcessor::DelayLine::read (int delaySamples) const
{
    const auto size = static_cast<int> (buffer.size());
    const auto index = (writeIndex - juce::jlimit (0, size - 1, delaySamples) + size) % size;
    return buffer[static_cast<size_t> (index)];
}

void PresenceFieldAudioProcessor::DelayLine::write (float value)
{
    buffer[static_cast<size_t> (writeIndex)] = value;
    writeIndex = (writeIndex + 1) % static_cast<int> (buffer.size());
}

void PresenceFieldAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const auto maxDelaySamples = static_cast<int> (std::ceil (sampleRate * 0.12));

    for (auto& delay : anchorDelay)
        delay.prepare (maxDelaySamples);
    for (auto& delay : memoryDelay)
        delay.prepare (maxDelaySamples);

    juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (samplesPerBlock), 1 };
    for (int ch = 0; ch < 2; ++ch)
    {
        presencePeak[ch].prepare (spec);
        airPeak[ch].prepare (spec);
        memoryWarmth[ch].prepare (spec);
        memoryDark[ch].prepare (spec);
        presencePeak[ch].reset();
        airPeak[ch].reset();
        memoryWarmth[ch].reset();
        memoryDark[ch].reset();
    }

    updateFilters();
}

void PresenceFieldAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool PresenceFieldAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
#endif

void PresenceFieldAudioProcessor::updateFilters()
{
    const auto airHz = airFrequencyHz.load();
    for (int ch = 0; ch < 2; ++ch)
    {
        *presencePeak[ch].coefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, 4200.0f, 1.2f, gainFromDb (3.5f));
        *airPeak[ch].coefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, airHz, 3.5f, gainFromDb (3.0f));
        *memoryWarmth[ch].coefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, 800.0f, 0.7f, gainFromDb (2.5f));
        *memoryDark[ch].coefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, 8000.0f, 2.0f, gainFromDb (-2.0f));
    }
}

float PresenceFieldAudioProcessor::softClip (float x, float drive)
{
    const auto amount = juce::jlimit (0.0f, 1.0f, drive);
    return juce::jmap (amount, x, std::tanh (x * (1.0f + amount * 4.0f)) / (1.0f + amount * 0.8f));
}

void PresenceFieldAudioProcessor::applyWidth (float& left, float& right, float width)
{
    const auto mid = (left + right) * 0.5f;
    const auto side = (left - right) * 0.5f * width;
    left = mid + side;
    right = mid - side;
}

void PresenceFieldAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    const auto samples = buffer.getNumSamples();
    const auto master = readParam (parameters, masterId) / 100.0f;
    const auto masterShape = cubicEase (master);
    const auto inGain = gainFromDb (readParam (parameters, inputId));
    const auto outGain = gainFromDb (readParam (parameters, outputId));
    const auto globalWidth = readParam (parameters, widthId) / 100.0f;
    const auto presenceAdvanceMs = std::abs (readParam (parameters, presenceDelayId));
    const auto memoryPostMs = readParam (parameters, memoryDelayId);
    const auto anchorDelaySamples = static_cast<int> (currentSampleRate * presenceAdvanceMs * 0.001);
    const auto memoryDelaySamples = static_cast<int> (currentSampleRate * (presenceAdvanceMs + memoryPostMs) * 0.001);

    auto sumSquares = 0.0;
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int i = 0; i < samples; ++i)
        {
            data[i] *= inGain;
            sumSquares += static_cast<double> (data[i]) * data[i];
        }
    }

    const auto rms = std::sqrt (sumSquares / static_cast<double> (juce::jmax (1, samples * 2)));
    const auto rmsDb = juce::Decibels::gainToDecibels (static_cast<float> (rms), -100.0f);
    inputRmsDb.store (rmsDb);

    const auto targetDistance = juce::jlimit (0.0f, 1.0f, juce::jmap (rmsDb, -35.0f, -9.0f, 0.0f, 1.0f));
    const auto coeff = targetDistance > smoothedDistance ? 0.012f : 0.003f;
    smoothedDistance += (targetDistance - smoothedDistance) * coeff;
    distanceFactor.store (smoothedDistance);

    const auto lfoStep = juce::MathConstants<float>::twoPi * readParam (parameters, airSpeedId) / static_cast<float> (currentSampleRate);
    const auto basePresence = juce::jmap (masterShape, 0.15f, 0.80f);
    const auto baseMemory = juce::jmap (masterShape, 0.10f, 0.70f);
    const auto presenceMod = juce::jmap (smoothedDistance, 1.25f, 0.55f);
    const auto memoryMod = juce::jmap (smoothedDistance, 0.45f, 1.25f);
    const auto presenceMix = basePresence * presenceMod;
    const auto memoryMix = baseMemory * memoryMod;
    const auto saturation = juce::jmap (masterShape, 0.0f, 0.12f);

    for (int i = 0; i < samples; ++i)
    {
        lfoPhase += lfoStep;
        if (lfoPhase > juce::MathConstants<float>::twoPi)
            lfoPhase -= juce::MathConstants<float>::twoPi;

        const auto airHz = 12000.0f + std::sin (lfoPhase) * 1500.0f * masterShape;
        airFrequencyHz.store (airHz);
        if (--samplesUntilFilterUpdate <= 0)
        {
            samplesUntilFilterUpdate = 256;
            updateFilters();
        }

        float input[2] = { buffer.getSample (0, i), buffer.getSample (1, i) };
        float anchor[2];
        float presence[2];
        float memory[2];

        for (int ch = 0; ch < 2; ++ch)
        {
            anchor[ch] = anchorDelay[ch].read (anchorDelaySamples);
            memory[ch] = memoryDelay[ch].read (memoryDelaySamples);
            anchorDelay[ch].write (input[ch]);
            memoryDelay[ch].write (input[ch]);

            presence[ch] = airPeak[ch].processSample (presencePeak[ch].processSample (input[ch]));
            memory[ch] = softClip (memoryDark[ch].processSample (memoryWarmth[ch].processSample (memory[ch])), saturation);
        }

        applyWidth (presence[0], presence[1], 0.80f);
        applyWidth (memory[0], memory[1], 1.40f);

        auto left = anchor[0] + presence[0] * presenceMix + memory[0] * memoryMix;
        auto right = anchor[1] + presence[1] * presenceMix + memory[1] * memoryMix;
        applyWidth (left, right, globalWidth);

        buffer.setSample (0, i, softClip (left * outGain, 0.10f));
        buffer.setSample (1, i, softClip (right * outGain, 0.10f));
    }
}

juce::AudioProcessorEditor* PresenceFieldAudioProcessor::createEditor()
{
    return new PresenceFieldAudioProcessorEditor (*this);
}

void PresenceFieldAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PresenceFieldAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PresenceFieldAudioProcessor();
}
