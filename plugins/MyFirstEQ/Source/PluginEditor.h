#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class MyFirstEQAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    explicit MyFirstEQAudioProcessorEditor (MyFirstEQAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MyFirstEQAudioProcessor& audioProcessor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MyFirstEQAudioProcessorEditor)
};
