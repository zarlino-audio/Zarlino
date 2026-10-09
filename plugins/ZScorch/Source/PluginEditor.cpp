/*
  ==============================================================================

    PluginEditor.cpp
    Created: 7 Feb 2025
    Author:  Zarlino Audio

    Studio-quality editor for ZScorch — Zarlino Look & Feel compliant.
    Simple/Advanced views, premium self-painting controls, 30 Hz timer sync.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "shared/licensing/ZUnlockDialog.h"

//==============================================================================
//  ZScorchAudioProcessorEditor
//==============================================================================

ZScorchAudioProcessorEditor::ZScorchAudioProcessorEditor (ZScorchAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&zscorchLnf);
    setResizable (false, false);
    setSize (Zarlino::Window::standard_w, Zarlino::Window::standard_h);

    licenseTooltip = std::make_unique<juce::TooltipWindow> (this, 400);

    createAttachments();

    // --- Macro controls ------------------------------------------------------
    setupZKnob (liftKnob,      "lift",      "Lift",      0.0,  1.0, 0.001, 0.5, "%");
    setupZKnob (characterKnob, "character", "Character", -1.0, 1.0, 0.001, 0.0, "ch");
    setupZKnob (mixKnob,       "mix",       "Mix",       0.0,  1.0, 0.001, 1.0, "%");

    // --- Saturator zone (rebound to the active band) -------------------------
    setupZKnob (satDriveKnob,         "satDriveLow",   "Drive",    0.0, 1.0, 0.001, 0.5, "%");
    setupZKnob (satHarmReductionKnob, "satHarmRedLow", "Harm Red", 0.0, 1.0, 0.001, 0.7, "%");
    setupZKnob (tiltAmountKnob,       "tiltAmount",    "Tilt",     0.0, 1.0, 0.001, 0.5, "%");
    setupZKnob (exciterAmountKnob,    "exciterAmount", "Exciter",  0.0, 1.0, 0.001, 0.0, "%");

    setupSatModeCombo();
    addAndMakeVisible (exciterEnabledToggle);
    describeControl (exciterEnabledToggle, "Exciter", "Enables the exciter stage");

    // --- Pre-Emphasis EQ -----------------------------------------------------
    setupZKnob (preEqKnobs[0], "preEqLowShelfFreq",  "Low Shelf Freq", 20.0,  500.0,   1.0,   100.0,  "Hz");
    setupZKnob (preEqKnobs[1], "preEqLowShelfGain",  "Low Shelf Gain", -12.0, 12.0,    0.1,   0.0,    "dB");
    setupZKnob (preEqKnobs[2], "preEqBellFreq",      "Bell Freq",      200.0, 8000.0,  1.0,   1000.0, "Hz");
    setupZKnob (preEqKnobs[3], "preEqBellGain",      "Bell Gain",      -12.0, 12.0,    0.1,   0.0,    "dB");
    setupZKnob (preEqKnobs[4], "preEqBellQ",         "Bell Q",         0.1,   10.0,    0.1,   0.707,  "Q");
    setupZKnob (preEqKnobs[5], "preEqHighShelfFreq", "High Shelf Freq", 2000.0, 20000.0, 1.0,   8000.0, "Hz");
    setupZKnob (preEqKnobs[6], "preEqHighShelfGain", "High Shelf Gain", -12.0, 12.0,    0.1,   0.0,    "dB");

    // --- Post-Emphasis EQ ----------------------------------------------------
    setupZKnob (postEqKnobs[0], "postEqLowShelfFreq",  "Low Shelf Freq", 20.0,  500.0,   1.0,   100.0,  "Hz");
    setupZKnob (postEqKnobs[1], "postEqLowShelfGain",  "Low Shelf Gain", -12.0, 12.0,    0.1,   0.0,    "dB");
    setupZKnob (postEqKnobs[2], "postEqBellFreq",      "Bell Freq",      200.0, 8000.0,  1.0,   1000.0, "Hz");
    setupZKnob (postEqKnobs[3], "postEqBellGain",      "Bell Gain",      -12.0, 12.0,    0.1,   0.0,    "dB");
    setupZKnob (postEqKnobs[4], "postEqBellQ",         "Bell Q",         0.1,   10.0,    0.1,   0.707,  "Q");
    setupZKnob (postEqKnobs[5], "postEqHighShelfFreq", "High Shelf Freq", 2000.0, 20000.0, 1.0,   8000.0, "Hz");
    setupZKnob (postEqKnobs[6], "postEqHighShelfGain", "High Shelf Gain", -12.0, 12.0,    0.1,   0.0,    "dB");

    // --- Output zone ---------------------------------------------------------
    setupZSlider (outputCeilingSlider, "outputCeiling", "Output Ceiling", -12.0, -0.1, 0.1, -0.3, "dB");
    setupZSlider (bandGainSliders[0], "bandGainLow",   "Low Band Gain",    -6.0, 6.0, 0.1, 0.0, "dB");
    setupZSlider (bandGainSliders[1], "bandGainLoMid", "Lo-Mid Band Gain", -6.0, 6.0, 0.1, 0.0, "dB");
    setupZSlider (bandGainSliders[2], "bandGainHiMid", "Hi-Mid Band Gain", -6.0, 6.0, 0.1, 0.0, "dB");
    setupZSlider (bandGainSliders[3], "bandGainHigh",  "High Band Gain",   -6.0, 6.0, 0.1, 0.0, "dB");
    addAndMakeVisible (oversample4xToggle);
    describeControl (oversample4xToggle, "Oversample 4x", "Runs the saturator at 4x sample rate");

    // --- View toggle ---------------------------------------------------------
    addAndMakeVisible (advancedToggle);
    describeControl (advancedToggle, "View", "Switch between Simple and Advanced");
    advancedToggle.onClick = [this]
    {
        showingAdvanced = advancedToggle.getToggleState();
        resized();
    };

    // --- Segmented controls --------------------------------------------------
    setupBandSelector();
    setupZoneSelector();

    bandEditSelector.onTabSelected = [this] (int id)
    {
        currentBand = id;
        rebindBandAttachments (id);
    };
    addAndMakeVisible (bandEditSelector);

    // --- Preset combo --------------------------------------------------------
    const auto numPresets = audioProcessor.getNumPrograms();
    for (int i = 0; i < numPresets; ++i)
        presetComboBox.addItem (audioProcessor.getProgramName (i), i + 1);
    presetComboBox.setSelectedId (audioProcessor.getCurrentPresetIndex() + 1, juce::dontSendNotification);
    presetComboBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (Zarlino::Palette::graphite_800));
    presetComboBox.setColour (juce::ComboBox::textColourId,       juce::Colour (Zarlino::Palette::mist_100));
    presetComboBox.setColour (juce::ComboBox::outlineColourId,     juce::Colour (Zarlino::Palette::graphite_700));
    presetComboBox.onChange = [this]
    {
        const auto idx = presetComboBox.getSelectedId() - 1;
        if (idx >= 0 && idx < audioProcessor.getNumPrograms())
            audioProcessor.setCurrentProgram (idx);
    };
    addAndMakeVisible (presetComboBox);

    // --- Header action buttons ------------------------------------------------
    undoButton.setButtonText ("UNDO");
    undoButton.onClick = [this]
    {
        if (auto* um = audioProcessor.getUndoManager())
            um->undo();
    };
    addAndMakeVisible (undoButton);

    redoButton.setButtonText ("REDO");
    redoButton.onClick = [this]
    {
        if (auto* um = audioProcessor.getUndoManager())
            um->redo();
    };
    addAndMakeVisible (redoButton);

    bypassButton.setButtonText ("BYPASS");
    bypassButton.setClickingTogglesState (true);
    bypassButton.onClick = [this]
    {
        audioProcessor.bypassEnabled.store (bypassButton.getToggleState());
    };
    addAndMakeVisible (bypassButton);

    abButton.setButtonText ("A");
    abButton.onClick = [this]
    {
        auto& vts = audioProcessor.getValueTreeState();

        auto capture = [&vts]() -> juce::MemoryBlock
        {
            juce::MemoryOutputStream stream;
            vts.state.writeToStream (stream);
            return stream.getMemoryBlock();
        };

        auto restore = [&vts] (const juce::MemoryBlock& block)
        {
            if (block.getSize() == 0)
                return;
            juce::MemoryInputStream stream (block, false);
            auto tree = juce::ValueTree::readFromStream (stream);
            if (tree.isValid())
                vts.replaceState (tree);
        };

        if (abOnA)
        {
            abStateA = capture();
            if (abStateB.getSize() == 0)
                abStateB = abStateA;   // prime the B slot with the current state
            else
                restore (abStateB);
            abOnA = false;
            abButton.setButtonText ("B");
        }
        else
        {
            abStateB = capture();
            restore (abStateA);
            abOnA = true;
            abButton.setButtonText ("A");
        }
    };
    addAndMakeVisible (abButton);

    unlockButton.setButtonText ("UNLOCK");
    unlockButton.onClick = [this]
    {
        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = "ZScorch License";
        options.dialogBackgroundColour = juce::Colour (Zarlino::Palette::obsidian_950);
        options.content.setOwned (new Zarlino::Licensing::ZUnlockDialog());
        options.content->setSize (500, 380);
        options.launchAsync();
        refreshLicenseDisplay();
    };
    addAndMakeVisible (unlockButton);

    licenseLabel.setJustificationType (juce::Justification::centredRight);
    licenseLabel.setFont (juce::Font (juce::FontOptions (10.0f)));
    licenseLabel.setColour (juce::Label::textColourId, juce::Colour (Zarlino::Palette::steel_300));
    addAndMakeVisible (licenseLabel);
    refreshLicenseDisplay();

    // --- Meters + spectrum ----------------------------------------------------
    inputMeter.setDirection (true);    // §4: IN/OUT meters are vertical, 18 segments
    outputMeter.setDirection (true);
    activityMeter.setDirection (false); // §4: ACTIVITY meter stays horizontal, 32 segments
    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);
    addAndMakeVisible (activityMeter);
    addAndMakeVisible (spectrum);

    // --- Initial state ---------------------------------------------------------
    rebindBandAttachments (0);

    const int activeBands = audioProcessor.getActiveNumBands();
    static const char* bandNames[] = { "LOW", "LO-MID", "HI-MID", "HIGH" };
    for (int i = 0; i < activeBands; ++i)
        bandEditSelector.addTab (bandNames[i], i);
    lastActiveBands = activeBands;

    showingAdvanced = false;
    currentZone = 0;
    currentBand = 0;
    spectrum.setActiveNumBands (activeBands);
    spectrum.setShowBandShading (true);

    startTimerHz (30);
}

ZScorchAudioProcessorEditor::~ZScorchAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void ZScorchAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace Zarlino;

    g.fillAll (juce::Colour (Palette::obsidian_950));

    const auto bounds = getLocalBounds();
    const int headerH = (int) Chrome::headerHeight;
    const int footerH = (int) Chrome::footerHeight;

    // Header bar
    g.setColour (juce::Colour (Palette::obsidian_900));
    g.fillRect (bounds.getX(), bounds.getY(), bounds.getWidth(), headerH);
    g.setColour (juce::Colour (Palette::graphite_700));
    g.drawHorizontalLine (headerH, 0.0f, (float) getWidth());

    // Title (text-only chrome, §9)
    g.setColour (juce::Colour (Palette::mist_100));
    g.setFont (juce::FontOptions (Typography::size_wordmark, juce::Font::bold));
    g.drawText ("ZSCORCH",
                juce::Rectangle<int> (Spacing::margin, 0, 180, headerH),
                juce::Justification::centredLeft);

    // Simple / Advanced toggle row labels
    const int toggleY = headerH;
    g.setFont (juce::FontOptions (Typography::size_caption, juce::Font::bold));
    g.setColour (juce::Colour (Palette::steel_300));
    g.drawText ("SIMPLE",
                juce::Rectangle<int> (advancedToggle.getX() - 56, toggleY, 48, 36),
                juce::Justification::centredRight);
    g.drawText ("ADVANCED",
                juce::Rectangle<int> (advancedToggle.getRight() + 8, toggleY, 56, 36),
                juce::Justification::centredLeft);

    // Footer
    const int footerY = getHeight() - footerH;
    g.setColour (juce::Colour (Palette::graphite_700));
    g.drawHorizontalLine (footerY, 0.0f, (float) getWidth());

    auto footerRect = juce::Rectangle<int> (Spacing::margin, footerY,
                                            getWidth() - Spacing::margin * 2, footerH);
    g.setFont (juce::FontOptions (Typography::size_caption, juce::Font::bold));
    g.setColour (juce::Colour (Palette::steel_300));
    g.drawText ("ZARLINO", footerRect.removeFromLeft (64), juce::Justification::centredLeft);

    // PRINT SERIES category tag — rust accent used only here + spectrum markers
    g.setColour (juce::Colour (Palette::cat_print));
    g.drawText ("PRINT SERIES", footerRect.removeFromLeft (92), juce::Justification::centredLeft);

    if (showingAdvanced)
    {
        // CPU / latency
        g.setColour (juce::Colour (Palette::steel_300));
        const juce::String cpuText = juce::String (audioProcessor.getProcessingLoadPercent(), 1) + "% CPU  ";
        const juce::String latText  = juce::String (audioProcessor.getLatencySamples()) + " smp";
        g.drawText (cpuText + latText, footerRect.removeFromRight (160), juce::Justification::centredRight);
    }
    else
    {
        // Simple view: live macro readouts + mode/status
        drawSimpleFooterStatus (g, footerRect);
    }

    // --- Module panels --------------------------------------------------------
    // Painted before the knob labels so the labels sit on top of the fills.
    if (showingAdvanced)
    {
        drawAdvancedMacroPanel (g);
        drawZonePanel (g);
    }
    else
    {
        drawActivityPanel (g);
        drawMacroCluster (g);
    }

    // --- View labels ----------------------------------------------------------
    if (showingAdvanced)
    {
        drawKnobLabel (g, liftKnob, "LIFT");
        drawKnobLabel (g, characterKnob, "CHARACTER");
        drawKnobLabel (g, mixKnob, "MIX");

        if (currentZone == 0)
        {
            drawKnobLabel (g, satDriveKnob, "DRIVE");
            drawKnobLabel (g, satModeCombo, "MODE");
            drawKnobLabel (g, satHarmReductionKnob, "HARM RED");
            drawKnobLabel (g, tiltAmountKnob, "TILT");
            drawKnobLabel (g, exciterAmountKnob, "EXCITER");
            if (exciterEnabledToggle.isVisible())
            {
                const auto t = exciterEnabledToggle.getBounds();
                g.setFont (juce::FontOptions (Typography::size_caption, juce::Font::bold));
                g.setColour (juce::Colour (Palette::steel_300));
                g.drawText ("EXCITER ON", juce::Rectangle<int> (t.getX() - 56, t.getY() - 10, 144, 12),
                            juce::Justification::centred);
            }
        }
        else if (currentZone == 1)
        {
            static const char* eqLabels[] = { "LS FREQ", "LS GAIN", "BELL FREQ", "BELL GAIN",
                                              "Q", "HS FREQ", "HS GAIN" };
            for (int i = 0; i < 7; ++i)
            {
                drawKnobLabel (g, preEqKnobs[i], eqLabels[i]);
                drawKnobLabel (g, postEqKnobs[i], eqLabels[i]);
            }

            if (zoneContentRect.getWidth() > 0)
            {
                const int panelW = (zoneContentRect.getWidth() - Spacing::gutter) / 2;
                drawSectionHeader (g, "PRE-EMPHASIS",
                                   juce::Rectangle<int> (zoneContentRect.getX(), zoneContentRect.getY(),
                                                         panelW, 18));
                drawSectionHeader (g, "POST-EMPHASIS",
                                   juce::Rectangle<int> (zoneContentRect.getX() + panelW + Spacing::gutter,
                                                         zoneContentRect.getY(), panelW, 18));
            }
        }
        else if (currentZone == 2)
        {
            static const char* bandNames[] = { "LOW", "LO-MID", "HI-MID", "HIGH" };
            for (int i = 0; i < 4; ++i)
                if (bandGainSliders[i].isVisible())
                    drawKnobLabel (g, bandGainSliders[i], bandNames[i]);
            drawKnobLabel (g, outputCeilingSlider, "CEILING");
            if (oversample4xToggle.isVisible())
            {
                const auto t = oversample4xToggle.getBounds();
                g.setFont (juce::FontOptions (Typography::size_caption, juce::Font::bold));
                g.setColour (juce::Colour (Palette::steel_300));
                g.drawText ("OVERSAMPLE 4X", juce::Rectangle<int> (t.getX() - 70, t.getY() - 10, 172, 12),
                            juce::Justification::centred);
            }
        }
    }
    else
    {
        drawKnobLabel (g, liftKnob, "LIFT");
        drawKnobLabel (g, characterKnob, "CHARACTER");
        drawKnobLabel (g, mixKnob, "MIX");
    }
}

//==============================================================================
void ZScorchAudioProcessorEditor::resized()
{
    using namespace Zarlino;

    auto bounds = getLocalBounds();
    auto headerRect = bounds.removeFromTop ((int) Chrome::headerHeight);

    // Header controls (right → left)
    const int btnH = 26;
    const int btnY = (headerRect.getHeight() - btnH) / 2;
    int x = getWidth() - Spacing::margin;
    unlockButton.setBounds (x - 76, btnY, 76, btnH);     x -= 76 + 8;
    abButton.setBounds      (x - 40, btnY, 40, btnH);     x -= 40 + 8;
    bypassButton.setBounds  (x - 60, btnY, 60, btnH);     x -= 60 + 8;
    redoButton.setBounds    (x - 44, btnY, 44, btnH);     x -= 44 + 8;
    undoButton.setBounds    (x - 44, btnY, 44, btnH);     x -= 44 + 8;
    licenseLabel.setBounds  (x - 110, btnY, 110, btnH);   x -= 110 + 8;

    // "Preset / Mode" chrome — the saturation mode lives in the header on the
    // simple view (the one tonal decision). On the advanced view the same combo
    // is hosted inside the Saturator zone, so it stays hidden here.
    if (showingAdvanced)
    {
        satModeCombo.setVisible (false);
        presetComboBox.setBounds (x - 168, btnY, 168, btnH);
    }
    else
    {
        satModeCombo.setVisible (true);
        satModeCombo.setBounds (x - 104, btnY, 96, btnH);  x -= 104 + 8;
        presetComboBox.setBounds (x - 168, btnY, 168, btnH);
    }

    if (showingAdvanced)
        layoutAdvancedView();
    else
        layoutSimpleView();
}

//==============================================================================
void ZScorchAudioProcessorEditor::layoutSimpleView()
{
    using namespace Zarlino;

    auto bounds = getLocalBounds();
    auto headerRect = bounds.removeFromTop ((int) Chrome::headerHeight);
    auto toggleRect = bounds.removeFromTop (36);
    bounds.removeFromBottom ((int) Chrome::footerHeight);

    const auto content = bounds.reduced (Spacing::margin, 8);

    // Visibility — hide advanced-only + zone controls; show the harmonic
    // activity display and the macro control cluster.
    spectrum.setVisible (true);
    spectrum.setShowHarmonicActivity (true);
    bandSelector.setVisible (false);
    zoneSelector.setVisible (false);
    inputMeter.setVisible (false);
    outputMeter.setVisible (false);
    activityMeter.setVisible (true);
    setZoneVisibility (-1);
    satModeCombo.setVisible (true);   // hosted in the header in simple view

    advancedToggle.setBounds (toggleRect.getCentreX() - 16, toggleRect.getCentreY() - 8, 32, 16);

    int y = content.getY();

    // ---- 1. HARMONIC ACTIVITY panel -----------------------------------------
    // Gives the large central area a purpose: a restrained live view of where
    // the harmonic energy is, split across the crossover bands.
    const int activityPanelH = 250;
    activityPanelRect = juce::Rectangle<int> (content.getX(), y, content.getWidth(), activityPanelH);

    // Section header (drawn in paint) + recessed spectrum well + activity strip
    const int headerH = 24;
    const int meterH  = 16;
    spectrum.setBounds (activityPanelRect.getX() + 4, activityPanelRect.getY() + headerH,
                        activityPanelRect.getWidth() - 8,
                        activityPanelRect.getHeight() - headerH - meterH - 12);
    activityMeter.setBounds (activityPanelRect.getX() + 12, activityPanelRect.getBottom() - meterH - 6,
                             activityPanelRect.getWidth() - 24, meterH);
    y = activityPanelRect.getBottom() + 16;

    // ---- 2. Macro control cluster -------------------------------------------
    // LIFT (hero, top centre) + CHARACTER / MIX (bottom row) inside one shared
    // panel so the three knobs read as a single tonal decision, not three
    // isolated controls floating in space.
    const int clusterH = 226;
    macroClusterRect = juce::Rectangle<int> (content.getX(), y, content.getWidth(), clusterH);

    const int heroW = 120;
    const int heroH = 104;
    liftKnob.setBounds (macroClusterRect.getCentreX() - heroW / 2,
                        macroClusterRect.getY() + 14, heroW, heroH);

    const int rowW = 108;
    const int rowH = 86;
    const int rowY = liftKnob.getBottom() + 8;
    characterKnob.setBounds (macroClusterRect.getCentreX() - rowW - 52, rowY, rowW, rowH);
    mixKnob.setBounds       (macroClusterRect.getCentreX() + 52, rowY, rowW, rowH);
}

//==============================================================================
void ZScorchAudioProcessorEditor::layoutAdvancedView()
{
    using namespace Zarlino;

    auto bounds = getLocalBounds();
    auto headerRect = bounds.removeFromTop ((int) Chrome::headerHeight);
    auto toggleRect = bounds.removeFromTop (36);
    bounds.removeFromBottom ((int) Chrome::footerHeight);

    const auto content = bounds.reduced (Spacing::margin, 8);

    spectrum.setVisible (true);
    spectrum.setShowHarmonicActivity (false);
    bandSelector.setVisible (true);
    zoneSelector.setVisible (true);
    inputMeter.setVisible (true);
    outputMeter.setVisible (true);
    activityMeter.setVisible (true);

    advancedToggle.setBounds (toggleRect.getCentreX() - 16, toggleRect.getCentreY() - 8, 32, 16);

    int y = content.getY();

    // Spectrum hero: IN/OUT vertical meters flank the analyzer,
    // ACTIVITY horizontal strip at the bottom (spec §4).
    const int spectrumH = 120;
    const int sideMeterW = 44;
    const int sideGap = 8;
    spectrum.setBounds (content.getX() + sideMeterW + sideGap, y,
                        content.getWidth() - 2 * (sideMeterW + sideGap), spectrumH);
    inputMeter.setBounds (content.getX(), y, sideMeterW, spectrumH);
    outputMeter.setBounds (content.getX() + content.getWidth() - sideMeterW, y,
                           sideMeterW, spectrumH);
    y += spectrumH + 4;

    activityMeter.setBounds (content.getX(), y, content.getWidth(), 28);
    y += 28 + 8;

    // Macro strip panel — Lift · Character · Mix. Same module language as the
    // simple view so the three macros read as one tonal decision in both views.
    const int knobW = 80;
    const int knobH = 76;
    const int totalW = 3 * knobW + 2 * 28;
    const int mx = content.getCentreX() - totalW / 2;
    liftKnob.setBounds (mx, y + 6, knobW, knobH);
    characterKnob.setBounds (mx + knobW + 28, y + 6, knobW, knobH);
    mixKnob.setBounds (mx + 2 * (knobW + 28), y + 6, knobW, knobH);
    advancedMacroPanelRect = juce::Rectangle<int> (liftKnob.getX() - 16, y,
                                                   (mixKnob.getRight() - liftKnob.getX()) + 32,
                                                   knobH + 12);
    y += advancedMacroPanelRect.getHeight() + 8;

    // Band selector (1..4)
    bandSelector.setBounds (content.getX(), y, content.getWidth(), 32);
    y += 32 + 8;

    // Zone tabs
    zoneSelector.setBounds (content.getX(), y, content.getWidth(), 32);
    y += 32 + 8;

    // Zone content
    zoneContentRect = juce::Rectangle<int> (content.getX(), y, content.getWidth(),
                                            content.getBottom() - y);
    layoutSaturatorZone (zoneContentRect);
    layoutEqZone (zoneContentRect);
    layoutOutputZone (zoneContentRect);

    setZoneVisibility (currentZone);
}

//==============================================================================
void ZScorchAudioProcessorEditor::layoutSaturatorZone (juce::Rectangle<int> r)
{
    using namespace Zarlino;
    if (r.getHeight() <= 0)
        return;

    // Band tabs (active bands only)
    bandEditSelector.setBounds (r.getX(), r.getY(), r.getWidth(), 32);

    // Two rows of three columns
    const int colW = 140;
    const int rowH = 68;
    const int x0 = r.getCentreX() - (3 * colW + 2 * 8) / 2;
    int y = r.getY() + 40;

    // Row 1: Drive | Mode | Harm Red (selected band)
    satDriveKnob.setBounds (x0, y, colW, rowH);
    satModeCombo.setBounds (x0 + colW + 8 + 10, y + 14, colW - 20, 28);
    satHarmReductionKnob.setBounds (x0 + 2 * (colW + 8), y, colW, rowH);
    y += rowH + 4;

    // Row 2: Tilt | Exciter | Exciter On
    tiltAmountKnob.setBounds (x0, y, colW, rowH);
    exciterAmountKnob.setBounds (x0 + colW + 8, y, colW, rowH);
    exciterEnabledToggle.setBounds (x0 + 2 * (colW + 8) + (colW - 32) / 2,
                                    y + (rowH - 16) / 2, 32, 16);
}

//==============================================================================
void ZScorchAudioProcessorEditor::layoutEqZone (juce::Rectangle<int> r)
{
    using namespace Zarlino;
    if (r.getHeight() <= 0)
        return;

    r.reduce (0, 4);
    const int panelW = (r.getWidth() - Spacing::gutter) / 2;
    const int labelH = 18;
    const int rowH = 70;
    const int gap = 8;

    auto layoutPanel = [this, labelH, rowH, gap] (juce::Rectangle<int> pr, ZarlinoKnob* knobs)
    {
        int y = pr.getY() + labelH;
        const int knobW = 80;

        // Row 1: LS Freq | LS Gain | Bell Freq | Bell Gain
        const int x0 = pr.getCentreX() - (4 * knobW + 3 * gap) / 2;
        knobs[0].setBounds (x0, y, knobW, rowH);
        knobs[1].setBounds (x0 + knobW + gap, y, knobW, rowH);
        knobs[2].setBounds (x0 + 2 * (knobW + gap), y, knobW, rowH);
        knobs[3].setBounds (x0 + 3 * (knobW + gap), y, knobW, rowH);
        y += rowH + gap;

        // Row 2: Q | HS Freq | HS Gain
        const int x1 = pr.getCentreX() - (3 * knobW + 2 * gap) / 2;
        knobs[4].setBounds (x1, y, knobW, rowH);
        knobs[5].setBounds (x1 + knobW + gap, y, knobW, rowH);
        knobs[6].setBounds (x1 + 2 * (knobW + gap), y, knobW, rowH);
    };

    layoutPanel (juce::Rectangle<int> (r.getX(), r.getY(), panelW, r.getHeight()), preEqKnobs);
    layoutPanel (juce::Rectangle<int> (r.getX() + panelW + Spacing::gutter, r.getY(),
                                       panelW, r.getHeight()), postEqKnobs);
}

//==============================================================================
void ZScorchAudioProcessorEditor::layoutOutputZone (juce::Rectangle<int> r)
{
    using namespace Zarlino;
    if (r.getHeight() <= 0)
        return;

    if (currentZone != 2)
    {
        for (int i = 0; i < 4; ++i)
            bandGainSliders[i].setVisible (false);
        outputCeilingSlider.setVisible (false);
        oversample4xToggle.setVisible (false);
        return;
    }

    r.reduce (0, 8);

    const int activeBands = juce::jmax (1, audioProcessor.getActiveNumBands());
    const int sliderW = (r.getWidth() - (activeBands - 1) * Spacing::gutter) / activeBands;
    const int rowH = 62;

    for (int i = 0; i < 4; ++i)
    {
        if (i < activeBands)
        {
            bandGainSliders[i].setBounds (r.getX() + i * (sliderW + Spacing::gutter),
                                          r.getY(), sliderW, rowH);
            bandGainSliders[i].setVisible (true);
        }
        else
        {
            bandGainSliders[i].setVisible (false);
        }
    }

    const int y2 = r.getY() + rowH + 10;
    outputCeilingSlider.setBounds (r.getX(), y2, r.getWidth() - 130, 48);
    oversample4xToggle.setBounds (r.getRight() - 100, y2 + 14, 32, 16);
}

//==============================================================================
void ZScorchAudioProcessorEditor::setZoneVisibility (int zone)
{
    const bool sat = (zone == 0);
    const bool eq  = (zone == 1);
    const bool out = (zone == 2);

    bandEditSelector.setVisible (sat);
    satDriveKnob.setVisible (sat);
    satModeCombo.setVisible (sat);
    satHarmReductionKnob.setVisible (sat);
    tiltAmountKnob.setVisible (sat);
    exciterAmountKnob.setVisible (sat);
    exciterEnabledToggle.setVisible (sat);

    for (int i = 0; i < 7; ++i)
    {
        preEqKnobs[i].setVisible (eq);
        postEqKnobs[i].setVisible (eq);
    }

    outputCeilingSlider.setVisible (out);
    oversample4xToggle.setVisible (out);
    if (!out)
        for (int i = 0; i < 4; ++i)
            bandGainSliders[i].setVisible (false);
}

//==============================================================================
void ZScorchAudioProcessorEditor::updateBandSelectorState()
{
    if (auto* param = audioProcessor.getValueTreeState().getParameter ("bandCount"))
    {
        const int idx = juce::roundToInt (param->getValue() * 3.0f);
        bandSelector.setSelected (idx);
    }
}

//==============================================================================
void ZScorchAudioProcessorEditor::rebindBandAttachments (int band)
{
    static const char* suffixes[] = { "Low", "LoMid", "HiMid", "High" };
    if (band < 0 || band > 3)
        band = 0;
    const juce::String suffix (suffixes[band]);
    auto& vts = audioProcessor.getValueTreeState();

    satDriveAttachment = nullptr;
    satModeAttachment = nullptr;
    satHarmReductionAttachment = nullptr;

    satDriveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        vts, "satDrive" + suffix, satDriveKnob);
    satModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        vts, "satMode" + suffix, satModeCombo);
    satHarmReductionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        vts, "satHarmRed" + suffix, satHarmReductionKnob);
}

//==============================================================================
void ZScorchAudioProcessorEditor::refreshLicenseDisplay()
{
    auto info = ZLicenseManager::getInstance().getStatusInfo();
    switch (info.state)
    {
        case Zarlino::Licensing::LicenseState::Licensed:
            licenseLabel.setText ("Licensed", juce::dontSendNotification);
            break;
        case Zarlino::Licensing::LicenseState::Trial:
            licenseLabel.setText ("Trial " + juce::String (info.daysRemaining) + "d", juce::dontSendNotification);
            break;
        case Zarlino::Licensing::LicenseState::TrialExpired:
            licenseLabel.setText ("Trial Expired", juce::dontSendNotification);
            break;
        default:
            licenseLabel.setText ("Unlicensed", juce::dontSendNotification);
            break;
    }
}

//==============================================================================
void ZScorchAudioProcessorEditor::updatePresetComboBox()
{
    const int idx = audioProcessor.getCurrentPresetIndex();
    if (presetComboBox.getSelectedId() - 1 != idx)
        presetComboBox.setSelectedId (idx + 1, juce::dontSendNotification);
}

//==============================================================================
void ZScorchAudioProcessorEditor::createAttachments()
{
    auto& vts = audioProcessor.getValueTreeState();

    liftAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        vts, "lift", liftKnob);
    characterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        vts, "character", characterKnob);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        vts, "mix", mixKnob);

    tiltAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        vts, "tiltAmount", tiltAmountKnob);
    exciterAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        vts, "exciterAmount", exciterAmountKnob);
    exciterEnabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        vts, "exciterEnabled", exciterEnabledToggle);

    static const char* bandSuffixes[] = { "Low", "LoMid", "HiMid", "High" };
    for (int i = 0; i < 4; ++i)
        bandGainAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            vts, "bandGain" + juce::String (bandSuffixes[i]), bandGainSliders[i]);

    static const char* eqIds[] = { "LowShelfFreq", "LowShelfGain", "BellFreq", "BellGain",
                                   "BellQ", "HighShelfFreq", "HighShelfGain" };
    for (int i = 0; i < 7; ++i)
    {
        preEqAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            vts, "preEq" + juce::String (eqIds[i]), preEqKnobs[i]);
        postEqAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            vts, "postEq" + juce::String (eqIds[i]), postEqKnobs[i]);
    }

    outputCeilingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        vts, "outputCeiling", outputCeilingSlider);
    oversample4xAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        vts, "oversample4x", oversample4xToggle);
}

//==============================================================================
void ZScorchAudioProcessorEditor::setupZKnob (ZarlinoKnob& knob, const juce::String& id,
                                              const juce::String& label, double min, double max,
                                              double interval, double def, const juce::String& suffix)
{
    knob.setRange (min, max, interval);
    knob.setDefaultValue (def);
    knob.setValue (def, juce::dontSendNotification);
    knob.setTextValueSuffix (suffix);
    knob.setTextFromValue ([suffix] (double v) -> juce::String
    {
        if (suffix == "Hz") return juce::String (juce::roundToInt (v)) + " Hz";
        if (suffix == "dB") return juce::String (v, 1) + " dB";
        if (suffix == "Q")  return juce::String (v, 2);
        if (suffix == "ch") return juce::String (juce::roundToInt (v * 100.0)) + "%";
        return juce::String (juce::roundToInt (v * 100.0)) + "%";
    });
    addAndMakeVisible (knob);
    describeControl (knob, label, "Automates " + id);
}

//==============================================================================
void ZScorchAudioProcessorEditor::setupZSlider (ZarlinoSlider& slider, const juce::String& id,
                                                const juce::String& label, double min, double max,
                                                double interval, double def, const juce::String& suffix)
{
    slider.setRange (min, max, interval);
    slider.setDefaultValue (def);
    slider.setValue (def, juce::dontSendNotification);
    slider.setTextValueSuffix (suffix);
    slider.textFromValueFunction = [suffix] (double v) -> juce::String
    {
        if (suffix == "dB") return juce::String (v, 1) + " dB";
        return juce::String (v, 1);
    };
    addAndMakeVisible (slider);
    describeControl (slider, label, "Automates " + id);
}

//==============================================================================
void ZScorchAudioProcessorEditor::setupBandSelector()
{
    bandSelector.addTab ("1", 1);
    bandSelector.addTab ("2", 2);
    bandSelector.addTab ("3", 3);
    bandSelector.addTab ("4", 4);
    bandSelector.onTabSelected = [this] (int id)
    {
        if (auto* param = audioProcessor.getValueTreeState().getParameter ("bandCount"))
            param->setValueNotifyingHost (param->getNormalisableRange().convertTo0to1 ((float) (id - 1)));
    };
    addAndMakeVisible (bandSelector);
}

//==============================================================================
void ZScorchAudioProcessorEditor::setupZoneSelector()
{
    zoneSelector.addTab ("Saturator", 0);
    zoneSelector.addTab ("EQ", 1);
    zoneSelector.addTab ("Output", 2);
    zoneSelector.onTabSelected = [this] (int id)
    {
        currentZone = id;
        setZoneVisibility (id);
    };
    zoneSelector.setSelected (0);
    addAndMakeVisible (zoneSelector);
}

//==============================================================================
void ZScorchAudioProcessorEditor::setupSatModeCombo()
{
    satModeCombo.addItemList ({ "Tube", "Tape", "Transistor", "Germanium", "Diode", "Wavefold" }, 1);
    satModeCombo.setColour (juce::ComboBox::outlineColourId, juce::Colour (Zarlino::Palette::graphite_700));
    addAndMakeVisible (satModeCombo);
}

//==============================================================================
void ZScorchAudioProcessorEditor::describeControl (juce::Component& comp,
                                                   const juce::String& title,
                                                   const juce::String& description)
{
    comp.setTitle (title);
    comp.setDescription (description);

    // JUCE 8: Component no longer derives from TooltipClient; only controls
    // that implement SettableTooltipClient (e.g. Button/ComboBox) receive a tooltip.
    if (auto* tooltipClient = dynamic_cast<juce::SettableTooltipClient*> (&comp))
        tooltipClient->setTooltip (title + (description.isEmpty() ? juce::String()
                                                                  : juce::String (" \xe2\x80\x94 ") + description));
}

//==============================================================================
void ZScorchAudioProcessorEditor::drawSectionHeader (juce::Graphics& g, const juce::String& text,
                                                     const juce::Rectangle<int>& area)
{
    using namespace Zarlino;
    g.setFont (juce::FontOptions (Typography::size_body, juce::Font::bold));
    g.setColour (juce::Colour (Palette::steel_300));
    g.drawText (text, area, juce::Justification::centredLeft);
}

//==============================================================================
void ZScorchAudioProcessorEditor::drawKnobLabel (juce::Graphics& g, juce::Component& comp,
                                                 const juce::String& text)
{
    using namespace Zarlino;
    if (!comp.isVisible())
        return;
    g.setFont (juce::FontOptions (Typography::size_caption, juce::Font::bold));
    g.setColour (juce::Colour (Palette::steel_300));
    g.drawText (text, comp.getBounds().removeFromBottom (14), juce::Justification::centred);
}

//==============================================================================
void ZScorchAudioProcessorEditor::drawActivityPanel (juce::Graphics& g)
{
    using namespace Zarlino;
    if (activityPanelRect.getWidth() <= 0)
        return;

    // Panel surface
    g.setColour (juce::Colour (Palette::obsidian_900));
    g.fillRoundedRectangle (activityPanelRect.toFloat(), Radius::standard);
    g.setColour (juce::Colour (Palette::graphite_700).withAlpha (0.8f));
    g.drawRoundedRectangle (activityPanelRect.toFloat().reduced (0.5f), Radius::standard, 1.0f);

    // Section header — the display's purpose is harmonic activity, not a
    // generic spectrum analyser.
    g.setFont (juce::FontOptions (Typography::size_caption, juce::Font::bold));
    g.setColour (juce::Colour (Palette::mist_100));
    g.drawText ("HARMONIC ACTIVITY",
                juce::Rectangle<int> (activityPanelRect.getX() + 14, activityPanelRect.getY() + 5, 190, 16),
                juce::Justification::centredLeft);

    // Technical detail on the right of the header
    g.setColour (juce::Colour (Palette::steel_300));
    g.drawText (juce::String (audioProcessor.getActiveNumBands()) + " BAND",
                juce::Rectangle<int> (activityPanelRect.getRight() - 88, activityPanelRect.getY() + 5, 74, 16),
                juce::Justification::centredRight);

    // Hairline under the header separates the title from the analysis well
    g.setColour (juce::Colour (Palette::graphite_700).withAlpha (0.5f));
    g.drawHorizontalLine (activityPanelRect.getY() + 24,
                          (float) activityPanelRect.getX() + 12,
                          (float) activityPanelRect.getRight() - 12);
}

//==============================================================================
void ZScorchAudioProcessorEditor::drawMacroCluster (juce::Graphics& g)
{
    using namespace Zarlino;
    if (macroClusterRect.getWidth() <= 0)
        return;

    // Shared surface — one tonal decision, one module
    g.setColour (juce::Colour (Palette::obsidian_900));
    g.fillRoundedRectangle (macroClusterRect.toFloat(), Radius::standard);
    g.setColour (juce::Colour (Palette::graphite_700).withAlpha (0.8f));
    g.drawRoundedRectangle (macroClusterRect.toFloat().reduced (0.5f), Radius::standard, 1.0f);

    // Subtle T-junction guide ties LIFT to CHARACTER / MIX into one cluster so
    // the three knobs read as a single control group, not three floats.
    const float cx    = (float) macroClusterRect.getCentreX();
    const float topY  = (float) liftKnob.getBottom() + 6.0f;
    const float baseY = (float) characterKnob.getBounds().getCentreY();
    g.setColour (juce::Colour (Palette::graphite_700).withAlpha (0.55f));
    g.drawLine (cx, topY, cx, baseY, 1.0f);
    g.drawLine ((float) characterKnob.getBounds().getCentreX(), baseY,
                (float) mixKnob.getBounds().getCentreX(), baseY, 1.0f);
}

//==============================================================================
void ZScorchAudioProcessorEditor::drawAdvancedMacroPanel (juce::Graphics& g)
{
    using namespace Zarlino;
    if (advancedMacroPanelRect.getWidth() <= 0)
        return;

    // Same module surface as the simple-view cluster — the macro row in the
    // advanced view belongs to the same tonal decision.
    g.setColour (juce::Colour (Palette::obsidian_900));
    g.fillRoundedRectangle (advancedMacroPanelRect.toFloat(), Radius::standard);
    g.setColour (juce::Colour (Palette::graphite_700).withAlpha (0.8f));
    g.drawRoundedRectangle (advancedMacroPanelRect.toFloat().reduced (0.5f), Radius::standard, 1.0f);
}

//==============================================================================
void ZScorchAudioProcessorEditor::drawZonePanel (juce::Graphics& g)
{
    using namespace Zarlino;
    if (zoneContentRect.getWidth() <= 0)
        return;

    // Module surface behind the active zone's controls so the zone reads as a
    // defined processing module rather than loose controls on the canvas.
    const auto pr = zoneContentRect.toFloat().reduced (2.0f);
    g.setColour (juce::Colour (Palette::obsidian_900));
    g.fillRoundedRectangle (pr, Radius::standard);
    g.setColour (juce::Colour (Palette::graphite_700).withAlpha (0.8f));
    g.drawRoundedRectangle (pr.reduced (0.5f), Radius::standard, 1.0f);
}

//==============================================================================
void ZScorchAudioProcessorEditor::drawSimpleFooterStatus (juce::Graphics& g,
                                                          juce::Rectangle<int> footerRect)
{
    using namespace Zarlino;
    if (footerRect.getWidth() <= 0)
        return;

    // Live macro readouts
    g.setFont (juce::FontOptions (Typography::size_caption, juce::Font::bold));
    g.setColour (juce::Colour (Palette::steel_300));
    const juce::String readouts = juce::String::formatted (
        "LIFT %d%%    CHARACTER %+d%%    MIX %d%%",
        juce::roundToInt (liftKnob.getValue() * 100.0),
        juce::roundToInt (characterKnob.getValue() * 100.0),
        juce::roundToInt (mixKnob.getValue() * 100.0));
    g.drawText (readouts, footerRect.removeFromLeft (footerRect.getWidth() - 230),
                juce::Justification::centredLeft);

    // Mode / status — rust accent carries ZScorch's Print Series identity
    juce::String status = getActiveModeName().toUpperCase();
    status += "  \xc2\xb7  " + juce::String (audioProcessor.getActiveNumBands()) + " BAND";
    if (isOversamplingEnabled())
        status += "  \xc2\xb7  4\xc3\x97";
    g.setColour (juce::Colour (Palette::cat_print));
    g.drawText (status, footerRect, juce::Justification::centredRight);
}

//==============================================================================
juce::String ZScorchAudioProcessorEditor::getActiveModeName() const
{
    static const char* suffixes[] = { "Low", "LoMid", "HiMid", "High" };
    static const char* modes[]    = { "Tube", "Tape", "Transistor",
                                      "Germanium", "Diode", "Wavefold" };
    const int band = juce::jlimit (0, 3, currentBand);
    if (auto* param = audioProcessor.getValueTreeState().getParameter (
            "satMode" + juce::String (suffixes[band])))
    {
        const int idx = juce::jlimit (0, 5, juce::roundToInt (param->getValue() * 5.0f));
        return juce::String (modes[idx]);
    }
    return "Tube";
}

//==============================================================================
bool ZScorchAudioProcessorEditor::isOversamplingEnabled() const
{
    if (auto* param = audioProcessor.getValueTreeState().getParameter ("oversample4x"))
        return param->getValue() >= 0.5f;
    return false;
}

//==============================================================================
void ZScorchAudioProcessorEditor::timerCallback()
{
    // Spectrum data (copy is made inside pushData)
    const auto& mag = audioProcessor.getMagnitudeData();
    if (!mag.empty())
        spectrum.pushData (mag.data(), (int) mag.size(), (float) audioProcessor.getHostSampleRate());
    spectrum.setActiveNumBands (audioProcessor.getActiveNumBands());
    spectrum.setShowBandShading (true);

    // Meters
    inputMeter.setLevel (audioProcessor.inputLevel.load (std::memory_order_relaxed));
    outputMeter.setLevel (audioProcessor.outputLevel.load (std::memory_order_relaxed));
    activityMeter.setLevel (audioProcessor.meterLevel.load (std::memory_order_relaxed));
    inputMeter.update();
    outputMeter.update();
    activityMeter.update();

    // Sync selectors from parameters
    updateBandSelectorState();
    updatePresetComboBox();

    // Rebuild band edit tabs when the band count changes
    const int activeBands = audioProcessor.getActiveNumBands();
    if (activeBands != lastActiveBands)
    {
        lastActiveBands = activeBands;
        currentBand = juce::jmin (currentBand, activeBands - 1);

        bandEditSelector.clearTabs();
        static const char* names[] = { "LOW", "LO-MID", "HI-MID", "HIGH" };
        for (int i = 0; i < activeBands; ++i)
            bandEditSelector.addTab (names[i], i);
        bandEditSelector.setSelected (currentBand);
        rebindBandAttachments (currentBand);
    }

    // Sync sat-mode combo from the active band's parameter
    static const char* suffixes[] = { "Low", "LoMid", "HiMid", "High" };
    if (auto* param = audioProcessor.getValueTreeState().getParameter ("satMode" + juce::String (suffixes[currentBand])))
    {
        const int idx = juce::roundToInt (param->getValue() * 5.0f);
        if (satModeCombo.getSelectedId() - 1 != idx)
            satModeCombo.setSelectedId (idx + 1, juce::dontSendNotification);
    }

    // Bypass state
    bypassButton.setToggleState (audioProcessor.bypassEnabled.load (std::memory_order_relaxed),
                                 juce::dontSendNotification);

    // License display refresh every ~30 frames
    if (++frameCount % 30 == 0)
        refreshLicenseDisplay();

    repaint();
}
