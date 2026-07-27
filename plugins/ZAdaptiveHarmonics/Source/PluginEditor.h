#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
    The ZAdaptiveHarmonics editor provides a two-tab interface:
      - Macro: Lift, Character, Mix
      - Advanced: per-module controls (Saturator, Tilt, Exciter)
*/
class ZAdaptiveHarmonicsAudioProcessorEditor final
    : public juce::AudioProcessorEditor
{
public:
    explicit ZAdaptiveHarmonicsAudioProcessorEditor (ZAdaptiveHarmonicsAudioProcessor&);
    ~ZAdaptiveHarmonicsAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ZAdaptiveHarmonicsAudioProcessor& audioProcessor;

    // Macro tab content — placeholder labels
    juce::Label liftLabel;
    juce::Label characterLabel;
    juce::Label mixLabel;

    // Advanced tab content — placeholder labels for per-module controls
    juce::Label saturatorLabel;
    juce::Label satEvenLabel;
    juce::Label satOddLabel;
    juce::Label satHarmReductionLabel;
    juce::Label tiltLabel;
    juce::Label exciterLabel;
    juce::Label exciterEnabledLabel;

    // Tab management
    juce::TabbedComponent tabs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZAdaptiveHarmonicsAudioProcessorEditor)
};
