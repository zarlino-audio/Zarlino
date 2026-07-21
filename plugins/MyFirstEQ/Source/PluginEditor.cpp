#include "PluginEditor.h"
#include "PluginProcessor.h"

MyFirstEQAudioProcessorEditor::MyFirstEQAudioProcessorEditor (MyFirstEQAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (500, 300);
}

void MyFirstEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.setColour (juce::Colours::white);
    g.setFont (18.0f);
    g.drawFittedText ("My First EQ", getLocalBounds(), juce::Justification::centredTop, 1);
}

void MyFirstEQAudioProcessorEditor::resized() {}
