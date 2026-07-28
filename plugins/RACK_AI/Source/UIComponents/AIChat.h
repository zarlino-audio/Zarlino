#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"

class AIChat : public juce::Component
{
public:
    explicit AIChat (RackAIAudioProcessor& processor);
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    RackAIAudioProcessor& mProcessor;
    std::unique_ptr<juce::TextEditor> mChatInput;
    std::unique_ptr<juce::TextEditor> mChatHistory;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIChat)
};
