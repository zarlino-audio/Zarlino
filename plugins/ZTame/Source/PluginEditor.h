#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/** Premium LookAndFeel — scientific audio instrument aesthetic.

    Color palette:
      Background:  #0A0F16, #101720, #151E2C
      Accent:      #22E8FF (Electric Cyan), #49B6FF (Cool Blue)
      Detection:   soft white glow
      Text:        white / dim gray
*/
class ZtameLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ZtameLookAndFeel();

    // -- colours ----------------------------------------------------------------
    static juce::Colour colBgBase()     { return juce::Colour (0xFF0A0F16); }
    static juce::Colour colBgPanel()    { return juce::Colour (0xFF101720); }
    static juce::Colour colBgRaised()   { return juce::Colour (0xFF151E2C); }
    static juce::Colour colAccentCyan() { return juce::Colour (0xFF22E8FF); }
    static juce::Colour colAccentBlue() { return juce::Colour (0xFF49B6FF); }
    static juce::Colour colText()       { return juce::Colour (0xFFE8ECF0); }
    static juce::Colour colDim()        { return juce::Colour (0xFF6A7A8E); }

    // -- overrides --------------------------------------------------------------
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPos, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& bg, bool over, bool down) override;
    juce::Font getLabelFont (juce::Label&) override;
};

//==============================================================================
/** Live resonance analyzer — FFT magnitude display with frequency grid.

    Draws an animated waveform of the current frequency spectrum with
    cyan glow highlights on the magnitude curve.  Sits across the upper
    portion of the plugin window.
*/
class ResonanceAnalyzer final : public juce::Component
{
public:
    ResonanceAnalyzer();

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Push a new frame of magnitude data (numBins bins, linear scale). */
    void pushMagnitude (const float* mag, int bins, double sampleRate);

private:
    // frequency labels
    void drawFrequencyGrid (juce::Graphics& g, const juce::Rectangle<int>& area);

    std::vector<float> smoothed;
    std::vector<float> display;
    double sampleRateHz = 44100.0;
    int binCount = 0;
};

//==============================================================================
/** Gain-reduction display — animated bar with peak-hold and numeric dB readout. */
class GainReductionDisplay final : public juce::Component
{
public:
    GainReductionDisplay();
    void paint (juce::Graphics&) override;
    void resized() override {}

    void setLevel (float db) { currentDb = juce::jlimit (-60.0f, 0.0f, db); repaint(); }

private:
    float currentDb = 0.0f;
    float peakDb    = 0.0f;
    float peakTimer = 0.0f;
};

//==============================================================================
/** Simple horizontal LED-style meter bar. */
class MeterBar final : public juce::Component
{
public:
    MeterBar();
    void paint (juce::Graphics&) override;
    void resized() override {}
    void setLevel (float db) { currentDb = juce::jlimit (-60.0f, 0.0f, db); repaint(); }

private:
    float currentDb = -60.0f;
};

//==============================================================================
/** Small scrolling reduction-history graph. */
class ReductionHistory final : public juce::Component
{
public:
    ReductionHistory();
    void paint (juce::Graphics&) override;
    void resized() override;
    void pushSample (float db);

private:
    std::vector<float> history;
    int writeIndex = 0;
    static constexpr int historyLength = 512;
};

//==============================================================================
/** The ZTame editor — premium resonance-tamer UI.

    Layout (approx 900 × 560):
      ┌──────────────────────────────────────────────┐
      │  ZTame   PRESET ▼   ↶ ↷  ⚙   Zarlino Audio  │  ← header
      ├──────────────────────────────────────────────┤
      │         Resonance Analyzer (FFT)             │  ← live spectrum
      │   20  50 100  200  500  1k  2k  5k 10k 20k  │
      ├──────┬──────┬──────┬──────┬─────────────────┤
      │ SENS │RANGE │ SEL  │XOVER │  GR METER       │
      │ knob │knob  │ knob │knob  │  [peak hold]    │
      ├──────┴──────┴──────┴──────┤  [dB numeric]   │
      │ ATTACK  RELEASE  DRY/WET │                  │
      │ knob     knob      knob  │                  │
      ├──────────────────────────┼──────────────────┤
      │ LISTEN    ⏻ BYPASS       │ IN │ OUT │ HIST │
      └──────────────────────────────────────────────┘
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

    ZTameAudioProcessor& audioProcessor;

    // LookAndFeel (owned, applied to all components)
    ZtameLookAndFeel ztameLNF;

    // Child components ---------------------------------------------------------

    // Header
    juce::Label titleLabel;
    juce::Label taglineLabel;

    // Resonance analyzer
    ResonanceAnalyzer analyzer;

    // Sliders + attachments
    struct LabelledKnob
    {
        juce::Slider slider;
        juce::Label label;
        juce::Label valueLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    LabelledKnob sensitivityKnob;
    LabelledKnob rangeKnob;
    LabelledKnob selectivityKnob;
    LabelledKnob crossoverKnob;
    LabelledKnob attackKnob;
    LabelledKnob releaseKnob;
    LabelledKnob dryWetKnob;

    // Buttons
    struct LabelledButton
    {
        juce::TextButton button;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    LabelledButton listenButton;
    LabelledButton bypassButton;

    // Gain reduction display
    GainReductionDisplay grDisplay;

    // I/O meters + history
    MeterBar inputMeter;
    MeterBar outputMeter;
    juce::Label inMeterLabel;
    juce::Label outMeterLabel;
    ReductionHistory historyGraph;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZTameAudioProcessorEditor)
};
