#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
    constexpr int kEditorWidth  = 620;
    constexpr int kEditorHeight = 450;

    juce::Colour accentColour()       { return juce::Colour (0xff53b7ff); }
    juce::Colour bgColour()           { return juce::Colour (0xff1a1a2e); }
    juce::Colour panelBgColour()      { return juce::Colour (0x66131a2e); }

    juce::Label* makePlaceholderLabel (const juce::String& text,
                                       juce::Component& parent)
    {
        auto* label = new juce::Label();
        label->setText (text, juce::dontSendNotification);
        label->setJustificationType (juce::Justification::centred);
        label->setFont (juce::Font (juce::FontOptions (16.0f)));
        label->setColour (juce::Label::textColourId,
                          juce::Colours::white.withAlpha (0.5f));
        parent.addAndMakeVisible (label);
        return label;
    }
}

//==============================================================================
class MacroTabContent final : public juce::Component
{
public:
    explicit MacroTabContent (ZAdaptiveHarmonicsAudioProcessorEditor& owner)
        : editor (owner) {}

    void resized() override
    {
        auto area = getLocalBounds().reduced (20, 20);
        const auto slotH = area.getHeight() / 3;
        editor.liftLabel.setBounds      (area.removeFromTop (slotH).reduced (20, 8));
        editor.characterLabel.setBounds (area.removeFromTop (slotH).reduced (20, 8));
        editor.mixLabel.setBounds       (area.reduced (20, 8));
    }

private:
    ZAdaptiveHarmonicsAudioProcessorEditor& editor;
};

//==============================================================================
class AdvancedTabContent final : public juce::Component
{
public:
    explicit AdvancedTabContent (ZAdaptiveHarmonicsAudioProcessorEditor& owner)
        : editor (owner) {}

    void resized() override
    {
        auto area = getLocalBounds().reduced (16, 12);
        const auto rowH = area.getHeight() / 4;

        // Row 1: Saturator section header
        auto row1 = area.removeFromTop (rowH);
        editor.saturatorLabel.setBounds (row1.reduced (8, 4));

        // Row 2: Saturator Even / Odd / Harm Reduction
        auto row2 = area.removeFromTop (rowH).reduced (4, 0);
        const auto colW2 = row2.getWidth() / 3;
        editor.satEvenLabel.setBounds          (row2.removeFromLeft (colW2).reduced (4, 4));
        editor.satOddLabel.setBounds           (row2.removeFromLeft (colW2).reduced (4, 4));
        editor.satHarmReductionLabel.setBounds (row2.reduced (4, 4));

        // Row 3: Tilt section
        auto row3 = area.removeFromTop (rowH);
        editor.tiltLabel.setBounds (row3.reduced (8, 4));

        // Row 4: Exciter section
        auto row4 = area;
        auto col4 = row4.getWidth() / 2;
        editor.exciterLabel.setBounds        (row4.removeFromLeft (col4).reduced (8, 4));
        editor.exciterEnabledLabel.setBounds (row4.reduced (8, 4));
    }

private:
    ZAdaptiveHarmonicsAudioProcessorEditor& editor;
};

//==============================================================================
ZAdaptiveHarmonicsAudioProcessorEditor::ZAdaptiveHarmonicsAudioProcessorEditor (
    ZAdaptiveHarmonicsAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      tabs (juce::TabbedButtonBar::TabsAtTop)
{
    // --- Macro tab ---
    makePlaceholderLabel ("Lift",      *this)->setBounds (0, 0, 1, 1);
    makePlaceholderLabel ("Character", *this)->setBounds (0, 0, 1, 1);
    makePlaceholderLabel ("Mix",       *this)->setBounds (0, 0, 1, 1);

    auto* macroContent = new MacroTabContent (*this);
    tabs.addTab ("Macro", bgColour(), macroContent, true);
    macroContent->addAndMakeVisible (liftLabel);
    macroContent->addAndMakeVisible (characterLabel);
    macroContent->addAndMakeVisible (mixLabel);

    // --- Advanced tab ---
    makePlaceholderLabel ("Saturator",      *this)->setBounds (0, 0, 1, 1);
    makePlaceholderLabel ("Even Harmonics", *this)->setBounds (0, 0, 1, 1);
    makePlaceholderLabel ("Odd Harmonics",  *this)->setBounds (0, 0, 1, 1);
    makePlaceholderLabel ("Harm Reduction", *this)->setBounds (0, 0, 1, 1);
    makePlaceholderLabel ("Spectral Tilt",  *this)->setBounds (0, 0, 1, 1);
    makePlaceholderLabel ("Exciter Amt",    *this)->setBounds (0, 0, 1, 1);
    makePlaceholderLabel ("Exciter On/Off", *this)->setBounds (0, 0, 1, 1);

    auto* advContent = new AdvancedTabContent (*this);
    tabs.addTab ("Advanced", bgColour(), advContent, true);
    advContent->addAndMakeVisible (saturatorLabel);
    advContent->addAndMakeVisible (satEvenLabel);
    advContent->addAndMakeVisible (satOddLabel);
    advContent->addAndMakeVisible (satHarmReductionLabel);
    advContent->addAndMakeVisible (tiltLabel);
    advContent->addAndMakeVisible (exciterLabel);
    advContent->addAndMakeVisible (exciterEnabledLabel);

    tabs.setCurrentTabIndex (0); // Macro is default
    tabs.setTabBarDepth (28);
    addAndMakeVisible (tabs);

    setSize (kEditorWidth, kEditorHeight);
}

//==============================================================================
void ZAdaptiveHarmonicsAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Dark background
    g.setColour (bgColour());
    g.fillRoundedRectangle (bounds, 18.0f);

    // Subtle border
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (bounds.reduced (1.0f), 18.0f, 1.0f);

    // Title area
    const int titleTop    = 8;
    const int titleHeight = 36;
    g.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)));
    g.setColour (juce::Colours::white);
    g.drawText ("ZAdaptiveHarmonics", 0, titleTop, getWidth(), titleHeight,
                juce::Justification::centred, false);

    // Accent bar under title
    auto accentBar = juce::Rectangle<int> (40, titleTop + titleHeight + 6,
                                           getWidth() - 80, 3);
    g.setColour (accentColour());
    g.fillRoundedRectangle (accentBar.toFloat(), 1.5f);

    // Subtitle
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.setColour (juce::Colour (0xff8edcff));
    g.drawText ("adaptive harmonic processor • Zarlino Audio",
                0, titleTop + titleHeight + 14, getWidth(), 18,
                juce::Justification::centred, false);
}

//==============================================================================
void ZAdaptiveHarmonicsAudioProcessorEditor::resized()
{
    const int tabTop = 95;
    tabs.setBounds (0, tabTop, getWidth(), getHeight() - tabTop);
}
