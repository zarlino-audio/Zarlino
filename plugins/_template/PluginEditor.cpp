#include "PluginEditor.h"
#include "PluginProcessor.h"

TemplatePluginAudioProcessorEditor::TemplatePluginAudioProcessorEditor (TemplatePluginAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (400, 300);
}

void TemplatePluginAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::darkgrey);
    g.setColour (juce::Colours::white);
    g.setFont (15.0f);
    g.drawFittedText ("Template Plugin", getLocalBounds(), juce::Justification::centred, 1);
}

void TemplatePluginAudioProcessorEditor::resized() {}
