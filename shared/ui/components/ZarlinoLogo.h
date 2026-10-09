#pragma once

#include <JuceHeader.h>

//==============================================================================
//  Shared drawn-logo component — renders the Zarlino wordmark without needing
//  any external image/binary data. Drop-in across all plugin editors.
//==============================================================================
struct ZarlinoLogoComponent : juce::Component
{
    ZarlinoLogoComponent() = default;

    explicit ZarlinoLogoComponent (juce::Colour accent, juce::Colour text)
        : accentColour (accent), textColour (text) {}

    void paint (juce::Graphics& g) override
    {
        const auto w = static_cast<float> (getWidth());
        const auto h = static_cast<float> (getHeight());

        const auto accentWidth = juce::jlimit (4.0f, 7.0f, h * 0.14f);
        const auto leftPadding = juce::jmax (10.0f, h * 0.28f);

        g.setColour (accentColour);
        g.fillRoundedRectangle (0.0f, 0.0f, accentWidth, h, accentWidth * 0.5f);

        g.setColour (textColour);
        g.setFont (juce::Font (juce::FontOptions (juce::jlimit (14.0f, 24.0f, h * 0.52f), juce::Font::bold)));
        g.drawText ("ZARLINO", leftPadding, 0.0f, w - leftPadding, h,
                    juce::Justification::centredLeft, false);
    }

    juce::Colour accentColour { 0xff35d0ba };  // Zarlino cyan
    juce::Colour textColour   { 0xffeef3f7 };  // near-white
};
