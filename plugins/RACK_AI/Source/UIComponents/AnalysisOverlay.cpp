#include "AnalysisOverlay.h"

AnalysisOverlay::AnalysisOverlay (RackAIAudioProcessor& processor) : mProcessor (processor) {}

void AnalysisOverlay::paint (juce::Graphics& g)
{
    // Draw spectrum and analysis data
    g.fillAll (juce::Colours::black.withAlpha (0.3f));
}
