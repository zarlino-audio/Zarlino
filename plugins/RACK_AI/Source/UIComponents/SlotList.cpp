#include "SlotList.h"

SlotList::SlotList (RackAIAudioProcessor& processor) : mProcessor (processor) {}

void SlotList::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::darkgrey);
    g.setColour (juce::Colours::white);
    g.drawText ("Slot List", getLocalBounds(), juce::Justification::centred);
}

void SlotList::resized() {}
