#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
    The ZTame editor provides controls for:
      - Sensitivity
      - Range (max suppression depth)
      - Selectivity
      - Split-band crossover frequency
      - Attack / Release
      - Listen/Solo toggle
      - Dry/Wet mix
      - Bypass
      - Gain reduction meter
*/
class ZTameAudioProcessorEditor final
    : public juce::AudioProcessorEditor
    , private juce::Timer
{
public:
    explicit ZTameAudioProcessorEditor (ZTameAudioProcessor&);
    ~ZTameAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // Helper: create a labelled slider
    struct LabelledSlider
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    // Helper: create a labelled button
    struct LabelledButton
    {
        juce::ToggleButton button;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    ZTameAudioProcessor& audioProcessor;

    LabelledSlider sensitivitySlider;
    LabelledSlider rangeSlider;
    LabelledSlider selectivitySlider;
    LabelledSlider crossoverSlider;
    LabelledSlider attackSlider;
    LabelledSlider releaseSlider;
    LabelledSlider dryWetSlider;
    LabelledButton listenButton;
    LabelledButton bypassButton;

    // GR meter
    juce::Label grMeterLabel;
    float displayGR = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZTameAudioProcessorEditor)
};
