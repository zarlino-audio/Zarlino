#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class ZVocalAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                  private juce::Timer
{
public:
    explicit ZVocalAudioProcessorEditor (ZVocalAudioProcessor&);
    ~ZVocalAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct ImmersiveLookAndFeel final : juce::LookAndFeel_V4
    {
        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                               float sliderPosProportional, float rotaryStartAngle,
                               float rotaryEndAngle, juce::Slider&) override;
    };

    struct MacroControl
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<SliderAttachment> attachment;
    };

    class RoomCanvas final : public juce::Component
    {
    public:
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        struct Artist
        {
            juce::Point<float> position { 0.5f, 0.56f };
            juce::Colour jacket { 0xffd9a441 };
        };

        int hitTest (juce::Point<int>) const;
        juce::Rectangle<float> artistBounds (const Artist&) const;

        std::vector<Artist> artists { Artist{} };
        int selected = 0;
    };

    MacroControl& addMacro (const juce::String& name, const juce::String& id, juce::Colour accent);
    void timerCallback() override;

    ZVocalAudioProcessor& processor;
    ImmersiveLookAndFeel immersiveLookAndFeel;
    RoomCanvas room;
    std::vector<std::unique_ptr<MacroControl>> macros;
    juce::Label title;
    juce::Label meterLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZVocalAudioProcessorEditor)
};
