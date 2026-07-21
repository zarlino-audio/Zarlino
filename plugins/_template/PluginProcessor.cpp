#include "PluginProcessor.h"
#include "PluginEditor.h"

TemplatePluginAudioProcessor::TemplatePluginAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
#endif
{
}

TemplatePluginAudioProcessor::~TemplatePluginAudioProcessor() {}

const juce::String TemplatePluginAudioProcessor::getName() const { return JucePlugin_Name; }

bool TemplatePluginAudioProcessor::acceptsMidi() const { return false; }
bool TemplatePluginAudioProcessor::producesMidi() const { return false; }
bool TemplatePluginAudioProcessor::isMidiEffect() const { return false; }
double TemplatePluginAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int TemplatePluginAudioProcessor::getNumPrograms() { return 1; }
int TemplatePluginAudioProcessor::getCurrentProgram() { return 0; }
void TemplatePluginAudioProcessor::setCurrentProgram (int) {}
const juce::String TemplatePluginAudioProcessor::getProgramName (int) { return {}; }
void TemplatePluginAudioProcessor::changeProgramName (int, const juce::String&) {}

void TemplatePluginAudioProcessor::prepareToPlay (double, int) {}
void TemplatePluginAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool TemplatePluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
    return true;
}
#endif

void TemplatePluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int channel = 0; channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());
}

bool TemplatePluginAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* TemplatePluginAudioProcessor::createEditor() { return new TemplatePluginAudioProcessorEditor (*this); }

void TemplatePluginAudioProcessor::getStateInformation (juce::MemoryBlock& destData) { (void) destData; }
void TemplatePluginAudioProcessor::setStateInformation (const void*, int) {}

