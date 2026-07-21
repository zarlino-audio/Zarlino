#include "SlotList.h"

SlotList::SlotList (RACK_AIAudioProcessor& processor) : mProcessor (processor) {}

void SlotList::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::darkgrey);
    g.setColour (juce::Colours::white);
    g.drawText ("Slot List", getLocalBounds(), juce::Justification::centred);
}

void SlotList::resized() {}
