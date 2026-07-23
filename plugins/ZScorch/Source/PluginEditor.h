#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
    ZScorch editor — full control surface for the multi-band saturator.

    Layout:
      Top:    Input Gain, Pre-EQ Enable
      Row 1:  Pre-EQ Low Shelf (Freq, Gain, Q)
      Row 2:  Pre-EQ Bell (Freq, Gain, Q)
      Row 3:  Pre-EQ High Shelf (Freq, Gain, Q)
      Row 4:  Crossover (Low-Mid, Mid-HiMid, HiMid-High)
      Row 5-8: Saturation × 4 bands (Mode, Drive, Asym, Mix, Solo, Mute)
      Row 9:  Post-EQ Low Shelf (Freq, Gain, Q)
      Row 10: Post-EQ Bell (Freq, Gain, Q)
      Row 11: Post-EQ High Shelf (Freq, Gain, Q)
      Bottom: Output Gain, Ceiling, Dry/Wet, Post-EQ Enable, Bypass
*/
class ZScorchAudioProcessorEditor final
    : public juce::AudioProcessorEditor
    , private juce::Timer
{
public:
    explicit ZScorchAudioProcessorEditor (ZScorchAudioProcessor&);
    ~ZScorchAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct LabelledSlider
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct LabelledCombo
    {
        juce::ComboBox combo;
        juce::Label    label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    struct LabelledButton
    {
        juce::ToggleButton button;
        juce::Label        label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    ZScorchAudioProcessor& audioProcessor;

    // Input
    LabelledSlider  inputGainSlider;
    LabelledButton  bypassButton;

    // Pre-EQ
    LabelledButton  preEqEnableButton;
    LabelledSlider  preLowFreq, preLowGain, preLowQ;
    LabelledSlider  preBellFreq, preBellGain, preBellQ;
    LabelledSlider  preHighFreq, preHighGain, preHighQ;

    // Crossover
    LabelledSlider  xLowMid, xMidHiMid, xHiMidHigh;

    // Saturation × 4 bands
    std::array<LabelledCombo,  4> satMode;
    std::array<LabelledSlider, 4> satDrive;
    std::array<LabelledSlider, 4> satAsym;
    std::array<LabelledSlider, 4> satMix;
    std::array<LabelledButton, 4> satSolo;
    std::array<LabelledButton, 4> satMute;

    // Post-EQ
    LabelledButton  postEqEnableButton;
    LabelledSlider  postLowFreq, postLowGain, postLowQ;
    LabelledSlider  postBellFreq, postBellGain, postBellQ;
    LabelledSlider  postHighFreq, postHighGain, postHighQ;

    // Output
    LabelledSlider  outputGainSlider;
    LabelledSlider  ceilingSlider;
    LabelledSlider  dryWetSlider;

    // Meters
    juce::Label inMeterLabel;
    juce::Label outMeterLabel;
    juce::Label grMeterLabel;
    float displayInDb  = -120.0f;
    float displayOutDb = -120.0f;
    float displayGR    = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZScorchAudioProcessorEditor)
};