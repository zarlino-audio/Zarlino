#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <string>

namespace zfactory
{
namespace shell
{
namespace design
{

//==============================================================================
// The Zarlino design system.
//
// Every generated product is drawn from these tokens. A specification can choose
// its accent colour and its copy, but it cannot choose a layout, a font, a
// corner radius or a spacing: those are the product's visual identity and they
// belong to the vendor, not to the document. That is what keeps 3 templates x N
// specifications recognisably the same product family.
//==============================================================================

/** Fixed spacing scale. Every inset, gap and control size is a multiple of the
    base unit, so no template can drift into accidental misalignment. */
struct Metrics
{
    static constexpr int unit = 4;

    static constexpr int inset = unit * 4;        // 16
    static constexpr int gap = unit * 2;          // 8
    static constexpr int rowGap = unit * 3;       // 12

    static constexpr int knobSize = unit * 18;    // 72
    static constexpr int knobSizeCompact = unit * 14;   // 56
    static constexpr int faderWidth = unit * 14;  // 56
    static constexpr int faderHeight = unit * 24; // 96
    static constexpr int switchWidth = unit * 16; // 64
    static constexpr int switchHeight = unit * 8; // 32

    static constexpr int labelHeight = unit * 4;  // 16
    static constexpr int valueHeight = unit * 4;  // 16
    static constexpr int headerHeight = unit * 6; // 24
    static constexpr int headerGap = unit * 2;    // 8

    static constexpr int minControlWidth = unit * 20;   // 80
};

//==============================================================================
/** The product's colour set. Only `accent` comes from the specification; the
    rest are derived from it so that a product can never end up with an
    unreadable or off-brand palette. */
struct Palette
{
    juce::Colour background { 0xff14161a };
    juce::Colour panel { 0xff1c1f25 };
    juce::Colour panelAlt { 0xff23272e };
    juce::Colour outline { 0xff2f353d };
    juce::Colour text { 0xffe8eaed };
    juce::Colour textDim { 0xff9aa2ad };
    juce::Colour accent { 0xff4c9aff };

    /** Builds the palette from a "#RRGGBB" accent. An unparseable colour falls
        back to the Zarlino default rather than producing an invisible UI. */
    static Palette fromAccent (const std::string& hex);
};

//==============================================================================
// Typography. One family, three sizes, no per-template variation.

inline juce::Font titleFont()
{
    return juce::Font (juce::FontOptions (16.0f).withStyle ("Bold"));
}

inline juce::Font bodyFont()
{
    return juce::Font (juce::FontOptions (13.0f));
}

inline juce::Font bodyFontBold()
{
    return juce::Font (juce::FontOptions (13.0f).withStyle ("Bold"));
}

inline juce::Font labelFont()
{
    return juce::Font (juce::FontOptions (11.0f));
}

inline juce::Font valueFont()
{
    return juce::Font (juce::FontOptions (11.0f).withStyle ("Bold"));
}

//==============================================================================
// Shared painting so all three templates look like one product.

inline void paintPanel (juce::Graphics& g, juce::Rectangle<int> bounds, const Palette& palette,
                        bool emphasised)
{
    const auto corner = static_cast<float> (Metrics::unit * 2);
    const auto area = bounds.toFloat().reduced (0.5f);

    g.setColour (emphasised ? palette.panelAlt : palette.panel);
    g.fillRoundedRectangle (area, corner);

    g.setColour (palette.outline.withAlpha (emphasised ? 0.9f : 0.6f));
    g.drawRoundedRectangle (area, corner, 1.0f);
}

inline void paintHorizontalRule (juce::Graphics& g, juce::Rectangle<int> bounds, const Palette& palette)
{
    g.setColour (palette.outline.withAlpha (0.7f));
    g.fillRect (bounds.withHeight (1));
}

inline void paintFooterText (juce::Graphics& g, juce::Rectangle<int> bounds, const Palette& palette,
                             const juce::String& text)
{
    g.setColour (palette.textDim.withAlpha (0.75f));
    g.setFont (labelFont());
    g.drawText (text, bounds, juce::Justification::centredLeft, true);
}

/** Applies the palette to JUCE's own widget defaults, so sliders, combo boxes
    and buttons never show a stock JUCE colour in a Zarlino product. */
void applyLookAndFeel (juce::LookAndFeel& lookAndFeel, const Palette& palette);

} // namespace design
} // namespace shell
} // namespace zfactory
