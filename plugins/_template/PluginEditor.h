#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class TemplatePluginAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    explicit TemplatePluginAudioProcessorEditor (TemplatePluginAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    TemplatePluginAudioProcessor& audioProcessor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TemplatePluginAudioProcessorEditor)
};
