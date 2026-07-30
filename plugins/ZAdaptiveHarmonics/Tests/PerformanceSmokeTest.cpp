#include <JuceHeader.h>
#include "PluginProcessor.h"

int main()
{
    ZAdaptiveHarmonicsAudioProcessor proc;
    proc.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    // Fill with pink-ish noise
    juce::Random rng;
    for (int i = 0; i < 512; ++i)
    {
        buffer.setSample(0, i, rng.nextFloat() * 0.5f - 0.25f);
        buffer.setSample(1, i, rng.nextFloat() * 0.5f - 0.25f);
    }

    // Warm up
    for (int i = 0; i < 10; ++i)
        proc.processBlock(buffer, midi);

    // Measure
    constexpr int kIterations = 1000;
    const auto start = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < kIterations; ++i)
        proc.processBlock(buffer, midi);
    const auto end = juce::Time::getMillisecondCounterHiRes();

    const double durationMicros = (end - start) * 1000.0; // ms to µs
    const double avgMicros = durationMicros / kIterations;
    const double maxMicros = (512.0 / 44100.0) * 1e6; // time budget for 512 samples

    std::cout << "Average processBlock time: " << avgMicros << " us" << std::endl;
    std::cout << "Budget: " << maxMicros << " us" << std::endl;
    std::cout << "Usage: " << (avgMicros / maxMicros * 100.0) << "%" << std::endl;

    proc.releaseResources();

    if (avgMicros > maxMicros)
    {
        std::cerr << "FAIL: Exceeds real-time budget!" << std::endl;
        return 1;
    }

    std::cout << "Performance test passed." << std::endl;
    return 0;
}
