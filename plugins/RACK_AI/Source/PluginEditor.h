#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class RackAIAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer
{
public:
    explicit RackAIAudioProcessorEditor (RackAIAudioProcessor&);
    ~RackAIAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class SlotRow;
    class DecisionLogTable;

    void timerCallback() override;
    void refreshSlotRows();
    void refreshChat();
    void refreshAnalysisLabels();
    void submitPrompt();
    void layoutQuickPrompts (juce::Rectangle<int> area);

    RackAIAudioProcessor& audioProcessor;

    juce::OwnedArray<SlotRow> slotRows;
    juce::Viewport slotViewport;
    juce::Component slotContent;

    juce::TextButton addSlotButton { "Add Slot" };
    juce::ToggleButton analysisToggle { "Analysis" };
    juce::TextButton autoButton { "Auto" };
    juce::TextButton suggestButton { "Suggest" };

    juce::Label titleLabel;
    juce::Label rackSummaryLabel;
    juce::Label analysisLabel;
    juce::TextEditor intentEditor;
    juce::TextEditor chatHistory;
    juce::TextEditor promptEditor;
    juce::TextButton sendButton { "Send" };
    std::array<std::unique_ptr<juce::TextButton>, 5> quickPromptButtons;

    juce::TabbedComponent bottomTabs { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<DecisionLogTable> logTable;
    juce::Component pluginHostPlaceholder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackAIAudioProcessorEditor)
};
