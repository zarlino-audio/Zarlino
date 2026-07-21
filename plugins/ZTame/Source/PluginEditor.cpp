#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
    juce::Colour accentColour() { return juce::Colour (0xff53b7ff); }
    juce::Colour glowColour() { return juce::Colour (0xff8e63ff); }
}

//==============================================================================
ZTameAudioProcessorEditor::ZTameAudioProcessorEditor (
        ZTameAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    const auto setupSlider = [this] (LabelledSlider& ls, const juce::String& name,
                                      const juce::String& paramId,
                                      const juce::String& suffix)
    {
        auto& sld = ls.slider;
        sld.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        sld.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 22);
        sld.setColour (juce::Slider::rotarySliderFillColourId, accentColour());
        sld.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff26324d));
        sld.setColour (juce::Slider::thumbColourId, juce::Colour (0xff84d8ff));
        sld.setColour (juce::Slider::trackColourId, juce::Colours::transparentBlack);
        sld.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
        sld.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0x66151c2a));
        sld.setTextValueSuffix (suffix);
        sld.setVelocityModeParameters (1.0, 1, 0.0, false);
        addAndMakeVisible (sld);

        auto& lbl = ls.label;
        lbl.setText (name, juce::dontSendNotification);
        lbl.setJustificationType (juce::Justification::centred);
        lbl.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        lbl.attachToComponent (&sld, false);
        lbl.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (lbl);

        ls.attachment = std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment> (
            audioProcessor.getValueTreeState(), paramId, sld);
    };

    const auto setupButton = [this] (LabelledButton& lb, const juce::String& name,
                                      const juce::String& paramId)
    {
        auto& btn = lb.button;
        btn.setButtonText (name);
        btn.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
        btn.setColour (juce::ToggleButton::tickColourId, accentColour());
        btn.setColour (juce::TextButton::buttonOnColourId, accentColour());
        btn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff22314d));
        addAndMakeVisible (btn);

        auto& lbl = lb.label;
        lbl.setText (name, juce::dontSendNotification);
        lbl.setJustificationType (juce::Justification::centred);
        lbl.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        lbl.attachToComponent (&btn, false);
        addAndMakeVisible (lbl);

        lb.attachment = std::make_unique<
            juce::AudioProcessorValueTreeState::ButtonAttachment> (
            audioProcessor.getValueTreeState(), paramId, btn);
    };

    setupSlider (sensitivitySlider, "Sensitivity",  "sensitivity",    "%");
    setupSlider (rangeSlider,       "Range",        "range",          " dB");
    setupSlider (selectivitySlider, "Selectivity",  "selectivity",    "%");
    setupSlider (crossoverSlider,   "Crossover",    "crossoverFreq",  " Hz");
    setupSlider (attackSlider,      "Attack",       "attack",         " ms");
    setupSlider (releaseSlider,     "Release",      "release",        " ms");
    setupSlider (dryWetSlider,      "Dry/Wet",      "dryWet",         "%");

    setupButton (listenButton,  "Listen",  "listenMode");
    setupButton (bypassButton,  "Bypass",  "bypass");

    grMeterLabel.setJustificationType (juce::Justification::centred);
    grMeterLabel.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    grMeterLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    grMeterLabel.setText ("GR: 0.0 dB", juce::dontSendNotification);
    addAndMakeVisible (grMeterLabel);

    setSize (760, 430);
    startTimerHz (30);
}

ZTameAudioProcessorEditor::~ZTameAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void ZTameAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto bgGradient = juce::ColourGradient (
        juce::Colour (0xff0a1020), 0.0f, 0.0f,
        juce::Colour (0xff20133d), 0.0f, static_cast<float> (getHeight()), false);
    g.setGradientFill (bgGradient);
    g.fillRoundedRectangle (bounds, 24.0f);

    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (bounds.reduced (1.0f), 24.0f, 1.0f);

    g.setColour (juce::Colour (0xff3d4d73));
    g.drawRoundedRectangle (bounds.reduced (2.0f), 24.0f, 1.0f);

    auto headerArea = juce::Rectangle<int> (24, 20, getWidth() - 48, 90);
    auto accentBar = headerArea.withHeight (4).withY (headerArea.getBottom() - 4);
    g.setColour (accentColour());
    g.fillRoundedRectangle (accentBar.toFloat(), 2.0f);

    g.setFont (juce::Font (juce::FontOptions (30.0f, juce::Font::bold)));
    g.setColour (juce::Colours::white);
    g.drawText ("ZTame", 0, 22, getWidth(), 36, juce::Justification::centred, false);

    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.setColour (juce::Colour (0xff8edcff));
    g.drawText ("immersive resonance shaping • Zarlino Audio", 0, 56, getWidth(), 22,
                juce::Justification::centred, false);

    auto panelBounds = getLocalBounds().reduced (18, 90);
    g.setColour (juce::Colour (0x66131a2e));
    g.fillRoundedRectangle (panelBounds.toFloat(), 20.0f);

    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (panelBounds.toFloat().reduced (0.5f), 20.0f, 1.0f);

    const auto meterBounds = getLocalBounds().reduced (26, 26).removeFromBottom (34);
    g.setColour (juce::Colour (0xff17233a));
    g.fillRoundedRectangle (meterBounds.toFloat(), 8.0f);

    if (displayGR > 0.5f)
    {
        const auto maxGR = 24.0f;
        const auto norm = juce::jlimit (0.0f, 1.0f, displayGR / maxGR);
        auto fillBounds = meterBounds;
        fillBounds.setWidth (static_cast<int> (fillBounds.getWidth() * norm));
        g.setColour (juce::Colour (0xff53b7ff).withAlpha (0.95f));
        g.fillRoundedRectangle (fillBounds.toFloat(), 8.0f);
    }
}

void ZTameAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (24, 96);
    auto topRow = area.removeFromTop (120);
    auto midRow = area.removeFromTop (120);

    auto topSlots = topRow.reduced (6, 0);
    const auto topSlotW = topSlots.getWidth() / 4;
    sensitivitySlider.slider.setBounds (topSlots.removeFromLeft (topSlotW).reduced (8, 0));
    rangeSlider.slider.setBounds       (topSlots.removeFromLeft (topSlotW).reduced (8, 0));
    selectivitySlider.slider.setBounds  (topSlots.removeFromLeft (topSlotW).reduced (8, 0));
    crossoverSlider.slider.setBounds    (topSlots.reduced (8, 0));

    auto midSlots = midRow.reduced (8, 0);
    const auto midSlotW = midSlots.getWidth() / 3;
    attackSlider.slider.setBounds  (midSlots.removeFromLeft (midSlotW).reduced (8, 0));
    releaseSlider.slider.setBounds (midSlots.removeFromLeft (midSlotW).reduced (8, 0));
    dryWetSlider.slider.setBounds  (midSlots.reduced (8, 0));

    const auto bottomArea = area.reduced (10, 8);
    auto btnArea = bottomArea;
    listenButton.button.setBounds (btnArea.removeFromLeft (110).reduced (4));
    bypassButton.button.setBounds (btnArea.removeFromLeft (110).reduced (4));

    grMeterLabel.setBounds (btnArea.reduced (10, 2));
}

//==============================================================================
void ZTameAudioProcessorEditor::timerCallback()
{
    const auto rawGR = audioProcessor.getGainReductionDb();
    displayGR += (rawGR - displayGR) * 0.3f;
    grMeterLabel.setText ("GR: " + juce::String (displayGR, 1) + " dB",
                          juce::dontSendNotification);
    repaint();
}
