/*
  ==============================================================================

    PluginEditor.h
    Created: 7 Feb 2025
    Author:  Zarlino Audio

    Studio-quality editor for ZScorch — Zarlino Look & Feel compliant.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "shared/ui/theme/ZarlinoLookAndFeel.h"

// =============================================================================
// ZScorch Look & Feel — stays on the 10px radius / 5-color budget.
// =============================================================================
class ZScorchLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ZScorchLookAndFeel()
    {
        using namespace Zarlino;

        setColour (juce::ComboBox::outlineColourId,               juce::Colour (Palette::graphite_700));
        setColour (juce::ComboBox::backgroundColourId,            juce::Colour (Palette::obsidian_900));
        setColour (juce::ComboBox::textColourId,                  juce::Colour (Palette::steel_300));
        setColour (juce::ComboBox::arrowColourId,                 juce::Colour (Palette::steel_300));
        setColour (juce::PopupMenu::backgroundColourId,           juce::Colour (Palette::obsidian_900));
        setColour (juce::PopupMenu::textColourId,                 juce::Colour (Palette::mist_100));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (Palette::electric_blue));
        setColour (juce::PopupMenu::highlightedTextColourId,      juce::Colour (Palette::mist_100));
        setColour (juce::PopupMenu::headerTextColourId,           juce::Colour (Palette::steel_300));
        setColour (juce::TextButton::buttonColourId,              juce::Colour (Palette::graphite_800));
        setColour (juce::TextButton::buttonOnColourId,            juce::Colour (Palette::electric_blue));
        setColour (juce::TextButton::textColourOffId,             juce::Colour (Palette::mist_100));
        setColour (juce::TextButton::textColourOnId,              juce::Colour (Palette::obsidian_950));
    }

    void drawComboBox (juce::Graphics& g, int /*width*/, int /*height*/, bool /*isButtonDown*/,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox& box) override
    {
        using namespace Zarlino;
        const auto bounds = box.getLocalBounds().toFloat();
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (bounds, Radius::standard);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (bounds.reduced (0.5f), Radius::standard, 1.0f);

        const auto arrowBounds = juce::Rectangle<float> ((float) buttonX, (float) buttonY,
                                                          (float) buttonW, (float) buttonH)
                                     .reduced (0.0f, 7.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        juce::Path p;
        p.addTriangle (arrowBounds.getX(), arrowBounds.getY() + 2.0f,
                       arrowBounds.getCentreX(), arrowBounds.getBottom() - 1.0f,
                       arrowBounds.getRight(), arrowBounds.getY() + 2.0f);
        g.fillPath (p);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override
    {
        using namespace Zarlino;
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);

        if (button.getToggleState())
            g.setColour (button.findColour (juce::TextButton::buttonOnColourId));
        else if (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted)
            g.setColour (juce::Colour (Palette::graphite_700));
        else
            g.setColour (backgroundColour.isTransparent()
                             ? button.findColour (juce::TextButton::buttonColourId)
                             : backgroundColour);

        g.fillRoundedRectangle (bounds, Radius::standard);
        g.setColour (juce::Colour (Palette::graphite_700));
        g.drawRoundedRectangle (bounds, Radius::standard, 1.0f);
    }
};

// =============================================================================
// ZarlinoKnob — premium rotary control (hero 64 / standard 48 / secondary 32).
// =============================================================================
class ZarlinoKnob  : public juce::Slider
{
public:
    enum class SizeClass { hero, standard, secondary };

    ZarlinoKnob (SizeClass sizeClass)
        : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox),
          sizeClass (sizeClass)
    {
        setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (Zarlino::Palette::electric_blue));
        setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (Zarlino::Palette::graphite_700));
        setColour (juce::Slider::textBoxTextColourId, juce::Colour (Zarlino::Palette::mist_100));
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (Zarlino::Palette::graphite_700));
        setMouseDragSensitivity (300);
        setWantsKeyboardFocus (true);
        setDoubleClickReturnValue (true, defaultValue);
    }

    double getDefaultValue() const noexcept { return defaultValue; }

    void setDefaultValue (double value) noexcept
    {
        defaultValue = value;
        setDoubleClickReturnValue (true, value);
    }

    float getDialDiameter() const noexcept
    {
        switch (sizeClass)
        {
            case SizeClass::hero:      return Zarlino::Knob::diameter_hero;
            case SizeClass::standard:  return Zarlino::Knob::diameter_standard;
            default:                   return Zarlino::Knob::diameter_secondary;
        }
    }

    void setTextFromValue (std::function<juce::String (double)> fn)
    {
        textFromValueFn = std::move (fn);
    }

    juce::String getTextFromValue (double value) override
    {
        if (textFromValueFn != nullptr)
            return textFromValueFn (value);
        return juce::Slider::getTextFromValue (value);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace Zarlino;

        const auto bounds = getLocalBounds().toFloat();
        const float diameter = jmin (getDialDiameter(), bounds.getHeight() - 8.0f);
        const juce::Rectangle<float> dialArea (bounds.getCentreX() - diameter / 2.0f,
                                               bounds.getCentreY() - diameter / 2.0f,
                                               diameter, diameter);

        const float startAngle = juce::degreesToRadians (Knob::arc_start_degrees);
        const float arcAngle   = juce::degreesToRadians (Knob::arc_angle);
        const float value      = (float) valueToProportionOfLength (getValue());

        // Matte centre
        g.setColour (juce::Colour (Palette::obsidian_900));
        g.fillEllipse (dialArea);

        // 1px outer ring
        g.setColour (juce::Colour (Palette::graphite_700));
        g.drawEllipse (dialArea, 1.0f);

        // Keyboard focus ring — subtle cyan halo while arrow-key editing
        if (hasKeyboardFocus (true))
        {
            g.setColour (juce::Colour (Palette::electric_blue).withAlpha (0.4f));
            g.drawEllipse (dialArea.expanded (5.0f), 1.5f);
        }

        // Track (dim remaining arc)
        juce::Path track;
        track.addArc (dialArea.getX(), dialArea.getY(), dialArea.getWidth(), dialArea.getHeight(),
                      startAngle, startAngle + arcAngle, true);
        juce::PathStrokeType strokeTrack (Knob::track_stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour (juce::Colour (Palette::graphite_700));
        g.strokePath (track, strokeTrack);

        // Value arc (glow + core)
        const float valueAngle = startAngle + arcAngle * value;
        if (value > 0.0f)
        {
            juce::Path valuePath;
            valuePath.addArc (dialArea.getX(), dialArea.getY(), dialArea.getWidth(), dialArea.getHeight(),
                              startAngle, valueAngle, true);
            juce::PathStrokeType strokeValue (Knob::track_fill_stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
            const auto fillColour = findColour (juce::Slider::rotarySliderFillColourId);
            g.setColour (fillColour.withAlpha (0.12f));
            g.strokePath (valuePath, juce::PathStrokeType (strokeValue.getStrokeThickness() + 4.0f,
                                                           juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (fillColour.withAlpha (0.85f));
            g.strokePath (valuePath, strokeValue);
        }

        // Tick marks
        g.setColour (juce::Colour (Palette::steel_300).withAlpha (0.30f));
        const int numTicks = 25;
        for (int i = 0; i <= numTicks; ++i)
        {
            const float tickAngle = startAngle + arcAngle * (float) i / (float) numTicks;
            const float cosA = std::cos (tickAngle);
            const float sinA = std::sin (tickAngle);
            const float radiusOuter = diameter / 2.0f - Knob::track_stroke - 3.0f;
            const float radiusInner = radiusOuter - 3.0f;
            g.drawLine (dialArea.getCentreX() + cosA * radiusInner,
                        dialArea.getCentreY() + sinA * radiusInner,
                        dialArea.getCentreX() + cosA * radiusOuter,
                        dialArea.getCentreY() + sinA * radiusOuter,
                        0.8f);
        }

        // Centre dot
        g.setColour (juce::Colour (Palette::mist_100));
        g.fillEllipse (dialArea.getCentreX() - 2.0f, dialArea.getCentreY() - 2.0f, 4.0f, 4.0f);

        // Default-position marker — where double-click snaps back to
        const float defProportion = (float) valueToProportionOfLength (defaultValue);
        const float defAngle = startAngle + arcAngle * defProportion;
        const float defRadius = diameter / 2.0f - Knob::track_fill_stroke * 0.5f;
        g.setColour (juce::Colour (Palette::steel_300).withAlpha (0.9f));
        g.fillEllipse (dialArea.getCentreX() + std::cos (defAngle) * defRadius - 1.75f,
                       dialArea.getCentreY() + std::sin (defAngle) * defRadius - 1.75f,
                       3.5f, 3.5f);

        // Readout on hover / drag
        if (isMouseOverOrDragging())
        {
            const auto readout = getTextFromValue (getValue());
            g.setFont (juce::FontOptions (Typography::size_caption, juce::Font::bold));
            const float textW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), readout);
            const juce::Rectangle<float> bubble (dialArea.getCentreX() - textW / 2.0f - 6.0f,
                                                 dialArea.getY() - 20.0f,
                                                 textW + 12.0f, 16.0f);
            g.setColour (juce::Colour (Palette::obsidian_950).withAlpha (0.92f));
            g.fillRoundedRectangle (bubble, Radius::standard * 0.5f);
            g.setColour (juce::Colour (Palette::mist_100));
            g.drawText (readout, bubble, juce::Justification::centred);
        }
    }

private:
    SizeClass sizeClass;
    double defaultValue = 0.5;
    std::function<juce::String (double)> textFromValueFn;
};

// =============================================================================
// ZarlinoSlider — horizontal fader (2px radius thumb).
// =============================================================================
class ZarlinoSlider  : public juce::Slider
{
public:
    ZarlinoSlider()
        : juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox)
    {
        setColour (juce::Slider::trackColourId,              juce::Colour (Zarlino::Palette::graphite_700));
        setColour (juce::Slider::rotarySliderFillColourId,   juce::Colour (Zarlino::Palette::electric_blue));
        setColour (juce::Slider::thumbColourId,              juce::Colour (Zarlino::Palette::steel_300));
        setWantsKeyboardFocus (true);
        setDoubleClickReturnValue (true, getDefaultValue());
    }

    double getDefaultValue() const noexcept { return defaultValue; }

    void setDefaultValue (double value) noexcept
    {
        defaultValue = value;
        setDoubleClickReturnValue (true, value);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace Zarlino;

        const auto area = getLocalBounds().toFloat();
        const float trackY = area.getCentreY();
        const float trackX0 = 10.0f;
        const float trackX1 = area.getRight() - 12.0f;
        const float prop = (float) valueToProportionOfLength (getValue());
        const float knobX = trackX0 + (trackX1 - trackX0) * prop;

        // Track (2px)
        g.setColour (juce::Colour (Palette::graphite_700));
        g.fillRoundedRectangle (juce::Rectangle<float> (trackX0, trackY - 1.0f,
                                                        trackX1 - trackX0, 2.0f),
                                Radius::fader);

        // Fill
        if (prop > 0.0f)
        {
            g.setColour (juce::Colour (Palette::electric_blue));
            g.fillRoundedRectangle (juce::Rectangle<float> (trackX0, trackY - 1.0f,
                                                            (knobX - trackX0), 2.0f),
                                    Radius::fader);
        }

        // Centre tick for symmetric ranges
        const float centreX = trackX0 + (trackX1 - trackX0) * 0.5f;
        g.setColour (juce::Colour (Palette::steel_300).withAlpha (0.45f));
        g.drawLine (centreX, trackY - 4.0f, centreX, trackY + 4.0f, 1.0f);

        // Default-position marker — where double-click snaps back to
        const float defX = trackX0 + (trackX1 - trackX0)
                           * (float) valueToProportionOfLength (getDefaultValue());
        g.setColour (juce::Colour (Palette::steel_300).withAlpha (0.75f));
        g.fillEllipse (defX - 1.5f, trackY - 1.5f, 3.0f, 3.0f);

        // 4px handle with steel-300 border
        const juce::Rectangle<float> thumb (knobX - 6.0f, trackY - 2.0f, 12.0f, 4.0f);
        g.setColour (juce::Colour (Palette::obsidian_950));
        g.fillRoundedRectangle (thumb, Radius::fader);
        g.setColour (juce::Colour (Palette::steel_300));
        g.drawRoundedRectangle (thumb, Radius::fader, 1.0f);
    }

private:
    double defaultValue = 0.0;
};

// =============================================================================
// ZarlinoToggle — 32x16 pill switch.
// =============================================================================
class ZarlinoToggle  : public juce::ToggleButton
{
public:
    ZarlinoToggle()
    {
        setWantsKeyboardFocus (true);
    }

    void paintButton (juce::Graphics& g, bool /*isMouseOverButton*/, bool /*isButtonDown*/) override
    {
        using namespace Zarlino;

        const auto area = getLocalBounds().toFloat();
        const auto track = juce::Rectangle<float> (0.0f, area.getCentreY() - Toggle::height / 2.0f,
                                                   (float) Toggle::width, (float) Toggle::height);

        if (getToggleState())
            g.setColour (juce::Colour (Palette::electric_blue));
        else
            g.setColour (juce::Colour (Palette::graphite_700));

        g.fillRoundedRectangle (track, track.getHeight() / 2.0f);

        const float dotDiameter = track.getHeight() - Toggle::dot_pad * 2.0f;
        const float dotX = getToggleState()
                               ? track.getRight() - Toggle::dot_pad - dotDiameter
                               : track.getX() + Toggle::dot_pad;

        g.setColour (juce::Colour (Palette::mist_100));
        g.fillEllipse (dotX, track.getY() + Toggle::dot_pad, dotDiameter, dotDiameter);
    }
};

// =============================================================================
// ZarlinoButton — text button (primary / secondary / destructive).
// =============================================================================
class ZarlinoButton  : public juce::TextButton
{
public:
    enum class Variant { primary, secondary, destructive };

    explicit ZarlinoButton (Variant variant = Variant::secondary)
        : variant (variant)
    {
        setLookAndFeel (&fallbackLnf);
        setColour (juce::TextButton::textColourOffId, juce::Colour (Zarlino::Palette::mist_100));
        setColour (juce::TextButton::textColourOnId,  juce::Colour (Zarlino::Palette::obsidian_950));

        if (variant == Variant::primary)
        {
            setColour (juce::TextButton::buttonColourId,   juce::Colour (Zarlino::Palette::electric_blue));
            setColour (juce::TextButton::buttonOnColourId, juce::Colour (Zarlino::Palette::electric_blue));
        }
        else if (variant == Variant::destructive)
        {
            setColour (juce::TextButton::buttonColourId,   juce::Colour (Zarlino::Palette::graphite_800));
            setColour (juce::TextButton::buttonOnColourId, juce::Colour (Zarlino::Palette::alert_red));
            setColour (juce::TextButton::textColourOnId,   juce::Colour (Zarlino::Palette::mist_100));
        }

        setWantsKeyboardFocus (true);
    }

    ~ZarlinoButton() override
    {
        setLookAndFeel (nullptr);
    }

private:
    Variant variant;
    ZScorchLookAndFeel fallbackLnf;
};

// =============================================================================
// ZarlinoSegmentedLookAndFeel — modern segmented-control rendering: tabs sit in
// a recessed well (painted by ZarlinoSegmented) and the active tab is a cyan
// "pill".
// =============================================================================
class ZarlinoSegmentedLookAndFeel  : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& /*backgroundColour*/,
                               bool over, bool down) override
    {
        using namespace Zarlino;
        auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);

        if (button.getToggleState())
        {
            // Active tab — cyan pill
            g.setColour (juce::Colour (Palette::electric_blue).withAlpha (0.16f));
            g.fillRoundedRectangle (bounds, Radius::standard * 0.8f);
            g.setColour (juce::Colour (Palette::electric_blue).withAlpha (0.55f));
            g.drawRoundedRectangle (bounds, Radius::standard * 0.8f, 1.0f);
        }
        else if (over || down)
        {
            // Hover hint on inactive tabs
            g.setColour (juce::Colour (Palette::graphite_700).withAlpha (0.55f));
            g.fillRoundedRectangle (bounds, Radius::standard * 0.8f);
        }
    }
};

// =============================================================================
// ZarlinoSegmented — segmented tab strip: recessed well + cyan active pill.
// =============================================================================
class ZarlinoSegmented  : public juce::Component
{
public:
    void addTab (const juce::String& name, int id)
    {
        auto* button = tabs.add (new juce::TextButton (name));
        button->setRadioGroupId (1);
        button->setClickingTogglesState (true);
        button->setLookAndFeel (&segmentedLnf);
        button->setColour (juce::TextButton::buttonColourId,    juce::Colours::transparentBlack);
        button->setColour (juce::TextButton::buttonOnColourId,  juce::Colours::transparentBlack);
        button->setColour (juce::TextButton::textColourOffId,   juce::Colour (Zarlino::Palette::steel_300));
        button->setColour (juce::TextButton::textColourOnId,    juce::Colour (Zarlino::Palette::electric_blue));
        button->setWantsKeyboardFocus (true);
        if (tabs.size() == 1)
            button->setToggleState (true, juce::dontSendNotification);
        button->onClick = [this, id]
        {
            if (onTabSelected != nullptr)
                onTabSelected (id);
        };
        addAndMakeVisible (button);
    }

    void clearTabs()
    {
        tabs.clear();
    }

    void setSelected (int id)
    {
        for (auto* button : tabs)
        {
            const int buttonId = tabs.indexOf (button);
            button->setToggleState (buttonId == id, juce::dontSendNotification);
        }
    }

    void paint (juce::Graphics& g) override
    {
        using namespace Zarlino;

        // Recessed container well for the segmented control
        const auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (Palette::obsidian_900));
        g.fillRoundedRectangle (b, Radius::standard);
        g.setColour (juce::Colour (Palette::graphite_700).withAlpha (0.7f));
        g.drawRoundedRectangle (b.reduced (0.5f), Radius::standard, 1.0f);
    }

    void resized() override
    {
        if (tabs.isEmpty())
            return;

        const auto bounds = getLocalBounds();
        const int gap = 4;
        const int totalGaps = gap * (tabs.size() - 1);
        const int w = (bounds.getWidth() - totalGaps) / tabs.size();
        int x = bounds.getX();
        for (auto* button : tabs)
        {
            button->setBounds (x, bounds.getY(), w, bounds.getHeight());
            x += w + gap;
        }
    }

    std::function<void (int)> onTabSelected;

private:
    juce::OwnedArray<juce::TextButton> tabs;
    ZarlinoSegmentedLookAndFeel segmentedLnf;
};

// =============================================================================
// ActivityMeter — horizontal LED-style level meter with ballistics.
// =============================================================================
class ActivityMeter  : public juce::Component
{
public:
    ActivityMeter (const juce::String& labelText)
        : label (labelText)
    {
        // §8 motion: 300 ms attack / 800 ms release at the editor's 30 Hz timer.
        attack  = 1.0f - std::exp (- (1.0f / 30.0f) / 0.30f);
        release = 1.0f - std::exp (- (1.0f / 30.0f) / 0.80f);
    }

    void setDirection (bool verticalMeter) noexcept { vertical = verticalMeter; }

    void setLevel (float level)
    {
        targetLevel = juce::jlimit (0.0f, 1.0f, level);
    }

    void setBallistics (float attackCoeff, float releaseCoeff) noexcept
    {
        attack = attackCoeff;
        release = releaseCoeff;
    }

    void update()
    {
        if (smoothedLevel < targetLevel)
            smoothedLevel += (targetLevel - smoothedLevel) * attack;
        else
            smoothedLevel += (targetLevel - smoothedLevel) * release;
    }

    void paint (juce::Graphics& g) override
    {
        using namespace Zarlino;

        auto bounds = getLocalBounds();

        if (vertical)
        {
            const float labelH = 11.0f;
            g.setFont (juce::FontOptions (Typography::size_micro, juce::Font::bold));
            g.setColour (juce::Colour (Palette::steel_300));
            g.drawText (label, bounds.getX(), bounds.getBottom() - (int) labelH,
                        bounds.getWidth(), (int) labelH, juce::Justification::centred);

            const auto meterArea = bounds.removeFromBottom (bounds.getHeight() - (int) labelH).reduced (3, 0);
            const int numSegments = 18;
            const int gap = 2;
            const float segH = (float) (meterArea.getHeight() - gap * (numSegments - 1)) / (float) numSegments;
            const int litSegments = juce::roundToInt (smoothedLevel * (float) numSegments);

            for (int i = 0; i < numSegments; ++i)
            {
                juce::Colour c;
                const float fraction = (float) (i + 1) / (float) numSegments;
                if (fraction > 0.95f)
                    c = juce::Colour (Palette::alert_red);
                else if (fraction > 0.80f)
                    c = juce::Colour (Palette::signal_gold);
                else
                    c = juce::Colour (Palette::electric_blue);

                g.setColour (i < litSegments ? c : juce::Colour (Palette::graphite_700));
                g.fillRoundedRectangle ((float) meterArea.getX(),
                                        meterArea.getBottom() - (float) (i + 1) * (segH + gap),
                                        (float) meterArea.getWidth(), segH,
                                        Radius::meter);
            }
            return;
        }

        const float labelW = 72.0f;
        g.setFont (juce::FontOptions (Typography::size_caption, juce::Font::bold));
        g.setColour (juce::Colour (Palette::steel_300));
        g.drawText (label, 0, 0, (int) labelW, bounds.getHeight(), juce::Justification::centredLeft);

        const auto meterArea = bounds.withLeft ((int) labelW).reduced (0, 2);
        const int numSegments = 32;
        const int gap = 2;
        const float segW = (float) (meterArea.getWidth() - gap * (numSegments - 1)) / (float) numSegments;
        const int litSegments = juce::roundToInt (smoothedLevel * (float) numSegments);

        for (int i = 0; i < numSegments; ++i)
        {
            juce::Colour c;
            const float fraction = (float) (i + 1) / (float) numSegments;
            if (fraction > 0.95f)
                c = juce::Colour (Palette::alert_red);
            else if (fraction > 0.80f)
                c = juce::Colour (Palette::signal_gold);
            else
                c = juce::Colour (Palette::electric_blue);

            if (i < litSegments)
                g.setColour (c);
            else
                g.setColour (juce::Colour (Palette::graphite_700));

            g.fillRoundedRectangle (meterArea.getX() + (float) i * (segW + gap),
                                    (float) meterArea.getY(),
                                    segW, (float) meterArea.getHeight(),
                                    Radius::meter);
        }
    }

private:
    juce::String label;
    bool vertical = false;
    float targetLevel = 0.0f;
    float smoothedLevel = 0.0f;
    float attack = 0.105f;
    float release = 0.041f;
};

// =============================================================================
// ZScorchSpectrum — spectrum analysis display (hero component).
// =============================================================================
class ZScorchSpectrum  : public juce::Component
{
public:
    ZScorchSpectrum()
    {
        for (int i = 0; i < numWindowFrames; ++i)
            windowedData.push_back (std::vector<float> (513, 0.0f));
    }

    void pushData (const float* data, int size, float hostSampleRate)
    {
        if (data == nullptr || size <= 0)
            return;

        this->sampleRate = hostSampleRate;
        if (hostSampleRate <= 0.0f)
            return;

        if (lastData.size() != (size_t) size)
            lastData.resize ((size_t) size);

        juce::FloatVectorOperations::copy (lastData.data(), data, size);

        // Add to window (ring buffer of frames)
        std::vector<float> frame ((size_t) size);
        juce::FloatVectorOperations::copy (frame.data(), data, size);
        windowedData[(size_t) frameIndex] = frame;
        frameIndex = (frameIndex + 1) % numWindowFrames;
    }

    void paint (juce::Graphics& g) override
    {
        using namespace Zarlino;

        const auto bounds = getLocalBounds();
        g.fillAll (juce::Colour (Palette::obsidian_950));

        const auto area = bounds.reduced (1);
        const auto plot = area.reduced (4, 8);
        // Reserve extra vertical space for the per-band harmonic activity
        // strip when the display is in "HARMONIC ACTIVITY" mode.
        const int bottomY = plot.getBottom() - (showHarmonicActivity ? 20 : 8);

        // dB grid -96..0 (every 12 dB)
        g.setColour (juce::Colour (Palette::graphite_700).withAlpha (0.35f));
        for (int db = -96; db <= 0; db += 12)
        {
            const float y = dbToY ((float) db, plot, bottomY);
            g.drawHorizontalLine ((int) y, (float) plot.getX(), (float) plot.getRight());
        }

        // Frequency grid (log)
        static const float freqs[] = { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
                                       1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };
        g.setColour (juce::Colour (Palette::graphite_700).withAlpha (0.35f));
        for (float f : freqs)
        {
            const float x = freqToX (f, plot, bottomY);
            g.drawVerticalLine ((int) x, (float) plot.getY(), (float) bottomY);
        }

        // Frequency labels
        g.setFont (juce::FontOptions (Typography::size_micro));
        g.setColour (juce::Colour (Palette::steel_300));
        for (float f : freqs)
        {
            const float x = freqToX (f, plot, bottomY);
            const juce::String label = f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k"
                                                    : juce::String ((int) f);
            g.drawText (label, juce::Rectangle<float> (x - 12.0f, (float) bottomY + 3.0f, 24.0f, 10.0f),
                        juce::Justification::centred);
        }

        // dB labels
        g.setFont (juce::FontOptions (Typography::size_micro));
        for (int db = -96; db <= 0; db += 24)
        {
            const float y = dbToY ((float) db, plot, bottomY);
            g.setColour (juce::Colour (Palette::steel_300).withAlpha (0.8f));
            g.drawText (juce::String (db), juce::Rectangle<float> (plot.getX() - 1.0f, y - 5.0f, 22.0f, 10.0f),
                        juce::Justification::centredLeft);
        }

        // Band shading (graphite-800 regions between crossovers) + rust markers + band labels
        if (showBandShading)
        {
            const int numBands = juce::jmax (1, activeNumBands);
            static const char* bandNames[] = { "LOW", "LO-MID", "HI-MID", "HIGH" };

            float xPrev = (float) plot.getX();
            for (int b = 0; b < numBands; ++b)
            {
                const float bandRight = (b == numBands - 1) ? (float) plot.getRight()
                                                            : freqToX (getCrossoverFreq (numBands, b + 1), plot, bottomY);
                const juce::Rectangle<float> region (xPrev, (float) plot.getY(),
                                                     bandRight - xPrev, (float) (bottomY - plot.getY()));

                g.setColour (juce::Colour (Palette::graphite_800).withAlpha (b % 2 == 0 ? 0.55f : 0.25f));
                g.fillRect (region);

                // Band label (top-left corner)
                g.setFont (juce::FontOptions (Typography::size_micro, juce::Font::bold));
                g.setColour (juce::Colour (Palette::steel_300).withAlpha (0.85f));
                g.drawText (bandNames[b], region.withSizeKeepingCentre (juce::jmin (64.0f, region.getWidth()), 12.0f),
                            juce::Justification::centred);

                xPrev = bandRight;
            }

            // Rust crossover markers
            g.setColour (juce::Colour (Palette::cat_print).withAlpha (0.9f));
            for (int b = 1; b < numBands; ++b)
            {
                const float crossoverHz = getCrossoverFreq (numBands, b);
                const float x = freqToX (crossoverHz, plot, bottomY);
                g.drawVerticalLine ((int) x, (float) plot.getY(), (float) bottomY);
            }
        }

        // Smoothed spectrum curve (average of window frames)
        if (!lastData.empty() && sampleRate > 0.0f)
        {
            const int numBins = (int) lastData.size();
            juce::Path path;
            juce::Path fillPath;
            bool firstPoint = true;

            for (int i = 0; i < numBins; ++i)
            {
                float avgMag = 0.0f;
                for (const auto& frame : windowedData)
                {
                    if ((int) frame.size() > i)
                        avgMag += frame[(size_t) i];
                }
                avgMag /= (float) windowedData.size();

                const float freq = (float) i * sampleRate / (float) (numBins * 2);
                const float x = freqToX (freq, plot, bottomY);
                const float db = juce::Decibels::gainToDecibels (avgMag, -120.0f);
                const float y = dbToY (db, plot, bottomY);

                if (firstPoint)
                {
                    path.startNewSubPath (x, y);
                    fillPath.startNewSubPath (x, (float) bottomY);
                    firstPoint = false;
                }
                else
                {
                    path.lineTo (x, y);
                    fillPath.lineTo (x, y);
                }
            }

            // Fill
            g.setColour (juce::Colour (Palette::electric_blue).withAlpha (0.08f));
            g.fillPath (fillPath);

            // Glow + core line
            g.setColour (juce::Colour (Palette::electric_blue).withAlpha (0.2f));
            g.strokePath (path, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
            g.setColour (juce::Colour (Palette::electric_blue).withAlpha (0.9f));
            g.strokePath (path, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        // Per-band harmonic activity strip — which crossover band is carrying
        // the most harmonic energy right now. Purely visual; computed from the
        // windowed magnitude frames the processor publishes for the UI.
        if (showHarmonicActivity && !lastData.empty() && sampleRate > 0.0f)
        {
            const int numBands = juce::jmax (1, activeNumBands);
            const int numBins  = (int) lastData.size();
            float bandEnergy[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

            for (int b = 0; b < numBands && b < 4; ++b)
            {
                const float fLo = (b == 0) ? 20.0f : getCrossoverFreq (numBands, b);
                const float fHi = (b == numBands - 1) ? 20000.0f : getCrossoverFreq (numBands, b + 1);
                float sum = 0.0f;
                int count = 0;

                for (int i = 0; i < numBins; ++i)
                {
                    const float freq = (float) i * sampleRate / (float) (numBins * 2);
                    if (freq >= fLo && freq < fHi)
                    {
                        float avg = 0.0f;
                        for (const auto& frame : windowedData)
                            if ((int) frame.size() > i)
                                avg += frame[(size_t) i];
                        avg /= (float) windowedData.size();
                        sum += avg;
                        ++count;
                    }
                }

                if (count > 0)
                    bandEnergy[b] = sum / (float) count;
            }

            float maxE = 0.0f;
            for (int b = 0; b < numBands; ++b)
                maxE = juce::jmax (maxE, bandEnergy[b]);
            if (maxE <= 0.0f)
                maxE = 1.0f;

            const juce::Rectangle<float> strip ((float) plot.getX(), (float) bottomY + 13.0f,
                                                (float) (plot.getRight() - plot.getX()), 6.0f);
            const float gap = 3.0f;
            const float segW = (strip.getWidth() - gap * (numBands - 1)) / (float) numBands;

            for (int b = 0; b < numBands; ++b)
            {
                const float prop = juce::jlimit (0.0f, 1.0f,
                                                 std::log10 (1.0f + 9.0f * bandEnergy[b] / maxE));
                const juce::Rectangle<float> seg (strip.getX() + (float) b * (segW + gap),
                                                  strip.getY(), segW, strip.getHeight());

                g.setColour (juce::Colour (Palette::graphite_700).withAlpha (0.55f));
                g.fillRoundedRectangle (seg, Radius::meter);

                if (prop > 0.02f)
                {
                    const float fillH = seg.getHeight() * prop;
                    g.setColour (juce::Colour (Palette::electric_blue).withAlpha (0.85f));
                    g.fillRoundedRectangle (juce::Rectangle<float> (seg.getX(),
                                                                    seg.getBottom() - fillH,
                                                                    seg.getWidth(), fillH),
                                            Radius::meter);
                }
            }
        }
    }

    static float getCrossoverFreq (int numBands, int bandIndex)
    {
        // LR4: 2-band=250Hz, 3-band=+1.5kHz, 4-band=+6kHz
        switch (numBands)
        {
            case 2:  return bandIndex == 1 ? 250.0f : 0.0f;
            case 3:  return bandIndex == 1 ? 250.0f : (bandIndex == 2 ? 1500.0f : 0.0f);
            case 4:  return bandIndex == 1 ? 250.0f : (bandIndex == 2 ? 1500.0f : 6000.0f);
            default: return 0.0f;
        }
    }

    void setActiveNumBands (int bands) noexcept { activeNumBands = bands; }
    void setShowBandShading (bool show) noexcept { showBandShading = show; }

    /** Simple-view "HARMONIC ACTIVITY" mode: draws a per-band energy strip
        along the bottom of the plot so the display reads as a harmonic
        (not just spectral) monitor. UI-only — derives from the magnitude
        data the processor already publishes; no DSP involvement. */
    void setShowHarmonicActivity (bool show) noexcept { showHarmonicActivity = show; }

private:
    static constexpr int numWindowFrames = 8;
    static constexpr float kMinDb = -96.0f;
    static constexpr float kMaxDb = 0.0f;

    std::vector<std::vector<float>> windowedData;
    std::vector<float> lastData;
    int frameIndex = 0;
    float sampleRate = 44100.0f;
    int activeNumBands = 3;
    bool showBandShading = false;
    bool showHarmonicActivity = false;

    static float freqToX (float freq, const juce::Rectangle<int>& plot, int /*bottomY*/)
    {
        const float nyquist = 20000.0f;
        const float x0 = (float) plot.getX();
        const float x1 = (float) plot.getRight();
        const float logF = std::log (freq / 20.0f) / std::log (nyquist / 20.0f);
        return x0 + juce::jlimit (0.0f, 1.0f, logF) * (x1 - x0);
    }

    static float dbToY (float db, const juce::Rectangle<int>& plot, int bottomY)
    {
        const float t = 1.0f - (db - kMinDb) / (kMaxDb - kMinDb);
        return (float) plot.getY() + juce::jlimit (0.0f, 1.0f, t) * (float) (bottomY - plot.getY());
    }
};

// =============================================================================
// Main editor
// =============================================================================
class ZScorchAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit ZScorchAudioProcessorEditor (ZScorchAudioProcessor&);
    ~ZScorchAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // This reference is provided as a quick way for the editor to
    // access the processor object that created it.
    ZScorchAudioProcessor& audioProcessor;

    // --- Controls -----------------------------------------------------------
    ZarlinoKnob liftKnob { ZarlinoKnob::SizeClass::hero };
    ZarlinoKnob characterKnob { ZarlinoKnob::SizeClass::standard };
    ZarlinoKnob mixKnob { ZarlinoKnob::SizeClass::standard };

    ZarlinoKnob satDriveKnob { ZarlinoKnob::SizeClass::standard };
    ZarlinoKnob satHarmReductionKnob { ZarlinoKnob::SizeClass::standard };
    ZarlinoKnob tiltAmountKnob { ZarlinoKnob::SizeClass::standard };
    ZarlinoKnob exciterAmountKnob { ZarlinoKnob::SizeClass::standard };

    // EQ knobs: [0]=LowShelfFreq [1]=LowShelfGain [2]=BellFreq [3]=BellGain
    //           [4]=BellQ [5]=HighShelfFreq [6]=HighShelfGain
    // §4.2 — freq/Q knobs are secondary (32px); gain knobs are standard (48px).
    ZarlinoKnob preEqKnobs[7]   { { ZarlinoKnob::SizeClass::secondary },
                                  { ZarlinoKnob::SizeClass::standard },
                                  { ZarlinoKnob::SizeClass::secondary },
                                  { ZarlinoKnob::SizeClass::standard },
                                  { ZarlinoKnob::SizeClass::secondary },
                                  { ZarlinoKnob::SizeClass::secondary },
                                  { ZarlinoKnob::SizeClass::standard } };
    ZarlinoKnob postEqKnobs[7]  { { ZarlinoKnob::SizeClass::secondary },
                                  { ZarlinoKnob::SizeClass::standard },
                                  { ZarlinoKnob::SizeClass::secondary },
                                  { ZarlinoKnob::SizeClass::standard },
                                  { ZarlinoKnob::SizeClass::secondary },
                                  { ZarlinoKnob::SizeClass::secondary },
                                  { ZarlinoKnob::SizeClass::standard } };

    ZarlinoSlider outputCeilingSlider;
    ZarlinoSlider bandGainSliders[4];
    ZarlinoToggle oversample4xToggle;
    ZarlinoToggle exciterEnabledToggle;
    ZarlinoToggle advancedToggle;

    juce::ComboBox presetComboBox;
    juce::ComboBox satModeCombo;

    ZarlinoButton undoButton { ZarlinoButton::Variant::secondary };
    ZarlinoButton redoButton { ZarlinoButton::Variant::secondary };
    ZarlinoButton bypassButton { ZarlinoButton::Variant::secondary };
    ZarlinoButton abButton { ZarlinoButton::Variant::secondary };
    ZarlinoButton unlockButton { ZarlinoButton::Variant::primary };

    ZarlinoSegmented bandSelector;
    ZarlinoSegmented bandEditSelector;
    ZarlinoSegmented zoneSelector;

    ZScorchSpectrum spectrum;
    ActivityMeter inputMeter { "IN" };
    ActivityMeter outputMeter { "OUT" };
    ActivityMeter activityMeter { "ACTIVITY" };

    juce::Label licenseLabel;

    // --- Attachments --------------------------------------------------------
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> liftAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> characterAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;

    // Per-band saturator controls are single controls rebound to the active band.
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> satDriveAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> satModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> satHarmReductionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tiltAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> exciterAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> exciterEnabledAttachment;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bandGainAttachments[4];

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> preEqAttachments[7];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> postEqAttachments[7];

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputCeilingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> oversample4xAttachment;

    std::unique_ptr<juce::TooltipWindow> licenseTooltip;

    // --- State ----------------------------------------------------------------
    ZScorchLookAndFeel zscorchLnf;
    juce::MemoryBlock abStateA;  // serialized APVTS state for the A/B snapshot
    juce::MemoryBlock abStateB;
    bool showingAdvanced = false;
    bool abOnA = true;        // A/B snapshot toggle state (which slot is active)
    int currentZone = 0;      // 0 = saturator, 1 = EQ, 2 = output
    int currentBand = 0;      // 0..3
    int lastActiveBands = 1;  // cached band count to detect changes in the timer
    bool spectrumBandShadingOn = false;
    int frameCount = 0;
    juce::Rectangle<int> zoneContentRect;   // current advanced-zone content area
    juce::Rectangle<int> activityPanelRect;       // simple-view harmonic activity panel
    juce::Rectangle<int> macroClusterRect;        // simple-view macro control cluster
    juce::Rectangle<int> advancedMacroPanelRect;  // advanced-view macro strip panel

    // --- Helpers --------------------------------------------------------------
    void setupZKnob (ZarlinoKnob&, const juce::String& id, const juce::String& label,
                     double min, double max, double interval, double def,
                     const juce::String& suffix);
    void setupZSlider (ZarlinoSlider&, const juce::String& id, const juce::String& label,
                       double min, double max, double interval, double def,
                       const juce::String& suffix);
    void setupBandSelector();
    void setupZoneSelector();
    void setupSatModeCombo();
    void layoutSimpleView();
    void layoutAdvancedView();
    void layoutSaturatorZone (juce::Rectangle<int>);
    void layoutEqZone (juce::Rectangle<int>);
    void layoutOutputZone (juce::Rectangle<int>);
    void setZoneVisibility (int zone);
    void updateBandSelectorState();
    void rebindBandAttachments (int band);
    void refreshLicenseDisplay();
    void updatePresetComboBox();
    void createAttachments();
    void describeControl (juce::Component&, const juce::String& title, const juce::String& description);
    void drawSectionHeader (juce::Graphics&, const juce::String&, const juce::Rectangle<int>&);
    void drawKnobLabel (juce::Graphics&, juce::Component&, const juce::String&);
    void drawActivityPanel (juce::Graphics&);
    void drawMacroCluster (juce::Graphics&);
    void drawAdvancedMacroPanel (juce::Graphics&);
    void drawZonePanel (juce::Graphics&);
    void drawSimpleFooterStatus (juce::Graphics&, juce::Rectangle<int>);
    juce::String getActiveModeName() const;
    bool isOversamplingEnabled() const;

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZScorchAudioProcessorEditor)
};
