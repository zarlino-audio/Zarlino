#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
    constexpr int kEditorWidth  = 620;
    constexpr int kEditorHeight = 480;
    constexpr int kMeterHeight  = 28;
}

//==============================================================================
// Styling helpers

juce::Colour ZAdaptiveHarmonicsAudioProcessorEditor::accentColour()
{
    return juce::Colour (0xff53b7ff);
}

juce::Colour ZAdaptiveHarmonicsAudioProcessorEditor::bgColour()
{
    return juce::Colour (0xff1a1a2e);
}

void ZAdaptiveHarmonicsAudioProcessorEditor::setupKnob (juce::Slider& slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 20);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accentColour());
    slider.setColour (juce::Slider::rotarySliderOutlineColourId,
                      juce::Colours::white.withAlpha (0.15f));
    slider.setColour (juce::Slider::thumbColourId, accentColour());
    slider.setColour (juce::Slider::textBoxTextColourId,
                      juce::Colours::white.withAlpha (0.8f));
    slider.setColour (juce::Slider::textBoxOutlineColourId,
                      juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId,
                      juce::Colours::white.withAlpha (0.05f));
}

void ZAdaptiveHarmonicsAudioProcessorEditor::setupLabel (juce::Label& label,
                                                          const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::Font (juce::FontOptions (13.0f)));
    label.setColour (juce::Label::textColourId,
                     juce::Colours::white.withAlpha (0.7f));
}

//==============================================================================
// Macro tab content component

class MacroTabContent final : public juce::Component
{
public:
    explicit MacroTabContent (ZAdaptiveHarmonicsAudioProcessorEditor& owner)
        : editor (owner)
    {
        addAndMakeVisible (editor.liftKnob);
        addAndMakeVisible (editor.characterKnob);
        addAndMakeVisible (editor.mixKnob);
        addAndMakeVisible (editor.liftLabel);
        addAndMakeVisible (editor.characterLabel);
        addAndMakeVisible (editor.mixLabel);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (24, 16);

        // Meter strip at the bottom
        const auto meterArea = area.removeFromBottom (kMeterHeight);
        editor.meterBounds = meterArea.toFloat();

        // Remaining space for knobs
        const int knobAreaHeight = area.getHeight();
        const int largeKnobSize  = juce::jmin (120, knobAreaHeight - 30);
        const int smallKnobSize  = juce::jmin (80, knobAreaHeight - 60);
        const int labelHeight    = 18;

        // "Lift" — large center knob
        auto liftArea = juce::Rectangle<int> (0, 0, area.getWidth(), largeKnobSize + labelHeight + 8);
        liftArea.setCentre (area.getCentreX(), area.getY() + liftArea.getHeight() / 2);
        editor.liftKnob.setBounds (liftArea.withHeight (largeKnobSize));
        editor.liftLabel.setBounds (liftArea.getX(), liftArea.getBottom() - labelHeight,
                                     liftArea.getWidth(), labelHeight);

        // "Character" — smaller, below and left
        const int yBot = area.getY() + largeKnobSize + labelHeight + 8;
        const int botHeight = area.getBottom() - yBot;
        auto charArea = juce::Rectangle<int> (0, 0, smallKnobSize, smallKnobSize + labelHeight + 8);
        charArea.setPosition (area.getCentreX() - smallKnobSize - 20,
                              yBot + (botHeight - charArea.getHeight()) / 2);
        editor.characterKnob.setBounds (charArea.withHeight (smallKnobSize));
        editor.characterLabel.setBounds (charArea.getX(), charArea.getBottom() - labelHeight,
                                          charArea.getWidth(), labelHeight);

        // "Mix" — smaller, below and right
        auto mixArea = juce::Rectangle<int> (0, 0, smallKnobSize, smallKnobSize + labelHeight + 8);
        mixArea.setPosition (area.getCentreX() + 20,
                             yBot + (botHeight - mixArea.getHeight()) / 2);
        editor.mixKnob.setBounds (mixArea.withHeight (smallKnobSize));
        editor.mixLabel.setBounds (mixArea.getX(), mixArea.getBottom() - labelHeight,
                                    mixArea.getWidth(), labelHeight);
    }

    void paint (juce::Graphics& g) override
    {
        // Meter bar background
        const auto& mb = editor.meterBounds;
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (mb, 4.0f);

        // Meter bar fill
        const float level = juce::jlimit (0.0f, 1.0f, editor.displayedMeterLevel);
        if (level > 0.01f)
        {
            auto fill = mb.reduced (2.0f);
            fill.setWidth (fill.getWidth() * level);
            g.setColour (ZAdaptiveHarmonicsAudioProcessorEditor::accentColour()
                          .withAlpha (0.7f + level * 0.3f));
            g.fillRoundedRectangle (fill, 3.0f);
        }

        // Meter label
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawText ("Activity", mb, juce::Justification::centred);
    }

private:
    ZAdaptiveHarmonicsAudioProcessorEditor& editor;
};

//==============================================================================
// Advanced tab content component

class AdvancedTabContent final : public juce::Component
{
public:
    explicit AdvancedTabContent (ZAdaptiveHarmonicsAudioProcessorEditor& owner)
        : editor (owner)
    {
        addAndMakeVisible (editor.satEvenKnob);
        addAndMakeVisible (editor.satOddKnob);
        addAndMakeVisible (editor.satHarmReductionKnob);
        addAndMakeVisible (editor.tiltAmountKnob);
        addAndMakeVisible (editor.exciterAmountKnob);
        addAndMakeVisible (editor.exciterEnabledButton);

        addAndMakeVisible (editor.satEvenLabel);
        addAndMakeVisible (editor.satOddLabel);
        addAndMakeVisible (editor.satHarmReductionLabel);
        addAndMakeVisible (editor.tiltAmountLabel);
        addAndMakeVisible (editor.exciterAmountLabel);
        addAndMakeVisible (editor.exciterEnabledLabel);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (20, 16);
        const int labelHeight = 16;
        const int knobHeight  = 74;
        const int sectionGap  = 18;
        const int knobWidth   = 84;
        const int rowHeight   = knobHeight + labelHeight + 4;

        // --- Row 1: Saturator section spacing ---
        area.removeFromTop (20);

        // --- Row 2: Saturator knobs ---
        auto row2 = area.removeFromTop (rowHeight);
        {
            const int threeW = row2.getWidth() / 3;
            auto r2a = row2.removeFromLeft (threeW);
            editor.satEvenKnob.setBounds (r2a.getX(), r2a.getY(), knobWidth, knobHeight);
            editor.satEvenLabel.setBounds (r2a.getX(), r2a.getY() + knobHeight, knobWidth, labelHeight);

            auto r2b = row2.removeFromLeft (threeW);
            editor.satOddKnob.setBounds (r2b.getX(), r2b.getY(), knobWidth, knobHeight);
            editor.satOddLabel.setBounds (r2b.getX(), r2b.getY() + knobHeight, knobWidth, labelHeight);

            editor.satHarmReductionKnob.setBounds (row2.getX(), row2.getY(), knobWidth, knobHeight);
            editor.satHarmReductionLabel.setBounds (row2.getX(), row2.getY() + knobHeight, knobWidth, labelHeight);
        }

        area.removeFromTop (sectionGap);

        // --- Row 3: Tilt section spacing ---
        area.removeFromTop (20);

        auto r3knob = area.removeFromTop (rowHeight);
        editor.tiltAmountKnob.setBounds (r3knob.getX(), r3knob.getY(), knobWidth, knobHeight);
        editor.tiltAmountLabel.setBounds (r3knob.getX(), r3knob.getY() + knobHeight, knobWidth, labelHeight);

        area.removeFromTop (sectionGap);

        // --- Row 4: Exciter section ---
        area.removeFromTop (20);

        auto r4body = area.removeFromTop (rowHeight);
        editor.exciterAmountKnob.setBounds (r4body.getX(), r4body.getY(), knobWidth, knobHeight);
        editor.exciterAmountLabel.setBounds (r4body.getX(), r4body.getY() + knobHeight, knobWidth, labelHeight);

        const int toggleX = r4body.getX() + knobWidth + 24;
        const int toggleW = 80;
        const int toggleH = 30;
        editor.exciterEnabledButton.setBounds (toggleX,
            r4body.getY() + knobHeight / 2 - toggleH / 2, toggleW, toggleH);
        editor.exciterEnabledLabel.setBounds (toggleX,
            r4body.getY() + knobHeight / 2 + toggleH / 2 + 4, toggleW, labelHeight);
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().reduced (20, 16);
        const int rowHeight = 90;

        // Section header style
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.setColour (ZAdaptiveHarmonicsAudioProcessorEditor::accentColour().withAlpha (0.8f));

        auto r1 = area.removeFromTop (20);
        g.drawText ("Saturator", r1.toFloat(), juce::Justification::centredLeft);

        area.removeFromTop (rowHeight);
        area.removeFromTop (18);

        auto r3top = area.removeFromTop (20);
        g.drawText ("Dynamic Spectral Tilt", r3top.toFloat(), juce::Justification::centredLeft);

        area.removeFromTop (rowHeight - 20);
        area.removeFromTop (18);

        auto r4top = area.removeFromTop (20);
        g.drawText ("Exciter", r4top.toFloat(), juce::Justification::centredLeft);
    }

private:
    ZAdaptiveHarmonicsAudioProcessorEditor& editor;
};

//==============================================================================
ZAdaptiveHarmonicsAudioProcessorEditor::ZAdaptiveHarmonicsAudioProcessorEditor (
    ZAdaptiveHarmonicsAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      tabs (juce::TabbedButtonBar::TabsAtTop)
{
    // --- Setup all knobs ---
    setupKnob (liftKnob);
    setupKnob (characterKnob);
    setupKnob (mixKnob);
    setupKnob (satEvenKnob);
    setupKnob (satOddKnob);
    setupKnob (satHarmReductionKnob);
    setupKnob (tiltAmountKnob);
    setupKnob (exciterAmountKnob);

    // --- Setup labels ---
    setupLabel (liftLabel,      "Lift");
    setupLabel (characterLabel, "Warm \xe2\x80\xa2 Character \xe2\x80\xa2 Bright");
    setupLabel (mixLabel,       "Dry \xe2\x80\xa2 Mix \xe2\x80\xa2 Wet");
    setupLabel (satEvenLabel,          "Even");
    setupLabel (satOddLabel,           "Odd");
    setupLabel (satHarmReductionLabel, "Harm Redux");
    setupLabel (tiltAmountLabel,       "Tilt Amount");
    setupLabel (exciterAmountLabel,    "Exciter Amount");
    setupLabel (exciterEnabledLabel,   "Enable");

    // --- Setup exciter toggle ---
    exciterEnabledButton.setColour (juce::ToggleButton::tickColourId, accentColour());
    exciterEnabledButton.setColour (juce::ToggleButton::tickDisabledColourId,
                                     juce::Colours::white.withAlpha (0.15f));

    // --- APVTS attachments ---
    auto& apvts = audioProcessor.getValueTreeState();
    liftAttachment      = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "lift", liftKnob);
    characterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "character", characterKnob);
    mixAttachment       = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "mix", mixKnob);
    satEvenAttachment   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "saturatorEven", satEvenKnob);
    satOddAttachment    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "saturatorOdd", satOddKnob);
    satHarmReductionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "saturatorHarmReduction", satHarmReductionKnob);
    tiltAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "tiltAmount", tiltAmountKnob);
    exciterAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "exciterAmount", exciterAmountKnob);
    exciterEnabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "exciterEnabled", exciterEnabledButton);

    // --- Tab setup ---
    auto* macroContent = new MacroTabContent (*this);
    tabs.addTab ("Macro", bgColour(), macroContent, true);

    auto* advContent = new AdvancedTabContent (*this);
    tabs.addTab ("Advanced", bgColour(), advContent, true);

    tabs.setCurrentTabIndex (0);
    tabs.setTabBarDepth (28);
    addAndMakeVisible (tabs);

    setSize (kEditorWidth, kEditorHeight);

    // Start meter polling at ~25 fps
    startTimerHz (25);
}

//==============================================================================
void ZAdaptiveHarmonicsAudioProcessorEditor::timerCallback()
{
    const float target = audioProcessor.meterLevel.load (std::memory_order_relaxed);
    displayedMeterLevel += (target - displayedMeterLevel) * 0.3f;
    // Repaint only the meter area
    auto meterRect = meterBounds.toNearestInt().expanded (2);
    repaint (meterRect);
}

//==============================================================================
void ZAdaptiveHarmonicsAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Dark background
    g.setColour (bgColour());
    g.fillRoundedRectangle (bounds, 18.0f);

    // Subtle border
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (bounds.reduced (1.0f), 18.0f, 1.0f);

    // Title area
    const int titleTop    = 8;
    const int titleHeight = 36;
    g.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)));
    g.setColour (juce::Colours::white);
    g.drawText ("ZAdaptiveHarmonics", 0, titleTop, getWidth(), titleHeight,
                juce::Justification::centred, false);

    // Accent bar under title
    auto accentBar = juce::Rectangle<int> (40, titleTop + titleHeight + 6,
                                           getWidth() - 80, 3);
    g.setColour (accentColour());
    g.fillRoundedRectangle (accentBar.toFloat(), 1.5f);

    // Subtitle
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.setColour (juce::Colour (0xff8edcff));
    g.drawText ("adaptive harmonic processor \xe2\x80\xa2 Zarlino Audio",
                0, titleTop + titleHeight + 14, getWidth(), 18,
                juce::Justification::centred, false);
}

//==============================================================================
void ZAdaptiveHarmonicsAudioProcessorEditor::resized()
{
    const int tabTop = 95;
    tabs.setBounds (0, tabTop, getWidth(), getHeight() - tabTop);
}
