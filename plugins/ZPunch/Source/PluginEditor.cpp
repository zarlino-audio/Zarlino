#include "PluginEditor.h"
#include "PluginProcessor.h"

//==============================================================================
ZPunchAudioProcessorEditor::ZPunchAudioProcessorEditor (ZPunchAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Dark UI theme
    const auto bgColour    = juce::Colour (0xff1a1a2e);
    const auto knobColour  = juce::Colour (0xffe94560);
    const auto textColour  = juce::Colour (0xffeeeeee);
    const auto meterLow    = juce::Colour (0xff00b894);
    const auto meterHigh   = juce::Colour (0xffe94560);

    auto setupSlider = [&](juce::Slider& s, juce::Label& l, const juce::String& name)
    {
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
        s.setColour (juce::Slider::rotarySliderFillColourId, knobColour);
        s.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff333355));
        s.setColour (juce::Slider::textBoxTextColourId, textColour);
        s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff0f0f23));
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0x00000000));
        addAndMakeVisible (s);

        l.setText (name, juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, textColour);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::Font (13.0f));
        addAndMakeVisible (l);
    };

    setupSlider (inputGainSlider,   inputGainLabel,   "Input");
    setupSlider (attackSlider,      attackLabel,      "Attack");
    setupSlider (sustainSlider,     sustainLabel,     "Sustain");
    setupSlider (attackTimeSlider,  attackTimeLabel,  "Atk Time");
    setupSlider (releaseTimeSlider, releaseTimeLabel, "Rel Time");
    setupSlider (sensitivitySlider, sensitivityLabel, "Sense");
    setupSlider (saturationSlider,  saturationLabel,  "Saturate");
    setupSlider (outputGainSlider,  outputGainLabel,  "Output");
    setupSlider (dryWetSlider,      dryWetLabel,      "Mix");

    // Bypass button
    bypassButton.setButtonText ("BYPASS");
    bypassButton.setColour (juce::ToggleButton::textColourId, textColour);
    bypassButton.setColour (juce::ToggleButton::tickColourId, knobColour);
    bypassButton.setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (0xff555555));
    addAndMakeVisible (bypassButton);

    // Meter labels
    auto setupMeter = [&](juce::Label& m)
    {
        m.setColour (juce::Label::textColourId, meterLow);
        m.setFont (juce::Font (12.0f, juce::Font::bold));
        m.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (m);
    };
    setupMeter (inputMeterLabel);
    setupMeter (outputMeterLabel);
    inputMeterLabel.setText ("In: --- dB", juce::dontSendNotification);
    outputMeterLabel.setText ("Out: --- dB", juce::dontSendNotification);

    // Attach to parameters
    auto& apvts = audioProcessor.getValueTreeState();
    const auto id = [](const char* s) { return juce::ParameterID { s, 1 }; };

    inputGainAttach   = std::make_unique<Attachment> (apvts, "inputGain",   inputGainSlider);
    attackAttach      = std::make_unique<Attachment> (apvts, "attack",      attackSlider);
    sustainAttach     = std::make_unique<Attachment> (apvts, "sustain",     sustainSlider);
    attackTimeAttach  = std::make_unique<Attachment> (apvts, "attackTime",  attackTimeSlider);
    releaseTimeAttach = std::make_unique<Attachment> (apvts, "releaseTime", releaseTimeSlider);
    sensitivityAttach = std::make_unique<Attachment> (apvts, "sensitivity", sensitivitySlider);
    saturationAttach  = std::make_unique<Attachment> (apvts, "saturation",  saturationSlider);
    outputGainAttach  = std::make_unique<Attachment> (apvts, "outputGain",  outputGainSlider);
    dryWetAttach      = std::make_unique<Attachment> (apvts, "dryWet",      dryWetSlider);
    bypassAttach      = std::make_unique<ButtonAttachment> (apvts, "bypass", bypassButton);

    setSize (620, 440);

    // Start meter refresh timer (30 Hz)
    startTimerHz (30);
}

//==============================================================================
void ZPunchAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto bgColour = juce::Colour (0xff1a1a2e);
    g.fillAll (bgColour);

    // Title
    g.setColour (juce::Colour (0xffeeeeee));
    g.setFont (juce::Font (22.0f, juce::Font::bold));
    g.drawText ("ZPunch", 20, 8, 200, 30, juce::Justification::left);

    g.setFont (juce::Font (11.0f));
    g.setColour (juce::Colour (0xff888899));
    g.drawText ("Transient Shaper", 20, 34, 200, 16, juce::Justification::left);

    // Section dividers
    g.setColour (juce::Colour (0xff333355));
    g.drawLine (20.0f, 56.0f, 600.0f, 56.0f, 1.0f);

    // Section labels
    g.setColour (juce::Colour (0xff888899));
    g.setFont (juce::Font (11.0f, juce::Font::bold));

    const int topSectionY    = 66;
    const int midSectionY    = 222;

    g.drawText ("SHAPING",        150, topSectionY,    80, 16, juce::Justification::centred);
    g.drawText ("DETECTOR",       330, topSectionY,    80, 16, juce::Justification::centred);
    g.drawText ("CHARACTER",      510, topSectionY,    80, 16, juce::Justification::centred);

    g.drawLine (20.0f, static_cast<float> (midSectionY - 6), 600.0f,
                static_cast<float> (midSectionY - 6), 1.0f);
    g.drawText ("MIX & BYPASS", 250, midSectionY, 120, 16, juce::Justification::centred);
}

//==============================================================================
void ZPunchAudioProcessorEditor::resized()
{
    const int sliderSize  = 72;
    const int labelHeight = 18;
    const int topRowY     = 84;
    const int midRowY     = 244;
    const int meterRowY   = 390;

    // Helper: place a slider + label
    auto place = [&](int x, int y, juce::Slider& s, juce::Label& l)
    {
        s.setBounds (x, y, sliderSize, sliderSize);
        l.setBounds (x - 8, y + sliderSize, sliderSize + 16, labelHeight);
    };

    // Top row — shaping controls
    place ( 60, topRowY, attackSlider, attackLabel);          // Attack
    place (160, topRowY, sustainSlider, sustainLabel);        // Sustain
    place (260, topRowY, attackTimeSlider, attackTimeLabel);  // Attack Time
    place (360, topRowY, releaseTimeSlider, releaseTimeLabel);// Release Time
    place (460, topRowY, sensitivitySlider, sensitivityLabel);// Sensitivity

    // Input + Output + Saturation (spread across)
    place (60,  midRowY, inputGainSlider,  inputGainLabel);     // Input
    place (160, midRowY, outputGainSlider, outputGainLabel);    // Output
    place (260, midRowY, saturationSlider, saturationLabel);    // Saturation
    place (360, midRowY, dryWetSlider, dryWetLabel);            // Mix

    // Bypass button
    bypassButton.setBounds (470, midRowY + 18, 100, 32);

    // Meter labels
    inputMeterLabel.setBounds  (20, meterRowY, 200, 20);
    outputMeterLabel.setBounds (230, meterRowY, 200, 20);
}

//==============================================================================
void ZPunchAudioProcessorEditor::timerCallback()
{
    const auto inPeak  = audioProcessor.getInputPeakDb();
    const auto outPeak = audioProcessor.getOutputPeakDb();

    auto fmt = [](float db)
    {
        if (db < -90.0f)
            return juce::String ("--- dB");
        return juce::String (db, 1) + " dB";
    };

    inputMeterLabel.setText  ("In:  " + fmt (inPeak),  juce::dontSendNotification);
    outputMeterLabel.setText ("Out: " + fmt (outPeak), juce::dontSendNotification);

    // Color output meter red if near clipping
    if (outPeak > -3.0f)
        outputMeterLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe94560));
    else if (outPeak > -6.0f)
        outputMeterLabel.setColour (juce::Label::textColourId, juce::Colour (0xfffdcb6e));
    else
        outputMeterLabel.setColour (juce::Label::textColourId, juce::Colour (0xff00b894));
}
