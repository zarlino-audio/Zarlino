#include "AIChat.h"

AIChat::AIChat (RackAIAudioProcessor& processor) : mProcessor (processor)
{
    mChatInput = std::make_unique<juce::TextEditor>();
    mChatInput->setMultiLine (true);
    addAndMakeVisible (*mChatInput);
    
    mChatHistory = std::make_unique<juce::TextEditor>();
    mChatHistory->setReadOnly (true);
    mChatHistory->setMultiLine (true);
    addAndMakeVisible (*mChatHistory);
}

void AIChat::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::darkgrey);
}

void AIChat::resized()
{
    auto bounds = getLocalBounds();
    mChatHistory->setBounds (bounds.removeFromTop (bounds.getHeight() - 100));
    mChatInput->setBounds (bounds);
}
