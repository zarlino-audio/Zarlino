#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"

class ChangeLogPanel : public juce::Component
{
public:
    explicit ChangeLogPanel (RackAIAudioProcessor& processor);
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    RackAIAudioProcessor& mProcessor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChangeLogPanel)
};
