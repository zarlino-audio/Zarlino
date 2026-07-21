#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class VocalToneShaperAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                  private juce::Timer
{
public:
    explicit VocalToneShaperAudioProcessorEditor (VocalToneShaperAudioProcessor&);
    ~VocalToneShaperAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct ImmersiveLookAndFeel final : juce::LookAndFeel_V4
    {
        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                               float sliderPosProportional, float rotaryStartAngle,
                               float rotaryEndAngle, juce::Slider&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;
        void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                           int buttonX, int buttonY, int buttonW, int buttonH,
                           juce::ComboBox&) override;
    };

    struct SliderControl
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct ToggleControl
    {
        juce::ToggleButton button;
        std::unique_ptr<ButtonAttachment> attachment;
    };

    struct ChoiceControl
    {
        juce::Label label;
        juce::ComboBox box;
        std::unique_ptr<ComboAttachment> attachment;
    };

    void timerCallback() override;
    SliderControl& addSlider (const juce::String& name, const juce::String& id);
    ToggleControl& addToggle (const juce::String& name, const juce::String& id);
    ChoiceControl& addChoice (const juce::String& name, const juce::String& id);

    VocalToneShaperAudioProcessor& processor;
    ImmersiveLookAndFeel immersiveLookAndFeel;
    std::vector<std::unique_ptr<SliderControl>> sliders;
    std::vector<std::unique_ptr<ToggleControl>> toggles;
    std::vector<std::unique_ptr<ChoiceControl>> choices;
    juce::Label meterLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalToneShaperAudioProcessorEditor)
};
