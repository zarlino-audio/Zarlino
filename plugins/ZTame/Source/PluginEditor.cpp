#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

#ifndef ZTAME_VERSION
 #define ZTAME_VERSION "1.1.0"
#endif

//==============================================================================
namespace ztame
{
    juce::Colour backgroundTop()    { return juce::Colour (0xff0a1120); }
    juce::Colour backgroundBottom() { return juce::Colour (0xff160f2c); }
    juce::Colour panelFill()        { return juce::Colour (0xcc101a2c); }
    juce::Colour panelStroke()      { return juce::Colour (0x33ffffff); }
    juce::Colour accent()           { return juce::Colour (0xff53b7ff); }
    juce::Colour accentSoft()       { return juce::Colour (0xff9fdcff); }
    juce::Colour violet()           { return juce::Colour (0xff8e63ff); }
    juce::Colour amber()            { return juce::Colour (0xffffb347); }
    juce::Colour textBright()       { return juce::Colour (0xffeaf3ff); }
    juce::Colour textDim()          { return juce::Colour (0xff8fa2c4); }
    juce::Colour danger()           { return juce::Colour (0xffff6b6b); }
}

//==============================================================================
ZTameLookAndFeel::ZTameLookAndFeel()
{
    setColour (juce::Slider::rotarySliderFillColourId,    ztame::accent());
    setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff1b2740));
    setColour (juce::Slider::thumbColourId,               ztame::accentSoft());
    setColour (juce::Label::textColourId,                 ztame::textBright());
    setColour (juce::ResizableWindow::backgroundColourId, ztame::backgroundBottom());
}

void ZTameLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float rotaryStartAngle,
                                         float rotaryEndAngle, juce::Slider&)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    const auto lineW  = juce::jmax (2.5f, radius * 0.15f);
    const auto arcR   = radius - lineW * 0.9f;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (juce::Colour (0xff1b2740));
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    if (sliderPos > 0.0005f)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                           rotaryStartAngle, angle, true);

        g.setGradientFill (juce::ColourGradient (ztame::accentSoft(), centre.x - arcR, centre.y + arcR,
                                                 ztame::violet(), centre.x + arcR, centre.y - arcR, false));
        g.strokePath (arc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    const auto bodyR = juce::jmax (2.0f, radius - lineW * 2.3f);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b3a56), centre.x, centre.y - bodyR,
                                             juce::Colour (0xff121a2a), centre.x, centre.y + bodyR,
                                             false));
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

    g.setColour (juce::Colour (0x22ffffff));
    g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.7f, -bodyR + 3.0f, 3.4f, bodyR * 0.62f, 1.7f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));

    g.setColour (ztame::accentSoft());
    g.fillPath (pointer);
}

//==============================================================================
// ZTameSpectrumDisplay
//==============================================================================
ZTameSpectrumDisplay::ZTameSpectrumDisplay (ZTameAudioProcessor& p)
    : processor (p)
{
    setWantsKeyboardFocus (false);
    setTooltip ("Live view: blue = input, cyan = output, amber = what ZTame is removing.\n"
                "Drag the shaded band to move the focus range, drag its edges to resize, "
                "double-click to reset it to full range.");
}

juce::Rectangle<float> ZTameSpectrumDisplay::plotArea() const
{
    return getLocalBounds().toFloat().reduced (10.0f, 8.0f).withTrimmedBottom (20.0f);
}

float ZTameSpectrumDisplay::freqToX (float freqHz) const
{
    const auto plot = plotArea();
    return plot.getX() + ZTameAudioProcessor::freqToDisplayNorm (freqHz) * plot.getWidth();
}

float ZTameSpectrumDisplay::xToFreq (float x) const
{
    const auto plot = plotArea();
    const float norm = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / juce::jmax (1.0f, plot.getWidth()));
    return ZTameAudioProcessor::displayNormToFreq (norm);
}

ZTameSpectrumDisplay::DragMode ZTameSpectrumDisplay::hitTest (float x) const
{
    const auto plot = plotArea();
    const float xLo = freqToX (bandLowHz);
    const float xHi = freqToX (bandHighHz);

    if (std::abs (x - xLo) <= 10.0f) return DragMode::lowEdge;
    if (std::abs (x - xHi) <= 10.0f) return DragMode::highEdge;
    if (x > xLo && x < xHi && x >= plot.getX() - 4.0f && x <= plot.getRight() + 4.0f)
        return DragMode::body;

    return DragMode::none;
}

void ZTameSpectrumDisplay::setSnapshot (const ZTameAudioProcessor::UiSnapshot& s, bool valid)
{
    if (valid)
    {
        inputDb     = s.inputDb;
        outputDb    = s.outputDb;
        removedDb   = s.reductionDb;
        sidechainDb = s.sidechainDb;
        peakFreqHz  = s.peakFreqHz;
        peakReductionDb = s.peakReductionDb;
        numPeaks    = juce::jlimit (0, maxPeaks, s.numPeaks);
        displayThresholdDb = s.thresholdDb;
        listening      = s.listenMode;
        sidechainLive  = s.sidechainLive;
        bypassed       = s.bypassed;
    }

    if (auto* lo = processor.getValueTreeState().getRawParameterValue (ZTameAudioProcessor::paramFocusLow))
        bandLowHz = lo->load();
    if (auto* hi = processor.getValueTreeState().getRawParameterValue (ZTameAudioProcessor::paramFocusHigh))
        bandHighHz = hi->load();
}

void ZTameSpectrumDisplay::beginGestures()
{
    if (gesturesActive)
        return;

    auto& vts = processor.getValueTreeState();

    for (auto* id : { ZTameAudioProcessor::paramFocusLow, ZTameAudioProcessor::paramFocusHigh })
        if (auto* p = vts.getParameter (id))
            p->beginChangeGesture();

    gesturesActive = true;
}

void ZTameSpectrumDisplay::endGestures()
{
    if (! gesturesActive)
        return;

    auto& vts = processor.getValueTreeState();

    for (auto* id : { ZTameAudioProcessor::paramFocusLow, ZTameAudioProcessor::paramFocusHigh })
        if (auto* p = vts.getParameter (id))
            p->endChangeGesture();

    gesturesActive = false;
}

void ZTameSpectrumDisplay::writeBand (float lowHz, float highHz, bool notifyHost)
{
    lowHz  = juce::jlimit (20.0f, 20000.0f, lowHz);
    highHz = juce::jlimit (20.0f, 20000.0f, highHz);

    const float minSpan = 1.25f;   // quarter octave

    if (highHz < lowHz * minSpan)
    {
        if (dragMode == DragMode::lowEdge)
            lowHz = highHz / minSpan;
        else
            highHz = lowHz * minSpan;
    }

    if (lowHz > 20000.0f / minSpan)
        lowHz = 20000.0f / minSpan;

    bandLowHz  = juce::jlimit (20.0f, 20000.0f, lowHz);
    bandHighHz = juce::jlimit (bandLowHz * minSpan, 20000.0f, highHz);

    if (notifyHost)
    {
        auto& vts = processor.getValueTreeState();

        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (
                vts.getParameter (ZTameAudioProcessor::paramFocusLow)))
            p->setValueNotifyingHost (p->convertTo0to1 (bandLowHz));

        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (
                vts.getParameter (ZTameAudioProcessor::paramFocusHigh)))
            p->setValueNotifyingHost (p->convertTo0to1 (bandHighHz));
    }

    repaint();
}

void ZTameSpectrumDisplay::mouseMove (const juce::MouseEvent& e)
{
    const auto mode = hitTest (e.position.x);

    if (mode != hoverMode)
    {
        hoverMode = mode;

        switch (mode)
        {
            case DragMode::lowEdge:
            case DragMode::highEdge: setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
            case DragMode::body:     setMouseCursor (juce::MouseCursor::DraggingHandCursor);    break;
            default:                 setMouseCursor (juce::MouseCursor::NormalCursor);          break;
        }

        repaint();
    }
}

void ZTameSpectrumDisplay::mouseExit (const juce::MouseEvent&)
{
    if (dragMode == DragMode::none && hoverMode != DragMode::none)
    {
        hoverMode = DragMode::none;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ZTameSpectrumDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragMode = hitTest (e.position.x);

    if (dragMode == DragMode::none)
        return;

    dragStartX = e.position.x;
    dragStartLow = bandLowHz;
    dragStartHigh = bandHighHz;
    beginGestures();
    repaint();
}

void ZTameSpectrumDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMode == DragMode::none)
        return;

    const float freq = xToFreq (e.position.x);

    if (dragMode == DragMode::lowEdge)
    {
        writeBand (freq, bandHighHz, true);
    }
    else if (dragMode == DragMode::highEdge)
    {
        writeBand (bandLowHz, freq, true);
    }
    else
    {
        // Body drag: shift the whole band in log-frequency space, width preserved.
        const float spanN = ZTameAudioProcessor::freqToDisplayNorm (dragStartHigh)
                          - ZTameAudioProcessor::freqToDisplayNorm (dragStartLow);
        const float deltaN = ZTameAudioProcessor::freqToDisplayNorm (xToFreq (e.position.x))
                           - ZTameAudioProcessor::freqToDisplayNorm (xToFreq (dragStartX));

        const float startN = ZTameAudioProcessor::freqToDisplayNorm (dragStartLow);
        const float lowN = juce::jlimit (0.0f, 1.0f - spanN, startN + deltaN);

        writeBand (ZTameAudioProcessor::displayNormToFreq (lowN),
                   ZTameAudioProcessor::displayNormToFreq (lowN + spanN), true);
    }
}

void ZTameSpectrumDisplay::mouseUp (const juce::MouseEvent&)
{
    endGestures();
    dragMode = DragMode::none;
    repaint();
}

void ZTameSpectrumDisplay::mouseDoubleClick (const juce::MouseEvent&)
{
    dragMode = DragMode::none;
    beginGestures();
    writeBand (20.0f, 20000.0f, true);
    endGestures();
}

void ZTameSpectrumDisplay::paint (juce::Graphics& g)
{
    const auto plot = plotArea();

    constexpr float dbTop = 6.0f;
    constexpr float dbBottom = -96.0f;

    auto dbToY = [&] (float db)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db - dbBottom) / (dbTop - dbBottom));
        return plot.getBottom() - t * plot.getHeight();
    };

    // --- plot background ---------------------------------------------------
    g.setColour (juce::Colour (0xcc0b1424));
    g.fillRoundedRectangle (plot, 8.0f);

    // --- grid --------------------------------------------------------------
    g.setFont (juce::Font (juce::FontOptions (9.5f)));

    for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f, -72.0f, -84.0f })
    {
        const float y = dbToY (db);
        g.setColour (juce::Colour (db == 0.0f ? 0x2effffff : 0x14ffffff));
        g.drawHorizontalLine ((int) std::round (y), plot.getX(), plot.getRight());

        if (db <= -12.0f)
        {
            g.setColour (ztame::textDim().withAlpha (0.55f));
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() + 3.0f, y - 10.0f,
                                                                       40.0f, 10.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    struct GridLine { float hz; const char* label; };
    for (auto line : { GridLine { 50.0f, "50" }, GridLine { 100.0f, "100" },
                       GridLine { 200.0f, "200" }, GridLine { 500.0f, "500" },
                       GridLine { 1000.0f, "1k" }, GridLine { 2000.0f, "2k" },
                       GridLine { 5000.0f, "5k" }, GridLine { 10000.0f, "10k" } })
    {
        const float x = freqToX (line.hz);
        g.setColour (juce::Colour (0x12ffffff));
        g.drawVerticalLine ((int) std::round (x), plot.getY(), plot.getBottom());
        g.setColour (ztame::textDim().withAlpha (0.7f));
        g.drawText (line.label, juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 3.0f, 40.0f, 12.0f),
                    juce::Justification::centred, false);
    }

    g.setColour (ztame::textDim().withAlpha (0.7f));
    g.drawText ("20 Hz", juce::Rectangle<float> (plot.getX(), plot.getBottom() + 3.0f, 60.0f, 12.0f),
                juce::Justification::centredLeft, false);
    g.drawText ("20 kHz", juce::Rectangle<float> (plot.getRight() - 60.0f, plot.getBottom() + 3.0f, 60.0f, 12.0f),
                juce::Justification::centredRight, false);

    // --- focus band --------------------------------------------------------
    const float xLo = freqToX (bandLowHz);
    const float xHi = freqToX (bandHighHz);
    const bool active = hoverMode != DragMode::none || dragMode != DragMode::none;

    {
        juce::Rectangle<float> band (xLo, plot.getY(), juce::jmax (2.0f, xHi - xLo), plot.getHeight());

        g.setColour (ztame::accent().withAlpha (active ? 0.16f : 0.10f));
        g.fillRect (band);

        g.setColour (ztame::accent().withAlpha (active ? 0.85f : 0.5f));
        g.drawVerticalLine ((int) std::round (xLo), plot.getY(), plot.getBottom());
        g.drawVerticalLine ((int) std::round (xHi), plot.getY(), plot.getBottom());

        // grab handles
        const float handleH = 22.0f;
        const float handleY = plot.getCentreY() - handleH * 0.5f;

        g.setColour (ztame::accent().withAlpha (active ? 1.0f : 0.8f));
        g.fillRoundedRectangle (xLo - 3.0f, handleY, 6.0f, handleH, 3.0f);
        g.fillRoundedRectangle (xHi - 3.0f, handleY, 6.0f, handleH, 3.0f);
    }

    // --- curves ------------------------------------------------------------
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (plot.toNearestInt());

        const int n = numDisplayBins;

        auto xOfBin = [&] (int i)
        {
            return plot.getX() + ((float) i + 0.5f) / (float) n * plot.getWidth();
        };

        // input fill
        juce::Path inputFill;
        inputFill.startNewSubPath (plot.getX(), plot.getBottom());
        for (int i = 0; i < n; ++i)
            inputFill.lineTo (xOfBin (i), dbToY (inputDb[(size_t) i]));
        inputFill.lineTo (plot.getRight(), plot.getBottom());
        inputFill.closeSubPath();

        g.setGradientFill (juce::ColourGradient (juce::Colour (0x55284f80), 0.0f, plot.getY(),
                                                 juce::Colour (0x08284f80), 0.0f, plot.getBottom(), false));
        g.fillPath (inputFill);

        // removed-energy shading (between input and output) — amber
        juce::Path removedPath;
        removedPath.startNewSubPath (xOfBin (0), dbToY (inputDb[0]));
        for (int i = 1; i < n; ++i)
            removedPath.lineTo (xOfBin (i), dbToY (inputDb[(size_t) i]));
        for (int i = n - 1; i >= 0; --i)
            removedPath.lineTo (xOfBin (i), dbToY (outputDb[(size_t) i]));
        removedPath.closeSubPath();

        g.setColour (ztame::amber().withAlpha (bypassed ? 0.06f : 0.38f));
        g.fillPath (removedPath);

        // sidechain trace
        if (sidechainLive && ! bypassed)
        {
            juce::Path scPath;
            scPath.startNewSubPath (xOfBin (0), dbToY (sidechainDb[0]));
            for (int i = 1; i < n; ++i)
                scPath.lineTo (xOfBin (i), dbToY (sidechainDb[(size_t) i]));

            const float dashLengths[] = { 3.0f, 3.0f };
            juce::Path dashed;
            juce::PathStrokeType (1.1f).createDashedStroke (dashed, scPath, dashLengths, 2);

            g.setColour (ztame::violet().withAlpha (0.75f));
            g.fillPath (dashed);
        }

        // input line
        juce::Path inputLine;
        inputLine.startNewSubPath (xOfBin (0), dbToY (inputDb[0]));
        for (int i = 1; i < n; ++i)
            inputLine.lineTo (xOfBin (i), dbToY (inputDb[(size_t) i]));
        g.setColour (juce::Colour (0xff5f8dd6).withAlpha (0.9f));
        g.strokePath (inputLine, juce::PathStrokeType (1.2f));

        // output line
        juce::Path outputLine;
        outputLine.startNewSubPath (xOfBin (0), dbToY (outputDb[0]));
        for (int i = 1; i < n; ++i)
            outputLine.lineTo (xOfBin (i), dbToY (outputDb[(size_t) i]));
        g.setColour (bypassed ? ztame::textDim() : ztame::accentSoft());
        g.strokePath (outputLine, juce::PathStrokeType (1.6f));

        // detected resonance markers
        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        for (int p = 0; p < numPeaks; ++p)
        {
            const float f = peakFreqHz[(size_t) p];
            const float x = freqToX (f);

            const int bin = juce::jlimit (0, n - 1,
                                          (int) (ZTameAudioProcessor::freqToDisplayNorm (f) * (float) n));
            const float yIn = dbToY (inputDb[(size_t) bin]);

            g.setColour (ztame::amber());
            g.fillEllipse (x - 3.0f, yIn - 3.0f, 6.0f, 6.0f);

            g.setColour (ztame::amber().withAlpha (0.95f));
            g.drawText ("-" + juce::String (peakReductionDb[(size_t) p], 1),
                        juce::Rectangle<float> (x - 22.0f, yIn - 20.0f, 44.0f, 12.0f),
                        juce::Justification::centred, false);
        }
    }

    // --- overlays ----------------------------------------------------------
    if (bypassed)
    {
        g.setColour (juce::Colour (0x66000000));
        g.fillRoundedRectangle (plot, 8.0f);
        g.setColour (ztame::textBright().withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
        g.drawText ("BYPASSED", plot, juce::Justification::centred, false);
    }
    else if (listening)
    {
        g.setColour (ztame::amber().withAlpha (0.14f));
        g.fillRoundedRectangle (plot.withSizeKeepingCentre (150.0f, 26.0f), 6.0f);
        g.setColour (ztame::amber());
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText ("HEARING REMOVED ENERGY", plot.withSizeKeepingCentre (300.0f, 26.0f),
                    juce::Justification::centred, false);
    }

    // --- legend ------------------------------------------------------------
    g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
    g.setColour (juce::Colour (0xff5f8dd6));
    g.drawText ("IN", juce::Rectangle<float> (plot.getRight() - 152.0f, plot.getY() + 2.0f, 26.0f, 14.0f),
                juce::Justification::centredRight, false);
    g.setColour (ztame::accentSoft());
    g.drawText ("OUT", juce::Rectangle<float> (plot.getRight() - 118.0f, plot.getY() + 2.0f, 34.0f, 14.0f),
                juce::Justification::centredRight, false);
    g.setColour (ztame::amber());
    g.drawText ("REMOVED", juce::Rectangle<float> (plot.getRight() - 246.0f, plot.getY() + 2.0f, 122.0f, 14.0f),
                juce::Justification::centredRight, false);

    // focus band readout
    {
        auto fmt = [] (float hz)
        {
            return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz"
                                 : juce::String (hz, hz >= 100.0f ? 0 : 1) + " Hz";
        };

        const juce::String bandText = fmt (bandLowHz) + "  —  " + fmt (bandHighHz);
        const auto textArea = juce::Rectangle<float> (plot.getX() + 6.0f, plot.getBottom() - 20.0f,
                                                      juce::jmin (plot.getWidth() - 12.0f, 190.0f), 15.0f);

        g.setColour (juce::Colour (0xaa0b1424));
        g.fillRoundedRectangle (textArea, 4.0f);
        g.setColour (ztame::accent().withAlpha (0.9f));
        g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
        g.drawText ("FOCUS  " + bandText, textArea, juce::Justification::centred, false);

        g.setColour (ztame::textDim().withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (9.5f)));
        g.drawText ("trigger +" + juce::String (displayThresholdDb, 1) + " dB",
                    juce::Rectangle<float> (textArea.getRight() + 6.0f, textArea.getY(), 150.0f, 15.0f),
                    juce::Justification::centredLeft, false);
    }
}

//==============================================================================
// ZTameGainReductionMeter
//==============================================================================
void ZTameGainReductionMeter::setValues (float averageDb, float peakDb)
{
    average = juce::jmax (0.0f, averageDb);
    peak    = juce::jmax (0.0f, peakDb);
    repaint();
}

void ZTameGainReductionMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (3.0f, 2.0f);

    g.setColour (juce::Colour (0xcc0b1424));
    g.fillRoundedRectangle (bounds, 7.0f);

    auto titleArea = bounds.removeFromTop (16.0f);
    auto valueArea = bounds.removeFromBottom (34.0f);
    auto track = bounds.reduced (12.0f, 8.0f);

    g.setColour (ztame::textDim());
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText ("GR", titleArea, juce::Justification::centred, false);

    g.setColour (juce::Colour (0xff16213a));
    g.fillRoundedRectangle (track, 5.0f);

    constexpr float maxDb = 24.0f;
    const float avgNorm = juce::jlimit (0.0f, 1.0f, average / maxDb);
    const float peakNorm = juce::jlimit (0.0f, 1.0f, peak / maxDb);

    if (avgNorm > 0.001f)
    {
        auto fill = track.withHeight (track.getHeight() * avgNorm);
        fill.setY (track.getY());

        g.setGradientFill (juce::ColourGradient (ztame::amber(), track.getCentreX(), track.getY(),
                                                 ztame::accent(), track.getCentreX(), track.getBottom(),
                                                 false));
        g.fillRoundedRectangle (fill, 5.0f);
    }

    // scale ticks
    g.setColour (juce::Colour (0x44ffffff));
    for (float db : { 3.0f, 6.0f, 12.0f, 18.0f })
    {
        const float y = track.getY() + track.getHeight() * (db / maxDb);
        g.drawHorizontalLine ((int) std::round (y), track.getRight() - 4.0f, track.getRight());
    }

    if (peakNorm > 0.001f)
    {
        const float y = track.getY() + track.getHeight() * peakNorm;
        g.setColour (ztame::danger());
        g.fillRect (track.getX(), y - 1.0f, track.getWidth(), 2.0f);
    }

    g.setColour (ztame::textBright());
    g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    g.drawText (juce::String (average, 1), valueArea.removeFromTop (16.0f),
                juce::Justification::centred, false);

    g.setColour (ztame::textDim());
    g.setFont (juce::Font (juce::FontOptions (9.5f)));
    g.drawText ("pk " + juce::String (peak, 1), valueArea, juce::Justification::centred, false);
}

//==============================================================================
// ZTameKnob
//==============================================================================
ZTameKnob::ZTameKnob (ZTameAudioProcessor& p, const juce::String& parameterId,
                      const juce::String& captionText, const juce::String& tooltipText)
    : caption (captionText)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    slider.setDoubleClickReturnValue (true,
        p.getValueTreeState().getParameter (parameterId) != nullptr
            ? p.getValueTreeState().getParameter (parameterId)->convertFrom0to1 (
                  p.getValueTreeState().getParameter (parameterId)->getDefaultValue())
            : 0.0);
    slider.setVelocityModeParameters (1.0, 1, 0.0, false);
    slider.setTooltip (tooltipText);
    slider.setTitle (captionText);

    if (auto* param = p.getValueTreeState().getParameter (parameterId))
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            p.getValueTreeState(), parameterId, slider);

    addAndMakeVisible (slider);
    setTooltip (tooltipText);
}

void ZTameKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (34);            // value + caption, painted by this component
    slider.setBounds (r);
}

void ZTameKnob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();

    g.setColour (ztame::textBright());
    g.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
    g.drawText (slider.getTextFromValue (slider.getValue()),
                r.removeFromBottom (18), juce::Justification::centred, false);

    g.setColour (ztame::textDim());
    g.setFont (juce::Font (juce::FontOptions (10.5f)));
    g.drawText (caption.toUpperCase(), r.removeFromBottom (15),
                juce::Justification::centred, false);
}

//==============================================================================
// ZTameToggleTile
//==============================================================================
ZTameToggleTile::ZTameToggleTile (ZTameAudioProcessor& p, const juce::String& parameterId,
                                  const juce::String& titleText, const juce::String& tooltipText)
    : processor (p), paramId (parameterId), title (titleText), tint (ztame::accent())
{
    setTooltip (tooltipText);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ZTameToggleTile::setVisualState (bool isOn, const juce::String& newSubText,
                                      bool isEnabled, juce::Colour accentOverride)
{
    const bool changed = (isOn != on) || (newSubText != subText)
                      || (isEnabled != enabled) || (accentOverride != juce::Colour());

    on = isOn;
    subText = newSubText;
    enabled = isEnabled;

    if (accentOverride != juce::Colour())
        tint = accentOverride;

    if (changed)
        repaint();
}

void ZTameToggleTile::toggle()
{
    if (! enabled)
        return;

    if (auto* p = processor.getValueTreeState().getParameter (paramId))
    {
        const float current = p->getValue();
        p->beginChangeGesture();
        // Choice parameters cycle; anything else flips.
        const bool isChoice = dynamic_cast<juce::AudioParameterChoice*> (p) != nullptr;
        p->setValueNotifyingHost (isChoice ? (current < 0.5f ? 1.0f : 0.0f)
                                           : (current > 0.5f ? 0.0f : 1.0f));
        p->endChangeGesture();
    }

    repaint();
}

void ZTameToggleTile::mouseDown (const juce::MouseEvent&)
{
    toggle();
}

void ZTameToggleTile::mouseEnter (const juce::MouseEvent&)
{
    hovered = true;
    repaint();
}

void ZTameToggleTile::mouseExit (const juce::MouseEvent&)
{
    hovered = false;
    repaint();
}

void ZTameToggleTile::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const bool interactive = enabled;

    auto fill = on ? tint.withAlpha (0.22f)
                   : (hovered && interactive ? juce::Colour (0x332c3a55) : juce::Colour (0x2a1b2740));

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 7.0f);

    g.setColour (on ? tint.withAlpha (0.95f)
                    : (interactive ? ztame::panelStroke() : juce::Colour (0x1affffff)));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 7.0f, 1.2f);

    auto r = getLocalBounds().reduced (10, 4);

    g.setColour (on ? ztame::textBright() : (interactive ? ztame::textBright().withAlpha (0.82f)
                                                         : ztame::textDim().withAlpha (0.7f)));
    g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    g.drawText (title, r.removeFromTop (subText.isEmpty() ? r.getHeight() : 15),
                juce::Justification::centredLeft, false);

    if (subText.isNotEmpty())
    {
        g.setColour (on ? tint.withAlpha (0.9f) : ztame::textDim().withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (9.5f)));
        g.drawText (subText, r, juce::Justification::centredLeft, false);
    }
}

//==============================================================================
// ZTameEditorContent
//==============================================================================
ZTameEditorContent::ZTameEditorContent (ZTameAudioProcessor& p)
    : processor (p),
      spectrum (p),
      listenTile (p, ZTameAudioProcessor::paramListenMode, "Δ LISTEN",
                  "Audition ONLY the energy ZTame removes (+6 dB monitor gain). "
                  "If you hear an objectionable ring, that is exactly what is being tamed."),
      bypassTile (p, ZTameAudioProcessor::paramBypass, "BYPASS",
                  "True bypass of the suppression engine. Latency is kept so host delay "
                  "compensation stays aligned — use it for A/B."),
      sidechainTile (p, ZTameAudioProcessor::paramSidechain, "SC  SIDECHAIN",
                     "Detect resonances from the sidechain input instead of the track itself, "
                     "then suppress them on the main input. Falls back automatically when no "
                     "sidechain signal is present."),
      linkTile (p, ZTameAudioProcessor::paramStereoLink, "LINK  STEREO",
                "On: both channels share one detector (stable imaging). "
                "Off: each channel is detected independently."),
      midSideTile (p, ZTameAudioProcessor::paramMidSide, "M/S  MODE",
                   "Process mid and side instead of left and right. A centred resonance is "
                   "then tamed without touching the stereo width of the track."),
      resolutionTile (p, ZTameAudioProcessor::paramResolution, "RES  2048 PRECISE",
                      "Analysis resolution / latency. Precise = 2048-point frames (46 ms at "
                      "44.1 kHz, finer low-frequency detail). Fast = 1024-point frames "
                      "(23 ms, lower CPU).")
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (false);

    addAndMakeVisible (spectrum);
    addAndMakeVisible (meter);

    knobs[0] = std::make_unique<ZTameKnob> (p, ZTameAudioProcessor::paramSensitivity, "Sensitivity",
        "How easily a peak is treated as a resonance. Higher = tames more. "
        "Sets the level a bin must exceed, relative to its own local neighbourhood.");
    knobs[1] = std::make_unique<ZTameKnob> (p, ZTameAudioProcessor::paramSelectivity, "Selectivity",
        "How sharply defined a peak must be to trigger. Higher = only obvious ring, "
        "leaves natural tone alone. Independent of Sensitivity (±6 dB of trigger range).");
    knobs[2] = std::make_unique<ZTameKnob> (p, ZTameAudioProcessor::paramRange, "Depth",
        "Maximum cut applied to any single resonance, in dB. 0 dB = analyse only.");
    knobs[3] = std::make_unique<ZTameKnob> (p, ZTameAudioProcessor::paramAttack, "Attack",
        "How fast a notch closes onto a resonance that appears. Frame period is 23 ms, "
        "so short values engage within one frame.");
    knobs[4] = std::make_unique<ZTameKnob> (p, ZTameAudioProcessor::paramRelease, "Release",
        "How quickly a notch opens again once the resonance stops.");
    knobs[5] = std::make_unique<ZTameKnob> (p, ZTameAudioProcessor::paramDryWet, "Mix",
        "Parallel blend of the processed signal and the untouched input. "
        "Both paths are latency-aligned, so the blend stays phase-coherent.");

    for (auto& k : knobs)
        addAndMakeVisible (*k);

    addAndMakeVisible (listenTile);
    addAndMakeVisible (bypassTile);
    addAndMakeVisible (sidechainTile);
    addAndMakeVisible (linkTile);
    addAndMakeVisible (midSideTile);
    addAndMakeVisible (resolutionTile);

    // --- preset bar --------------------------------------------------------
    presetBox.setTooltip ("Factory starting points. Pick the closest one, then trim "
                          "Sensitivity and Depth to taste. Your DAW still saves the full state.");
    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.setJustificationType (juce::Justification::centredLeft);
    presetBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xcc0e1728));
    presetBox.setColour (juce::ComboBox::outlineColourId, ztame::panelStroke());
    presetBox.setColour (juce::ComboBox::textColourId, ztame::textBright());
    presetBox.setColour (juce::ComboBox::arrowColourId, ztame::accent());
    presetBox.addItem ("Init (defaults)", 1);
    presetBox.addItem ("Vocal Harshness Tamer", 2);
    presetBox.addItem ("De-Boom", 3);
    presetBox.addItem ("Guitar Resonance Smooth", 4);
    presetBox.addItem ("Cymbal Ring Killer", 5);
    presetBox.addItem ("Master Polish", 6);
    presetBox.addItem ("Podcast Clarity", 7);
    presetBox.onChange = [this] { applyPreset (presetBox.getSelectedId()); };
    addAndMakeVisible (presetBox);

    startTimerHz (30);
}

//==============================================================================
namespace
{
    struct PresetValues
    {
        float sensitivity, selectivity, rangeDb, focusLow, focusHigh, attackMs, releaseMs;
    };

    // Six starting points. They only set the sound-shaping controls: routing
    // (sidechain, M/S), mix, listen and bypass are left to the user.
    PresetValues presetFor (int index)
    {
        switch (index)
        {
            case 2:  return { 68.0f, 55.0f, 14.0f,  1500.0f, 12000.0f,   6.0f, 150.0f };  // vocal
            case 3:  return { 60.0f, 40.0f, 16.0f,    60.0f,   400.0f,  10.0f, 250.0f };  // de-boom
            case 4:  return { 62.0f, 50.0f, 12.0f,   200.0f,  6000.0f,   8.0f, 180.0f };  // guitar
            case 5:  return { 75.0f, 60.0f, 14.0f,  4000.0f, 16000.0f,   3.0f,  90.0f };  // cymbal
            case 6:  return { 55.0f, 45.0f,  8.0f,    20.0f, 20000.0f,  12.0f, 300.0f };  // master
            case 7:  return { 70.0f, 58.0f, 12.0f,   300.0f, 10000.0f,   5.0f, 120.0f };  // podcast
            default: return { 65.0f, 50.0f, 12.0f,    20.0f, 20000.0f,   8.0f, 180.0f };  // init
        }
    }
}

void ZTameEditorContent::applyPreset (int presetIndex)
{
    const auto v = presetFor (presetIndex <= 0 ? 1 : presetIndex);

    auto& vts = processor.getValueTreeState();

    const auto set = [&vts] (const char* id, float value)
    {
        if (auto* param = vts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    set (ZTameAudioProcessor::paramSensitivity, v.sensitivity);
    set (ZTameAudioProcessor::paramSelectivity, v.selectivity);
    set (ZTameAudioProcessor::paramRange,       v.rangeDb);
    set (ZTameAudioProcessor::paramFocusLow,    v.focusLow);
    set (ZTameAudioProcessor::paramFocusHigh,   v.focusHigh);
    set (ZTameAudioProcessor::paramAttack,      v.attackMs);
    set (ZTameAudioProcessor::paramRelease,     v.releaseMs);

    repaint();
}

ZTameEditorContent::~ZTameEditorContent()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ZTameEditorContent::resized()
{
    spectrum.setBounds (20, 68, 892, 328);
    meter.setBounds (922, 68, 54, 328);

    // DETECT panel
    knobs[0]->setBounds (36, 440, 150, 122);
    knobs[1]->setBounds (206, 440, 150, 122);

    // SHAPE panel
    knobs[2]->setBounds (398, 440, 96, 122);
    knobs[3]->setBounds (500, 440, 96, 122);
    knobs[4]->setBounds (602, 440, 96, 122);

    // OUTPUT panel
    knobs[5]->setBounds (728, 440, 104, 122);
    resolutionTile.setBounds (842, 430, 122, 34);
    linkTile.setBounds (842, 468, 122, 34);
    midSideTile.setBounds (842, 506, 122, 34);
    sidechainTile.setBounds (842, 544, 122, 34);

    listenTile.setBounds (694, 14, 158, 44);
    bypassTile.setBounds (862, 14, 114, 44);

    presetBox.setBounds (300, 16, 250, 26);
}

void ZTameEditorContent::timerCallback()
{
    refreshTelemetry();
    repaint();
}

void ZTameEditorContent::refreshTelemetry()
{
    if (processor.getLatestSnapshot (snapshot))
        haveSnapshot = true;

    const bool valid = haveSnapshot;
    spectrum.setSnapshot (snapshot, valid);

    if (valid)
        meter.setValues (snapshot.grAverageDb, snapshot.grPeakDb);

    auto& vts = processor.getValueTreeState();

    const auto readBool = [&vts] (const char* id)
    {
        if (auto* p = vts.getRawParameterValue (id))
            return p->load() > 0.5f;
        return false;
    };

    listenTile.setVisualState (readBool (ZTameAudioProcessor::paramListenMode),
                               "+6 dB monitor", true, ztame::amber());

    bypassTile.setVisualState (readBool (ZTameAudioProcessor::paramBypass),
                               "A/B reference", true, ztame::danger());

    sidechainConnected = processor.isSidechainConnected();

    const bool scOn = readBool (ZTameAudioProcessor::paramSidechain);
    const bool scLive = valid && snapshot.sidechainLive;

    sidechainTile.setVisualState (scOn,
                                  ! sidechainConnected ? "no input connected"
                                                       : (scLive ? "live — detecting"
                                                                 : "waiting for signal"),
                                  true, ztame::violet());

    linkTile.setVisualState (readBool (ZTameAudioProcessor::paramStereoLink),
                             "one detector", true, ztame::accent());

    midSideTile.setVisualState (readBool (ZTameAudioProcessor::paramMidSide),
                                "mid + side engine", true, ztame::violet());

    const bool precise = readBool (ZTameAudioProcessor::paramResolution);
    resolutionTile.setVisualState (precise,
                                   precise ? "46 ms latency" : "23 ms latency", true, ztame::accent());

    if (valid)
    {
        const auto sr = snapshot.sampleRate > 0.0 ? snapshot.sampleRate : 44100.0;
        const auto latencyMs = 1000.0 * (double) snapshot.fftSize / sr;

        statusLeft = "Trigger " + juce::String (snapshot.thresholdDb, 1)
                   + " dB above local level   ·   Engine " + juce::String (snapshot.fftSize)
                   + " (latency " + juce::String (latencyMs, 1) + " ms)";

        statusRight = juce::String (sr / 1000.0, 1) + " kHz"
                    + (snapshot.midSide ? "   ·   M/S" : "")
                    + "   ·   Sidechain: "
                    + (sidechainConnected ? (scLive ? "live" : "connected, silent") : "not connected")
                    + "   ·   v" + juce::String (ZTAME_VERSION);
    }
}

void ZTameEditorContent::paint (juce::Graphics& g)
{
    // Keep the visualisation live even when the host throttles timers (and when the
    // editor is rendered offscreen, e.g. by the UI-shot tool).
    refreshTelemetry();
    // Window background
    g.setGradientFill (juce::ColourGradient (ztame::backgroundTop(), 0.0f, 0.0f,
                                             ztame::backgroundBottom(), 0.0f, (float) getHeight(), false));
    g.fillAll();

    // Header
    g.setColour (ztame::textBright());
    g.setFont (juce::Font (juce::FontOptions (30.0f, juce::Font::bold)));
    g.drawText ("ZTAME", 28, 14, 260, 34, juce::Justification::centredLeft, false);

    g.setColour (ztame::accentSoft());
    g.setFont (juce::Font (juce::FontOptions (11.5f)));
    g.drawText ("resonance suppressor  ·  tame the ring, keep the tone",
                30, 46, 420, 16, juce::Justification::centredLeft, false);

    g.setColour (ztame::textDim().withAlpha (0.8f));
    g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
    g.drawText ("PRESET", 300, 4, 120, 12, juce::Justification::centredLeft, false);

    // Panels
    const auto drawPanel = [&g] (juce::Rectangle<int> r, const juce::String& title,
                                 const juce::String& subtitle)
    {
        g.setColour (ztame::panelFill());
        g.fillRoundedRectangle (r.toFloat(), 12.0f);
        g.setColour (ztame::panelStroke());
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 12.0f, 1.0f);

        g.setColour (ztame::accent());
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (title, r.withTrimmedLeft (16).withTrimmedTop (10).withHeight (14),
                    juce::Justification::centredLeft, false);

        if (subtitle.isNotEmpty())
        {
            g.setColour (ztame::textDim());
            g.setFont (juce::Font (juce::FontOptions (9.5f)));
            g.drawText (subtitle, r.withTrimmedLeft (16).withTrimmedTop (24).withHeight (12),
                        juce::Justification::centredLeft, false);
        }
    };

    drawPanel ({ 20, 408, 356, 200 }, "DETECT", "what counts as a resonance");
    drawPanel ({ 386, 408, 316, 200 }, "SHAPE", "how the notches behave");
    drawPanel ({ 712, 408, 264, 200 }, "OUTPUT", "blend, audition, routing");

    // Detector read-out inside the DETECT panel
    if (haveSnapshot)
    {
        g.setColour (ztame::textDim());
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("Trigger requires +" + juce::String (snapshot.thresholdDb, 1)
                    + " dB over the local spectrum",
                    juce::Rectangle<int> (36, 574, 330, 26), juce::Justification::centredLeft, false);
    }

    // Status strip
    g.setColour (ztame::textDim());
    g.setFont (juce::Font (juce::FontOptions (10.0f)));

    if (statusLeft.isNotEmpty())
    {
        g.drawText (statusLeft, 24, 616, 520, 16, juce::Justification::centredLeft, false);

        g.setColour (ztame::textDim().withAlpha (0.75f));
        g.drawText (statusRight, 560, 616, 416, 16, juce::Justification::centredRight, false);
    }
}

//==============================================================================
// ZTameAudioProcessorEditor
//==============================================================================
ZTameAudioProcessorEditor::ZTameAudioProcessorEditor (ZTameAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      content (p)
{
    addAndMakeVisible (content);

    setResizable (true, true);
    setResizeLimits (750, 480, 1700, 1088);

    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) ztame::designWidth / (double) ztame::designHeight);

    setSize (ztame::designWidth, ztame::designHeight);

    audioProcessor.setUiActive (true);
}

ZTameAudioProcessorEditor::~ZTameAudioProcessorEditor()
{
    audioProcessor.setUiActive (false);
}

void ZTameAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (ztame::backgroundBottom());

    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0x22ffffff));
    g.drawRect (bounds, 1.0f);
}

void ZTameAudioProcessorEditor::resized()
{
    const float scale = juce::jmin ((float) getWidth() / (float) ztame::designWidth,
                                    (float) getHeight() / (float) ztame::designHeight);

    const float dx = ((float) getWidth()  - (float) ztame::designWidth  * scale) * 0.5f;
    const float dy = ((float) getHeight() - (float) ztame::designHeight * scale) * 0.5f;

    content.setBounds (0, 0, ztame::designWidth, ztame::designHeight);
    content.setTransform (juce::AffineTransform::scale (scale).translated (dx, dy));
}
