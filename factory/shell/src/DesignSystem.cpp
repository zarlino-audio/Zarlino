#include "zfactory/shell/DesignSystem.h"

namespace zfactory
{
namespace shell
{
namespace design
{

namespace
{

/** "#RRGGBB" or "#AARRGGBB"; anything else is rejected rather than guessed at. */
bool parseAccent (const std::string& hex, juce::Colour& out)
{
    if (hex.size() != 7 && hex.size() != 9)
        return false;

    if (hex[0] != '#')
        return false;

    for (size_t i = 1; i < hex.size(); ++i)
    {
        const auto character = hex[i];

        const auto isDecimalDigit = character >= '0' && character <= '9';
        const auto isLowerHex = character >= 'a' && character <= 'f';
        const auto isUpperHex = character >= 'A' && character <= 'F';

        if (! isDecimalDigit && ! isLowerHex && ! isUpperHex)
            return false;
    }

    out = juce::Colour::fromString (juce::String (hex));
    return true;
}

} // namespace

//==============================================================================
Palette Palette::fromAccent (const std::string& hex)
{
    Palette palette;

    juce::Colour accent;

    if (parseAccent (hex, accent))
        palette.accent = accent;

    // Everything else is derived, so a specification cannot produce an
    // unreadable interface by choosing an unusual accent.
    palette.background = palette.accent.withMultipliedSaturation (0.08f)
                             .withMultipliedBrightness (0.11f);
    palette.panel = palette.accent.withMultipliedSaturation (0.10f)
                        .withMultipliedBrightness (0.15f);
    palette.panelAlt = palette.accent.withMultipliedSaturation (0.12f)
                           .withMultipliedBrightness (0.19f);
    palette.outline = palette.accent.withMultipliedSaturation (0.18f)
                          .withMultipliedBrightness (0.28f);

    palette.text = juce::Colours::white.withMultipliedBrightness (0.94f);
    palette.textDim = palette.text.withAlpha (0.62f);

    return palette;
}

//==============================================================================
void applyLookAndFeel (juce::LookAndFeel& lookAndFeel, const Palette& palette)
{
    lookAndFeel.setColour (juce::Slider::rotarySliderFillColourId, palette.accent);
    lookAndFeel.setColour (juce::Slider::rotarySliderOutlineColourId, palette.outline);
    lookAndFeel.setColour (juce::Slider::thumbColourId, palette.accent);
    lookAndFeel.setColour (juce::Slider::trackColourId, palette.outline);
    lookAndFeel.setColour (juce::Slider::backgroundColourId, palette.panel);
    lookAndFeel.setColour (juce::Slider::textBoxTextColourId, palette.text);
    lookAndFeel.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour (juce::Slider::textBoxHighlightColourId, palette.accent.withAlpha (0.4f));

    lookAndFeel.setColour (juce::Label::textColourId, palette.text);
    lookAndFeel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    lookAndFeel.setColour (juce::ComboBox::backgroundColourId, palette.panelAlt);
    lookAndFeel.setColour (juce::ComboBox::textColourId, palette.text);
    lookAndFeel.setColour (juce::ComboBox::outlineColourId, palette.outline);
    lookAndFeel.setColour (juce::ComboBox::arrowColourId, palette.textDim);

    lookAndFeel.setColour (juce::PopupMenu::backgroundColourId, palette.panelAlt);
    lookAndFeel.setColour (juce::PopupMenu::textColourId, palette.text);
    lookAndFeel.setColour (juce::PopupMenu::highlightedBackgroundColourId, palette.accent);
    lookAndFeel.setColour (juce::PopupMenu::highlightedTextColourId, palette.background);

    lookAndFeel.setColour (juce::TextButton::buttonColourId, palette.panelAlt);
    lookAndFeel.setColour (juce::TextButton::buttonOnColourId, palette.accent);
    lookAndFeel.setColour (juce::TextButton::textColourOffId, palette.textDim);
    lookAndFeel.setColour (juce::TextButton::textColourOnId, palette.background);

    lookAndFeel.setColour (juce::TooltipWindow::backgroundColourId, palette.panelAlt);
    lookAndFeel.setColour (juce::TooltipWindow::textColourId, palette.text);
    lookAndFeel.setColour (juce::TooltipWindow::outlineColourId, palette.outline);
}

} // namespace design
} // namespace shell
} // namespace zfactory
