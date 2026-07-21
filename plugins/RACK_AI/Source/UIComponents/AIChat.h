#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"

class AIChat : public juce::Component
{
public:
    explicit AIChat (RACK_AIAudioProcessor& processor);
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    RACK_AIAudioProcessor& mProcessor;
    std::unique_ptr<juce::TextEditor> mChatInput;
    std::unique_ptr<juce::TextEditor> mChatHistory;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIChat)
};
