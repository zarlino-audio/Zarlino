#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class ZPunchAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit ZPunchAudioProcessorEditor (ZPunchAudioProcessor&);
    ~ZPunchAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    ZPunchAudioProcessor& audioProcessor;

    // Sliders
    juce::Slider inputGainSlider;
    juce::Slider attackSlider;
    juce::Slider sustainSlider;
    juce::Slider attackTimeSlider;
    juce::Slider releaseTimeSlider;
    juce::Slider sensitivitySlider;
    juce::Slider saturationSlider;
    juce::Slider outputGainSlider;
    juce::Slider dryWetSlider;
    juce::ToggleButton bypassButton;

    // Labels
    juce::Label inputGainLabel;
    juce::Label attackLabel;
    juce::Label sustainLabel;
    juce::Label attackTimeLabel;
    juce::Label releaseTimeLabel;
    juce::Label sensitivityLabel;
    juce::Label saturationLabel;
    juce::Label outputGainLabel;
    juce::Label dryWetLabel;

    // Meter display
    juce::Label inputMeterLabel;
    juce::Label outputMeterLabel;

    // Attachments
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<Attachment> inputGainAttach;
    std::unique_ptr<Attachment> attackAttach;
    std::unique_ptr<Attachment> sustainAttach;
    std::unique_ptr<Attachment> attackTimeAttach;
    std::unique_ptr<Attachment> releaseTimeAttach;
    std::unique_ptr<Attachment> sensitivityAttach;
    std::unique_ptr<Attachment> saturationAttach;
    std::unique_ptr<Attachment> outputGainAttach;
    std::unique_ptr<Attachment> dryWetAttach;
    std::unique_ptr<ButtonAttachment> bypassAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZPunchAudioProcessorEditor)
};
