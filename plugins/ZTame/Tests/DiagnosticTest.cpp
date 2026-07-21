/*
    Quick diagnostic: isolate bypass passthrough and boundary param behavior
*/
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <iostream>
#include <cmath>

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    // ---- Test 1: Bypass passthrough ----
    std::cout << "=== Test 1: Bypass passthrough ===" << std::endl;
    {
        ZTameAudioProcessor proc;
        proc.prepareToPlay(44100.0, 512);

        auto& vts = proc.getValueTreeState();
        if (auto* p = vts.getParameter("bypass"))
            p->setValueNotifyingHost(p->convertTo0to1(1.0f));

        // Verify bypass is active
        auto rawBypass = vts.getRawParameterValue("bypass");
        std::cout << "  Raw bypass value: " << (rawBypass ? rawBypass->load() : -1.0f) << std::endl;

        juce::AudioBuffer<float> buf(2, 512);
        for (int s = 0; s < 512; ++s)
            for (int ch = 0; ch < 2; ++ch)
                buf.setSample(ch, s, std::sin(2.0 * 3.14159265 * 440.0 * s / 44100.0) * 0.7f);

        auto ref = juce::AudioBuffer<float>(2, 512);
        ref.makeCopyOf(buf);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);

        int mismatches = 0;
        for (int ch = 0; ch < 2; ++ch)
        {
            float maxDiff = 0.0f;
            for (int s = 0; s < 512; ++s)
            {
                auto diff = std::abs(buf.getSample(ch, s) - ref.getSample(ch, s));
                maxDiff = std::max(maxDiff, diff);
                if (diff >= 1e-7f) ++mismatches;
            }
            std::cout << "  Channel " << ch << " max diff: " << maxDiff << std::endl;
        }
        std::cout << "  Total mismatches: " << mismatches << "/1024" << std::endl;
        std::cout << "  Bypass " << (mismatches == 0 ? "PASSED" : "FAILED") << std::endl;
    }

    // ---- Test 2: Boundary parameters ----
    std::cout << "\n=== Test 2: Boundary parameters ===" << std::endl;
    {
        ZTameAudioProcessor proc;
        proc.prepareToPlay(44100.0, 512);

        auto& vts = proc.getValueTreeState();

        struct ParamRange { const char* id; float min; float max; float def; };
        ParamRange params[] = {
            {"sensitivity", 0.0f, 100.0f, 50.0f},
            {"range", 0.0f, 24.0f, 12.0f},
            {"selectivity", 0.0f, 100.0f, 50.0f},
            {"crossoverFreq", 20.0f, 20000.0f, 0.0f},
            {"attack", 0.1f, 100.0f, 30.0f},
            {"release", 10.0f, 1000.0f, 200.0f},
            {"dryWet", 0.0f, 100.0f, 100.0f},
        };

        for (auto& pr : params)
        {
            for (float val : {pr.min, pr.max})
            {
                // Reset all params to defaults
                for (auto& reset : params)
                {
                    if (auto* p = vts.getParameter(reset.id))
                        p->setValueNotifyingHost(p->convertTo0to1(reset.def));
                }
                if (auto* p = vts.getParameter("bypass"))
                    p->setValueNotifyingHost(p->convertTo0to1(0.0f));
                if (auto* p = vts.getParameter(pr.id))
                    p->setValueNotifyingHost(p->convertTo0to1(val));

                std::cout << "  Testing " << pr.id << " = " << val << " ... " << std::flush;

                juce::AudioBuffer<float> buf(2, 512);
                for (int s = 0; s < 512; ++s)
                    for (int ch = 0; ch < 2; ++ch)
                        buf.setSample(ch, s, std::sin(2.0 * 3.14159265 * 500.0 * s / 44100.0) * 0.5f);

                juce::MidiBuffer midi;
                proc.processBlock(buf, midi);

                bool ok = true;
                for (int ch = 0; ch < 2 && ok; ++ch)
                {
                    for (int s = 0; s < 512 && ok; ++s)
                    {
                        auto sample = buf.getSample(ch, s);
                        if (std::isnan(sample)) { std::cout << "NaN!" << std::endl; ok = false; }
                        else if (std::isinf(sample)) { std::cout << "Inf!" << std::endl; ok = false; }
                        else if (std::abs(sample) > 10.0f) { std::cout << "Overflow: " << sample << std::endl; ok = false; }
                    }
                }
                if (ok) std::cout << "OK" << std::endl;
            }
        }
        std::cout << "  Boundary test complete." << std::endl;
    }

    // ---- Test 3: Frequency response with zero params ----
    std::cout << "\n=== Test 3: Frequency response (sensitivity=0, range=0) ===" << std::endl;
    {
        ZTameAudioProcessor proc;
        proc.prepareToPlay(48000.0, 512);

        auto& vts = proc.getValueTreeState();
        auto setP = [&](const char* id, float v) {
            if (auto* p = vts.getParameter(id))
                p->setValueNotifyingHost(p->convertTo0to1(v));
        };
        setP("bypass", 0.0f);
        setP("sensitivity", 0.0f);
        setP("range", 0.0f);
        setP("dryWet", 100.0f);

        constexpr int N = 8192;
        juce::AudioBuffer<float> buf(2, N);
        for (int s = 0; s < N; ++s)
            buf.setSample(0, s, std::sin(2.0 * 3.14159265 * 1000.0 * s / 48000.0) * 0.5f);
        buf.copyFrom(1, 0, buf, 0, 0, N);

        juce::AudioBuffer<float> ref(2, N);
        ref.makeCopyOf(buf);

        for (int offset = 0; offset < N; offset += 512)
        {
            juce::AudioBuffer<float> block(2, 512);
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom(ch, 0, buf, ch, offset, 512);
            juce::MidiBuffer midi;
            proc.processBlock(block, midi);
            for (int ch = 0; ch < 2; ++ch)
                buf.copyFrom(ch, offset, block, ch, 0, 512);
        }

        constexpr int trimStart = 2048;
        constexpr int trimEnd = 1024;
        double inSumSq = 0.0, outSumSq = 0.0;
        for (int s = trimStart; s < N - trimEnd; ++s)
        {
            inSumSq += static_cast<double>(ref.getSample(0, s) * ref.getSample(0, s));
            outSumSq += static_cast<double>(buf.getSample(0, s) * buf.getSample(0, s));
        }
        int validLen = N - trimStart - trimEnd;
        auto inRmsDb = juce::Decibels::gainToDecibels(std::sqrt(inSumSq / validLen), -120.0f);
        auto outRmsDb = juce::Decibels::gainToDecibels(std::sqrt(outSumSq / validLen), -120.0f);
        std::cout << "  Input RMS: " << inRmsDb << " dBFS" << std::endl;
        std::cout << "  Output RMS: " << outRmsDb << " dBFS" << std::endl;
        std::cout << "  Attenuation: " << (inRmsDb - outRmsDb) << " dB" << std::endl;
    }

    std::cout << "\n=== Diagnostic complete ===" << std::endl;
    return 0;
}
