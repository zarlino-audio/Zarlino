/*
    ZTAME-DIAG — temporary diagnostic measurements for WOLA level verification.
    Prints RMS of sine passthrough and DC to identify gain/scaling issues.
*/
#include <JuceHeader.h>
#include <cstdio>
#include "PluginProcessor.h"

namespace
{
    static ZTameAudioProcessor* makeDiagProc (double sr, int bs)
    {
        auto* proc = new ZTameAudioProcessor();
        proc->prepareToPlay (sr, bs);
        return proc;
    }

    static void setD (ZTameAudioProcessor& p, const char* id, float v)
    {
        auto& vts = p.getValueTreeState();
        if (auto* param = vts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (v));
    }

    static float diagRms (const juce::AudioBuffer<float>& buf, int ch, int start, int len)
    {
        double sumSq = 0.0;
        for (int i = start; i < start + len; ++i)
            sumSq += static_cast<double> (buf.getSample (ch, i))
                   * static_cast<double> (buf.getSample (ch, i));
        return juce::Decibels::gainToDecibels (
            static_cast<float> (std::sqrt (sumSq / len)), -160.0f);
    }

    static juce::AudioBuffer<float> diagSine (float freq, double sr, int len, float amp)
    {
        juce::AudioBuffer<float> b (1, len);
        for (int s = 0; s < len; ++s)
            b.setSample (0, s, amp * std::sin (juce::MathConstants<float>::twoPi
                                              * freq * s / static_cast<float> (sr)));
        return b;
    }

    static juce::AudioBuffer<float> diagProcess (
        ZTameAudioProcessor& proc,
        const juce::AudioBuffer<float>& input,
        int blockSize)
    {
        auto N = input.getNumSamples();
        juce::AudioBuffer<float> output (2, N);
        for (int offset = 0; offset < N; offset += blockSize)
        {
            auto bs = std::min (blockSize, N - offset);
            juce::AudioBuffer<float> block (2, bs);
            block.copyFrom (0, 0, input, 0, offset, bs);
            block.copyFrom (1, 0, input, 0, offset, bs);
            juce::MidiBuffer midi;
            proc.processBlock (block, midi);
            output.copyFrom (0, offset, block, 0, 0, bs);
            output.copyFrom (1, offset, block, 1, 0, bs);
        }
        return output;
    }
}

class ZTameDiagnosticTest : public juce::UnitTest
{
public:
    ZTameDiagnosticTest() : UnitTest ("ZTAME-DIAG: WOLA Level Diagnostics") {}

    void runTest() override
    {
        constexpr double sr = 48000.0;
        constexpr int N = 8192;

        beginTest ("Sine passthrough RMS (sensitivity=0, range=0)");
        {
            auto* proc = makeDiagProc (sr, 512);
            setD (*proc, "bypass", 0.0f);
            setD (*proc, "sensitivity", 0.0f);
            setD (*proc, "range", 0.0f);
            setD (*proc, "dryWet", 100.0f);

            auto in = diagSine (1000.0f, sr, N, 0.5f);
            auto out = diagProcess (*proc, in, 512);

            const auto inRms = diagRms (in, 0, 2048, N - 4096);
            const auto outRms = diagRms (out, 0, 2048, N - 4096);
            const auto outCh1 = diagRms (out, 1, 2048, N - 4096);
            std::printf ("[DIAG] Sine: inRMS=%.2f dB, outRMS ch0=%.2f dB, ch1=%.2f dB, out/in=%.2f dB\n",
                         (double) inRms, (double) outRms, (double) outCh1, (double) (outRms - inRms));
            std::fflush (stdout);

            juce::String sampleLog = "[DIAG] Sine out@4096: ";
            for (int s = 4096; s < 4104; ++s)
                sampleLog += juce::String (out.getSample (0, s), 5) + " ";
            std::printf ("%s\n", sampleLog.toRawUTF8());
            std::fflush (stdout);
            juce::String inLog = "[DIAG] Sine in @4096: ";
            for (int s = 4096; s < 4104; ++s)
                inLog += juce::String (in.getSample (0, s), 5) + " ";
            std::printf ("%s\n", inLog.toRawUTF8());
            std::fflush (stdout);
        }

        beginTest ("DC passthrough (sensitivity=0, range=0)");
        {
            auto* proc = makeDiagProc (sr, 512);
            setD (*proc, "bypass", 0.0f);
            setD (*proc, "sensitivity", 0.0f);
            setD (*proc, "range", 0.0f);
            setD (*proc, "dryWet", 100.0f);

            juce::AudioBuffer<float> in (1, N);
            in.clear();
            in.applyGain (0.5f);
            auto out = diagProcess (*proc, in, 512);
            float sumOut = 0.0f;
            for (int s = 4096; s < 8192; ++s)
                sumOut += out.getSample (0, s);
            std::printf ("[DIAG] DC: input=0.5, output avg=%.6f\n", (double) (sumOut / 4096.0f));
            std::fflush (stdout);
        }

        beginTest ("Noise passthrough (sensitivity=0, range=0)");
        {
            auto* proc = makeDiagProc (sr, 512);
            setD (*proc, "bypass", 0.0f);
            setD (*proc, "sensitivity", 0.0f);
            setD (*proc, "range", 0.0f);
            setD (*proc, "dryWet", 100.0f);

            juce::Random rng (42);
            juce::AudioBuffer<float> in (1, N);
            for (int s = 0; s < N; ++s)
                in.setSample (0, s, (rng.nextFloat() * 2.0f - 1.0f) * 0.5f);
            auto out = diagProcess (*proc, in, 512);
            std::printf ("[DIAG] Noise: inRMS=%.2f dB, outRMS=%.2f dB\n",
                         (double) diagRms (in, 0, 2048, N - 4096),
                         (double) diagRms (out, 0, 2048, N - 4096));
            std::fflush (stdout);
        }

        beginTest ("Direct FFT round-trip (isolate engine normalization)");
        {
            constexpr int order = 11;
            constexpr int fftN = 1 << order;
            juce::dsp::FFT fft (order);
            std::vector<float> buf (static_cast<size_t> (fftN * 2), 0.0f);

            double inSumSq = 0.0;
            for (int i = 0; i < fftN; ++i)
            {
                buf[static_cast<size_t> (i)] = 0.5f * std::sin (
                    juce::MathConstants<float>::twoPi * 1000.0f * i / 48000.0f);
                inSumSq += static_cast<double> (buf[static_cast<size_t> (i)])
                         * static_cast<double> (buf[static_cast<size_t> (i)]);
            }

            fft.performRealOnlyForwardTransform (buf.data(), true);
            fft.performRealOnlyInverseTransform (buf.data());

            double outSumSq = 0.0;
            for (int i = 0; i < fftN; ++i)
            {
                const auto v = static_cast<double> (buf[static_cast<size_t> (i)]);
                outSumSq += v * v;
            }
            const auto inRms  = std::sqrt (inSumSq / fftN);
            const auto outRms = std::sqrt (outSumSq / fftN);
            std::printf ("[DIAG] FFT round-trip: inRMS=%.6f outRMS=%.6f ratio=%.3f (1.0 = normalized, %.0f = N-scale, %.0f = 2N-scale)\n",
                         (double) inRms, (double) outRms, (double) (outRms / inRms),
                         (double) fftN, (double) (fftN * 2));
            std::fflush (stdout);
        }
    }
};

static ZTameDiagnosticTest diagnosticTest;
