#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
    The ZAdaptiveHarmonics editor provides a two-tab interface:
      - Macro: Lift, Character, Mix + activity meter
      - Advanced: per-module controls (Saturator, Tilt, Exciter)
*/
class ZAdaptiveHarmonicsAudioProcessorEditor final
    : public juce::AudioProcessorEditor,
      private juce::Timer
{
public:
    explicit ZAdaptiveHarmonicsAudioProcessorEditor (ZAdaptiveHarmonicsAudioProcessor&);
    ~ZAdaptiveHarmonicsAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    friend class MacroTabContent;
    friend class AdvancedTabContent;

    void timerCallback() override;

    ZAdaptiveHarmonicsAudioProcessor& audioProcessor;

    // --- Macro tab controls ---
    juce::Slider liftKnob;
    juce::Slider characterKnob;
    juce::Slider mixKnob;

    juce::Label liftLabel;
    juce::Label characterLabel;
    juce::Label mixLabel;

    // Meter bar on Macro tab
    mutable juce::Rectangle<float> meterBounds;
    mutable float displayedMeterLevel = 0.0f;

    // --- Advanced tab controls ---
    juce::Slider satEvenKnob;
    juce::Slider satOddKnob;
    juce::Slider satHarmReductionKnob;
    juce::Slider tiltAmountKnob;
    juce::Slider exciterAmountKnob;
    juce::ToggleButton exciterEnabledButton;

    juce::Label satEvenLabel;
    juce::Label satOddLabel;
    juce::Label satHarmReductionLabel;
    juce::Label tiltAmountLabel;
    juce::Label exciterAmountLabel;
    juce::Label exciterEnabledLabel;

    // APVTS attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> liftAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> characterAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> satEvenAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> satOddAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> satHarmReductionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tiltAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> exciterAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> exciterEnabledAttachment;

    // Tab management
    juce::TabbedComponent tabs;

    // Styling helpers
    void setupKnob (juce::Slider& slider);
    void setupLabel (juce::Label& label, const juce::String& text);
    static juce::Colour accentColour();
    static juce::Colour bgColour();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZAdaptiveHarmonicsAudioProcessorEditor)
};
