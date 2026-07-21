#pragma once

#include <JuceHeader.h>
#include "../primitives/KriPaint.h"

class KriKnob : public juce::Slider
{
public:
    KriKnob()
    {
        setSliderStyle (juce::Slider::RotaryVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (8.0f);
        KriPaint::drawInsetWell (g, bounds.reduced (2.0f), bounds.getWidth() * 0.5f);

        float startAngle = juce::MathConstants<float>::pi * 1.25f;
        float endAngle   = juce::MathConstants<float>::pi * 2.75f;
        float valueAngle = startAngle + (float) valueToProportionOfLength (getValue()) * (endAngle - startAngle);

        juce::Path arcTrack;
        arcTrack.addCentredArc (bounds.getCentreX(), bounds.getCentreY(),
                                 bounds.getWidth() * 0.47f, bounds.getHeight() * 0.47f,
                                 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour (KriTheme::ACCENT_DIM));
        g.strokePath (arcTrack, juce::PathStrokeType (2.0f));

        juce::Path arcFill;
        arcFill.addCentredArc (bounds.getCentreX(), bounds.getCentreY(),
                                bounds.getWidth() * 0.47f, bounds.getHeight() * 0.47f,
                                0.0f, startAngle, valueAngle, true);
        g.setColour (juce::Colour (KriTheme::ACCENT));
        g.strokePath (arcFill, juce::PathStrokeType (2.5f));

        KriPaint::drawKnobCap (g, bounds.reduced (bounds.getWidth() * 0.18f), valueAngle);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KriKnob)
};
