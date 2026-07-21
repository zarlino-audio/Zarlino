#pragma once
#include <JuceHeader.h>
#include "../theme/KriTheme.h"

class KriPaint
{
public:
    static void drawRaisedPanel (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        juce::ColourGradient bodyGrad (
            juce::Colour (KriTheme::FACEPLATE_EDGE), bounds.getX(), bounds.getY(),
            juce::Colour (KriTheme::FACEPLATE_SHADE), bounds.getX(), bounds.getBottom(),
            false);
        g.setGradientFill (bodyGrad);
        g.fillRoundedRectangle (bounds, KriTheme::PANEL_CORNER_RADIUS);

        g.setColour (juce::Colour (0xFF606878));
        g.drawLine (bounds.getX() + KriTheme::PANEL_CORNER_RADIUS, bounds.getY() + 0.5f,
                    bounds.getRight() - KriTheme::PANEL_CORNER_RADIUS, bounds.getY() + 0.5f, 1.0f);

        g.setColour (juce::Colour (0xFF0A0B0F));
        g.drawLine (bounds.getX() + KriTheme::PANEL_CORNER_RADIUS, bounds.getBottom() - 0.5f,
                    bounds.getRight() - KriTheme::PANEL_CORNER_RADIUS, bounds.getBottom() - 0.5f, 1.0f);
    }

    static void drawKnobCap (juce::Graphics& g, juce::Rectangle<float> bounds, float angleRad)
    {
        float cx = bounds.getCentreX();
        float cy = bounds.getCentreY();
        float r  = bounds.getWidth() * 0.5f;

        juce::Path shadow;
        shadow.addEllipse (bounds.translated (KriTheme::KNOB_SHADOW_OFFSET,
                                            KriTheme::KNOB_SHADOW_OFFSET));
        juce::DropShadow ds (juce::Colour (0xAA000000), (int)KriTheme::KNOB_SHADOW_BLUR, {});
        ds.drawForPath (g, shadow);

        g.setColour (juce::Colour (KriTheme::KNOB_SKIRT));
        g.fillEllipse (bounds.expanded (3.0f));

        juce::ColourGradient capGrad (
            juce::Colour (0xFF3A3E4A), cx - r * 0.3f, cy - r * 0.3f,
            juce::Colour (KriTheme::KNOB_BODY), cx, cy,
            true);
        g.setGradientFill (capGrad);
        g.fillEllipse (bounds);

        float hx = cx - r * 0.28f;
        float hy = cy - r * 0.32f;
        juce::ColourGradient specGrad (
            juce::Colour (KriTheme::KNOB_SPECULAR).withAlpha (0.7f), hx, hy,
            juce::Colour (KriTheme::KNOB_SPECULAR).withAlpha (0.0f), hx, hy + r * 0.35f,
            true);
        g.setGradientFill (specGrad);
        g.fillEllipse (hx - r * 0.12f, hy - r * 0.08f, r * 0.24f, r * 0.16f);

        float dotDist = r * 0.62f;
        float dotX = cx + std::sin (angleRad) * dotDist;
        float dotY = cy - std::cos (angleRad) * dotDist;
        g.setColour (juce::Colour (KriTheme::ACCENT));
        g.fillEllipse (dotX - 2.5f, dotY - 2.5f, 5.0f, 5.0f);
    }

    static void drawInsetWell (juce::Graphics& g, juce::Rectangle<float> bounds, float radius = 3.0f)
    {
        g.setColour (juce::Colour (0xFF131518));
        g.fillRoundedRectangle (bounds, radius);

        g.setColour (juce::Colour (0xFF000000).withAlpha (0.6f));
        g.drawLine (bounds.getX() + radius, bounds.getY() + 0.5f,
                    bounds.getRight() - radius, bounds.getY() + 0.5f, 1.0f);

        g.setColour (juce::Colour (0xFF404858).withAlpha (0.4f));
        g.drawLine (bounds.getX() + radius, bounds.getBottom() - 0.5f,
                    bounds.getRight() - radius, bounds.getBottom() - 0.5f, 1.0f);
    }

    static void drawLED (juce::Graphics& g, juce::Rectangle<float> bounds,
                         juce::Colour colour, bool isOn)
    {
        float cx = bounds.getCentreX();
        float cy = bounds.getCentreY();
        float r  = bounds.getWidth() * 0.5f;

        if (isOn)
        {
            juce::ColourGradient glow (
                colour.withAlpha (0.35f), cx, cy,
                colour.withAlpha (0.0f), cx, cy + r * 2.5f,
                true);
            g.setGradientFill (glow);
            g.fillEllipse (bounds.expanded (r * 1.5f));
        }

        juce::Colour body = isOn ? colour : colour.withBrightness (0.12f).withSaturation (0.3f);
        g.setColour (body);
        g.fillEllipse (bounds);

        if (isOn)
        {
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.fillEllipse (cx - r * 0.25f, cy - r * 0.35f, r * 0.35f, r * 0.25f);
        }
    }
};
