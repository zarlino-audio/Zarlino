#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
    juce::Colour accentColour()   { return juce::Colour (0xffff6b35); }  // Scorch orange
    juce::Colour glowColour()     { return juce::Colour (0xffffb347); }
    juce::Colour bgDark()         { return juce::Colour (0xff0a0a0f); }
    juce::Colour bgMid()          { return juce::Colour (0xff161a24); }
}

//==============================================================================
ZScorchAudioProcessorEditor::ZScorchAudioProcessorEditor (ZScorchAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    const auto setupSlider = [this] (LabelledSlider& ls, const juce::String& name,
                                      const juce::String& paramId,
                                      const juce::String& suffix)
    {
        auto& sld = ls.slider;
        sld.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        sld.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 20);
        sld.setColour (juce::Slider::rotarySliderFillColourId, accentColour());
        sld.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff2a2035));
        sld.setColour (juce::Slider::thumbColourId, juce::Colour (0xffff8c42));
        sld.setColour (juce::Slider::trackColourId, juce::Colours::transparentBlack);
        sld.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
        sld.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0x66151c2a));
        sld.setTextValueSuffix (suffix);
        sld.setVelocityModeParameters (1.0, 1, 0.0, false);
        addAndMakeVisible (sld);

        auto& lbl = ls.label;
        lbl.setText (name, juce::dontSendNotification);
        lbl.setJustificationType (juce::Justification::centred);
        lbl.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        lbl.attachToComponent (&sld, false);
        lbl.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (lbl);

        ls.attachment = std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment> (
            audioProcessor.getValueTreeState(), paramId, sld);
    };

    const auto setupCombo = [this] (LabelledCombo& lc, const juce::String& name,
                                     const juce::String& paramId)
    {
        auto& cb = lc.combo;
        cb.addItemList ({ "Tube", "Tape", "Transistor", "Germanium", "Diode", "Wavefold" }, 1);
        cb.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1a1f2e));
        cb.setColour (juce::ComboBox::textColourId, juce::Colours::white);
        cb.setColour (juce::ComboBox::outlineColourId, accentColour());
        cb.setColour (juce::ComboBox::arrowColourId, accentColour());
        addAndMakeVisible (cb);

        auto& lbl = lc.label;
        lbl.setText (name, juce::dontSendNotification);
        lbl.setJustificationType (juce::Justification::centred);
        lbl.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        lbl.attachToComponent (&cb, false);
        lbl.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (lbl);

        lc.attachment = std::make_unique<
            juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            audioProcessor.getValueTreeState(), paramId, cb);
    };

    const auto setupButton = [this] (LabelledButton& lb, const juce::String& name,
                                      const juce::String& paramId)
    {
        auto& btn = lb.button;
        btn.setButtonText (name);
        btn.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
        btn.setColour (juce::ToggleButton::tickColourId, accentColour());
        btn.setColour (juce::TextButton::buttonOnColourId, accentColour());
        btn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1e2438));
        addAndMakeVisible (btn);

        auto& lbl = lb.label;
        lbl.setText (name, juce::dontSendNotification);
        lbl.setJustificationType (juce::Justification::centred);
        lbl.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        lbl.attachToComponent (&btn, false);
        addAndMakeVisible (lbl);

        lb.attachment = std::make_unique<
            juce::AudioProcessorValueTreeState::ButtonAttachment> (
            audioProcessor.getValueTreeState(), paramId, btn);
    };

    // Input
    setupSlider (inputGainSlider, "Input", "inputGain", " dB");
    setupButton (bypassButton, "Bypass", "bypass");

    // Pre-EQ
    setupButton (preEqEnableButton, "Pre-EQ", "preEqEnable");
    setupSlider (preLowFreq,  "Pre Low F",  "preEqLowFreq",  " Hz");
    setupSlider (preLowGain,  "Pre Low G",  "preEqLowGain",  " dB");
    setupSlider (preLowQ,     "Pre Low Q",  "preEqLowQ",     "");
    setupSlider (preBellFreq, "Pre Bell F", "preEqBellFreq", " Hz");
    setupSlider (preBellGain, "Pre Bell G", "preEqBellGain", " dB");
    setupSlider (preBellQ,    "Pre Bell Q", "preEqBellQ",    "");
    setupSlider (preHighFreq, "Pre Hi F",   "preEqHighFreq", " Hz");
    setupSlider (preHighGain, "Pre Hi G",   "preEqHighGain", " dB");
    setupSlider (preHighQ,    "Pre Hi Q",   "preEqHighQ",    "");

    // Crossover
    setupSlider (xLowMid,    "X Lo-Mid",   "xOverLowMid",    " Hz");
    setupSlider (xMidHiMid,  "X Mid-HiM",  "xOverMidHiMid",  " Hz");
    setupSlider (xHiMidHigh, "X HiM-Hi",   "xOverHiMidHigh", " Hz");

    // Saturation bands
    const char* bandNames[4] = { "Lo", "LoMid", "HiMid", "Hi" };
    for (int b = 0; b < 4; ++b)
    {
        juce::String prefix = juce::String::formatted ("satMode%s", bandNames[b]);
        setupCombo (satMode[b],  juce::String (bandNames[b]) + " Mode", prefix);

        juce::String dPrefix = juce::String::formatted ("satDrive%s", bandNames[b]);
        setupSlider (satDrive[b], juce::String (bandNames[b]) + " Drv", dPrefix, "%");

        juce::String aPrefix = juce::String::formatted ("satAsym%s", bandNames[b]);
        setupSlider (satAsym[b], juce::String (bandNames[b]) + " Asym", aPrefix, "%");

        juce::String mPrefix = juce::String::formatted ("satMix%s", bandNames[b]);
        setupSlider (satMix[b], juce::String (bandNames[b]) + " Mix", mPrefix, "%");

        juce::String sPrefix = juce::String::formatted ("satSolo%s", bandNames[b]);
        setupButton (satSolo[b], "S", sPrefix);

        juce::String muPrefix = juce::String::formatted ("satMute%s", bandNames[b]);
        setupButton (satMute[b], "M", muPrefix);
    }

    // Post-EQ
    setupButton (postEqEnableButton, "Post-EQ", "postEqEnable");
    setupSlider (postLowFreq,  "Post Low F",  "postEqLowFreq",  " Hz");
    setupSlider (postLowGain,  "Post Low G",  "postEqLowGain",  " dB");
    setupSlider (postLowQ,     "Post Low Q",  "postEqLowQ",     "");
    setupSlider (postBellFreq, "Post Bell F", "postEqBellFreq", " Hz");
    setupSlider (postBellGain, "Post Bell G", "postEqBellGain", " dB");
    setupSlider (postBellQ,    "Post Bell Q", "postEqBellQ",    "");
    setupSlider (postHighFreq, "Post Hi F",   "postEqHighFreq", " Hz");
    setupSlider (postHighGain, "Post Hi G",   "postEqHighGain", " dB");
    setupSlider (postHighQ,    "Post Hi Q",   "postEqHighQ",    "");

    // Output
    setupSlider (outputGainSlider, "Out Gain", "outputGain", " dB");
    setupSlider (ceilingSlider,    "Ceiling",  "ceiling",    " dB");
    setupSlider (dryWetSlider,     "Dry/Wet",  "dryWet",     "%");

    // Meters
    auto setupMeter = [this] (juce::Label& lbl, const juce::String& text)
    {
        lbl.setJustificationType (juce::Justification::centred);
        lbl.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        lbl.setColour (juce::Label::textColourId, juce::Colours::white);
        lbl.setText (text, juce::dontSendNotification);
        addAndMakeVisible (lbl);
    };
    setupMeter (inMeterLabel,  "In: -∞ dB");
    setupMeter (outMeterLabel, "Out: -∞ dB");
    setupMeter (grMeterLabel,  "GR: 0.0 dB");

    setSize (1100, 680);
    startTimerHz (30);
}

ZScorchAudioProcessorEditor::~ZScorchAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void ZScorchAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    auto bgGradient = juce::ColourGradient (
        bgDark(), 0.0f, 0.0f,
        juce::Colour (0xff1a1025), 0.0f, static_cast<float> (getHeight()), false);
    g.setGradientFill (bgGradient);
    g.fillRoundedRectangle (bounds, 24.0f);

    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (bounds.reduced (1.0f), 24.0f, 1.0f);

    // Header
    auto headerArea = juce::Rectangle<int> (24, 16, getWidth() - 48, 70);
    g.setFont (juce::Font (juce::FontOptions (28.0f, juce::Font::bold)));
    g.setColour (juce::Colours::white);
    g.drawText ("ZScorch", 0, 18, getWidth(), 32, juce::Justification::centred, false);

    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.setColour (accentColour());
    g.drawText ("multi-band harmonic saturator • Zarlino Audio", 0, 48, getWidth(), 20,
                juce::Justification::centred, false);

    // Panel background
    auto panelBounds = getLocalBounds().reduced (14, 78);
    g.setColour (bgMid().withAlpha (0.85f));
    g.fillRoundedRectangle (panelBounds.toFloat(), 18.0f);

    // Meter bar at bottom
    const auto meterBounds = getLocalBounds().reduced (20, 20).removeFromBottom (30);
    g.setColour (juce::Colour (0xff1e2438));
    g.fillRoundedRectangle (meterBounds.toFloat(), 6.0f);
}

void ZScorchAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (16, 82);

    // Row heights
    const int rowH = 72;
    const int btnW = 38;

    // Row 0: Input Gain + Bypass
    auto row0 = area.removeFromTop (rowH);
    inputGainSlider.slider.setBounds (row0.removeFromLeft (90).reduced (4));
    row0.removeFromLeft (20);
    bypassButton.button.setBounds (row0.removeFromLeft (70).reduced (4));

    // Row 1: Pre-EQ Row
    auto preRow1 = area.removeFromTop (rowH);
    preEqEnableButton.button.setBounds (preRow1.removeFromLeft (btnW).reduced (4));
    preLowFreq.slider.setBounds (preRow1.removeFromLeft (72).reduced (4));
    preLowGain.slider.setBounds (preRow1.removeFromLeft (72).reduced (4));
    preLowQ.slider.setBounds   (preRow1.removeFromLeft (72).reduced (4));

    // Row 2: Pre-EQ Bell
    auto preRow2 = area.removeFromTop (rowH);
    preRow2.removeFromLeft (btnW + 4);
    preBellFreq.slider.setBounds (preRow2.removeFromLeft (72).reduced (4));
    preBellGain.slider.setBounds (preRow2.removeFromLeft (72).reduced (4));
    preBellQ.slider.setBounds   (preRow2.removeFromLeft (72).reduced (4));

    // Row 3: Pre-EQ High Shelf
    auto preRow3 = area.removeFromTop (rowH);
    preRow3.removeFromLeft (btnW + 4);
    preHighFreq.slider.setBounds (preRow3.removeFromLeft (72).reduced (4));
    preHighGain.slider.setBounds (preRow3.removeFromLeft (72).reduced (4));
    preHighQ.slider.setBounds   (preRow3.removeFromLeft (72).reduced (4));

    // Row 4: Crossover
    auto xRow = area.removeFromTop (rowH);
    xLowMid.slider.setBounds   (xRow.removeFromLeft (90).reduced (4));
    xMidHiMid.slider.setBounds (xRow.removeFromLeft (90).reduced (4));
    xHiMidHigh.slider.setBounds(xRow.removeFromLeft (90).reduced (4));

    // Rows 5-8: Saturation bands
    for (int b = 0; b < 4; ++b)
    {
        auto satRow = area.removeFromTop (rowH);
        satMode[b].combo.setBounds   (satRow.removeFromLeft (76).reduced (4));
        satDrive[b].slider.setBounds (satRow.removeFromLeft (72).reduced (4));
        satAsym[b].slider.setBounds  (satRow.removeFromLeft (72).reduced (4));
        satMix[b].slider.setBounds   (satRow.removeFromLeft (72).reduced (4));
        satSolo[b].button.setBounds  (satRow.removeFromLeft (btnW).reduced (4));
        satMute[b].button.setBounds  (satRow.removeFromLeft (btnW).reduced (4));
    }

    // Row 9: Post-EQ Row
    auto postRow1 = area.removeFromTop (rowH);
    postEqEnableButton.button.setBounds (postRow1.removeFromLeft (btnW).reduced (4));
    postLowFreq.slider.setBounds (postRow1.removeFromLeft (72).reduced (4));
    postLowGain.slider.setBounds (postRow1.removeFromLeft (72).reduced (4));
    postLowQ.slider.setBounds   (postRow1.removeFromLeft (72).reduced (4));

    // Row 10: Post-EQ Bell
    auto postRow2 = area.removeFromTop (rowH);
    postRow2.removeFromLeft (btnW + 4);
    postBellFreq.slider.setBounds (postRow2.removeFromLeft (72).reduced (4));
    postBellGain.slider.setBounds (postRow2.removeFromLeft (72).reduced (4));
    postBellQ.slider.setBounds   (postRow2.removeFromLeft (72).reduced (4));

    // Row 11: Post-EQ High Shelf
    auto postRow3 = area.removeFromTop (rowH);
    postRow3.removeFromLeft (btnW + 4);
    postHighFreq.slider.setBounds (postRow3.removeFromLeft (72).reduced (4));
    postHighGain.slider.setBounds (postRow3.removeFromLeft (72).reduced (4));
    postHighQ.slider.setBounds   (postRow3.removeFromLeft (72).reduced (4));

    // Row 12: Output
    auto outRow = area;
    outputGainSlider.slider.setBounds (outRow.removeFromLeft (90).reduced (4));
    ceilingSlider.slider.setBounds (outRow.removeFromLeft (90).reduced (4));
    dryWetSlider.slider.setBounds (outRow.removeFromLeft (90).reduced (4));

    // Meters
    auto meterArea = getLocalBounds().removeFromBottom (28).reduced (28, 0);
    const int mw = meterArea.getWidth() / 3;
    inMeterLabel.setBounds  (meterArea.removeFromLeft (mw).reduced (4, 2));
    outMeterLabel.setBounds (meterArea.removeFromLeft (mw).reduced (4, 2));
    grMeterLabel.setBounds  (meterArea.reduced (4, 2));
}

//==============================================================================
void ZScorchAudioProcessorEditor::timerCallback()
{
    auto smooth = [] (float& display, float raw) {
        display += (raw - display) * 0.3f;
    };

    smooth (displayInDb,  audioProcessor.getInputLevelDb());
    smooth (displayOutDb, audioProcessor.getOutputLevelDb());
    smooth (displayGR,    audioProcessor.getGainReductionDb());

    inMeterLabel.setText  ("In:  " + juce::String (displayInDb, 1) + " dB",  juce::dontSendNotification);
    outMeterLabel.setText ("Out: " + juce::String (displayOutDb, 1) + " dB", juce::dontSendNotification);
    grMeterLabel.setText  ("GR: "  + juce::String (displayGR, 1) + " dB",    juce::dontSendNotification);
}