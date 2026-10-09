#include "PluginEditor.h"

namespace
{
    const auto ink = juce::Colour (0xfff2ece2);
    const auto dimInk = juce::Colour (0xffb8ada1);
    const auto night = juce::Colour (0xff121316);
    const auto floorTone = juce::Colour (0xff3a3028);

    void styleMacro (juce::Slider& slider, juce::Colour accent)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 20);
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3f444a));
        slider.setColour (juce::Slider::thumbColourId, ink);
        slider.setColour (juce::Slider::textBoxTextColourId, ink);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff191b1e));
        slider.setColour (juce::Slider::textBoxOutlineColourId, accent.withAlpha (0.5f));
    }

    float dbToMeter (float db)
    {
        return juce::jlimit (0.0f, 1.0f, juce::jmap (db, -48.0f, 0.0f, 0.0f, 1.0f));
    }
}

void ZVocalAudioProcessorEditor::ImmersiveLookAndFeel::drawRotarySlider (
    juce::Graphics& g, int x, int y, int width, int height, float pos,
    float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                          static_cast<float> (width), static_cast<float> (height)).reduced (6.0f);
    const auto dial = bounds.removeFromTop (juce::jmin (bounds.getWidth(), bounds.getHeight() - 18.0f))
                            .withSizeKeepingCentre (juce::jmin (bounds.getWidth(), bounds.getHeight()),
                                                    juce::jmin (bounds.getWidth(), bounds.getHeight()));
    const auto centre = dial.getCentre();
    const auto radius = dial.getWidth() * 0.5f;
    const auto angle = rotaryStartAngle + pos * (rotaryEndAngle - rotaryStartAngle);
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);

    g.setGradientFill ({ juce::Colour (0xff46413b), centre.x - radius, centre.y - radius,
                         juce::Colour (0xff131416), centre.x + radius, centre.y + radius, false });
    g.fillEllipse (dial);
    g.setColour (juce::Colour (0xff756b61));
    g.drawEllipse (dial, 1.3f);

    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, radius - 7.0f, radius - 7.0f, 0.0f,
                       rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (juce::Colour (0xff27292d));
    g.strokePath (arc, { 6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius - 7.0f, radius - 7.0f, 0.0f,
                         rotaryStartAngle, angle, true);
    g.setColour (accent);
    g.strokePath (value, { 6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -radius + 11.0f, 3.0f, radius * 0.48f, 1.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    g.setColour (ink);
    g.fillPath (pointer);
}

ZVocalAudioProcessorEditor::ZVocalAudioProcessorEditor (ZVocalAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setResizable (true, true);
    setResizeLimits (960, 610, 1680, 940);
    setSize (1180, 700);
    setLookAndFeel (&immersiveLookAndFeel);

    title.setText ("ZVocal", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (25.0f, juce::Font::bold)));
    title.setColour (juce::Label::textColourId, ink);
    addAndMakeVisible (title);

    meterLabel.setJustificationType (juce::Justification::centredRight);
    meterLabel.setColour (juce::Label::textColourId, dimInk);
    meterLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    addAndMakeVisible (meterLabel);

    addAndMakeVisible (room);

    addMacro ("Colour", "macro.colour", juce::Colour (0xffdfb45d));
    addMacro ("Texture", "macro.texture", juce::Colour (0xffc8715c));
    addMacro ("Thickness", "macro.thickness", juce::Colour (0xff8fbf82));
    addMacro ("Punch", "macro.punch", juce::Colour (0xff71a9d6));
    addMacro ("Focus", "macro.focus", juce::Colour (0xffd785b8));

    startTimerHz (24);
}

ZVocalAudioProcessorEditor::~ZVocalAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

ZVocalAudioProcessorEditor::MacroControl& ZVocalAudioProcessorEditor::addMacro (
    const juce::String& name, const juce::String& id, juce::Colour accent)
{
    auto macro = std::make_unique<MacroControl>();
    macro->label.setText (name, juce::dontSendNotification);
    macro->label.setJustificationType (juce::Justification::centred);
    macro->label.setColour (juce::Label::textColourId, ink);
    macro->label.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    styleMacro (macro->slider, accent);
    addAndMakeVisible (macro->label);
    addAndMakeVisible (macro->slider);
    macro->attachment = std::make_unique<SliderAttachment> (processor.getValueTreeState(), id, macro->slider);
    macros.push_back (std::move (macro));
    return *macros.back();
}

void ZVocalAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (night);
    auto bounds = getLocalBounds().toFloat();
    g.setGradientFill ({ juce::Colour (0xff1f2025), bounds.getCentreX(), 0.0f,
                         juce::Colour (0xff0f1012), bounds.getCentreX(), bounds.getBottom(), false });
    g.fillRect (bounds);

    auto floorBand = bounds.removeFromBottom (130.0f);
    g.setGradientFill ({ floorTone.brighter (0.12f), floorBand.getCentreX(), floorBand.getY(),
                         juce::Colour (0xff171411), floorBand.getCentreX(), floorBand.getBottom(), false });
    g.fillRect (floorBand);

    g.setColour (juce::Colour (0x55d9a441));
    g.drawLine (38.0f, 74.0f, static_cast<float> (getWidth() - 38), 74.0f, 1.0f);
}

void ZVocalAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (22, 16);
    auto header = bounds.removeFromTop (48);
    title.setBounds (header.removeFromLeft (360));
    meterLabel.setBounds (header);

    auto macroStrip = bounds.removeFromBottom (150).reduced (2, 8);
    const auto cellW = macroStrip.getWidth() / juce::jmax (1, static_cast<int> (macros.size()));
    for (auto& macro : macros)
    {
        auto cell = macroStrip.removeFromLeft (cellW).reduced (8, 0);
        macro->label.setBounds (cell.removeFromTop (22));
        macro->slider.setBounds (cell);
    }

    room.setBounds (bounds.reduced (0, 8));
}

void ZVocalAudioProcessorEditor::timerCallback()
{
    meterLabel.setText ("In " + juce::String (processor.getInputPeakDb(), 1)
                        + " dBFS    Gain Riding " + juce::String (processor.getStageDReductionDb(), 1)
                        + " dB    Sibilance " + juce::String (processor.getStageFReductionDb(), 1)
                        + " dB    Out " + juce::String (processor.getOutputPeakDb(), 1) + " dBFS",
                        juce::dontSendNotification);
}

juce::Rectangle<float> ZVocalAudioProcessorEditor::RoomCanvas::artistBounds (const Artist& artist) const
{
    const auto b = getLocalBounds().toFloat();
    const auto centre = juce::Point<float> (b.getX() + artist.position.x * b.getWidth(),
                                           b.getY() + artist.position.y * b.getHeight());
    return juce::Rectangle<float> (86.0f, 166.0f).withCentre (centre.translated (0.0f, -14.0f));
}

int ZVocalAudioProcessorEditor::RoomCanvas::hitTest (juce::Point<int> point) const
{
    for (int i = static_cast<int> (artists.size()) - 1; i >= 0; --i)
        if (artistBounds (artists[static_cast<size_t> (i)]).contains (point.toFloat()))
            return i;

    return -1;
}

void ZVocalAudioProcessorEditor::RoomCanvas::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto wall = b.reduced (8.0f);

    g.setGradientFill ({ juce::Colour (0xff252026), wall.getCentreX(), wall.getY(),
                         juce::Colour (0xff15171a), wall.getCentreX(), wall.getBottom(), false });
    g.fillRoundedRectangle (wall, 8.0f);
    g.setColour (juce::Colour (0xff4b3f36));
    g.drawRoundedRectangle (wall, 8.0f, 1.2f);

    auto backWall = wall.reduced (28.0f, 22.0f);
    g.setColour (juce::Colour (0xff2e2927));
    g.fillRect (backWall.removeFromBottom (92.0f));

    const auto rug = juce::Rectangle<float> (wall.getWidth() * 0.56f, 72.0f)
                       .withCentre ({ wall.getCentreX(), wall.getBottom() - 62.0f });
    g.setColour (juce::Colour (0xff5b2830));
    g.fillEllipse (rug);
    g.setColour (juce::Colour (0x558f6b47));
    g.drawEllipse (rug.reduced (8.0f), 2.0f);

    const auto micX = wall.getCentreX();
    const auto micTop = wall.getY() + 116.0f;
    const auto micBottom = wall.getBottom() - 54.0f;
    g.setColour (juce::Colour (0xffaaa39a));
    g.drawLine (micX, micTop + 48.0f, micX, micBottom, 3.0f);
    g.fillRoundedRectangle (micX - 11.0f, micTop, 22.0f, 46.0f, 7.0f);
    g.setColour (juce::Colour (0xff2d3034));
    g.drawRoundedRectangle (micX - 18.0f, micTop - 8.0f, 36.0f, 60.0f, 18.0f, 2.0f);

    g.setColour (juce::Colour (0x33dfb45d));
    juce::Path cone;
    cone.addTriangle (micX, micTop + 22.0f, wall.getX() + 110.0f, wall.getY() + 32.0f,
                      wall.getRight() - 110.0f, wall.getY() + 32.0f);
    g.fillPath (cone);

    for (size_t i = 0; i < artists.size(); ++i)
    {
        const auto body = artistBounds (artists[i]);
        const auto active = static_cast<int> (i) == selected;
        g.setColour (active ? juce::Colour (0x66dfb45d) : juce::Colour (0x33111111));
        g.fillEllipse (body.withY (body.getBottom() - 18.0f).withHeight (24.0f).expanded (16.0f, 0.0f));

        g.setColour (juce::Colour (0xff8a5e42));
        g.fillEllipse (body.getCentreX() - 22.0f, body.getY(), 44.0f, 50.0f);
        g.setColour (artists[i].jacket);
        g.fillRoundedRectangle (body.getX() + 15.0f, body.getY() + 48.0f, body.getWidth() - 30.0f, 82.0f, 18.0f);
        g.setColour (juce::Colour (0xff202124));
        g.drawLine (body.getCentreX(), body.getY() + 58.0f, body.getCentreX(), body.getY() + 126.0f, 2.0f);
        g.setColour (juce::Colour (0xff111214));
        g.drawLine (body.getCentreX() - 13.0f, body.getY() + 126.0f, body.getCentreX() - 20.0f, body.getBottom(), 6.0f);
        g.drawLine (body.getCentreX() + 13.0f, body.getY() + 126.0f, body.getCentreX() + 20.0f, body.getBottom(), 6.0f);

        if (active)
        {
            g.setColour (juce::Colour (0xffdfb45d));
            g.drawRoundedRectangle (body.expanded (8.0f), 18.0f, 2.0f);
        }
    }
}

void ZVocalAudioProcessorEditor::RoomCanvas::mouseDown (const juce::MouseEvent& event)
{
    const auto hit = hitTest (event.getPosition());
    if (hit >= 0)
        selected = hit;
    repaint();
}

void ZVocalAudioProcessorEditor::RoomCanvas::mouseDrag (const juce::MouseEvent& event)
{
    if (selected < 0 || selected >= static_cast<int> (artists.size()))
        return;

    auto& artist = artists[static_cast<size_t> (selected)];
    const auto b = getLocalBounds().toFloat();
    artist.position.x = juce::jlimit (0.12f, 0.88f, event.position.x / juce::jmax (1.0f, b.getWidth()));
    artist.position.y = juce::jlimit (0.34f, 0.86f, event.position.y / juce::jmax (1.0f, b.getHeight()));
    repaint();
}

void ZVocalAudioProcessorEditor::RoomCanvas::mouseDoubleClick (const juce::MouseEvent& event)
{
    const auto hit = hitTest (event.getPosition());
    if (hit < 0 || artists.size() >= 5)
        return;

    auto clone = artists[static_cast<size_t> (hit)];
    const auto leftSide = event.position.x < static_cast<float> (getWidth()) * 0.5f;
    clone.position.x = juce::jlimit (0.12f, 0.88f, clone.position.x + (leftSide ? -0.12f : 0.12f));
    clone.position.y = juce::jlimit (0.34f, 0.86f, clone.position.y + 0.035f);
    clone.jacket = clone.jacket.interpolatedWith (juce::Colours::white, 0.18f);
    artists.push_back (clone);
    selected = static_cast<int> (artists.size()) - 1;
    repaint();
}
