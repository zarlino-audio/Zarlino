#include <JuceHeader.h>
#include "PluginProcessor.h"

int main()
{
    ZAdaptiveHarmonicsAudioProcessor proc;
    proc.prepareToPlay(44100.0, 256);

    juce::AudioBuffer<float> buffer(2, 256);
    juce::MidiBuffer midi;
    
    // Generate a sine wave
    for (int i = 0; i < 256; ++i)
        buffer.setSample(0, i, 0.5f * std::sin(2.0f * 3.14159f * 440.0f * i / 44100.0f));
    buffer.copyFrom(1, 0, buffer, 0, 0, 256);
    
    proc.processBlock(buffer, midi);
    proc.releaseResources();
    
    // Create editor
    auto* editor = proc.createEditor();
    delete editor;
    
    std::cout << "Smoke test passed." << std::endl;
    return 0;
}
