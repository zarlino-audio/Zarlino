#pragma once
#include <cstdint>

//==============================================================================
// Zarlino Look & Feel — binding design tokens per Zarlino-Audio-Look-and-Feel-Spec-1
// Single source of truth for all plugin UI. No plugin may hard-code color/spacing
// values; all references must route through this header.
//==============================================================================

namespace Zarlino
{

//==============================================================================
// §2.1 Core Palette (locked hex values)
//==============================================================================
namespace Palette
{
    constexpr uint32_t obsidian_950    = 0xFF0A0A0C;  // primary background (plugin canvas)
    constexpr uint32_t obsidian_900    = 0xFF131316;  // panel / module background
    constexpr uint32_t graphite_800    = 0xFF1C1C1F;  // raised surface (cards, meter wells)
    constexpr uint32_t graphite_700    = 0xFF2A2A2E;  // borders, dividers, inactive control track
    constexpr uint32_t graphite_600    = 0xFF3D3D42;  // hover state on neutral controls
    constexpr uint32_t steel_400       = 0xFF6B6B70;  // disabled text, tick marks
    constexpr uint32_t steel_300       = 0xFF9B9BA0;  // secondary text, unit labels
    constexpr uint32_t mist_100        = 0xFFE8E8EA;  // primary text, active labels
    constexpr uint32_t electric_blue   = 0xFF2F6FFF;  // the single interaction colour
    constexpr uint32_t signal_gold     = 0xFFD4AF37;  // premium / certification / achievement only
    constexpr uint32_t alert_red       = 0xFFFF4F4F;  // clipping, errors, destructive actions only

    // §10.1 Category accents
    constexpr uint32_t cat_lift        = 0xFFB8843F;  // Lift Series (dynamics): muted amber
    constexpr uint32_t cat_print       = 0xFFB85C3F;  // Print Series (saturation/character): muted rust
    constexpr uint32_t cat_flow        = 0xFF3FA8A0;  // Flow Series (modulation/spatial): muted teal
    constexpr uint32_t cat_stem        = 0xFF7A6FCC;  // Stem Series (restoration/separation): muted violet
    constexpr uint32_t cat_mix         = 0xFF5C7AA0;  // Mix Series (bus/master processing): muted slate blue
}

//==============================================================================
// §3 Grid & Spacing (§7 Spacing Scale)
//==============================================================================
namespace Spacing
{
    constexpr int baseUnit   = 8;
    constexpr int subUnit    = 4;

    constexpr int space_1 = 4;    // icon-to-label gap, hairline offsets
    constexpr int space_2 = 8;    // control-to-control gap within a module
    constexpr int space_3 = 16;   // module internal padding
    constexpr int space_4 = 24;   // window outer margin, section gap
    constexpr int space_5 = 32;   // gap between unrelated module groups
    constexpr int space_6 = 48;   // header-to-content separation

    constexpr int gutter = 16;    // between modules (§3)
    constexpr int margin = 24;    // fixed outer margin (§3)
}

//==============================================================================
// §3 Window resolutions (JUCE-native, integer-scalable for HiDPI)
//==============================================================================
namespace Window
{
    constexpr int compact_w  = 640;
    constexpr int compact_h  = 480;
    constexpr int standard_w = 960;
    constexpr int standard_h = 640;
    constexpr int suite_w    = 1280;
    constexpr int suite_h    = 800;
}

//==============================================================================
// §4 Typography — Inter face, five weights, fixed type scale
//==============================================================================
namespace Typography
{
    // §4.1 Typeface name (must match the Inter font name embedded in JUCE binary data)
    constexpr const char* typeface = "Inter";

    // §4.2 Weight scale — integer style flags for use with juce::Font
    namespace Weight
    {
        constexpr int regular  = 0;   // Inter Regular (400) — juce::Font::plain
        constexpr int medium   = 2;   // Inter Medium (500) — mapped as italic flag since JUCE lacks medium; override via custom Typeface if available
        constexpr int semiBold = 1;   // Inter SemiBold (600) — juce::Font::bold flag
        constexpr int bold     = 1;   // Inter Bold (700) — juce::Font::bold flag
        // Inter Black (900) — reserved for wordmark, never used in-app
    }

    // §4.3 Type scale (px)
    constexpr float size_micro    = 10.0f;   // tick labels, dB scale marks
    constexpr float size_caption  = 11.0f;   // unit suffixes, footnotes
    constexpr float size_body     = 13.0f;   // parameter labels
    constexpr float size_label    = 14.0f;   // control names
    constexpr float size_headline = 20.0f;   // module titles
    constexpr float size_display  = 32.0f;   // primary numeric readout
    constexpr float size_wordmark = 24.0f;   // header plugin name, tracking +2%

    constexpr float line_height_micro    = 12.0f;
    constexpr float line_height_caption  = 16.0f;
    constexpr float line_height_body     = 16.0f;
    constexpr float line_height_label    = 16.0f;
    constexpr float line_height_headline = 24.0f;
    constexpr float line_height_display  = 32.0f;
}

//==============================================================================
// §6 Corner Radius
//==============================================================================
namespace Radius
{
    constexpr float standard  = 10.0f;  // all rectangular surfaces
    constexpr float circle    = 50.0f;  // value for border-radius: 50% (interpreted as semicircle at render-time)
    constexpr float meter     = 2.0f;   // meter wells and LED segments
    constexpr float fader     = 2.0f;   // fader thumbs
}

//==============================================================================
// §9 Shadow tokens (only two permitted)
//==============================================================================
namespace Shadow
{
    // shadow-raised: for buttons and knobs at rest
    constexpr float raised_opacity = 0.4f;
    constexpr float raised_y       = 1.0f;
    constexpr float raised_blur    = 2.0f;

    // shadow-well: for recessed meter wells and text input fields
    constexpr float well_opacity = 0.6f;
}

//==============================================================================
// §11.1 Rotary Knob sizes
//==============================================================================
namespace Knob
{
    constexpr float diameter_hero      = 64.0f;  // primary/hero parameter
    constexpr float diameter_standard  = 48.0f;  // standard
    constexpr float diameter_secondary = 32.0f;  // secondary/trim
    constexpr float indicator_stroke   = 2.0f;   // indicator line stroke width
    constexpr float track_stroke       = 2.0f;   // arc track stroke width
    constexpr float track_fill_stroke  = 2.5f;   // filled arc stroke width
    constexpr float arc_angle          = 270.0f; // full arc in degrees
    constexpr float arc_start_degrees  = 225.0f; // 7 o'clock position
}

//==============================================================================
// §11.2 Slider / Fader
//==============================================================================
namespace Fader
{
    constexpr float track_thickness = 4.0f;
    constexpr float thumb_size      = 12.0f;
    constexpr float thumb_radius    = 2.0f;
}

//==============================================================================
// §11.3 Toggle / Switch
//==============================================================================
namespace Toggle
{
    constexpr float width  = 32.0f;
    constexpr float height = 16.0f;
    constexpr float dot_pad = 2.0f;
}

//==============================================================================
// §11.5 Buttons
//==============================================================================
namespace Button
{
    constexpr float height_standard = 32.0f;
    constexpr float height_primary  = 40.0f;
}

//==============================================================================
// §11.4 Meter — LED-Segment colour ramp
//==============================================================================
namespace Meter
{
    constexpr uint32_t low     = Palette::steel_300;     // −∞ to −18 dBFS
    constexpr uint32_t nominal = Palette::electric_blue; // −18 to −6 dBFS
    constexpr uint32_t hot     = Palette::signal_gold;   // −6 to −1 dBFS
    constexpr uint32_t clip    = Palette::alert_red;      // above −1 dBFS

    constexpr float segment_radius = Radius::meter; // 2px per §6 exception

    // §8 Motion: meter ballistics
    constexpr float attack_ms  = 300.0f;
    constexpr float release_ms = 800.0f;
}

//==============================================================================
// §10 Header/Footer structure
//==============================================================================
namespace Chrome
{
    constexpr float headerHeight = 48.0f;
    constexpr float footerHeight = 28.0f;
    constexpr float sectionHeaderHeight = 28.0f;
    constexpr float dividerThickness = 1.0f;
}

} // namespace Zarlino

//==============================================================================
// ZarlinoLookAndFeel — shared dark-theme LookAndFeel for all Zarlino plugins.
// Standard JUCE components are styled with Zarlino palette tokens.
// Plugins may further override specific component drawing for custom widgets.
//==============================================================================
#include <JuceHeader.h>

class ZarlinoLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ZarlinoLookAndFeel()
    {
        // Colour scheme for standard JUCE widgets
        setColour (juce::ResizableWindow::backgroundColourId,  juce::Colour (Zarlino::Palette::obsidian_950));

        // Sliders (rotary)
        setColour (juce::Slider::rotarySliderFillColourId,     juce::Colour (Zarlino::Palette::electric_blue));
        setColour (juce::Slider::rotarySliderOutlineColourId,  juce::Colour (Zarlino::Palette::graphite_700));
        setColour (juce::Slider::thumbColourId,                juce::Colour (Zarlino::Palette::mist_100));
        setColour (juce::Slider::trackColourId,                juce::Colour (Zarlino::Palette::graphite_700));
        setColour (juce::Slider::backgroundColourId,           juce::Colour (Zarlino::Palette::obsidian_900));
        setColour (juce::Slider::textBoxTextColourId,          juce::Colour (Zarlino::Palette::mist_100));
        setColour (juce::Slider::textBoxBackgroundColourId,    juce::Colour (Zarlino::Palette::obsidian_950));
        setColour (juce::Slider::textBoxOutlineColourId,       juce::Colour (Zarlino::Palette::graphite_700));
        setColour (juce::Slider::textBoxHighlightColourId,     juce::Colour (Zarlino::Palette::electric_blue));

        // Buttons
        setColour (juce::TextButton::buttonColourId,           juce::Colour (Zarlino::Palette::graphite_800));
        setColour (juce::TextButton::buttonOnColourId,         juce::Colour (Zarlino::Palette::electric_blue));
        setColour (juce::TextButton::textColourOffId,          juce::Colour (Zarlino::Palette::mist_100));
        setColour (juce::TextButton::textColourOnId,           juce::Colour (Zarlino::Palette::mist_100));

        // ComboBox
        setColour (juce::ComboBox::backgroundColourId,         juce::Colour (Zarlino::Palette::obsidian_900));
        setColour (juce::ComboBox::textColourId,               juce::Colour (Zarlino::Palette::mist_100));
        setColour (juce::ComboBox::outlineColourId,            juce::Colour (Zarlino::Palette::graphite_700));
        setColour (juce::ComboBox::arrowColourId,              juce::Colour (Zarlino::Palette::steel_300));
        setColour (juce::ComboBox::buttonColourId,             juce::Colour (Zarlino::Palette::graphite_700));
        setColour (juce::ComboBox::focusedOutlineColourId,     juce::Colour (Zarlino::Palette::electric_blue));

        // Popup menu
        setColour (juce::PopupMenu::backgroundColourId,        juce::Colour (Zarlino::Palette::obsidian_900));
        setColour (juce::PopupMenu::textColourId,              juce::Colour (Zarlino::Palette::mist_100));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (Zarlino::Palette::graphite_700));
        setColour (juce::PopupMenu::highlightedTextColourId,   juce::Colour (Zarlino::Palette::electric_blue));

        // Labels
        setColour (juce::Label::textColourId,                  juce::Colour (Zarlino::Palette::mist_100));
        setColour (juce::Label::outlineColourId,               juce::Colour (Zarlino::Palette::graphite_700));

        // Toggle buttons
        setColour (juce::ToggleButton::textColourId,           juce::Colour (Zarlino::Palette::mist_100));
        setColour (juce::ToggleButton::tickColourId,           juce::Colour (Zarlino::Palette::electric_blue));
        setColour (juce::ToggleButton::tickDisabledColourId,   juce::Colour (Zarlino::Palette::steel_400));

        // Text editor
        setColour (juce::TextEditor::backgroundColourId,       juce::Colour (Zarlino::Palette::obsidian_900));
        setColour (juce::TextEditor::textColourId,             juce::Colour (Zarlino::Palette::mist_100));
        setColour (juce::TextEditor::highlightColourId,        juce::Colour (Zarlino::Palette::electric_blue));
        setColour (juce::TextEditor::highlightedTextColourId,  juce::Colour (Zarlino::Palette::mist_100));
        setColour (juce::TextEditor::outlineColourId,          juce::Colour (Zarlino::Palette::graphite_700));
        setColour (juce::TextEditor::focusedOutlineColourId,   juce::Colour (Zarlino::Palette::electric_blue));

        // Scroll bars
        setColour (juce::ScrollBar::thumbColourId,             juce::Colour (Zarlino::Palette::graphite_600));
        setColour (juce::ScrollBar::trackColourId,             juce::Colour (Zarlino::Palette::obsidian_900));

        // Tooltips
        setColour (juce::TooltipWindow::backgroundColourId,    juce::Colour (Zarlino::Palette::obsidian_900));
        setColour (juce::TooltipWindow::textColourId,          juce::Colour (Zarlino::Palette::mist_100));
        setColour (juce::TooltipWindow::outlineColourId,       juce::Colour (Zarlino::Palette::graphite_700));

        // Alert window
        setColour (juce::AlertWindow::backgroundColourId,      juce::Colour (Zarlino::Palette::obsidian_900));
        setColour (juce::AlertWindow::textColourId,            juce::Colour (Zarlino::Palette::mist_100));
        setColour (juce::AlertWindow::outlineColourId,         juce::Colour (Zarlino::Palette::graphite_700));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                                     static_cast<float> (width), static_cast<float> (height))
                               .reduced (4.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const auto trackRadius = radius * 0.72f;

        // Track arc
        juce::Path trackPath;
        trackPath.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f,
                                 rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (juce::Colour (Zarlino::Palette::graphite_700));
        g.strokePath (trackPath, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));

        // Filled arc
        juce::Path fillPath;
        fillPath.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f,
                                rotaryStartAngle, angle, true);
        g.setColour (juce::Colour (Zarlino::Palette::electric_blue));
        g.strokePath (fillPath, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved));

        // Indicator line
        const float indLen = radius * 0.75f;
        const float indX = centre.x + std::sin (angle) * indLen;
        const float indY = centre.y - std::cos (angle) * indLen;
        juce::Path ind;
        ind.startNewSubPath (centre.x, centre.y);
        ind.lineTo (indX, indY);
        g.setColour (juce::Colour (Zarlino::Palette::mist_100));
        g.strokePath (ind, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (2.0f);
        const bool active = button.getToggleState();

        // Pill background
        const auto pillColour = active ? juce::Colour (Zarlino::Palette::electric_blue)
                                       : juce::Colour (Zarlino::Palette::graphite_700);
        g.setColour (pillColour);
        g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);

        // Dot
        const float dotSize = bounds.getHeight() - 4.0f;
        const float dotX = active ? bounds.getRight() - dotSize - 2.0f : bounds.getX() + 2.0f;
        g.setColour (juce::Colour (Zarlino::Palette::mist_100));
        g.fillEllipse (dotX, bounds.getY() + 2.0f, dotSize, dotSize);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));
        g.setColour (juce::Colour (Zarlino::Palette::obsidian_900));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (juce::Colour (Zarlino::Palette::graphite_700));
        g.drawRoundedRectangle (bounds.reduced (1.0f), 4.0f, 1.0f);

        // Arrow
        juce::Path arrow;
        const float cx = static_cast<float> (width - 14);
        const float cy = static_cast<float> (height) * 0.5f;
        arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour (juce::Colour (Zarlino::Palette::steel_300));
        g.fillPath (arrow);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& /*bg*/, bool isMouseOver, bool isMouseDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        auto baseColour = juce::Colour (Zarlino::Palette::graphite_800);

        if (isMouseDown)
            baseColour = juce::Colour (Zarlino::Palette::graphite_600);
        else if (isMouseOver)
            baseColour = juce::Colour (Zarlino::Palette::graphite_700);

        if (button.getToggleState())
            baseColour = juce::Colour (Zarlino::Palette::electric_blue);

        g.setColour (baseColour);
        g.fillRoundedRectangle (bounds, 4.0f);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZarlinoLookAndFeel)
};
