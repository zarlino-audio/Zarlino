#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"

class AnalysisOverlay : public juce::Component
{
public:
    explicit AnalysisOverlay (RackAIAudioProcessor& processor);
    void paint (juce::Graphics& g) override;

private:
    RackAIAudioProcessor& mProcessor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisOverlay)
};
