#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/** Shared palette for the ZTame editor. */
namespace ztame
{
    constexpr int   designWidth  = 1000;
    constexpr int   designHeight = 640;

    juce::Colour backgroundTop();
    juce::Colour backgroundBottom();
    juce::Colour panelFill();
    juce::Colour panelStroke();
    juce::Colour accent();
    juce::Colour accentSoft();
    juce::Colour violet();
    juce::Colour amber();
    juce::Colour textBright();
    juce::Colour textDim();
    juce::Colour danger();
}

//==============================================================================
/** Knob / toggle / slider styling for the whole editor. */
class ZTameLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ZTameLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;
};

//==============================================================================
/**
    The live visualisation: input spectrum, output spectrum, the energy that is
    being removed (amber), detected resonance markers, and the draggable focus
    band that decides where suppression may happen.
*/
class ZTameSpectrumDisplay final : public juce::Component,
                                   public juce::SettableTooltipClient
{
public:
    explicit ZTameSpectrumDisplay (ZTameAudioProcessor&);

    void setSnapshot (const ZTameAudioProcessor::UiSnapshot& snapshot, bool valid);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    enum class DragMode { none, body, lowEdge, highEdge };

    juce::Rectangle<float> plotArea() const;
    float freqToX (float freqHz) const;
    float xToFreq (float x) const;
    DragMode hitTest (float x) const;
    void  writeBand (float lowHz, float highHz, bool notifyHost);
    void  beginGestures();
    void  endGestures();

    ZTameAudioProcessor& processor;

    static constexpr int numDisplayBins = ZTameAudioProcessor::UiSnapshot::numDisplayBins;
    static constexpr int maxPeaks       = ZTameAudioProcessor::UiSnapshot::maxPeaks;

    std::array<float, numDisplayBins> inputDb  {};
    std::array<float, numDisplayBins> outputDb {};
    std::array<float, numDisplayBins> removedDb {};
    std::array<float, numDisplayBins> sidechainDb {};
    std::array<float, maxPeaks> peakFreqHz {};
    std::array<float, maxPeaks> peakReductionDb {};
    int   numPeaks = 0;
    float displayThresholdDb = 12.0f;

    float bandLowHz = 20.0f;
    float bandHighHz = 20000.0f;

    DragMode dragMode = DragMode::none;
    DragMode hoverMode = DragMode::none;
    float dragStartX = 0.0f;
    float dragStartLow = 20.0f;
    float dragStartHigh = 20000.0f;
    bool  gesturesActive = false;
    bool  listening = false;
    bool  sidechainLive = false;
    bool  bypassed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZTameSpectrumDisplay)
};

//==============================================================================
/** Vertical gain-reduction meter: average bar + peak hold marker + numbers. */
class ZTameGainReductionMeter final : public juce::Component
{
public:
    ZTameGainReductionMeter() = default;

    void setValues (float averageDb, float peakDb);

    void paint (juce::Graphics&) override;

private:
    float average = 0.0f;
    float peak = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZTameGainReductionMeter)
};

//==============================================================================
/** Rotary knob with caption, live value and tooltip. */
class ZTameKnob final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    ZTameKnob (ZTameAudioProcessor&, const juce::String& parameterId,
               const juce::String& captionText, const juce::String& tooltipText);

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Slider slider;

private:
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZTameKnob)
};

//==============================================================================
/** Pill-shaped toggle / cycle control with a live sub-label. */
class ZTameToggleTile final : public juce::Component,
                              public juce::SettableTooltipClient
{
public:
    ZTameToggleTile (ZTameAudioProcessor&, const juce::String& parameterId,
                     const juce::String& titleText, const juce::String& tooltipText);

    /** Called from the editor timer — visual state only, never writes parameters. */
    void setVisualState (bool isOn, const juce::String& newSubText,
                         bool isEnabled = true, juce::Colour accentOverride = juce::Colour());

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void toggle();

    ZTameAudioProcessor& processor;
    juce::String paramId;
    juce::String title;
    juce::String subText;
    bool   on = false;
    bool   enabled = true;
    bool   hovered = false;
    juce::Colour tint;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZTameToggleTile)
};

//==============================================================================
/**
    All controls live here at a fixed design resolution; the editor scales this
    component so the layout stays pixel-perfect at any window size.
*/
class ZTameEditorContent final : public juce::Component,
                                 private juce::Timer
{
public:
    explicit ZTameEditorContent (ZTameAudioProcessor&);
    ~ZTameEditorContent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshTelemetry();
    void applyPreset (int presetIndex);

    ZTameAudioProcessor& processor;
    ZTameLookAndFeel lookAndFeel;

    ZTameSpectrumDisplay spectrum;
    ZTameGainReductionMeter meter;

    std::array<std::unique_ptr<ZTameKnob>, 6> knobs;

    juce::ComboBox presetBox;

    ZTameToggleTile listenTile;
    ZTameToggleTile bypassTile;
    ZTameToggleTile sidechainTile;
    ZTameToggleTile linkTile;
    ZTameToggleTile midSideTile;
    ZTameToggleTile resolutionTile;

    ZTameAudioProcessor::UiSnapshot snapshot;
    bool   haveSnapshot = false;
    bool   sidechainConnected = false;
    juce::String statusLeft, statusRight;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZTameEditorContent)
};

//==============================================================================
/** Host-facing editor: fixed-aspect, scalable, owns the content + tooltips. */
class ZTameAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ZTameAudioProcessorEditor (ZTameAudioProcessor&);
    ~ZTameAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ZTameAudioProcessor& audioProcessor;
    ZTameEditorContent content;
    juce::TooltipWindow tooltipWindow { this, 650 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZTameAudioProcessorEditor)
};
