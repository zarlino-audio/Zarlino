/*
    Minimal crash isolation for boundary parameters
*/
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <iostream>

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::cout << "=== Crash Isolation ===" << std::endl;

    ZTameAudioProcessor proc;
    proc.prepareToPlay(44100.0, 512);

    auto& vts = proc.getValueTreeState();
    auto setP = [&](const char* id, float v) {
        if (auto* p = vts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(v));
    };

    // Set bypass off and other params
    setP("bypass", 0.0f);
    setP("sensitivity", 0.0f);
    setP("range", 12.0f);
    setP("selectivity", 50.0f);
    setP("crossoverFreq", 20.0f);  // use legal value, not 0.0
    setP("attack", 30.0f);
    setP("release", 200.0f);
    setP("dryWet", 100.0f);
    setP("listenMode", 0.0f);

    std::cout << "Params set. Creating buffer..." << std::endl;

    juce::AudioBuffer<float> buf(2, 512);
    for (int s = 0; s < 512; ++s)
    {
        auto val = std::sin(2.0 * 3.14159265 * 500.0 * s / 44100.0) * 0.5f;
        buf.setSample(0, s, val);
        buf.setSample(1, s, val);
    }

    std::cout << "Buffer created. Calling processBlock..." << std::endl;
    juce::MidiBuffer midi;
    proc.processBlock(buf, midi);
    std::cout << "processBlock completed. Checking output..." << std::endl;

    for (int ch = 0; ch < 2; ++ch)
    {
        int nans = 0, infs = 0;
        float maxAbs = 0.0f;
        for (int s = 0; s < 512; ++s)
        {
            auto v = buf.getSample(ch, s);
            if (std::isnan(v)) ++nans;
            else if (std::isinf(v)) ++infs;
            else maxAbs = std::max(maxAbs, std::abs(v));
        }
        std::cout << "  Channel " << ch << ": maxAbs=" << maxAbs
                  << " nans=" << nans << " infs=" << infs << std::endl;
    }

    // Now try incrementally changing params
    std::cout << "\nTesting sensitivity sweep..." << std::endl;
    float sensitivities[] = {0.0f, 50.0f, 100.0f};
    for (auto sens : sensitivities)
    {
        setP("sensitivity", sens);
        juce::AudioBuffer<float> b(2, 512);
        b.makeCopyOf(buf);
        juce::MidiBuffer m;
        std::cout << "  sensitivity=" << sens << " ... " << std::flush;
        proc.processBlock(b, m);
        std::cout << "OK (maxAbs=" << b.getMagnitude(0, 0, 512) << ")" << std::endl;
    }

    std::cout << "\nTesting crossoverFreq sweep..." << std::endl;
    float xovers[] = {20.0f, 1000.0f, 20000.0f};
    for (auto xover : xovers)
    {
        setP("crossoverFreq", xover);
        juce::AudioBuffer<float> b(2, 512);
        b.makeCopyOf(buf);
        juce::MidiBuffer m;
        std::cout << "  crossoverFreq=" << xover << " ... " << std::flush;
        proc.processBlock(b, m);
        std::cout << "OK" << std::endl;
    }

    std::cout << "\nTesting dryWet sweep..." << std::endl;
    float dryWets[] = {0.0f, 50.0f, 100.0f};
    for (auto dw : dryWets)
    {
        setP("dryWet", dw);
        juce::AudioBuffer<float> b(2, 512);
        b.makeCopyOf(buf);
        juce::MidiBuffer m;
        std::cout << "  dryWet=" << dw << " ... " << std::flush;
        proc.processBlock(b, m);
        std::cout << "OK" << std::endl;
    }

    std::cout << "\n=== All isolation tests passed ===" << std::endl;
    return 0;
}
