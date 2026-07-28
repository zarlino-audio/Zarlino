#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
ZAdaptiveHarmonicsAudioProcessor::ZAdaptiveHarmonicsAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ZAdaptiveHarmonicsState", createParameterLayout())
{
    // Cache raw parameter pointers for audio-thread access
    liftParam                = apvts.getRawParameterValue ("lift");
    characterParam           = apvts.getRawParameterValue ("character");
    mixParam                 = apvts.getRawParameterValue ("mix");
    saturatorEvenParam       = apvts.getRawParameterValue ("saturatorEven");
    saturatorOddParam        = apvts.getRawParameterValue ("saturatorOdd");
    saturatorHarmReductionParam = apvts.getRawParameterValue ("saturatorHarmReduction");
    tiltAmountParam          = apvts.getRawParameterValue ("tiltAmount");
    exciterAmountParam       = apvts.getRawParameterValue ("exciterAmount");
    exciterEnabledParam      = apvts.getRawParameterValue ("exciterEnabled");
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
ZAdaptiveHarmonicsAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Macro controls
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "lift", "Lift",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "character", "Character",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

    // Saturator module
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "saturatorEven", "Saturator Even",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "saturatorOdd", "Saturator Odd",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "saturatorHarmReduction", "Sat Harm Reduction",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.7f));

    // Dynamic Spectral Tilt
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "tiltAmount", "Tilt Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    // Exciter (disabled by default)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "exciterAmount", "Exciter Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "exciterEnabled", "Exciter Enabled",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
void ZAdaptiveHarmonicsAudioProcessor::prepareToPlay (double sampleRate,
                                                       int samplesPerBlock)
{
    currentSampleRate    = sampleRate;
    currentSamplesPerBlock = samplesPerBlock;

    const auto numChannels = static_cast<int> (
        juce::jmin (getTotalNumOutputChannels(), getTotalNumInputChannels()));

    // Allocate dry buffer for wet/dry mix
    dryBuffer.setSize (numChannels, samplesPerBlock, false, true);
    dryBuffer.clear();

    // Initialize DSP modules
    analysisCore.prepare (sampleRate, samplesPerBlock);
    controlLayer.prepare (sampleRate);
    saturator.prepare (sampleRate, samplesPerBlock, numChannels);
    tilt.prepare (sampleRate, samplesPerBlock, numChannels);
    exciter.prepare (sampleRate, samplesPerBlock, numChannels);

    // Report combined latency from oversampling stages
    const int totalLatency = saturator.getLatencySamples()
                           + exciter.getLatencySamples();
    setLatencySamples (totalLatency);

    controlCounter = 0;
}

void ZAdaptiveHarmonicsAudioProcessor::releaseResources()
{
    analysisCore.reset();
    controlLayer.reset();
    saturator.reset();
    tilt.reset();
    exciter.reset();
    dryBuffer.clear();
}

//==============================================================================
#ifndef JucePlugin_PreferredChannelConfigurations
bool ZAdaptiveHarmonicsAudioProcessor::isBusesLayoutSupported (
    const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
#endif

//==============================================================================
void ZAdaptiveHarmonicsAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                      juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const auto numChannels = juce::jmin (getTotalNumOutputChannels(),
                                         buffer.getNumChannels());
    if (numChannels <= 0)
        return;

    // --- Read atomic parameter values once per block ---
    const float liftVal         = liftParam->load();
    const float characterVal    = characterParam->load();
    const float mixVal          = mixParam->load();
    const float satEvenVal      = saturatorEvenParam->load();
    const float satOddVal       = saturatorOddParam->load();
    const float satHarmRedVal   = saturatorHarmReductionParam->load();
    const float tiltVal         = tiltAmountParam->load();
    const float exciterAmtVal   = exciterAmountParam->load();
    const float exciterEnVal    = exciterEnabledParam->load();

    // --- Save dry buffer for wet/dry mix ---
    dryBuffer.makeCopyOf (buffer, true);

    // --- Control-rate analysis (every ~256 samples) ---
    controlCounter += numSamples;
    if (controlCounter >= kControlInterval)
    {
        controlCounter -= kControlInterval;

        analysisCore.process (buffer, numSamples, numChannels);
        const auto& cv = analysisCore.getControlVector();

        controlLayer.update (cv, liftVal, characterVal,
                             satEvenVal, satOddVal, satHarmRedVal,
                             tiltVal, exciterAmtVal, exciterEnVal,
                             currentSampleRate);

        // Update meter level (smoothed activity)
        const float targetMeter = cv.activity * liftVal;
        meterLevel.store (targetMeter, std::memory_order_relaxed);
    }

    // --- Advance smoothed targets through this block ---
    controlLayer.skipSamples (numSamples);

    // --- Block processing with current smoothed targets ---
    const auto targets = controlLayer.getCurrentTargets();

    // 1. Saturator
    if (liftVal > 0.001f)
    {
        saturator.process (buffer, targets.satEven, targets.satOdd,
                           targets.satHarmReduction);
    }

    // 2. Dynamic Spectral Tilt
    if (liftVal > 0.001f)
    {
        tilt.process (buffer, targets.tiltAmount);
    }

    // 3. Exciter (gated)
    if (exciterEnVal > 0.5f && liftVal > 0.001f)
    {
        exciter.process (buffer, targets.exciterAmount, targets.exciterEnabled);
    }

    // 4. Dry/Wet mix
    if (mixVal < 0.999f)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* wetData = buffer.getWritePointer (ch);
            const auto* dryData = dryBuffer.getReadPointer (ch);
            for (int s = 0; s < numSamples; ++s)
            {
                wetData[s] = dryData[s] + mixVal * (wetData[s] - dryData[s]);
            }
        }
    }

    // Mute any unused output channels
    for (int ch = numChannels; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);
}

//==============================================================================
void ZAdaptiveHarmonicsAudioProcessor::getStateInformation (
    juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::MemoryOutputStream stream (destData, true);
    stream.writeInt (1); // version
    state.writeToStream (stream);
}

void ZAdaptiveHarmonicsAudioProcessor::setStateInformation (
    const void* data, int sizeInBytes)
{
    juce::MemoryInputStream stream (data, static_cast<size_t> (sizeInBytes), false);
    const auto version = stream.readInt(); // version
    juce::ignoreUnused (version);
    apvts.replaceState (juce::ValueTree::readFromStream (stream));
}

//==============================================================================
juce::AudioProcessorEditor* ZAdaptiveHarmonicsAudioProcessor::createEditor()
{
    return new ZAdaptiveHarmonicsAudioProcessorEditor (*this);
}

//==============================================================================
// This creates new instances of the plugin for the host to use
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ZAdaptiveHarmonicsAudioProcessor();
}
