#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class PresenceFieldAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                private juce::Timer
{
public:
    explicit PresenceFieldAudioProcessorEditor (PresenceFieldAudioProcessor&);
    ~PresenceFieldAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void configureSlider (juce::Slider&, juce::Label&, const juce::String& text);

    PresenceFieldAudioProcessor& audioProcessor;
    juce::Slider masterSlider;
    juce::Slider inputSlider;
    juce::Slider outputSlider;
    juce::Slider airSpeedSlider;
    juce::Slider widthSlider;
    juce::ToggleButton identityLock;
    juce::Label masterLabel;
    juce::Label inputLabel;
    juce::Label outputLabel;
    juce::Label airSpeedLabel;
    juce::Label widthLabel;
    juce::Label meterLabel;

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<Attachment> masterAttachment;
    std::unique_ptr<Attachment> inputAttachment;
    std::unique_ptr<Attachment> outputAttachment;
    std::unique_ptr<Attachment> airSpeedAttachment;
    std::unique_ptr<Attachment> widthAttachment;
    std::unique_ptr<ButtonAttachment> lockAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresenceFieldAudioProcessorEditor)
};
