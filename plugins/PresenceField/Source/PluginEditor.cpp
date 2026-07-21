#include "PluginEditor.h"

namespace
{
    const auto bg = juce::Colour (0xff12151a);
    const auto panel = juce::Colour (0xff202631);
    const auto edge = juce::Colour (0xff394355);
    const auto text = juce::Colour (0xffeef3f7);
    const auto muted = juce::Colour (0xff9aa6b6);
    const auto accent = juce::Colour (0xff35d0ba);
    const auto amber = juce::Colour (0xffffc857);
}

PresenceFieldAudioProcessorEditor::PresenceFieldAudioProcessorEditor (PresenceFieldAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    auto& state = audioProcessor.getValueTreeState();

    configureSlider (masterSlider, masterLabel, "Presence Field");
    masterSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    masterSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 24);

    configureSlider (inputSlider, inputLabel, "Input");
    configureSlider (outputSlider, outputLabel, "Output");
    configureSlider (airSpeedSlider, airSpeedLabel, "Air Drift");
    configureSlider (widthSlider, widthLabel, "Width");

    identityLock.setButtonText ("Identity Lock");
    identityLock.setColour (juce::ToggleButton::textColourId, text);

    meterLabel.setColour (juce::Label::textColourId, text);
    meterLabel.setJustificationType (juce::Justification::centred);

    juce::Component* components[] = { &masterSlider, &inputSlider, &outputSlider, &airSpeedSlider, &widthSlider,
                                      &identityLock, &masterLabel, &inputLabel, &outputLabel, &airSpeedLabel,
                                      &widthLabel, &meterLabel };

    for (auto* c : components)
        addAndMakeVisible (c);

    masterAttachment = std::make_unique<Attachment> (state, "master", masterSlider);
    inputAttachment = std::make_unique<Attachment> (state, "inputGain", inputSlider);
    outputAttachment = std::make_unique<Attachment> (state, "outputGain", outputSlider);
    airSpeedAttachment = std::make_unique<Attachment> (state, "airSpeed", airSpeedSlider);
    widthAttachment = std::make_unique<Attachment> (state, "globalWidth", widthSlider);
    lockAttachment = std::make_unique<ButtonAttachment> (state, "identityLock", identityLock);

    setResizable (true, true);
    setResizeLimits (520, 360, 900, 620);
    setSize (640, 430);
    startTimerHz (20);
}

void PresenceFieldAudioProcessorEditor::configureSlider (juce::Slider& slider, juce::Label& label, const juce::String& labelText)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 22);
    slider.setColour (juce::Slider::thumbColourId, accent);
    slider.setColour (juce::Slider::trackColourId, accent.withAlpha (0.75f));
    slider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff11151b));

    label.setText (labelText, juce::dontSendNotification);
    label.setColour (juce::Label::textColourId, muted);
    label.setJustificationType (juce::Justification::centredLeft);
}

void PresenceFieldAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    auto area = getLocalBounds().reduced (18);

    g.setColour (accent);
    g.fillRoundedRectangle (area.removeFromTop (4).toFloat(), 2.0f);
    area.removeFromTop (14);

    g.setColour (text);
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText ("Presence Field", area.removeFromTop (38), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (14.0f));
    g.setColour (muted);
    g.drawText ("Spatial identity engine for finished vocals", area.removeFromTop (24), juce::Justification::centredLeft);

    auto panelArea = getLocalBounds().reduced (18).withTrimmedTop (92);
    g.setColour (panel);
    g.fillRoundedRectangle (panelArea.toFloat(), 8.0f);
    g.setColour (edge);
    g.drawRoundedRectangle (panelArea.toFloat().reduced (0.5f), 8.0f, 1.0f);

    auto knobBounds = masterSlider.getBounds().toFloat().expanded (16.0f);
    g.setColour (juce::Colour (0xff151a21));
    g.fillEllipse (knobBounds);
    g.setColour (amber.withAlpha (0.45f));
    g.drawEllipse (knobBounds, 2.0f);
}

void PresenceFieldAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (32).withTrimmedTop (94);
    auto left = area.removeFromLeft (230);
    auto right = area.reduced (14, 8);

    masterLabel.setBounds (left.removeFromTop (24));
    masterSlider.setBounds (left.removeFromTop (190).reduced (10));
    identityLock.setBounds (left.removeFromTop (32).reduced (10, 0));

    auto row = [&] (juce::Label& label, juce::Slider& slider)
    {
        label.setBounds (right.removeFromTop (22));
        slider.setBounds (right.removeFromTop (34));
        right.removeFromTop (10);
    };

    row (inputLabel, inputSlider);
    row (outputLabel, outputSlider);
    row (airSpeedLabel, airSpeedSlider);
    row (widthLabel, widthSlider);
    meterLabel.setBounds (right.removeFromTop (58));
}

void PresenceFieldAudioProcessorEditor::timerCallback()
{
    const auto zone = audioProcessor.getDistanceFactor() < 0.3f ? "Intimate"
                    : audioProcessor.getDistanceFactor() < 0.7f ? "Mid"
                    : "Belt";

    meterLabel.setText ("Distance " + juce::String (audioProcessor.getDistanceFactor(), 2)
                        + "  " + zone
                        + "    Input " + juce::String (audioProcessor.getInputRmsDb(), 1) + " dBFS"
                        + "    Air " + juce::String (audioProcessor.getAirFrequencyHz() / 1000.0f, 1) + " kHz",
                        juce::dontSendNotification);
}
