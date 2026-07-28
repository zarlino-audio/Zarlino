#include "PluginEditor.h"
#include "PluginProcessor.h"

//==============================================================================
//  ZtameLookAndFeel — premium scientific-instrument aesthetic
//==============================================================================

ZtameLookAndFeel::ZtameLookAndFeel()
{

    setColour (juce::Slider::rotarySliderFillColourId,    colAccentCyan());
    setColour (juce::Slider::rotarySliderOutlineColourId, colBgRaised());
    setColour (juce::Slider::thumbColourId,               colAccentCyan());
    setColour (juce::Slider::trackColourId,               juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxTextColourId,         colDim());
    setColour (juce::Slider::textBoxOutlineColourId,      colBgBase().withAlpha (0.3f));
    setColour (juce::Slider::textBoxBackgroundColourId,   colBgRaised());

    setColour (juce::TextButton::textColourOffId, colDim());
    setColour (juce::TextButton::textColourOnId,  colText());
    setColour (juce::TextButton::buttonColourId,  colBgRaised());
    setColour (juce::TextButton::buttonOnColourId, colAccentCyan().withAlpha (0.25f));
    setColour (juce::ComboBox::outlineColourId,   colBgRaised().brighter (0.15f));

    setColour (juce::Label::textColourId, colText());

    setColour (juce::ResizableWindow::backgroundColourId, colBgBase());
}

// ---------------------------------------------------------------------------
//  Custom rotary slider — metallic rim + LED arc + dark centre
// ---------------------------------------------------------------------------
void ZtameLookAndFeel::drawRotarySlider (juce::Graphics& g,
    int x, int y, int w, int h,
    float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
    juce::Slider& /*slider*/)
{
    const auto radius = juce::jmin (static_cast<float> (w), static_cast<float> (h)) * 0.5f - 4.0f;
    const auto centreX = static_cast<float> (x) + static_cast<float> (w) * 0.5f;
    const auto centreY = static_cast<float> (y) + static_cast<float> (h) * 0.5f;
    const auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // outer metallic rim
    g.setColour (colBgRaised().brighter (0.25f));
    g.fillEllipse (centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f);

    // inner dark centre
    const auto innerR = radius * 0.78f;
    auto innerGrad = juce::ColourGradient (colBgPanel().brighter (0.08f),
        centreX - innerR, centreY - innerR,
        colBgBase(), centreX + innerR, centreY + innerR, true);
    g.setGradientFill (innerGrad);
    g.fillEllipse (centreX - innerR, centreY - innerR, innerR * 2.0f, innerR * 2.0f);

    // subtle ring line
    g.setColour (colAccentCyan().withAlpha (0.12f));
    g.drawEllipse (centreX - innerR, centreY - innerR, innerR * 2.0f, innerR * 2.0f, 1.0f);

    // LED arc (cyan glow)
    const auto arcR = radius * 0.90f;
    const auto arcThickness = 3.0f;
    const auto arcAngle = rotaryEndAngle - rotaryStartAngle;

    auto arcPath = juce::Path();
    arcPath.addCentredArc (centreX, centreY, arcR, arcR, 0.0f,
                           rotaryStartAngle, toAngle, true);

    juce::PathStrokeType arcStroke (arcThickness, juce::PathStrokeType::curved);
    g.setColour (colAccentCyan().withAlpha (0.85f));
    g.strokePath (arcPath, arcStroke);

    // glow layer (wider, more transparent)
    juce::PathStrokeType glowStroke (arcThickness + 4.0f, juce::PathStrokeType::curved);
    g.setColour (colAccentCyan().withAlpha (0.15f));
    g.strokePath (arcPath, glowStroke);

    // remaining arc (dim)
    auto remainingPath = juce::Path();
    remainingPath.addCentredArc (centreX, centreY, arcR, arcR, 0.0f,
                                 toAngle, rotaryStartAngle + arcAngle, true);
    g.setColour (colBgRaised().brighter (0.35f));
    g.strokePath (remainingPath, juce::PathStrokeType (1.5f));

    // tick marks
    constexpr int numTicks = 31;
    const auto tickInner = radius * 0.82f;
    const auto tickOuter = radius * 0.89f;
    for (int i = 0; i < numTicks; ++i)
    {
        const auto a = rotaryStartAngle + (static_cast<float> (i) / static_cast<float> (numTicks - 1)) * arcAngle;
        const auto cosA = std::cos (a);
        const auto sinA = std::sin (a);
        g.setColour (colBgRaised().brighter (0.5f));
        g.drawLine (centreX + cosA * tickInner, centreY + sinA * tickInner,
                    centreX + cosA * tickOuter, centreY + sinA * tickOuter, 0.8f);
    }

    // centre indicator dot
    const auto dotR = 4.0f;
    g.setColour (colAccentCyan());
    g.fillEllipse (centreX - dotR, centreY - dotR, dotR * 2.0f, dotR * 2.0f);
}

void ZtameLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& btn,
    const juce::Colour& /*bg*/, bool over, bool down)
{
    const auto area = btn.getLocalBounds().toFloat().reduced (1.0f);
    const auto isOn = btn.getToggleState();

    juce::Colour fill;
    if (isOn && btn.getName() == "BYPASS")
        fill = colAccentCyan().withAlpha (over ? 0.35f : 0.22f);
    else if (isOn)
        fill = colAccentBlue().withAlpha (over ? 0.30f : 0.18f);
    else
        fill = colBgRaised().withAlpha (over ? 0.9f : 0.7f);

    g.setColour (fill);
    g.fillRoundedRectangle (area, 6.0f);

    // border
    juce::Colour border = isOn ? colAccentCyan().withAlpha (0.5f)
                               : colBgRaised().brighter (0.2f).withAlpha (0.4f);
    if (down) border = colAccentCyan().withAlpha (0.7f);
    g.setColour (border);
    g.drawRoundedRectangle (area, 6.0f, 1.0f);

    // glow when on
    if (isOn)
    {
        g.setColour (colAccentCyan().withAlpha (0.08f));
        g.drawRoundedRectangle (area.expanded (2.0f), 8.0f, 3.0f);
    }
}

juce::Font ZtameLookAndFeel::getLabelFont (juce::Label&)
{
    // System default sans-serif at 12 pt
    return juce::Font (juce::FontOptions().withHeight (12.0f));
}

//==============================================================================
//  ResonanceAnalyzer — live FFT magnitude display
//==============================================================================

ResonanceAnalyzer::ResonanceAnalyzer()
{
    binCount = 512; // default until first push
    smoothed.resize (static_cast<size_t> (binCount), 0.0f);
    display.resize (static_cast<size_t> (binCount), 0.0f);
}

void ResonanceAnalyzer::resized()
{
    // no child components
}

void ResonanceAnalyzer::pushMagnitude (const float* mag, int bins, double sampleRate)
{
    if (bins != binCount)
    {
        binCount = bins;
        smoothed.assign (static_cast<size_t> (bins), 0.0f);
        display.assign (static_cast<size_t> (bins), 0.0f);
    }
    sampleRateHz = sampleRate;

    const auto size = static_cast<size_t> (bins);
    for (size_t i = 0; i < size; ++i)
    {
        // linear magnitude → dB, clamped
        const auto rawDb = juce::Decibels::gainToDecibels (mag[i], -120.0f);
        display[i] = rawDb;
    }

    // smooth into display
    constexpr float alpha = 0.35f;
    for (size_t i = 0; i < size; ++i)
        smoothed[i] += (display[i] - smoothed[i]) * alpha;

    repaint();
}

void ResonanceAnalyzer::drawFrequencyGrid (juce::Graphics& g,
                                           const juce::Rectangle<int>& area)
{
    const float freqs[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    g.setFont (juce::FontOptions (10.0f));
    g.setColour (ZtameLookAndFeel::colDim().withAlpha (0.6f));

    const auto nyquist = static_cast<float> (sampleRateHz * 0.5);
    const auto left   = static_cast<float> (area.getX());
    const auto width  = static_cast<float> (area.getWidth());
    const auto bottom = static_cast<float> (area.getBottom());
    const auto top    = static_cast<float> (area.getY());

    for (float f : freqs)
    {
        const auto normX = std::log (f / 20.0f) / std::log (nyquist / 20.0f);
        const auto x = left + normX * width;
        if (x < left || x > left + width) continue;

        g.setColour (ZtameLookAndFeel::colDim().withAlpha (0.2f));
        g.drawLine (x, top, x, bottom, 0.5f);

        // label
        juce::String label;
        if (f >= 1000.0f)
            label = juce::String (static_cast<int> (f / 1000.0f)) + "k";
        else
            label = juce::String (static_cast<int> (f));

        g.setColour (ZtameLookAndFeel::colDim().withAlpha (0.7f));
        g.drawText (label,
                    juce::Rectangle<int> (static_cast<int> (x - 14), area.getBottom() - 14, 28, 14),
                    juce::Justification::centred, false);
    }
}

void ResonanceAnalyzer::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto area   = bounds.reduced (8.0f, 6.0f);

    // background
    g.setColour (ZtameLookAndFeel::colBgBase());
    g.fillRoundedRectangle (bounds, 8.0f);

    g.setColour (ZtameLookAndFeel::colBgRaised().brighter (0.08f));
    g.fillRoundedRectangle (area, 6.0f);

    // frequency grid
    drawFrequencyGrid (g, area.toNearestInt());

    // draw magnitude spectrum
    if (smoothed.empty() || binCount < 4)
        return;

    const auto nyquist = static_cast<float> (sampleRateHz * 0.5);
    const auto left   = area.getX();
    const auto width  = area.getWidth();
    const auto bottom = area.getBottom() - 1.0f;
    const auto height = area.getHeight() - 20.0f; // leave room for freq labels

    juce::Path curve;
    bool started = false;

    for (int i = 0; i < binCount; ++i)
    {
        const auto freq = static_cast<float> (i) * static_cast<float> (sampleRateHz)
                          / static_cast<float> (binCount * 2);
        if (freq < 18.0f) continue;

        const auto normX = std::log (freq / 20.0f) / std::log (nyquist / 20.0f);
        const auto x = left + normX * width;

        const auto dbVal = juce::jlimit (-90.0f, 0.0f, smoothed[static_cast<size_t> (i)]);
        const auto normY = (dbVal + 90.0f) / 90.0f;
        const auto y = bottom - normY * height;

        if (! started)
        {
            curve.startNewSubPath (x, y);
            started = true;
        }
        else
        {
            curve.lineTo (x, y);
        }
    }

    // fill under curve
    if (started)
    {
        auto fill = juce::Path (curve);
        fill.lineTo (left + width, bottom);
        fill.lineTo (left, bottom);
        fill.closeSubPath();

        auto fillGrad = juce::ColourGradient (
            ZtameLookAndFeel::colAccentCyan().withAlpha (0.18f),
            left, bottom - height * 0.5f,
            ZtameLookAndFeel::colAccentCyan().withAlpha (0.02f),
            left, bottom, false);
        g.setGradientFill (fillGrad);
        g.fillPath (fill);

        // glow line
        g.setColour (ZtameLookAndFeel::colAccentCyan().withAlpha (0.35f));
        g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved));

        // bright core
        g.setColour (ZtameLookAndFeel::colAccentCyan().withAlpha (0.7f));
        g.strokePath (curve, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved));
    }

    // title
    g.setFont (juce::FontOptions (9.0f));
    g.setColour (ZtameLookAndFeel::colDim().withAlpha (0.5f));
    g.drawText ("RESONANCE ANALYZER",
                static_cast<int> (area.getX() + 8),
                static_cast<int> (area.getY() + 4), 140, 14,
                juce::Justification::left, false);
}

//==============================================================================
//  GainReductionDisplay — animated GR bar + numeric
//==============================================================================

GainReductionDisplay::GainReductionDisplay()
{
    setOpaque (false);
}

void GainReductionDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto r = 8.0f;

    // background
    g.setColour (ZtameLookAndFeel::colBgRaised());
    g.fillRoundedRectangle (bounds, r);

    // title
    g.setFont (juce::FontOptions (9.0f));
    g.setColour (ZtameLookAndFeel::colDim());
    g.drawText ("GAIN REDUCTION", bounds.reduced (10.0f, 4.0f)
                    .removeFromTop (16.0f).toNearestInt(),
                juce::Justification::left, false);

    // bar area
    const auto barArea = bounds.reduced (10.0f, 6.0f)
                              .withTrimmedTop (20.0f);
    const auto barWidth = barArea.getWidth() * 0.5f;
    const auto barX = barArea.getCentreX() - barWidth * 0.5f;

    // track background
    g.setColour (ZtameLookAndFeel::colBgBase());
    g.fillRoundedRectangle (barX, barArea.getY(), barWidth, barArea.getHeight(), 3.0f);

    // level fill (from left = 0 dB = no fill, to right = -60 dB)
    if (currentDb < -0.1f)
    {
        const auto norm = juce::jlimit (0.0f, 1.0f, -currentDb / 60.0f);
        const auto fillW = barWidth * norm;

        auto grad = juce::ColourGradient (
            ZtameLookAndFeel::colAccentCyan().withAlpha (0.9f),
            barX, 0.0f,
            ZtameLookAndFeel::colAccentBlue().withAlpha (0.6f),
            barX + barWidth, 0.0f, false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (barX, barArea.getY(), fillW, barArea.getHeight(), 3.0f);

        // peak hold
        const auto peakX = barX + barWidth * juce::jlimit (0.0f, 1.0f, -peakDb / 60.0f);
        g.setColour (ZtameLookAndFeel::colText().withAlpha (0.8f));
        g.fillRect (peakX - 1.0f, barArea.getY(), 2.0f, barArea.getHeight());
    }

    // numeric dB
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.setColour (ZtameLookAndFeel::colAccentCyan());
    const auto textArea = barArea.withX (barX + barWidth + 12.0f);
    g.drawText (juce::String (currentDb, 1) + " dB", textArea.toNearestInt(),
                juce::Justification::centredLeft, false);
}

//==============================================================================
//  MeterBar — horizontal LED-style meter
//==============================================================================

MeterBar::MeterBar()
{
    setOpaque (false);
}

void MeterBar::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (ZtameLookAndFeel::colBgRaised());
    g.fillRoundedRectangle (bounds, 4.0f);

    const auto norm = juce::jlimit (0.0f, 1.0f, (currentDb + 60.0f) / 60.0f);
    const auto fillW = bounds.getWidth() * norm;
    if (fillW > 1.0f)
    {
        auto grad = juce::ColourGradient (
            ZtameLookAndFeel::colAccentCyan().withAlpha (0.9f),
            0.0f, 0.0f,
            ZtameLookAndFeel::colAccentBlue().withAlpha (0.7f),
            fillW, 0.0f, false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bounds.withWidth (fillW), 4.0f);
    }
}

//==============================================================================
//  ReductionHistory — scrolling dB-over-time graph
//==============================================================================

ReductionHistory::ReductionHistory()
{
    history.resize (static_cast<size_t> (historyLength), 0.0f);
    setOpaque (false);
}

void ReductionHistory::resized() {}

void ReductionHistory::pushSample (float db)
{
    history[static_cast<size_t> (writeIndex)] = juce::jlimit (-60.0f, 0.0f, db);
    writeIndex = (writeIndex + 1) % historyLength;
    repaint();
}

void ReductionHistory::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (ZtameLookAndFeel::colBgRaised());
    g.fillRoundedRectangle (bounds, 4.0f);

    if (history.empty()) return;

    const auto width  = bounds.getWidth();
    const auto height = bounds.getHeight();
    const auto bottom = bounds.getBottom() - 1.0f;

    juce::Path p;
    p.startNewSubPath (0.0f, bottom);

    for (int i = 0; i < historyLength; ++i)
    {
        const auto idx = (writeIndex + i) % historyLength;
        const auto db  = history[static_cast<size_t> (idx)];
        const auto x   = (static_cast<float> (i) / static_cast<float> (historyLength - 1)) * width;
        const auto y   = bottom + db / 60.0f * height;
        p.lineTo (x, y);
    }

    p.lineTo (width, bottom);
    p.closeSubPath();

    g.setColour (ZtameLookAndFeel::colAccentCyan().withAlpha (0.25f));
    g.fillPath (p);

    // outline
    juce::Path outline;
    outline.startNewSubPath (0.0f, bottom);
    for (int i = 0; i < historyLength; ++i)
    {
        const auto idx = (writeIndex + i) % historyLength;
        const auto db  = history[static_cast<size_t> (idx)];
        const auto x   = (static_cast<float> (i) / static_cast<float> (historyLength - 1)) * width;
        const auto y   = bottom + db / 60.0f * height;
        outline.lineTo (x, y);
    }
    g.setColour (ZtameLookAndFeel::colAccentCyan().withAlpha (0.6f));
    g.strokePath (outline, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved));
}

//==============================================================================
//  ZTameAudioProcessorEditor — main editor
//==============================================================================

ZTameAudioProcessorEditor::ZTameAudioProcessorEditor (ZTameAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Apply custom L&F to the editor and all children
    setLookAndFeel (&ztameLNF);

    // -- helper: set up a rotary knob -------------------------------------------------
    const auto setupKnob = [this] (LabelledKnob& lk,
                                   const juce::String& name,
                                   const juce::String& paramId,
                                   const juce::String& suffix)
    {
        auto& sld = lk.slider;
        sld.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        sld.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        sld.setTextValueSuffix (suffix);
        sld.setVelocityModeParameters (1.0, 1, 0.0, false);
        addAndMakeVisible (sld);

        auto& lbl = lk.label;
        lbl.setText (name, juce::dontSendNotification);
        lbl.setJustificationType (juce::Justification::centred);
        lbl.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        lbl.setColour (juce::Label::textColourId, ZtameLookAndFeel::colDim());
        lbl.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (lbl);

        auto& val = lk.valueLabel;
        val.setJustificationType (juce::Justification::centred);
        val.setFont (juce::Font (juce::FontOptions (11.0f)));
        val.setColour (juce::Label::textColourId, ZtameLookAndFeel::colText());
        val.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (val);

        lk.attachment = std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment> (
            audioProcessor.getValueTreeState(), paramId, sld);

        // Keep value label in sync
        sld.onValueChange = [&val, &sld, suffix]
        {
            val.setText (juce::String (sld.getValue(), 1) + suffix,
                         juce::dontSendNotification);
        };
    };

    // -- helper: set up a toggle button ----------------------------------------------
    const auto setupBtn = [this] (LabelledButton& lb,
                                  const juce::String& name,
                                  const juce::String& paramId)
    {
        auto& btn = lb.button;
        btn.setName (name); // used by L&F for styling hints
        btn.setButtonText (name);
        btn.setClickingTogglesState (true);
        addAndMakeVisible (btn);

        auto& lbl = lb.label;
        lbl.setText (name, juce::dontSendNotification);
        lbl.setJustificationType (juce::Justification::centred);
        lbl.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        lbl.setColour (juce::Label::textColourId, ZtameLookAndFeel::colDim());
        lbl.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (lbl);

        lb.attachment = std::make_unique<
            juce::AudioProcessorValueTreeState::ButtonAttachment> (
            audioProcessor.getValueTreeState(), paramId, btn);
    };

    // -- header labels ---------------------------------------------------------------
    titleLabel.setText ("ZTame", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, ZtameLookAndFeel::colText());
    titleLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (titleLabel);

    taglineLabel.setText ("High-Resolution Resonance Suppressor",
                          juce::dontSendNotification);
    taglineLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    taglineLabel.setColour (juce::Label::textColourId, ZtameLookAndFeel::colDim());
    taglineLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (taglineLabel);

    // -- resonance analyzer ----------------------------------------------------------
    addAndMakeVisible (analyzer);

    // -- knobs -----------------------------------------------------------------------
    setupKnob (sensitivityKnob, "SENSITIVITY",  "sensitivity",    "%");
    setupKnob (rangeKnob,       "RANGE",        "range",          " dB");
    setupKnob (selectivityKnob, "SELECTIVITY",  "selectivity",    "%");
    setupKnob (crossoverKnob,   "CROSSOVER",    "crossoverFreq",  " Hz");
    setupKnob (attackKnob,      "ATTACK",       "attack",         " ms");
    setupKnob (releaseKnob,     "RELEASE",      "release",        " ms");
    setupKnob (dryWetKnob,      "DRY / WET",    "dryWet",         "%");

    // -- buttons ---------------------------------------------------------------------
    setupBtn (listenButton, "LISTEN", "listenMode");
    setupBtn (bypassButton, "BYPASS", "bypass");

    // -- meters ----------------------------------------------------------------------
    addAndMakeVisible (grDisplay);

    inMeterLabel.setText ("IN", juce::dontSendNotification);
    inMeterLabel.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
    inMeterLabel.setColour (juce::Label::textColourId, ZtameLookAndFeel::colDim());
    inMeterLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (inMeterLabel);

    outMeterLabel.setText ("OUT", juce::dontSendNotification);
    outMeterLabel.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
    outMeterLabel.setColour (juce::Label::textColourId, ZtameLookAndFeel::colDim());
    outMeterLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (outMeterLabel);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);
    addAndMakeVisible (historyGraph);

    // Initial value labels
    sensitivityKnob.slider.setValue (50.0f, juce::dontSendNotification);
    rangeKnob.slider.setValue (12.0f, juce::dontSendNotification);
    selectivityKnob.slider.setValue (50.0f, juce::dontSendNotification);
    crossoverKnob.slider.setValue (250.0f, juce::dontSendNotification);
    attackKnob.slider.setValue (3.0f, juce::dontSendNotification);
    releaseKnob.slider.setValue (100.0f, juce::dontSendNotification);
    dryWetKnob.slider.setValue (100.0f, juce::dontSendNotification);

    setSize (880, 540);
    startTimerHz (35);
}

ZTameAudioProcessorEditor::~ZTameAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void ZTameAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // solid background
    g.setColour (ZtameLookAndFeel::colBgBase());
    g.fillRoundedRectangle (bounds, 18.0f);

    // subtle inner border
    g.setColour (ZtameLookAndFeel::colBgRaised().brighter (0.06f));
    g.drawRoundedRectangle (bounds.reduced (1.5f), 18.0f, 1.0f);

    // branding at top-right
    g.setFont (juce::FontOptions (10.0f));
    g.setColour (ZtameLookAndFeel::colDim().withAlpha (0.5f));
    g.drawText ("Zarlino Audio",
                getWidth() - 140, 10, 128, 16,
                juce::Justification::right, false);
}

//==============================================================================
void ZTameAudioProcessorEditor::resized()
{
    const auto w = getWidth();
    const auto h = getHeight();

    // ---- header ----
    titleLabel.setBounds (0, 10, w, 32);
    taglineLabel.setBounds (0, 38, w, 16);

    // ---- analyzer ----
    const auto analyzerTop = 62;
    const auto analyzerBottom = analyzerTop + 130;
    analyzer.setBounds (20, analyzerTop, w - 40, analyzerBottom - analyzerTop);

    // ---- knobs + GR meter area ----
    auto mainArea = juce::Rectangle<int> (14, analyzerBottom + 10, w - 28, h - analyzerBottom - 20);

    // Top knob row: SENS | RANGE | SEL | XOVER
    auto topRow = mainArea.removeFromTop (110);
    const auto topSlotW = topRow.getWidth() / 4;

    const auto setupKnobLayout = [] (LabelledKnob& lk, juce::Rectangle<int> slot)
    {
        lk.label.setBounds (slot.removeFromTop (15));
        auto knobArea = slot;
        lk.slider.setBounds (knobArea);
        lk.valueLabel.setBounds (knobArea.getX(), knobArea.getBottom() - 16,
                                 knobArea.getWidth(), 14);
    };

    setupKnobLayout (sensitivityKnob,  topRow.removeFromLeft (topSlotW));
    setupKnobLayout (rangeKnob,        topRow.removeFromLeft (topSlotW));
    setupKnobLayout (selectivityKnob,  topRow.removeFromLeft (topSlotW));
    setupKnobLayout (crossoverKnob,    topRow);

    // Bottom knob row: ATTACK | RELEASE | DRY/WET
    auto botRow = mainArea.removeFromTop (100);
    const auto botSlotW = botRow.getWidth() / 3;

    setupKnobLayout (attackKnob,  botRow.removeFromLeft (botSlotW));
    setupKnobLayout (releaseKnob, botRow.removeFromLeft (botSlotW));
    setupKnobLayout (dryWetKnob,  botRow);

    // ---- GR display ----
    auto grRow = mainArea.removeFromTop (44);
    grDisplay.setBounds (grRow.reduced (4, 2));

    // ---- buttons + meters ----
    auto bottomRow = mainArea.reduced (4, 0);

    // LISTEN / BYPASS buttons
    const auto btnW = 88;
    const auto btnH = 28;
    auto btnArea = bottomRow.removeFromLeft (btnW * 2 + 12);
    listenButton.button.setBounds (btnArea.removeFromLeft (btnW).withHeight (btnH));
    btnArea.removeFromLeft (12);
    bypassButton.button.setBounds (btnArea.removeFromLeft (btnW).withHeight (btnH));

    // I/O meters
    const auto meterW = 80;
    const auto meterH = 10;
    const auto meterLabelH = 10;

    inMeterLabel.setBounds (bottomRow.getX(),
                            bottomRow.getY(), meterW, meterLabelH);
    inputMeter.setBounds (bottomRow.getX(),
                          bottomRow.getY() + meterLabelH + 2, meterW, meterH);
    bottomRow.removeFromLeft (meterW + 4);

    outMeterLabel.setBounds (bottomRow.getX(),
                             bottomRow.getY(), meterW, meterLabelH);
    outputMeter.setBounds (bottomRow.getX(),
                           bottomRow.getY() + meterLabelH + 2, meterW, meterH);
    bottomRow.removeFromLeft (meterW + 4);

    // reduction history
    historyGraph.setBounds (bottomRow.reduced (2, 0));
}

//==============================================================================
void ZTameAudioProcessorEditor::timerCallback()
{
    // -- GR meter --
    const auto rawGR = audioProcessor.getGainReductionDb();
    grDisplay.setLevel (rawGR);

    // -- resonance analyzer --
    if (const auto* mag = audioProcessor.getMagnitudeData())
    {
        const auto bins = audioProcessor.getNumBins();
        analyzer.pushMagnitude (mag, bins, audioProcessor.getCurrentSampleRate());
    }

    // -- I/O meters --
    inputMeter.setLevel (audioProcessor.getInputLevelDb());
    outputMeter.setLevel (audioProcessor.getOutputLevelDb());

    // -- reduction history --
    historyGraph.pushSample (rawGR);
}
