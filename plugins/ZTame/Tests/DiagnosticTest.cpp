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

    // ---- Test A: Minimal FFT chain test (heap, no destructor) ----
    std::cout << "=== Test A: Minimal FFT chain test (heap) ===" << std::endl;
    {
        auto* proc = new ZTameAudioProcessor();
        std::cout << "  prepareToPlay..." << std::endl;
        proc->prepareToPlay(44100.0, 512);
        std::cout << "  Creating buffer..." << std::endl;
        
        juce::AudioBuffer<float> buf(2, 512);
        buf.clear();
        for (int s = 0; s < 512; ++s)
            buf.setSample(0, s, std::sin(2.0 * 3.14159265 * 440.0 * s / 44100.0) * 0.1f);
        buf.copyFrom(1, 0, buf, 0, 0, 512);
        
        std::cout << "  processBlock..." << std::flush;
        juce::MidiBuffer midi;
        proc->processBlock(buf, midi);
        std::cout << " DONE" << std::endl;
        std::cout << "  Output sample[0]: " << buf.getSample(0, 0) << std::endl;
        
        std::cout << "  Second processBlock..." << std::flush;
        proc->processBlock(buf, midi);
        std::cout << " DONE" << std::endl;
        
        // Intentionally leak to avoid APVTS destructor issues in console context
        // (same pattern used by the main test suite)
    }
    std::cout << "  Test A scope ended." << std::endl;

    // ---- Test C: Boundary parameters (heap, verbose) ----
    std::cout << "\n=== Test C: Boundary parameters ===" << std::endl;
    {
        auto* proc = new ZTameAudioProcessor();
        proc->prepareToPlay(44100.0, 512);

        auto& vts = proc->getValueTreeState();
        auto setP = [&](const char* id, float v) {
            if (auto* p = vts.getParameter(id))
                p->setValueNotifyingHost(p->convertTo0to1(v));
        };

        struct ParamRange { const char* id; float min; float max; float def; };
        ParamRange params[] = {
            {"sensitivity", 0.0f, 100.0f, 50.0f},
            {"range", 0.0f, 24.0f, 12.0f},
            {"selectivity", 0.0f, 100.0f, 50.0f},
            {"crossoverFreq", 20.0f, 20000.0f, 1000.0f},
            {"attack", 0.1f, 100.0f, 30.0f},
            {"release", 10.0f, 1000.0f, 200.0f},
            {"dryWet", 0.0f, 100.0f, 100.0f},
        };

        for (auto& pr : params)
        {
            for (float val : {pr.min, pr.max})
            {
                std::cout << "  Testing " << pr.id << " = " << val << " ... " << std::flush;

                // Reset all params to defaults
                for (auto& reset : params)
                    setP(reset.id, reset.def);
                setP("bypass", 0.0f);
                setP(pr.id, val);

                juce::AudioBuffer<float> buf(2, 512);
                for (int s = 0; s < 512; ++s)
                    buf.setSample(0, s, std::sin(2.0 * 3.14159265 * 500.0 * s / 44100.0) * 0.5f);
                buf.copyFrom(1, 0, buf, 0, 0, 512);

                juce::MidiBuffer midi;
                proc->processBlock(buf, midi);

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

    // ---- Test D: Frequency response measurement ----
    std::cout << "\n=== Test D: Frequency response measurement ===" << std::endl;
    {
        auto* proc = new ZTameAudioProcessor();
        constexpr double sr = 48000.0;
        constexpr int N = 8192;
        proc->prepareToPlay(sr, 512);

        auto& vts = proc->getValueTreeState();
        auto setP = [&](const char* id, float v) {
            if (auto* p = vts.getParameter(id))
                p->setValueNotifyingHost(p->convertTo0to1(v));
        };
        setP("bypass", 0.0f);
        setP("sensitivity", 0.0f);
        setP("range", 0.0f);
        setP("dryWet", 100.0f);

        juce::AudioBuffer<float> inBuf(2, N);
        for (int s = 0; s < N; ++s)
            inBuf.setSample(0, s, std::sin(2.0 * 3.14159265 * 1000.0 * s / sr) * 0.5f);
        inBuf.copyFrom(1, 0, inBuf, 0, 0, N);

        juce::AudioBuffer<float> refBuf;
        refBuf.makeCopyOf(inBuf);
        juce::MidiBuffer midi;

        for (int offset = 0; offset < N; offset += 512)
        {
            juce::AudioBuffer<float> block(2, 512);
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom(ch, 0, inBuf, ch, offset, 512);
            proc->processBlock(block, midi);
            for (int ch = 0; ch < 2; ++ch)
                inBuf.copyFrom(ch, offset, block, ch, 0, 512);
        }

        constexpr int trimStart = 2048;
        constexpr int trimEnd = 1024;
        constexpr int validLen = N - trimStart - trimEnd;

        double inSumSq = 0.0, outSumSq = 0.0;
        for (int s = trimStart; s < N - trimEnd; ++s)
        {
            inSumSq += static_cast<double>(refBuf.getSample(0, s)) * refBuf.getSample(0, s);
            outSumSq += static_cast<double>(inBuf.getSample(0, s)) * inBuf.getSample(0, s);
        }
        float inRmsDb = juce::Decibels::gainToDecibels(static_cast<float>(std::sqrt(inSumSq / validLen)), -120.0f);
        float outRmsDb = juce::Decibels::gainToDecibels(static_cast<float>(std::sqrt(outSumSq / validLen)), -120.0f);
        float attenDb = inRmsDb - outRmsDb;
        std::cout << "  Input RMS: " << inRmsDb << " dBFS" << std::endl;
        std::cout << "  Output RMS: " << outRmsDb << " dBFS" << std::endl;
        std::cout << "  Attenuation: " << attenDb << " dB" << std::endl;

        // Also check: UN-TRIMMED output
        float peakRaw = 0.0f;
        int peakRawSample = 0;
        for (int s = 0; s < N; ++s) {
            float absVal = std::abs(inBuf.getSample(0, s));
            if (absVal > peakRaw) { peakRaw = absVal; peakRawSample = s; }
        }
        std::cout << "  Output peak (full, untrimmed): " << peakRaw << " at sample " << peakRawSample << std::endl;
        
        // Show raw output every 256 samples
        std::cout << "  Raw output (every 256):" << std::endl;
        for (int s = 0; s < N; s += 256) {
            std::cout << "    s=" << s << ": " << inBuf.getSample(0, s);
            if (s > 0) std::cout << " (diff prev: " << (inBuf.getSample(0, s) - inBuf.getSample(0, s-256)) << ")";
            std::cout << std::endl;
        }
        
        // Check output at various positions
        std::cout << "  Output samples at key positions:" << std::endl;
        for (int pos : {0, 512, 1024, 1536, 2048, 2560, 3072, 4096, 5120, 6144, 7168}) {
            if (pos < N)
                std::cout << "    s=" << pos << ": " << inBuf.getSample(0, pos) << std::endl;
        }
        
        // Check reference at same positions
        std::cout << "  Reference samples:" << std::endl;
        for (int pos : {0, 1024, 2048, 3072, 4096}) {
            if (pos < N)
                std::cout << "    s=" << pos << ": " << refBuf.getSample(0, pos) << std::endl;
        }
    }

    // ---- Test E: FFT/IFFT round-trip gain measurement ----
    std::cout << "\n=== Test E: FFT/IFFT round-trip gain ===" << std::endl;
    {
        constexpr int fftSize = 2048;
        constexpr int hopSize = 1024;
        constexpr double sr = 48000.0;

        juce::dsp::FFT fft(11); // order 11 = 2048

        // Create a Hann-windowed sine
        std::vector<float> frame(fftSize * 2, 0.0f); // complex interleaved
        std::vector<float> window(fftSize);
        for (int i = 0; i < fftSize; ++i)
        {
            window[i] = 0.5f * (1.0f - std::cos(2.0 * 3.14159265358979 * i / (fftSize - 1)));
            float sig = std::sin(2.0 * 3.14159265358979 * 1000.0 * i / sr) * 0.5f;
            frame[i * 2] = sig * window[i]; // real part
            frame[i * 2 + 1] = 0.0f;        // imag part
        }

        // Measure input RMS after windowing
        double inSumSq = 0.0;
        for (int i = 0; i < fftSize; ++i)
            inSumSq += frame[i * 2] * frame[i * 2];
        float inRms = std::sqrt(static_cast<float>(inSumSq / fftSize));
        std::cout << "  Input (windowed sine) RMS: " << juce::Decibels::gainToDecibels(inRms, -120.0f) << " dB"
                  << " (linear: " << inRms << ")" << std::endl;

        // Forward FFT
        fft.performRealOnlyForwardTransform(frame.data());

        // Magnitude of first few bins
        std::cout << "  After FFT - bin magnitudes: ";
        for (int b = 0; b < 5; ++b)
            std::cout << std::sqrt(frame[b * 2] * frame[b * 2] + frame[b * 2 + 1] * frame[b * 2 + 1]) << " ";
        std::cout << std::endl;

        // Inverse FFT
        fft.performRealOnlyInverseTransform(frame.data());

        // Measure IFFT output RMS
        double outSumSq = 0.0;
        for (int i = 0; i < fftSize; ++i)
            outSumSq += frame[i * 2] * frame[i * 2];
        float outRms = std::sqrt(static_cast<float>(outSumSq / fftSize));
        float outPeak = 0.0f;
        int peakIdx = 0;
        for (int i = 0; i < fftSize; ++i) {
            float absVal = std::abs(frame[i * 2]);
            if (absVal > outPeak) { outPeak = absVal; peakIdx = i; }
        }
        std::cout << "  IFFT output RMS: " << juce::Decibels::gainToDecibels(outRms, -120.0f) << " dB"
                  << " (linear: " << outRms << ")" << std::endl;
        std::cout << "  IFFT output peak: " << outPeak << " at index " << peakIdx << std::endl;

        float gain = outRms / (inRms + 1e-30f);
        std::cout << "  Round-trip gain: " << gain << " (" << juce::Decibels::gainToDecibels(gain, -120.0f) << " dB)" << std::endl;

        // Now simulate what happens with overlap-add: apply synthesis window, then divide by hopSize
        double accumSumSq = 0.0;
        for (int i = 0; i < fftSize; ++i)
        {
            float contrib = frame[i * 2] * window[i]; // synthesis window
            accumSumSq += contrib * contrib;
        }
        float accumRms = std::sqrt(static_cast<float>(accumSumSq / fftSize));
        float accumGain = accumRms / (inRms + 1e-30f);
        std::cout << "  After synthesis window RMS: " << juce::Decibels::gainToDecibels(accumRms, -120.0f) << " dB"
                  << " (gain: " << accumGain << " = " << juce::Decibels::gainToDecibels(accumGain, -120.0f) << " dB)" << std::endl;

        // With /hopSize scaling
        float afterDivHopRms = accumRms / static_cast<float>(hopSize);
        float afterDivHopGain = afterDivHopRms / (inRms + 1e-30f);
        std::cout << "  After /hopSize: " << juce::Decibels::gainToDecibels(afterDivHopRms, -120.0f) << " dB"
                  << " (gain: " << afterDivHopGain << " = " << juce::Decibels::gainToDecibels(afterDivHopGain, -120.0f) << " dB)" << std::endl;

        // What scaling factor would give unity gain?
        float unityFactor = 1.0f / (accumGain + 1e-30f);
        std::cout << "  Factor needed for unity after synthesis window: " << unityFactor
                  << " (" << juce::Decibels::gainToDecibels(unityFactor, -120.0f) << " dB)" << std::endl;
        std::cout << "  Factor / hopSize: " << (unityFactor / hopSize)
                  << " (" << juce::Decibels::gainToDecibels(unityFactor / hopSize, -120.0f) << " dB)" << std::endl;
        std::cout << "  Factor * hopSize: " << (unityFactor * hopSize)
                  << " (" << juce::Decibels::gainToDecibels(unityFactor * hopSize, -120.0f) << " dB)" << std::endl;
    }

    std::cout << "\n=== Diagnostic complete ===" << std::endl;

    // ---- Test F: Measure EXACT IFFT scaling ----
    std::cout << "\n=== Test F: Precise IFFT scaling measurement ===" << std::endl;
    {
        constexpr int N = 2048;
        juce::dsp::FFT fft2(11);

        std::vector<float> spectrum(N * 2, 0.0f);
        spectrum[0] = static_cast<float>(N);  // DC = N
        for (int b = 1; b < N/2; ++b)
            spectrum[b * 2] = 1.0f;  // all other bins re=1

        fft2.performRealOnlyInverseTransform(spectrum.data());

        float peak = 0.0f;
        for (int i = 0; i < N; ++i) {
            float val = spectrum[i * 2];
            if (std::abs(val) > peak) peak = std::abs(val);
        }
        std::cout << "  DC=N, other bins re=1 -> IFFT output peak: " << peak << std::endl;
        std::cout << "  First 10 samples: ";
        for (int i = 0; i < 10; ++i) std::cout << spectrum[i * 2] << " ";
        std::cout << std::endl;
    }

    // ---- Test H: Passthrough gain with white noise (no suppression trigger) ----
    std::cout << "\n=== Test H: Passthrough gain with white noise ===" << std::endl;
    {
        auto* proc = new ZTameAudioProcessor();
        constexpr double sr = 48000.0;
        constexpr int N = 8192;
        proc->prepareToPlay(sr, 512);

        auto& vts = proc->getValueTreeState();
        auto setP = [&](const char* id, float v) {
            if (auto* p = vts.getParameter(id))
                p->setValueNotifyingHost(p->convertTo0to1(v));
        };
        setP("bypass", 0.0f);
        setP("sensitivity", 0.0f);   // max threshold = 24 dB
        setP("range", 0.0f);         // no gain reduction
        setP("selectivity", 0.0f);
        setP("dryWet", 100.0f);

        // White noise: no single bin dominates, won't trigger suppression
        juce::AudioBuffer<float> inBuf(2, N);
        juce::Random rng;
        for (int s = 0; s < N; ++s) {
            float noise = (rng.nextFloat() * 2.0f - 1.0f) * 0.25f;
            inBuf.setSample(0, s, noise);
        }
        inBuf.copyFrom(1, 0, inBuf, 0, 0, N);
        juce::AudioBuffer<float> refBuf;
        refBuf.makeCopyOf(inBuf);
        juce::MidiBuffer midi;

        for (int offset = 0; offset < N; offset += 512) {
            juce::AudioBuffer<float> block(2, 512);
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom(ch, 0, inBuf, ch, offset, 512);
            proc->processBlock(block, midi);
            for (int ch = 0; ch < 2; ++ch)
                inBuf.copyFrom(ch, offset, block, ch, 0, 512);
        }

        constexpr int trimStart = 3072; // allow latency to settle
        constexpr int trimEnd = 1024;
        constexpr int validLen = N - trimStart - trimEnd;

        double inSumSq = 0.0, outSumSq = 0.0;
        for (int s = trimStart; s < N - trimEnd; ++s) {
            inSumSq += static_cast<double>(refBuf.getSample(0, s)) * refBuf.getSample(0, s);
            outSumSq += static_cast<double>(inBuf.getSample(0, s)) * inBuf.getSample(0, s);
        }
        float inRmsDb = juce::Decibels::gainToDecibels(
            static_cast<float>(std::sqrt(inSumSq / validLen)), -120.0f);
        float outRmsDb = juce::Decibels::gainToDecibels(
            static_cast<float>(std::sqrt(outSumSq / validLen)), -120.0f);
        std::cout << "  Noise input RMS: " << inRmsDb << " dBFS" << std::endl;
        std::cout << "  Noise output RMS: " << outRmsDb << " dBFS" << std::endl;
        std::cout << "  Passthrough gain: " << (outRmsDb - inRmsDb) << " dB" << std::endl;

        // Also test with bypass=true
        setP("bypass", 1.0f);
        inBuf.makeCopyOf(refBuf);
        for (int offset = 0; offset < N; offset += 512) {
            juce::AudioBuffer<float> block(2, 512);
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom(ch, 0, inBuf, ch, offset, 512);
            proc->processBlock(block, midi);
            for (int ch = 0; ch < 2; ++ch)
                inBuf.copyFrom(ch, offset, block, ch, 0, 512);
        }
        double bpInSumSq = 0.0, bpOutSumSq = 0.0;
        for (int s = trimStart; s < N - trimEnd; ++s) {
            bpInSumSq += static_cast<double>(refBuf.getSample(0, s)) * refBuf.getSample(0, s);
            bpOutSumSq += static_cast<double>(inBuf.getSample(0, s)) * inBuf.getSample(0, s);
        }
        float bpInRmsDb = juce::Decibels::gainToDecibels(
            static_cast<float>(std::sqrt(bpInSumSq / validLen)), -120.0f);
        float bpOutRmsDb = juce::Decibels::gainToDecibels(
            static_cast<float>(std::sqrt(bpOutSumSq / validLen)), -120.0f);
        std::cout << "  Bypass input RMS: " << bpInRmsDb << " dBFS" << std::endl;
        std::cout << "  Bypass output RMS: " << bpOutRmsDb << " dBFS" << std::endl;
        std::cout << "  Bypass gain: " << (bpOutRmsDb - bpInRmsDb) << " dB" << std::endl;

        // Show first few non-zero output samples for the non-bypass case
        std::cout << "  Non-bypass output samples (trimStart=" << trimStart << "): ";
        for (int s = trimStart; s < trimStart + 16; ++s)
            std::cout << inBuf.getSample(0, s) << " ";
        std::cout << std::endl;
    }

    // ---- Test G: FFT->IFFT with DC input ----
    std::cout << "\n=== Test G: FFT->IFFT round-trip on windowed DC ===" << std::endl;
    {
        constexpr int N = 2048;
        juce::dsp::FFT fft2(11);

        std::vector<float> frame(N * 2, 0.0f);
        for (int i = 0; i < N; ++i)
            frame[i * 2] = 1.0f * 0.5f * (1.0f - std::cos(2.0f * 3.14159265358979f * i / (N - 1)));

        float inPeak = 0.0f, inRms = 0.0f;
        for (int i = 0; i < N; ++i) {
            float v = frame[i * 2];
            if (v > inPeak) inPeak = v;
            inRms += v * v;
        }
        inRms = std::sqrt(inRms / N);
        std::cout << "  Windowed DC input peak: " << inPeak << " RMS: " << inRms << std::endl;

        fft2.performRealOnlyForwardTransform(frame.data());
        std::cout << "  After FFT DC bin: " << frame[0] << " Nyq: " << frame[N] << std::endl;

        fft2.performRealOnlyInverseTransform(frame.data());

        float outPeak = 0.0f, outRms = 0.0f;
        for (int i = 0; i < N; ++i) {
            float v = frame[i * 2];
            if (std::abs(v) > outPeak) outPeak = std::abs(v);
            outRms += v * v;
        }
        outRms = std::sqrt(outRms / N);
        std::cout << "  After IFFT peak: " << outPeak << " RMS: " << outRms << std::endl;
        std::cout << "  IFFT gain (peak): " << (outPeak / inPeak) << " RMS: " << (outRms / inRms) << std::endl;
    }

    return 0;
}
