#include "PluginProcessor.h"
#include "PluginEditor.h"

MyFirstEQAudioProcessor::MyFirstEQAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
#endif
{
}

MyFirstEQAudioProcessor::~MyFirstEQAudioProcessor() {}

const juce::String MyFirstEQAudioProcessor::getName() const { return JucePlugin_Name; }

bool MyFirstEQAudioProcessor::acceptsMidi() const { return false; }
bool MyFirstEQAudioProcessor::producesMidi() const { return false; }
bool MyFirstEQAudioProcessor::isMidiEffect() const { return false; }
double MyFirstEQAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int MyFirstEQAudioProcessor::getNumPrograms() { return 1; }
int MyFirstEQAudioProcessor::getCurrentProgram() { return 0; }
void MyFirstEQAudioProcessor::setCurrentProgram (int) {}
const juce::String MyFirstEQAudioProcessor::getProgramName (int) { return {}; }
void MyFirstEQAudioProcessor::changeProgramName (int, const juce::String&) {}

void MyFirstEQAudioProcessor::prepareToPlay (double, int) {}
void MyFirstEQAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool MyFirstEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
    return true;
}
#endif

void MyFirstEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int channel = 0; channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());
}

bool MyFirstEQAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* MyFirstEQAudioProcessor::createEditor() { return new MyFirstEQAudioProcessorEditor (*this); }

void MyFirstEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData) { (void) destData; }
void MyFirstEQAudioProcessor::setStateInformation (const void*, int) {}
