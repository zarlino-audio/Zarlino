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
}

void ZAdaptiveHarmonicsAudioProcessor::releaseResources()
{
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

    // Passthrough: input samples are already in the buffer; output is unchanged.
    // DSP modules will be added here to process each channel.

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
