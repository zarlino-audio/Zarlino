/*
    ZTame_PerfLab — engine measurement console app.

    Prints, for the current engine:

      * reported latency (must be 0),
      * suppression depth on a synthetic resonance and the collateral level
        change at a control tone two octaves away (transparency),
      * CPU cost of processBlock in ns/sample and as a percentage of realtime,
        for: active processing (32 bands), active processing (48 bands),
        idle (no reduction possible), silence fast path, bypass.

    Output is plain text so the release report can quote it verbatim:

        ZTame_PerfLab.exe > artifacts/ztame-v1.2.0/cpu-report.txt
*/
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "V11Engine.h"

#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <vector>

//==============================================================================
namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlockSize  = 512;

    void setP (ZTameAudioProcessor& p, const char* id, float value)
    {
        auto& vts = p.getValueTreeState();
        if (auto* param = vts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    /** Noise + a strong 2.4 kHz resonance + a quiet 300 Hz control tone. */
    juce::AudioBuffer<float> makeResonantNoise (int numSamples)
    {
        juce::AudioBuffer<float> b (2, numSamples);
        juce::Random rng (20240918);

        for (int s = 0; s < numSamples; ++s)
        {
            const auto t = (double) s / kSampleRate;
            const auto noise = (rng.nextFloat() * 2.0f - 1.0f) * 0.10f;
            const auto ring  = (float) std::sin (juce::MathConstants<double>::twoPi * 2400.0 * t) * 0.25f;
            const auto ctrl  = (float) std::sin (juce::MathConstants<double>::twoPi * 300.0 * t) * 0.05f;
            const auto v = noise + ring + ctrl;
            b.setSample (0, s, v);
            b.setSample (1, s, v);
        }

        return b;
    }

    juce::AudioBuffer<float> makeNoise (int numSamples, float amp)
    {
        juce::AudioBuffer<float> b (2, numSamples);
        juce::Random rng (7777);

        for (int s = 0; s < numSamples; ++s)
        {
            const auto v = (rng.nextFloat() * 2.0f - 1.0f) * amp;
            b.setSample (0, s, v);
            b.setSample (1, s, v);
        }

        return b;
    }

    void process (juce::AudioProcessor& p, juce::AudioBuffer<float>& buffer)
    {
        juce::MidiBuffer midi;

        for (int offset = 0; offset < buffer.getNumSamples(); offset += kBlockSize)
        {
            const int n = juce::jmin (kBlockSize, buffer.getNumSamples() - offset);
            juce::AudioBuffer<float> block (buffer.getNumChannels(), n);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                block.copyFrom (ch, 0, buffer, ch, offset, n);

            p.processBlock (block, midi);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.copyFrom (ch, offset, block, ch, 0, n);
        }
    }

    template <typename P>
    void setGenericParam (P& p, const char* id, float value)
    {
        auto& vts = p.getValueTreeState();
        if (auto* param = vts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    /** Single-bin DFT amplitude (dBFS) over [start, start+len). */
    float toneDb (const juce::AudioBuffer<float>& b, int ch, int start, int len, double freq)
    {
        double re = 0.0, im = 0.0;

        for (int i = 0; i < len; ++i)
        {
            const auto w = juce::MathConstants<double>::twoPi * freq * (double) (start + i) / kSampleRate;
            const auto v = (double) b.getSample (ch, start + i);
            re += v * std::cos (w);
            im += v * std::sin (w);
        }

        const auto amp = 2.0 * std::sqrt (re * re + im * im) / (double) len;
        return juce::Decibels::gainToDecibels ((float) amp, -140.0f);
    }

    double timeProcessing (juce::AudioProcessor& p, const juce::AudioBuffer<float>& source, int repeats)
    {
        juce::AudioBuffer<float> buffer (source.getNumChannels(), source.getNumSamples());

        double best = 1.0e30;

        for (int r = 0; r < repeats; ++r)
        {
            const auto t0 = std::chrono::high_resolution_clock::now();
            buffer.makeCopyOf (source, true);
            process (p, buffer);
            const auto t1 = std::chrono::high_resolution_clock::now();

            best = juce::jmin (best, std::chrono::duration<double> (t1 - t0).count());
        }

        return best;
    }

    /** RMS power (dBFS) in [fLow, fHigh) over [start, start+len) — FFT band power. */
    float bandPowerDb (const juce::AudioBuffer<float>& b, int ch, int start, int len,
                       double fLow, double fHigh)
    {
        constexpr int fftOrder = 12;                 // 4096
        constexpr int fftSize  = 1 << fftOrder;
        constexpr int numBins  = fftSize / 2 + 1;

        juce::dsp::FFT fft (fftOrder);
        std::vector<float> data ((size_t) 2 * fftSize, 0.0f);

        for (int i = 0; i < fftSize && start + i < b.getNumSamples(); ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi
                                                 * (double) i / (double) fftSize);
            data[(size_t) i] = (float) (b.getSample (ch, start + i) * w);
        }

        fft.performRealOnlyForwardTransform (data.data(), true);

        const int b0 = juce::jlimit (1, numBins - 1, (int) std::floor (fLow  * fftSize / kSampleRate));
        const int b1 = juce::jlimit (b0 + 1, numBins - 1, (int) std::ceil (fHigh * fftSize / kSampleRate));

        double sum = 0.0;

        for (int k = b0; k < b1; ++k)
            sum += (double) data[2 * k] * data[2 * k] + (double) data[2 * k + 1] * data[2 * k + 1];

        juce::ignoreUnused (len);

        return juce::Decibels::gainToDecibels ((float) std::sqrt (sum / (double) (b1 - b0) / (fftSize * fftSize * 0.25)), -140.0f);
    }
}

//==============================================================================
int main (int, char**)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "=== ZTame Perf / Calibration Lab (v" << ZTAME_VERSION << ") ===" << std::endl;

    // -----------------------------------------------------------------------
    // 1. Latency + suppression / transparency
    // -----------------------------------------------------------------------
    {
        auto input = makeResonantNoise ((int) (kSampleRate * 2.0));

        struct Config
        {
            const char* name;
            float sens, sel, range;
            int   res;
        };

        const Config configs[] =
        {
            { "engaged", 80.0f, 30.0f, 18.0f, 1 },
            { "default", 65.0f, 50.0f, 12.0f, 0 },
            { "default48", 65.0f, 50.0f, 12.0f, 1 },
        };

        const int skip = (int) (kSampleRate * 0.5);
        const int len  = (int) (kSampleRate * 1.0);

        for (const auto& cfg : configs)
        {
            auto* proc = new ZTameAudioProcessor();   // leaked on purpose (test-suite convention)
            proc->prepareToPlay (kSampleRate, kBlockSize);
            setP (*proc, "sensitivity", cfg.sens);
            setP (*proc, "selectivity", cfg.sel);
            setP (*proc, "range", cfg.range);
            setP (*proc, "resolution", (float) cfg.res);
            setP (*proc, "dryWet", 100.0f);

            auto output = input;
            process (*proc, output);

            const auto inRing  = toneDb (input,  0, skip, len, 2400.0);
            const auto outRing = toneDb (output, 0, skip, len, 2400.0);

            // Transparency = how much the BROADBAND material away from the resonance
            // changed while the resonance was being suppressed.
            const auto inLo   = bandPowerDb (input,  0, skip, len, 500.0, 900.0);
            const auto outLo  = bandPowerDb (output, 0, skip, len, 500.0, 900.0);
            const auto inHi   = bandPowerDb (input,  0, skip, len, 5000.0, 8000.0);
            const auto outHi  = bandPowerDb (output, 0, skip, len, 5000.0, 8000.0);

            std::cout << "\n[" << cfg.name << "]  latency = " << proc->getLatencySamples()
                      << " samples  |  peak GR " << juce::String (proc->getGainReductionPeakDb(), 1).toStdString()
                      << " dB" << std::endl;
            std::cout << "[" << cfg.name << "]  2.4k suppression = "
                      << juce::String (inRing - outRing, 1).toStdString() << " dB ("
                      << juce::String (inRing, 1).toStdString() << " -> "
                      << juce::String (outRing, 1).toStdString() << ")" << std::endl;
            std::cout << "[" << cfg.name << "]  noise 500-900  delta = "
                      << juce::String (outLo - inLo, 2).toStdString() << " dB" << std::endl;
            std::cout << "[" << cfg.name << "]  noise 5k-8k    delta = "
                      << juce::String (outHi - inHi, 2).toStdString() << " dB" << std::endl;

            // Where did the engine actually cut? Dump the reduction curve from the
            // telemetry frame (log-spaced display bins).
            proc->setUiActive (true);
            {
                auto probe = input;
                process (*proc, probe);
            }

            ZTameAudioProcessor::UiSnapshot snap;
            if (proc->getLatestSnapshot (snap))
            {
                std::cout << "[" << cfg.name << "]  cuts > 0.5 dB:";

                for (int d = 0; d < ZTameAudioProcessor::UiSnapshot::numDisplayBins; ++d)
                {
                    if (snap.reductionDb[(size_t) d] > 0.5f)
                    {
                        const auto f = ZTameAudioProcessor::displayNormToFreq (
                                           ((float) d + 0.5f)
                                           / (float) ZTameAudioProcessor::UiSnapshot::numDisplayBins);
                        std::cout << "  " << (int) f << "Hz:" << juce::String (snap.reductionDb[(size_t) d], 1).toStdString();
                    }
                }

                std::cout << std::endl;
            }
        }
    }

    // -----------------------------------------------------------------------
    // 2. Detector calibration: excess levels for typical material
    // -----------------------------------------------------------------------
    {
        auto sine = [&] (float freqHz, float amp, int numSamples)
        {
            juce::AudioBuffer<float> b (2, numSamples);

            for (int s = 0; s < numSamples; ++s)
            {
                const auto v = amp * (float) std::sin (juce::MathConstants<double>::twoPi
                                                       * (double) freqHz * (double) s / kSampleRate);
                b.setSample (0, s, v);
                b.setSample (1, s, v);
            }

            return b;
        };

        const int numSamples = (int) (kSampleRate * 2.0);
        const int skip = (int) (kSampleRate * 0.5);
        const int len  = (int) (kSampleRate * 1.0);

        auto noiseOnly = makeNoise (numSamples, 0.1f);

        auto noisePlusTone = noiseOnly;
        {
            const auto tone = sine (2400.0f, 0.5f, numSamples);
            noisePlusTone.addFrom (0, 0, tone, 0, 0, numSamples);
            noisePlusTone.addFrom (1, 0, tone, 1, 0, numSamples);
        }

        auto pureTone = sine (2400.0f, 0.5f, numSamples);

        std::cout << "\n[calib] suppression at 2.4 kHz after 1 s of adaptation" << std::endl;

        auto measureCase = [&] (const char* name, const juce::AudioBuffer<float>& in)
        {
            for (float sens : { 50.0f, 65.0f, 70.0f, 80.0f })
            {
                auto* proc = new ZTameAudioProcessor();
                proc->prepareToPlay (kSampleRate, kBlockSize);
                setP (*proc, "sensitivity", sens);
                setP (*proc, "range", 18.0f);
                setP (*proc, "attack", 5.0f);
                setP (*proc, "resolution", 0.0f);

                auto out = in;
                process (*proc, out);

                const auto suppression = toneDb (in, 0, skip, len, 2400.0)
                                       - toneDb (out, 0, skip, len, 2400.0);

                std::cout << "[calib] " << juce::String (name).paddedRight (' ', 12).toStdString()
                          << " sens=" << juce::String (sens, 0).paddedLeft (' ', 3).toStdString()
                          << "  peakGR=" << juce::String (proc->getGainReductionPeakDb(), 1).toStdString()
                          << " dB  suppression=" << juce::String (suppression, 1).toStdString()
                          << " dB" << std::endl;
            }
        };

        measureCase ("noise", noiseOnly);
        measureCase ("noise+tone", noisePlusTone);
        measureCase ("pure-tone", pureTone);
    }

    // -----------------------------------------------------------------------
    // 3. CPU: typical / worst / idle / silence / bypass
    // -----------------------------------------------------------------------
    // NOTE on methodology: consumer/VM CPUs throttle, so a single long block
    // measures "how hot the machine got", not the engine. Each scenario is
    // therefore measured in short slices, round-robin across 4 passes, and the
    // minimum per scenario is reported (all scenarios see the same conditions).
    {
        const double sliceSeconds = 2.0;
        const int numSamples = (int) (kSampleRate * sliceSeconds);
        const int passes = 4;

        auto noise = makeNoise (numSamples, 0.2f);
        auto resonant = makeResonantNoise (numSamples);

        juce::AudioBuffer<float> silence (2, numSamples);
        silence.clear();

        struct Case
        {
            const char* name;
            juce::AudioProcessor* proc;
            const juce::AudioBuffer<float>* source;
            std::function<float()> peakGr;
            double best = 1.0e30;
        };

        std::vector<Case> cases;

        auto addCase = [&] (const char* name, bool engineV11, float sens, float range, float res,
                            float bypass, const juce::AudioBuffer<float>& source)
        {
            juce::AudioProcessor* proc = engineV11
                                             ? (juce::AudioProcessor*) new ZTameV11Processor()
                                             : (juce::AudioProcessor*) new ZTameAudioProcessor();

            proc->prepareToPlay (kSampleRate, kBlockSize);

            std::function<float()> peak;

            if (engineV11)
            {
                auto* p11 = (ZTameV11Processor*) proc;
                setGenericParam (*p11, "sensitivity", sens);
                setGenericParam (*p11, "selectivity", 50.0f);
                setGenericParam (*p11, "range", range);
                setGenericParam (*p11, "resolution", res);
                setGenericParam (*p11, "bypass", bypass);
                setGenericParam (*p11, "dryWet", 100.0f);
                peak = [p11] { return p11->getGainReductionPeakDb(); };
            }
            else
            {
                auto* p12 = (ZTameAudioProcessor*) proc;
                setGenericParam (*p12, "sensitivity", sens);
                setGenericParam (*p12, "selectivity", 50.0f);
                setGenericParam (*p12, "range", range);
                setGenericParam (*p12, "resolution", res);
                setGenericParam (*p12, "bypass", bypass);
                setGenericParam (*p12, "dryWet", 100.0f);
                peak = [p12] { return p12->getGainReductionPeakDb(); };
            }

            cases.push_back ({ name, proc, &source, peak, 1.0e30 });
        };

        // v1.2 (zero latency, bell cascade)
        addCase ("v1.2 typical 32", false, 65.0f, 12.0f, 0.0f, 0.0f, resonant);
        addCase ("v1.2 typical 48", false, 65.0f, 12.0f, 1.0f, 0.0f, resonant);
        addCase ("v1.2 worst 32",   false, 85.0f, 20.0f, 0.0f, 0.0f, noise);
        addCase ("v1.2 idle",       false,  0.0f,  0.0f, 0.0f, 0.0f, noise);
        addCase ("v1.2 silence",    false, 65.0f, 12.0f, 0.0f, 0.0f, silence);
        addCase ("v1.2 bypass",     false, 65.0f, 12.0f, 0.0f, 1.0f, noise);

        // v1.1 (shipped STFT engine) — same material, same controls.
        addCase ("v1.1 typical fast",   true, 65.0f, 12.0f, 0.0f, 0.0f, resonant);
        addCase ("v1.1 typical prec",   true, 65.0f, 12.0f, 1.0f, 0.0f, resonant);
        addCase ("v1.1 idle",           true,  0.0f,  0.0f, 1.0f, 0.0f, noise);
        addCase ("v1.1 silence",        true, 65.0f, 12.0f, 1.0f, 0.0f, silence);

        // Warm-up (not measured).
        (void) timeProcessing (*cases.front().proc, *cases.front().source, 1);

        for (int pass = 0; pass < passes; ++pass)
            for (auto& c : cases)
                c.best = juce::jmin (c.best, timeProcessing (*c.proc, *c.source, 1));

        std::cout << "\n[cpu] stereo 48 kHz, " << kBlockSize << "-sample blocks, "
                  << "best of " << passes << " x " << sliceSeconds << " s" << std::endl;

        for (const auto& c : cases)
        {
            const auto perSampleNs = c.best / (double) numSamples * 1.0e9;
            const auto realtimePct = 100.0 * c.best / sliceSeconds;

            std::cout << "[cpu] " << juce::String (c.name).paddedRight (' ', 20).toStdString()
                      << " " << juce::String (perSampleNs, 1).paddedLeft (' ', 7).toStdString() << " ns/sample"
                      << "  " << juce::String (realtimePct, 2).paddedLeft (' ', 6).toStdString() << " % realtime"
                      << "  (peak GR " << juce::String (c.peakGr(), 1).toStdString()
                      << " dB)" << std::endl;
        }
    }

    std::cout << "\n=== done ===" << std::endl;
    std::exit (0);
}
