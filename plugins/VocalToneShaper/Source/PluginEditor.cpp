#include "PluginEditor.h"

namespace
{
    const auto bg = juce::Colour (0xff131417);
    const auto panel = juce::Colour (0xff1c1e22);
    const auto panelBright = juce::Colour (0xff272a30);
    const auto text = juce::Colour (0xffe8e8ea);
    const auto muted = juce::Colour (0xff9b9da3);

    juce::Colour accentForStage (int stage)
    {
        static const std::array<juce::Colour, 6> colours {
            juce::Colour (0xff3b82c4), juce::Colour (0xffd9822b), juce::Colour (0xff8b5cf6),
            juce::Colour (0xffc4453b), juce::Colour (0xff5ed4d4), juce::Colour (0xffd9457a)
        };

        return colours[static_cast<size_t> (juce::jlimit (0, 5, stage))];
    }

    struct StageSpec
    {
        const char* code;
        const char* title;
        int width;
    };

    constexpr std::array<StageSpec, 6> stages {{
        { "A", "TONE EQ", 180 },
        { "B", "PEAK TAMER", 160 },
        { "C", "MULTIBAND", 280 },
        { "D", "MAIN LEVELER", 180 },
        { "E", "AIR", 150 },
        { "F", "DE-ESS", 170 }
    }};

    constexpr auto learnedMakeupSliderIndex = 30;

    template <typename Vector>
    bool containsIndex (const Vector& vector, int index)
    {
        return index >= 0 && static_cast<size_t> (index) < vector.size();
    }

    juce::Rectangle<int> headerArea (juce::Rectangle<int> bounds) { return bounds.removeFromTop (72); }
    juce::Rectangle<int> footerArea (juce::Rectangle<int> bounds) { return bounds.removeFromBottom (76); }

    std::array<juce::Rectangle<int>, 6> stageAreas (juce::Rectangle<int> bounds)
    {
        bounds.removeFromTop (72);
        bounds.removeFromBottom (76);
        auto spine = bounds.reduced (18, 12);
        const auto totalNominal = 1120;
        const auto gap = 8;
        const auto usable = spine.getWidth() - gap * 5;
        const auto scale = static_cast<float> (usable) / static_cast<float> (totalNominal);

        std::array<juce::Rectangle<int>, 6> areas;
        for (size_t i = 0; i < stages.size(); ++i)
        {
            const auto width = juce::jmax (86, static_cast<int> (std::round (stages[i].width * scale)));
            areas[i] = spine.removeFromLeft (width);
            spine.removeFromLeft (gap);
        }

        return areas;
    }

    void styleSlider (juce::Slider& slider, juce::Colour accent)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 20);
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff4b5260));
        slider.setColour (juce::Slider::thumbColourId, text);
        slider.setColour (juce::Slider::textBoxTextColourId, text);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff111317));
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff3b414c));
    }
}

void VocalToneShaperAudioProcessorEditor::ImmersiveLookAndFeel::drawRotarySlider (
    juce::Graphics& g, int x, int y, int width, int height, float sliderPosProportional,
    float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                                static_cast<float> (width), static_cast<float> (height))
                          .reduced (5.0f);
    const auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight() - 18.0f);
    const auto knob = juce::Rectangle<float> (diameter, diameter).withCentre (bounds.getCentre().translated (0.0f, -5.0f));
    const auto radius = knob.getWidth() * 0.5f;
    const auto centre = knob.getCentre();
    const auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff343a44), centre.x - radius, centre.y - radius,
                                             juce::Colour (0xff111317), centre.x + radius, centre.y + radius, false));
    g.fillEllipse (knob);
    g.setColour (juce::Colour (0xff626a78));
    g.drawEllipse (knob, 1.4f);

    juce::Path backgroundArc;
    backgroundArc.addCentredArc (centre.x, centre.y, radius - 5.0f, radius - 5.0f, 0.0f,
                                 rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (juce::Colour (0xff242933));
    g.strokePath (backgroundArc, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path valueArc;
    valueArc.addCentredArc (centre.x, centre.y, radius - 5.0f, radius - 5.0f, 0.0f,
                            rotaryStartAngle, angle, true);
    g.setColour (accent);
    g.strokePath (valueArc, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto pointerLength = radius - 10.0f;
    const auto pointerThickness = 3.0f;
    juce::Path pointer;
    pointer.addRoundedRectangle (-pointerThickness * 0.5f, -pointerLength, pointerThickness, pointerLength * 0.58f, 1.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    g.setColour (juce::Colours::white.withAlpha (0.92f));
    g.fillPath (pointer);

    g.setColour (accent.withAlpha (0.28f));
    g.fillEllipse (knob.reduced (radius * 0.58f));
}

void VocalToneShaperAudioProcessorEditor::ImmersiveLookAndFeel::drawToggleButton (
    juce::Graphics& g, juce::ToggleButton& button, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const auto active = button.getToggleState();
    const auto accent = active ? juce::Colour (0xff5ed4d4) : juce::Colour (0xff4b5260);

    g.setColour (active ? juce::Colour (0xff12383b) : juce::Colour (0xff20242b));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (shouldDrawButtonAsDown ? accent.brighter (0.3f) : accent);
    g.drawRoundedRectangle (bounds, 4.0f, shouldDrawButtonAsHighlighted ? 2.0f : 1.2f);

    auto lamp = bounds.removeFromLeft (16.0f).reduced (4.0f);
    g.setColour (active ? juce::Colour (0xff5ed4d4) : juce::Colour (0xff555c68));
    g.fillEllipse (lamp);

    g.setColour (juce::Colour (0xfff4f6f8));
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().withTrimmedLeft (18),
                      juce::Justification::centredLeft, 1);
}

void VocalToneShaperAudioProcessorEditor::ImmersiveLookAndFeel::drawComboBox (
    juce::Graphics& g, int width, int height, bool isButtonDown, int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height)).reduced (1.0f);
    g.setColour (isButtonDown ? juce::Colour (0xff303744) : juce::Colour (0xff20252e));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (juce::Colour (0xff5ed4d4).withAlpha (0.75f));
    g.drawRoundedRectangle (bounds, 4.0f, 1.2f);

    juce::Path arrow;
    const auto cx = static_cast<float> (width - 14);
    const auto cy = static_cast<float> (height) * 0.5f;
    arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour (box.findColour (juce::ComboBox::textColourId));
    g.fillPath (arrow);
}

VocalToneShaperAudioProcessorEditor::VocalToneShaperAudioProcessorEditor (VocalToneShaperAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setResizable (true, true);
    setLookAndFeel (&immersiveLookAndFeel);
    setResizeLimits (1180, 620, 1920, 920);
    setSize (1500, 720);

    addChoice ("Routing", "global.routing.eq_comp_order");
    addChoice ("C Phase", "stageC.phase_mode");
    addChoice ("FET Ratio", "stageB.ratio");
    addChoice ("Leveler", "stageD.mode");
    addChoice ("De-ess", "stageF.mode");

    addToggle ("A", "stageA.bypass");
    addToggle ("B", "stageB.bypass");
    addToggle ("C", "stageC.bypass");
    addToggle ("D", "stageD.bypass");
    addToggle ("E", "stageE.bypass");
    addToggle ("F", "stageF.bypass");
    addToggle ("HPF", "stageA.hpf.enabled");
    addToggle ("Lin EQ", "global.linear_phase_eq");
    addToggle ("SC Listen", "stageF.listen_sidechain");

    addSlider ("Input", "global.input_trim");
    addSlider ("Output", "global.output_trim");
    addSlider ("Ceiling", "global.output_ceiling");
    addSlider ("A Mix", "stageA.mix");
    addSlider ("HPF", "stageA.hpf.freq_hz");
    addSlider ("Mud", "stageA.band_low_mud.gain_db");
    addSlider ("Mood", "stageA.band_mood.gain_db");
    addSlider ("Box", "stageA.band_presence_cut.gain_db");
    addSlider ("Air", "stageA.air_shelf.gain_db");
    addSlider ("Drive", "stageA.drive");
    addSlider ("B Mix", "stageB.mix");
    addSlider ("Input", "stageB.input_gain_db");
    addSlider ("Output", "stageB.output_gain_db");
    addSlider ("Attack", "stageB.attack_ms");
    addSlider ("Release", "stageB.release_ms");
    addSlider ("C Mix", "stageC.mix");
    addSlider ("Low", "stageC.band2.threshold_db");
    addSlider ("Low-Mid", "stageC.band3.threshold_db");
    addSlider ("Mid", "stageC.band4.threshold_db");
    addSlider ("Hi-Mid", "stageC.band5.threshold_db");
    addSlider ("High", "stageC.band6.threshold_db");
    addSlider ("D Mix", "stageD.mix");
    addSlider ("Peak", "stageD.peak_reduction");
    addSlider ("Gain", "stageD.gain_makeup_db");
    addSlider ("E Mix", "stageE.mix");
    addSlider ("Mid Air", "stageE.mid_air_amount");
    addSlider ("High Air", "stageE.high_air_amount");
    addSlider ("Thresh", "stageF.threshold_db");
    addSlider ("Freq", "stageF.frequency_hz");
    addSlider ("Range", "stageF.range_db");
    addSlider ("Learn", "global.auto_gain.learned_makeup_db");

    for (size_t i = 0; i < sliders.size(); ++i)
    {
        auto stage = 0;
        if (i >= 10 && i <= 14) stage = 1;
        else if (i >= 15 && i <= 20) stage = 2;
        else if (i >= 21 && i <= 23) stage = 3;
        else if (i >= 24 && i <= 26) stage = 4;
        else if (i >= 27 && i <= 29) stage = 5;
        styleSlider (sliders[i]->slider, accentForStage (stage));
    }

    meterLabel.setJustificationType (juce::Justification::centredRight);
    meterLabel.setColour (juce::Label::textColourId, text);
    meterLabel.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::plain)));
    addAndMakeVisible (meterLabel);

    startTimerHz (24);
}

VocalToneShaperAudioProcessorEditor::~VocalToneShaperAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

VocalToneShaperAudioProcessorEditor::SliderControl& VocalToneShaperAudioProcessorEditor::addSlider (const juce::String& name, const juce::String& id)
{
    auto control = std::make_unique<SliderControl>();
    control->label.setText (name, juce::dontSendNotification);
    control->label.setJustificationType (juce::Justification::centred);
    control->label.setColour (juce::Label::textColourId, text);
    control->label.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    styleSlider (control->slider, accentForStage (0));
    addAndMakeVisible (control->label);
    addAndMakeVisible (control->slider);
    control->attachment = std::make_unique<SliderAttachment> (processor.getValueTreeState(), id, control->slider);
    sliders.push_back (std::move (control));
    return *sliders.back();
}

VocalToneShaperAudioProcessorEditor::ToggleControl& VocalToneShaperAudioProcessorEditor::addToggle (const juce::String& name, const juce::String& id)
{
    auto control = std::make_unique<ToggleControl>();
    control->button.setButtonText (name);
    control->button.setColour (juce::ToggleButton::textColourId, text);
    control->button.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xff5ed4d4));
    addAndMakeVisible (control->button);
    control->attachment = std::make_unique<ButtonAttachment> (processor.getValueTreeState(), id, control->button);
    toggles.push_back (std::move (control));
    return *toggles.back();
}

VocalToneShaperAudioProcessorEditor::ChoiceControl& VocalToneShaperAudioProcessorEditor::addChoice (const juce::String& name, const juce::String& id)
{
    auto control = std::make_unique<ChoiceControl>();
    control->label.setText (name, juce::dontSendNotification);
    control->label.setJustificationType (juce::Justification::centredLeft);
    control->label.setColour (juce::Label::textColourId, muted);
    control->label.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::plain)));
    control->box.setColour (juce::ComboBox::backgroundColourId, panelBright);
    control->box.setColour (juce::ComboBox::textColourId, text);
    control->box.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3a3d44));
    addAndMakeVisible (control->label);
    addAndMakeVisible (control->box);
    control->attachment = std::make_unique<ComboAttachment> (processor.getValueTreeState(), id, control->box);
    choices.push_back (std::move (control));
    return *choices.back();
}

void VocalToneShaperAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    auto bounds = getLocalBounds();

    auto header = headerArea (bounds).reduced (18, 8);
    g.setColour (text);
    g.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    g.drawText ("Vocal Tone Shaper", header.removeFromLeft (310), juce::Justification::centredLeft);
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::plain)));
    g.setColour (muted);
    g.drawText ("six-stage vocal channel strip", header.removeFromLeft (210), juce::Justification::centredLeft);

    auto areas = stageAreas (getLocalBounds());
    for (size_t i = 0; i < areas.size(); ++i)
    {
        const auto area = areas[i].toFloat();
        const auto accent = accentForStage (static_cast<int> (i));
        g.setColour (panel);
        g.fillRoundedRectangle (area, 7.0f);
        g.setColour (juce::Colour (0xff353840));
        g.drawRoundedRectangle (area, 7.0f, 1.0f);

        auto top = areas[i].reduced (10, 8).removeFromTop (34);
        g.setColour (accent);
        g.fillEllipse (static_cast<float> (top.getX()), static_cast<float> (top.getY() + 7), 10.0f, 10.0f);
        g.fillRoundedRectangle (static_cast<float> (top.getX()), static_cast<float> (top.getBottom() - 3),
                                static_cast<float> (top.getWidth()), 3.0f, 1.5f);
        g.setColour (text);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText (juce::String (stages[i].code) + "  " + stages[i].title, top.withTrimmedLeft (18), juce::Justification::centredLeft);

        if (i < areas.size() - 1)
        {
            auto connector = juce::Rectangle<float> (area.getRight() + 1.0f, area.getCentreY() - 1.0f, 7.0f, 2.0f);
            g.setColour (accent.withAlpha (0.6f));
            g.fillRoundedRectangle (connector, 1.0f);
        }
    }

    auto footer = footerArea (getLocalBounds()).reduced (18, 6).toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (footer, 6.0f);
}

void VocalToneShaperAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    auto header = headerArea (bounds).reduced (18, 8);
    header.removeFromLeft (540);

    for (int i = 1; i < static_cast<int> (choices.size()); ++i)
    {
        auto cell = header.removeFromLeft (120).reduced (4, 0);
        choices[static_cast<size_t> (i)]->label.setBounds (cell.removeFromTop (18));
        choices[static_cast<size_t> (i)]->box.setBounds (cell);
    }

    meterLabel.setBounds (header.reduced (4, 0));

    auto footer = footerArea (getLocalBounds()).reduced (24, 8);
    for (int i = 0; i < 3; ++i)
    {
        if (! containsIndex (sliders, i))
            continue;

        auto cell = footer.removeFromLeft (96).reduced (2, 0);
        sliders[static_cast<size_t> (i)]->label.setBounds (cell.removeFromTop (18));
        sliders[static_cast<size_t> (i)]->slider.setBounds (cell);
    }

    if (! choices.empty())
    {
        footer.removeFromLeft (14);
        auto routing = footer.removeFromLeft (160).reduced (2, 0);
        choices[0]->label.setBounds (routing.removeFromTop (18));
        choices[0]->box.setBounds (routing);
    }

    if (containsIndex (sliders, learnedMakeupSliderIndex))
    {
        auto learn = footer.removeFromLeft (96).reduced (2, 0);
        sliders[static_cast<size_t> (learnedMakeupSliderIndex)]->label.setBounds (learn.removeFromTop (18));
        sliders[static_cast<size_t> (learnedMakeupSliderIndex)]->slider.setBounds (learn);
    }

    auto areas = stageAreas (getLocalBounds());
    const std::array<std::pair<int, int>, 6> sliderRanges {{
        { 3, 9 }, { 10, 14 }, { 15, 20 }, { 21, 23 }, { 24, 26 }, { 27, 29 }
    }};

    for (size_t stage = 0; stage < areas.size(); ++stage)
    {
        auto area = areas[stage].reduced (10, 8);
        auto top = area.removeFromTop (38);
        if (stage < toggles.size())
            toggles[stage]->button.setBounds (top.removeFromRight (44));

        if (stage == 0 && containsIndex (toggles, 6) && containsIndex (toggles, 7))
        {
            toggles[6]->button.setBounds (area.removeFromTop (24).removeFromLeft (64));
            toggles[7]->button.setBounds (area.removeFromTop (24).removeFromLeft (80));
        }
        else if (stage == 5 && containsIndex (toggles, 8))
        {
            toggles[8]->button.setBounds (area.removeFromTop (24).removeFromLeft (98));
        }

        const auto [start, end] = sliderRanges[stage];
        const auto count = end - start + 1;
        const auto columns = stage == 2 ? 3 : (areas[stage].getWidth() > 170 ? 2 : 1);
        const auto cellW = area.getWidth() / columns;
        const auto cellH = juce::jmax (92, area.getHeight() / juce::jmax (1, (count + columns - 1) / columns));

        for (int index = start; index <= end; ++index)
        {
            if (! containsIndex (sliders, index))
                continue;

            const auto local = index - start;
            const auto row = local / columns;
            const auto col = local % columns;
            auto cell = juce::Rectangle<int> (area.getX() + col * cellW, area.getY() + row * cellH, cellW, cellH).reduced (3);
            sliders[static_cast<size_t> (index)]->label.setBounds (cell.removeFromTop (18));
            sliders[static_cast<size_t> (index)]->slider.setBounds (cell);
        }
    }
}

void VocalToneShaperAudioProcessorEditor::timerCallback()
{
    meterLabel.setText ("In " + juce::String (processor.getInputPeakDb(), 1)
                        + " dBFS   GR B " + juce::String (processor.getStageBReductionDb(), 1)
                        + "  D " + juce::String (processor.getStageDReductionDb(), 1)
                        + "  F " + juce::String (processor.getStageFReductionDb(), 1)
                        + " dB   Out " + juce::String (processor.getOutputPeakDb(), 1)
                        + " dBFS   Lat " + juce::String (processor.getReportedLatencySamples()) + " smp",
                        juce::dontSendNotification);
}
