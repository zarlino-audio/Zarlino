#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"

class SlotList : public juce::Component
{
public:
    explicit SlotList (RACK_AIAudioProcessor& processor);
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    RACK_AIAudioProcessor& mProcessor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotList)
};
