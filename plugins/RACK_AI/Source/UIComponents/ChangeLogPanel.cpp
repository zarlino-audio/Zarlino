#include "ChangeLogPanel.h"

ChangeLogPanel::ChangeLogPanel (RACK_AIAudioProcessor& processor) : mProcessor (processor) {}

void ChangeLogPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::darkgrey);
    g.setColour (juce::Colours::white);
    g.drawText ("Change Log", getLocalBounds(), juce::Justification::centred);
}

void ChangeLogPanel::resized() {}
