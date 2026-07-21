#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"

class AnalysisOverlay : public juce::Component
{
public:
    explicit AnalysisOverlay (RACK_AIAudioProcessor& processor);
    void paint (juce::Graphics& g) override;

private:
    RACK_AIAudioProcessor& mProcessor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisOverlay)
};
