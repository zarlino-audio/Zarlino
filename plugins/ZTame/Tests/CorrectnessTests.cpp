/*
    ZTame-DSP-CORRECTNESS — correctness battery for the v1.1 engine.

    Contract under test (see specs/ZTame_v1.1_rework_design.md):

      1. Perfect reconstruction: with the gains at unity the wet path equals the
         input delayed by exactly N samples (residual <= -80 dBFS).
      2. Latency: the wet path delay equals the value reported to the host.
      3. Suppression happens only where a bin stands out against its local
         neighbourhood, only inside the focus band.
      4. Sidechain reroutes detection but never leaks into the output.
      5. Delta audition equals (dry - wet).
      6. Nothing blows up at parameter extremes, across sample rates, or with
         hostile (NaN / Inf / denormal) input.
*/

#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <algorithm>
#include <limits>
#include <vector>

//==============================================================================
namespace
{
    // NOTE: processors are intentionally leaked — AudioProcessor teardown can
    // hang console test builds (APVTS / message manager ordering).
    ZTameAudioProcessor* makeProc (double sr = 44100.0, int blockSize = 512)
    {
        auto* proc = new ZTameAudioProcessor();
        proc->prepareToPlay (sr, blockSize);
        return proc;
    }

    void setP (ZTameAudioProcessor& p, const char* id, float value)
    {
        auto& vts = p.getValueTreeState();
        if (auto* param = vts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    juce::AudioBuffer<float> makeSine (double freq, double sr, int numSamples,
                                       float amp = 0.5f, int numChannels = 2)
    {
        juce::AudioBuffer<float> buf (numChannels, numSamples);
        buf.clear();
        for (int s = 0; s < numSamples; ++s)
        {
            const auto val = amp * (float) std::sin (juce::MathConstants<double>::twoPi
                                                     * freq * (double) s / sr);
            for (int ch = 0; ch < numChannels; ++ch)
                buf.setSample (ch, s, val);
        }
        return buf;
    }

    juce::AudioBuffer<float> makeNoise (int numSamples, float amp, int seed = 4711,
                                        int numChannels = 2)
    {
        juce::Random rng (seed);
        juce::AudioBuffer<float> buf (numChannels, numSamples);
        for (int s = 0; s < numSamples; ++s)
        {
            const auto val = (rng.nextFloat() * 2.0f - 1.0f) * amp;
            for (int ch = 0; ch < numChannels; ++ch)
                buf.setSample (ch, s, val);
        }
        return buf;
    }

    /** Process a buffer in place, block by block, preserving its channel count. */
    void processBlocks (ZTameAudioProcessor& proc, juce::AudioBuffer<float>& buffer, int blockSize)
    {
        juce::MidiBuffer midi;
        const int total = buffer.getNumSamples();

        for (int offset = 0; offset < total; offset += blockSize)
        {
            const int n = juce::jmin (blockSize, total - offset);
            juce::AudioBuffer<float> block (buffer.getNumChannels(), n);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                block.copyFrom (ch, 0, buffer, ch, offset, n);

            proc.processBlock (block, midi);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.copyFrom (ch, offset, block, ch, 0, n);
        }
    }

    float rmsDb (const juce::AudioBuffer<float>& buf, int ch, int start, int len)
    {
        double sumSq = 0.0;
        for (int i = start; i < start + len; ++i)
        {
            const auto v = (double) buf.getSample (ch, i);
            sumSq += v * v;
        }
        return juce::Decibels::gainToDecibels ((float) std::sqrt (sumSq / (double) len), -140.0f);
    }

    /** Peak and RMS of (a - b) over a range, in dBFS. */
    struct Residual { float peakDb; float rmsDb; };

    Residual residual (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b,
                       int ch, int start, int len)
    {
        double sumSq = 0.0;
        float peak = 0.0f;

        for (int i = start; i < start + len; ++i)
        {
            const auto d = a.getSample (ch, i) - b.getSample (ch, i);
            peak = juce::jmax (peak, std::abs (d));
            sumSq += (double) d * (double) d;
        }

        return { juce::Decibels::gainToDecibels (peak, -140.0f),
                 juce::Decibels::gainToDecibels ((float) std::sqrt (sumSq / (double) len), -140.0f) };
    }

    /** Single-bin DFT amplitude at a frequency, in dBFS. */
    float toneAmpDb (const juce::AudioBuffer<float>& buf, int ch, int start, int len,
                     double freq, double sr)
    {
        double re = 0.0, im = 0.0;

        for (int i = 0; i < len; ++i)
        {
            const double w = juce::MathConstants<double>::twoPi * freq
                           * (double) (start + i) / sr;
            const auto v = (double) buf.getSample (ch, start + i);
            re += v * std::cos (w);
            im += v * std::sin (w);
        }

        const double amp = 2.0 * std::sqrt (re * re + im * im) / (double) len;
        return juce::Decibels::gainToDecibels ((float) amp, -140.0f);
    }

    int findPeakIndex (const juce::AudioBuffer<float>& buf, int ch)
    {
        int best = 0;
        float bestAbs = 0.0f;

        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            const auto v = std::abs (buf.getSample (ch, i));
            if (v > bestAbs) { bestAbs = v; best = i; }
        }

        return best;
    }
}

//==============================================================================
// ZTAME-DSP-CORRECTNESS-001: Impulse Response
class ZTameImpulseTest : public juce::UnitTest
{
public:
    ZTameImpulseTest() : UnitTest ("ZTAME-DSP-CORRECTNESS-001: Impulse Response") {}

    void runTest() override
    {
        beginTest ("Impulse produces no NaN/Inf and no unbounded ringing");

        auto* proc = makeProc (44100.0, 512);
        setP (*proc, "bypass", 0.0f);
        setP (*proc, "sensitivity", 70.0f);
        setP (*proc, "range", 18.0f);

        juce::AudioBuffer<float> buf (2, 4096);
        buf.clear();
        buf.setSample (0, 100, 1.0f);
        buf.setSample (1, 160, 1.0f);

        processBlocks (*proc, buf, 512);

        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < buf.getNumSamples(); ++s)
            {
                const auto v = buf.getSample (ch, s);
                expect (! std::isnan (v), "NaN at ch " + juce::String (ch) + " s " + juce::String (s));
                expect (! std::isinf (v), "Inf at ch " + juce::String (ch) + " s " + juce::String (s));
                expect (std::abs (v) < 4.0f, "Impulse rang above 4x at s " + juce::String (s));
            }
    }
};

//==============================================================================
// ZTAME-DSP-CORRECTNESS-002: Unity transparency of the STFT path
class ZTameFreqResponseTest : public juce::UnitTest
{
public:
    ZTameFreqResponseTest() : UnitTest ("ZTAME-DSP-CORRECTNESS-002: Frequency Response") {}

    void runTest() override
    {
        beginTest ("Gains at unity => the wet path is level-accurate (no COLA loss)");

        constexpr double sr = 48000.0;
        constexpr int N = 16384;

        auto* proc = makeProc (sr, 512);
        setP (*proc, "bypass", 0.0f);
        setP (*proc, "sensitivity", 0.0f);   // trigger at +24 dB: nothing qualifies
        setP (*proc, "range", 0.0f);         // depth 0 => gain == 1 everywhere
        setP (*proc, "dryWet", 100.0f);

        auto input = makeSine (1000.0, sr, N, 0.5f);
        auto output = input;
        processBlocks (*proc, output, 512);

        const int start = 8192;
        const int len   = 4096;

        const auto inRms  = rmsDb (input, 0, start, len);
        const auto outRms = rmsDb (output, 0, start, len);

        expectWithinAbsoluteError (outRms, inRms, 0.3f,
            "Wet path level mismatch: in=" + juce::String (inRms, 2)
            + " dB out=" + juce::String (outRms, 2) + " dB");

        // Also verify a second frequency and a noise input stay level-accurate.
        auto noiseIn = makeNoise (N, 0.25f);
        auto noiseOut = noiseIn;
        processBlocks (*proc, noiseOut, 512);

        const auto noiseInRms  = rmsDb (noiseIn, 0, start, len);
        const auto noiseOutRms = rmsDb (noiseOut, 0, start, len);

        expectWithinAbsoluteError (noiseOutRms, noiseInRms, 0.3f,
            "Noise level mismatch: in=" + juce::String (noiseInRms, 2)
            + " dB out=" + juce::String (noiseOutRms, 2) + " dB");
    }
};

//==============================================================================
// ZTAME-DSP-CORRECTNESS-003: Latency + perfect-reconstruction null
class ZTameLatencyNullTest : public juce::UnitTest
{
public:
    ZTameLatencyNullTest() : UnitTest ("ZTAME-DSP-CORRECTNESS-003: Latency / Null") {}

    void runTest() override
    {
        beginTest ("Wet delay equals the reported latency and nulls against the input");

        constexpr double sr = 44100.0;
        constexpr int N = 16384;

        auto* proc = makeProc (sr, 512);
        setP (*proc, "bypass", 0.0f);
        setP (*proc, "sensitivity", 0.0f);
        setP (*proc, "range", 0.0f);
        setP (*proc, "dryWet", 100.0f);

        const int latency = proc->getLatencySamples();
        expectGreaterThan (latency, 0, "Engine must report a non-zero latency");

        // --- impulse position check (exact sample delay) ---
        {
            juce::AudioBuffer<float> buf (2, 8192);
            buf.clear();
            buf.setSample (0, 1000, 1.0f);
            auto out = buf;
            processBlocks (*proc, out, 512);

            const int peak = findPeakIndex (out, 0);
            expectEquals (peak, 1000 + latency,
                          "Impulse emerged at " + juce::String (peak)
                          + ", expected " + juce::String (1000 + latency));
        }

        // --- null test: output == input delayed by latency ---
        auto* proc2 = makeProc (sr, 512);
        setP (*proc2, "bypass", 0.0f);
        setP (*proc2, "sensitivity", 0.0f);
        setP (*proc2, "range", 0.0f);
        setP (*proc2, "dryWet", 100.0f);

        auto input = makeNoise (N, 0.4f);
        auto output = input;
        processBlocks (*proc2, output, 512);

        juce::AudioBuffer<float> delayed (input.getNumChannels(), N);
        delayed.clear();
        for (int s = latency; s < N; ++s)
            for (int ch = 0; ch < delayed.getNumChannels(); ++ch)
                delayed.setSample (ch, s, input.getSample (ch, s - latency));

        const auto res = residual (output, delayed, 0, latency, N - latency - latency);

        expectLessThan (res.peakDb, -80.0f,
            "Null residual peak too high: " + juce::String (res.peakDb, 1) + " dBFS");
        expectLessThan (res.rmsDb, -90.0f,
            "Null residual RMS too high: " + juce::String (res.rmsDb, 1) + " dBFS");

        // --- dry/wet mix at 50 % must stay phase-coherent (no comb) ---
        auto* proc3 = makeProc (sr, 512);
        setP (*proc3, "bypass", 0.0f);
        setP (*proc3, "sensitivity", 0.0f);
        setP (*proc3, "range", 0.0f);
        setP (*proc3, "dryWet", 50.0f);

        auto mixed = input;
        processBlocks (*proc3, mixed, 512);

        const auto mixedRms = rmsDb (mixed, 0, latency + 2048, 4096);
        const auto inputRms = rmsDb (input, 0, latency + 2048, 4096);

        expectWithinAbsoluteError (mixedRms, inputRms, 0.3f,
            "50 % dry/wet comb-filtered the signal: dry=" + juce::String (inputRms, 2)
            + " mixed=" + juce::String (mixedRms, 2));
    }
};

//==============================================================================
// ZTAME-DSP-CORRECTNESS-004: Bypass = delayed, bit-exact passthrough
class ZTameBypassTest : public juce::UnitTest
{
public:
    ZTameBypassTest() : UnitTest ("ZTAME-DSP-CORRECTNESS-004: Bypass Passthrough") {}

    void runTest() override
    {
        beginTest ("Bypass returns the input unchanged, delayed by the reported latency");

        constexpr double sr = 44100.0;
        constexpr int N = 8192;

        auto* proc = makeProc (sr, N);
        setP (*proc, "bypass", 1.0f);
        setP (*proc, "sensitivity", 80.0f);
        setP (*proc, "range", 24.0f);

        const int latency = proc->getLatencySamples();

        auto input = makeSine (440.0, sr, N, 0.7f);
        auto output = input;
        processBlocks (*proc, output, 512);

        for (int s = 0; s < latency; ++s)
            for (int ch = 0; ch < 2; ++ch)
                expect (std::abs (output.getSample (ch, s)) < 1.0e-6f,
                        "Bypass is not producing silence before the latency point (s="
                        + juce::String (s) + ")");

        double maxDiff = 0.0;
        for (int s = latency; s < N; ++s)
            for (int ch = 0; ch < 2; ++ch)
                maxDiff = juce::jmax (maxDiff, (double) std::abs (output.getSample (ch, s)
                                                                  - input.getSample (ch, s - latency)));

        expectLessThan ((float) maxDiff, 1.0e-6f,
                        "Bypass is not bit-exact after the latency offset (maxDiff="
                        + juce::String (maxDiff));
    }
};

//==============================================================================
// ZTAME-DSP-CORRECTNESS-005: Boundary parameter values
class ZTameBoundaryTest : public juce::UnitTest
{
public:
    ZTameBoundaryTest() : UnitTest ("ZTAME-DSP-CORRECTNESS-005: Boundary Parameters") {}

    void runTest() override
    {
        beginTest ("Every parameter at its extremes stays finite and bounded");

        constexpr double sr = 44100.0;
        constexpr int N = 4096;

        struct ParamRange { const char* id; float min; float max; float def; };

        ParamRange params[] = {
            { ZTameAudioProcessor::paramSensitivity, 0.0f,    100.0f,  65.0f },
            { ZTameAudioProcessor::paramRange,       0.0f,     24.0f,  12.0f },
            { ZTameAudioProcessor::paramSelectivity, 0.0f,    100.0f,  50.0f },
            { ZTameAudioProcessor::paramFocusLow,   20.0f,  20000.0f,  20.0f },
            { ZTameAudioProcessor::paramFocusHigh,  20.0f,  20000.0f, 20000.0f },
            { ZTameAudioProcessor::paramAttack,      0.1f,    100.0f,   8.0f },
            { ZTameAudioProcessor::paramRelease,    10.0f,   1000.0f, 180.0f },
            { ZTameAudioProcessor::paramDryWet,      0.0f,    100.0f, 100.0f },
        };

        auto* proc = makeProc (sr, 512);
        auto input = makeSine (500.0, sr, N, 0.5f);
        input.addFrom (0, 0, makeSine (3700.0, sr, N, 0.4f), 0, 0, N);

        for (auto& pr : params)
        {
            for (float val : { pr.min, pr.max })
            {
                for (auto& reset : params)
                    setP (*proc, reset.id, reset.def);

                setP (*proc, ZTameAudioProcessor::paramBypass, 0.0f);
                setP (*proc, pr.id, val);
                setP (*proc, ZTameAudioProcessor::paramListenMode, 1.0f);

                auto buf = input;
                processBlocks (*proc, buf, 512);

                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < N; ++s)
                    {
                        const auto v = buf.getSample (ch, s);
                        expect (! std::isnan (v), "NaN at " + juce::String (pr.id)
                                                  + "=" + juce::String (val));
                        expect (! std::isinf (v), "Inf at " + juce::String (pr.id)
                                                  + "=" + juce::String (val));
                        expect (std::abs (v) < 4.0f, "Output above 4x at " + juce::String (pr.id)
                                                     + "=" + juce::String (val));
                    }
            }
        }
    }
};

//==============================================================================
// ZTAME-DSP-CORRECTNESS-006: Channel independence
class ZTameChannelIndependenceTest : public juce::UnitTest
{
public:
    ZTameChannelIndependenceTest()
        : UnitTest ("ZTAME-DSP-CORRECTNESS-006: Channel Independence") {}

    void runTest() override
    {
        beginTest ("Left-only input does not leak into the right channel");

        constexpr double sr = 44100.0;
        constexpr int N = 8192;

        auto* proc = makeProc (sr, 512);
        setP (*proc, "bypass", 0.0f);
        setP (*proc, "sensitivity", 60.0f);
        setP (*proc, "range", 12.0f);
        setP (*proc, "stereoLink", 0.0f);            // independent detectors
        setP (*proc, "attack", 5.0f);

        juce::AudioBuffer<float> buf (2, N);
        buf.clear();
        for (int s = 0; s < N; ++s)
            buf.setSample (0, s, (float) std::sin (juce::MathConstants<double>::twoPi
                                                   * 1000.0 * s / sr) * 0.5f);

        processBlocks (*proc, buf, 512);

        const auto leftRms  = rmsDb (buf, 0, 2048, N - 4096);
        const auto rightRms = rmsDb (buf, 1, 2048, N - 4096);

        expectGreaterThan (leftRms, -25.0f,
                           "Left channel lost its signal (" + juce::String (leftRms, 1) + " dB)");
        expectLessThan (rightRms, -80.0f,
                        "Right channel leaked: " + juce::String (rightRms, 1)
                        + " dB (left " + juce::String (leftRms, 1) + " dB)");
    }
};

//==============================================================================
// ZTAME-DSP-CORRECTNESS-007: Sample-rate independence
class ZTameSampleRateTest : public juce::UnitTest
{
public:
    ZTameSampleRateTest() : UnitTest ("ZTAME-DSP-CORRECTNESS-007: Sample-Rate Independence") {}

    void runTest() override
    {
        beginTest ("Null holds at 44.1k / 48k / 96k / 192k");

        for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            auto* proc = makeProc (sr, 512);
            setP (*proc, "bypass", 0.0f);
            setP (*proc, "sensitivity", 0.0f);
            setP (*proc, "range", 0.0f);

            const int latency = proc->getLatencySamples();
            constexpr int N = 16384;

            auto input = makeNoise (N, 0.3f);
            auto output = input;
            processBlocks (*proc, output, 512);

            juce::AudioBuffer<float> delayed (2, N);
            delayed.clear();
            for (int s = latency; s < N; ++s)
                for (int ch = 0; ch < 2; ++ch)
                    delayed.setSample (ch, s, input.getSample (ch, s - latency));

            const auto res = residual (output, delayed, 0, latency, N - 2 * latency);
            expectLessThan (res.peakDb, -80.0f,
                            "Null failed at " + juce::String (sr)
                            + " Hz: " + juce::String (res.peakDb, 1) + " dBFS");
        }
    }
};

//==============================================================================
// ZTAME-DSP-CORRECTNESS-008: Listen / delta audition
class ZTameListenModeTest : public juce::UnitTest
{
public:
    ZTameListenModeTest() : UnitTest ("ZTAME-DSP-CORRECTNESS-008: Delta Audition") {}

    void runTest() override
    {
        beginTest ("Delta = dry - wet (exact), with the documented +6 dB monitor gain");

        constexpr double sr = 44100.0;
        constexpr int N = 16384;

        // Resonant material so there is something to remove.
        auto input = makeNoise (N, 0.2f);
        for (int s = 0; s < N; ++s)
        {
            const auto tone = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi
                                                       * 3000.0 * s / sr);
            input.setSample (0, s, input.getSample (0, s) + tone);
            input.setSample (1, s, input.getSample (1, s) + tone);
        }

        const auto configure = [] (ZTameAudioProcessor& p)
        {
            setP (p, "bypass", 0.0f);
            setP (p, "sensitivity", 75.0f);
            setP (p, "range", 18.0f);
            setP (p, "dryWet", 100.0f);
            setP (p, "attack", 0.1f);
        };

        auto* procWet = makeProc (sr, 512);
        configure (*procWet);
        setP (*procWet, "listenMode", 0.0f);
        auto wet = input;
        processBlocks (*procWet, wet, 512);

        auto* procDelta = makeProc (sr, 512);
        configure (*procDelta);
        setP (*procDelta, "listenMode", 1.0f);
        auto delta = input;
        processBlocks (*procDelta, delta, 512);

        const int latency = procWet->getLatencySamples();

        juce::AudioBuffer<float> expected (2, N);
        expected.clear();
        for (int s = latency; s < N; ++s)
            for (int ch = 0; ch < 2; ++ch)
                expected.setSample (ch, s,
                    input.getSample (ch, s - latency) - wet.getSample (ch, s));

        // Undo the +6 dB monitor gain before comparing.
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < N; ++s)
                delta.setSample (ch, s, delta.getSample (ch, s) * 0.5f);

        const auto res = residual (delta, expected, 0, latency + 2048, N - latency - 4096);

        expectLessThan (res.rmsDb, -60.0f,
            "Delta is not (dry - wet): residual RMS " + juce::String (res.rmsDb, 1) + " dBFS");

        // The removed material must actually be significant for this source.
        const auto deltaRms = rmsDb (delta, 0, latency + 2048, N - latency - 4096);
        expectGreaterThan (deltaRms, -50.0f,
            "Delta contains almost nothing (" + juce::String (deltaRms, 1)
            + " dBFS) — suppression may not be engaging");
    }
};

//==============================================================================
// ZTAME-DSP-CORRECTNESS-009: Determinism
class ZTameDeterminismTest : public juce::UnitTest
{
public:
    ZTameDeterminismTest() : UnitTest ("ZTAME-DSP-CORRECTNESS-009: Determinism") {}

    void runTest() override
    {
        beginTest ("Same input + same parameters => bit-identical output (100 runs)");

        constexpr double sr = 44100.0;
        constexpr int N = 8192;

        auto input = makeNoise (N, 0.3f);

        auto* proc = makeProc (sr, 512);
        setP (*proc, "bypass", 0.0f);
        setP (*proc, "sensitivity", 55.0f);
        setP (*proc, "range", 15.0f);
        setP (*proc, "selectivity", 45.0f);

        auto reference = input;
        processBlocks (*proc, reference, 512);

        for (int run = 1; run < 100; ++run)
        {
            proc->prepareToPlay (sr, 512);

            auto output = input;
            processBlocks (*proc, output, 512);

            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < N; ++s)
                    if (std::abs (output.getSample (ch, s) - reference.getSample (ch, s)) > 1.0e-7f)
                    {
                        expect (false, "Non-deterministic at run " + juce::String (run)
                                       + " ch " + juce::String (ch) + " s " + juce::String (s));
                        return;
                    }
        }

        expect (true, "100 runs identical");
    }
};

//==============================================================================
// ZTAME-DSP-CORRECTNESS-010: Focus band
class ZTameFocusBandTest : public juce::UnitTest
{
public:
    ZTameFocusBandTest() : UnitTest ("ZTAME-DSP-CORRECTNESS-010: Focus Band") {}

    void runTest() override
    {
        beginTest ("Suppression only happens inside the focus band");

        constexpr double sr = 44100.0;
        constexpr int N = 16384;

        auto input = makeSine (8000.0, sr, N, 0.5f);   // a strong isolated 8 kHz peak

        const auto run = [&] (float lowHz, float highHz)
        {
            auto* proc = makeProc (sr, 512);
            setP (*proc, "bypass", 0.0f);
            setP (*proc, "sensitivity", 70.0f);
            setP (*proc, "range", 18.0f);
            setP (*proc, "focusLow", lowHz);
            setP (*proc, "focusHigh", highHz);
            setP (*proc, "attack", 0.1f);

            auto buf = input;
            processBlocks (*proc, buf, 512);

            return toneAmpDb (buf, 0, 8192, 4096, 8000.0, sr);
        };

        const auto insideDb  = run (6000.0f, 10000.0f);
        const auto outsideDb = run (200.0f, 800.0f);
        const auto fullDb    = run (20.0f, 20000.0f);

        expectLessThan (insideDb, -12.0f,
            "8 kHz peak not suppressed inside the band: " + juce::String (insideDb, 1) + " dBFS");
        expectGreaterThan (outsideDb, insideDb + 6.0f,
            "Band did not protect the peak: inside=" + juce::String (insideDb, 1)
            + " outside=" + juce::String (outsideDb, 1));
        expect (std::abs (fullDb - insideDb) < 3.0f,
            "Full-range band should behave like a wide band: full=" + juce::String (fullDb, 1)
            + " inside=" + juce::String (insideDb, 1));
    }
};

//==============================================================================
// ZTAME-DSP-CORRECTNESS-011: Sidechain routing
class ZTameSidechainTest : public juce::UnitTest
{
public:
    ZTameSidechainTest() : UnitTest ("ZTAME-DSP-CORRECTNESS-011: Sidechain") {}

    void runTest() override
    {
        beginTest ("Sidechain reroutes detection; it never leaks into the output");

        constexpr double sr = 44100.0;
        constexpr int N = 16384;

        // Main input: a steady 2 kHz tone (the engine treats it as a resonance).
        auto mainInput = makeSine (2000.0, sr, N, 0.4f);

        // Sidechain sources
        auto scQuietTone = makeSine (200.0, sr, N, 0.05f);
        auto scResonance = makeSine (2000.0, sr, N, 0.4f);

        const auto run = [&] (bool sidechainOn, const juce::AudioBuffer<float>& sidechain,
                              const juce::AudioBuffer<float>& main)
        {
            auto* proc = new ZTameAudioProcessor();

            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (juce::AudioChannelSet::stereo());    // main
            layout.inputBuses.add (juce::AudioChannelSet::stereo());    // sidechain
            layout.outputBuses.add (juce::AudioChannelSet::stereo());   // main

            const bool layoutOk = proc->setBusesLayout (layout);
            expect (layoutOk, "Sidechain bus layout rejected");
            proc->prepareToPlay (sr, 512);

            setP (*proc, "bypass", 0.0f);
            setP (*proc, "sensitivity", 70.0f);
            setP (*proc, "range", 18.0f);
            setP (*proc, "attack", 0.1f);
            setP (*proc, "sidechain", sidechainOn ? 1.0f : 0.0f);

            juce::AudioBuffer<float> buf (4, N);
            for (int s = 0; s < N; ++s)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    buf.setSample (ch, s, main.getNumSamples() > 0 ? main.getSample (ch, s) : 0.0f);
                    buf.setSample (2 + ch, s,
                        sidechain.getNumSamples() > 0 ? sidechain.getSample (ch, s) : 0.0f);
                }
            }

            processBlocks (*proc, buf, 512);

            return toneAmpDb (buf, 0, 8192, 4096, 2000.0, sr);
        };

        juce::AudioBuffer<float> silence;

        const auto internalDetect = run (false, silence, mainInput);
        const auto scQuiet        = run (true, scQuietTone, mainInput);
        const auto scResonant     = run (true, scResonance, mainInput);

        expectLessThan (internalDetect, -20.0f,
            "Main-input detection failed to tame the 2 kHz tone: "
            + juce::String (internalDetect, 1) + " dBFS");

        expectGreaterThan (scQuiet, internalDetect + 6.0f,
            "A quiet, non-resonant sidechain should protect the main tone: internal="
            + juce::String (internalDetect, 1) + " dB, sidechain=" + juce::String (scQuiet, 1) + " dB");

        expectLessThan (scResonant, internalDetect + 6.0f,
            "A resonant sidechain should drive suppression on the main path: internal="
            + juce::String (internalDetect, 1) + " dB, sidechain=" + juce::String (scResonant, 1) + " dB");

        // Sidechain content must never appear at the output.
        const auto leak = run (true, scResonance, silence);
        expectLessThan (leak, -80.0f,
            "Sidechain leaked into the output: " + juce::String (leak, 1) + " dBFS");
    }
};

//==============================================================================
// ZTAME-DSP-CORRECTNESS-012: Resolution modes
class ZTameResolutionTest : public juce::UnitTest
{
public:
    ZTameResolutionTest() : UnitTest ("ZTAME-DSP-CORRECTNESS-012: Resolution Modes") {}

    void runTest() override
    {
        beginTest ("Fast (1024) and Precise (2048) both null at their own latency");

        constexpr double sr = 44100.0;
        constexpr int N = 16384;

        for (int mode = 0; mode < 2; ++mode)
        {
            auto* proc = makeProc (sr, 512);
            setP (*proc, "bypass", 0.0f);
            setP (*proc, "sensitivity", 0.0f);
            setP (*proc, "range", 0.0f);
            setP (*proc, "resolution", (float) mode);

            // The parameter is latched at prepare time for the message-thread latency report.
            proc->prepareToPlay (sr, 512);

            const int latency = proc->getLatencySamples();
            const int expected = (mode == 0) ? 1024 : 2048;

            expectEquals (latency, expected,
                          "Mode " + juce::String (mode) + " reported latency " + juce::String (latency)
                          + ", expected " + juce::String (expected));

            auto input = makeNoise (N, 0.3f);
            auto output = input;
            processBlocks (*proc, output, 512);

            juce::AudioBuffer<float> delayed (2, N);
            delayed.clear();
            for (int s = latency; s < N; ++s)
                for (int ch = 0; ch < 2; ++ch)
                    delayed.setSample (ch, s, input.getSample (ch, s - latency));

            const auto res = residual (output, delayed, 0, latency, N - 2 * latency);
            expectLessThan (res.peakDb, -80.0f,
                            "Mode " + juce::String (mode) + " null peak "
                            + juce::String (res.peakDb, 1) + " dBFS");
        }
    }
};

//==============================================================================
// ZTAME-DSP-CORRECTNESS-013: Gain-reduction metering
class ZTameMeterTest : public juce::UnitTest
{
public:
    ZTameMeterTest() : UnitTest ("ZTAME-DSP-CORRECTNESS-013: GR Metering") {}

    void runTest() override
    {
        beginTest ("Meter follows the actual suppression and idles at zero");

        constexpr double sr = 44100.0;
        constexpr int N = 16384;

        auto resonant = makeNoise (N, 0.15f);
        for (int s = 0; s < N; ++s)
        {
            const auto tone = 0.6f * (float) std::sin (juce::MathConstants<double>::twoPi
                                                       * 4000.0 * s / sr);
            resonant.setSample (0, s, resonant.getSample (0, s) + tone);
            resonant.setSample (1, s, resonant.getSample (1, s) + tone);
        }

        // Engaged
        {
            auto* proc = makeProc (sr, 512);
            setP (*proc, "bypass", 0.0f);
            setP (*proc, "sensitivity", 80.0f);
            setP (*proc, "range", 20.0f);

            auto buf = resonant;
            processBlocks (*proc, buf, 512);

            expectGreaterThan (proc->getGainReductionPeakDb(), 3.0f,
                "Peak GR did not register suppression ("
                + juce::String (proc->getGainReductionPeakDb(), 1) + " dB)");
            expectGreaterThan (proc->getGainReductionDb(), 0.2f,
                "Average GR did not register suppression ("
                + juce::String (proc->getGainReductionDb(), 2) + " dB)");
        }

        // Idle (depth 0 => nothing can be reduced)
        {
            auto* proc = makeProc (sr, 512);
            setP (*proc, "bypass", 0.0f);
            setP (*proc, "sensitivity", 100.0f);
            setP (*proc, "range", 0.0f);

            auto buf = resonant;
            processBlocks (*proc, buf, 512);

            expectLessThan (proc->getGainReductionPeakDb(), 0.05f,
                "Meter moved with depth at 0 dB ("
                + juce::String (proc->getGainReductionPeakDb(), 2) + " dB)");
        }
    }
};

//==============================================================================
// ZTAME-DSP-CORRECTNESS-014: Shared display mapping (editor axis contract)
class ZTameDisplayMappingTest : public juce::UnitTest
{
public:
    ZTameDisplayMappingTest() : UnitTest ("ZTAME-DSP-CORRECTNESS-014: Display Mapping") {}

    void runTest() override
    {
        using P = ZTameAudioProcessor;

        beginTest ("Log frequency mapping is exact at the anchors");
        expectWithinAbsoluteError (P::freqToDisplayNorm (20.0f), 0.0f, 1.0e-5f);
        expectWithinAbsoluteError (P::freqToDisplayNorm (20000.0f), 1.0f, 1.0e-5f);

        beginTest ("Round trip 20 Hz .. 20 kHz");
        for (float f : { 20.0f, 100.0f, 250.0f, 1000.0f, 3150.0f, 12500.0f, 20000.0f })
        {
            const auto back = P::displayNormToFreq (P::freqToDisplayNorm (f));
            expectWithinAbsoluteError (back / f, 1.0f, 0.001f,
                "Round trip failed at " + juce::String (f) + " Hz (got "
                + juce::String (back) + ")");
        }

        beginTest ("Mapping is monotonic");
        float previous = -1.0f;
        for (int i = 0; i <= 64; ++i)
        {
            const auto f = P::displayNormToFreq ((float) i / 64.0f);
            expectGreaterThan (f, previous, "Mapping not monotonic at step " + juce::String (i));
            previous = f;
        }
    }
};

//==============================================================================
// ZTAME-DSP-CORRECTNESS-015: Editor telemetry (the "see what it is doing" path)
class ZTameTelemetryTest : public juce::UnitTest
{
public:
    ZTameTelemetryTest() : UnitTest ("ZTAME-DSP-CORRECTNESS-015: UI Telemetry") {}

    void runTest() override
    {
        beginTest ("Snapshot carries input/output spectra, removed energy, peaks and GR");

        constexpr double sr = 44100.0;
        constexpr int N = 32768;

        auto* proc = makeProc (sr, 512);
        proc->setUiActive (true);

        setP (*proc, "bypass", 0.0f);
        setP (*proc, "sensitivity", 80.0f);
        setP (*proc, "range", 20.0f);
        setP (*proc, "focusLow", 2000.0f);
        setP (*proc, "focusHigh", 6000.0f);

        auto input = makeNoise (N, 0.15f);
        for (int s = 0; s < N; ++s)
        {
            const auto tone = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi
                                                        * 4000.0 * s / sr);
            input.setSample (0, s, input.getSample (0, s) + tone);
            input.setSample (1, s, input.getSample (1, s) + tone);
        }

        auto buf = input;
        processBlocks (*proc, buf, 512);

        ZTameAudioProcessor::UiSnapshot snapshot;
        expect (proc->getLatestSnapshot (snapshot),
                "No snapshot was published while the editor was attached");

        const auto binOf = [] (float freq)
        {
            constexpr int numBins = ZTameAudioProcessor::UiSnapshot::numDisplayBins;
            return juce::jlimit (0, numBins - 1,
                                 (int) (ZTameAudioProcessor::freqToDisplayNorm (freq) * (float) numBins));
        };

        const int ringBin  = binOf (4000.0f);
        const int quietBin = binOf (150.0f);

        expectGreaterThan (snapshot.inputDb[(size_t) ringBin], -40.0f,
                           "Input spectrum does not show the 4 kHz ring");
        expectGreaterThan (snapshot.reductionDb[(size_t) ringBin], 3.0f,
                           "Removed-energy curve is empty at the ring frequency");
        expectGreaterThan (snapshot.inputDb[(size_t) ringBin] - snapshot.outputDb[(size_t) ringBin],
                           3.0f,
                           "Output spectrum is not below the input at the ring frequency");
        expectLessThan (snapshot.reductionDb[(size_t) quietBin], 1.5f,
                        "Suppression leaked outside the focus band in the telemetry");

        expectGreaterThan (snapshot.grPeakDb, 3.0f,
                           "GR telemetry did not register the suppression");

        // Peak markers must land on the ring.
        int bestPeak = -1;
        float bestDb = 0.0f;
        for (int i = 0; i < snapshot.numPeaks; ++i)
            if (snapshot.peakReductionDb[(size_t) i] > bestDb)
            {
                bestDb = snapshot.peakReductionDb[(size_t) i];
                bestPeak = i;
            }

        expect (bestPeak >= 0, "No resonance markers were published");
        if (bestPeak >= 0)
        {
            expectWithinAbsoluteError (snapshot.peakFreqHz[(size_t) bestPeak] / 4000.0f, 1.0f, 0.2f,
                "Resonance marker is at the wrong frequency ("
                + juce::String (snapshot.peakFreqHz[(size_t) bestPeak], 1) + " Hz)");
        }

        expectEquals (snapshot.fftSize, 2048, "Snapshot reports the wrong engine size");
        expect (! snapshot.bypassed, "Snapshot reports the wrong bypass state");
    }
};

//==============================================================================
// ZTAME-DSP-CORRECTNESS-016: Mid/Side mode
class ZTameMidSideTest : public juce::UnitTest
{
public:
    ZTameMidSideTest() : UnitTest ("ZTAME-DSP-CORRECTNESS-016: Mid/Side Mode") {}

    void runTest() override
    {
        constexpr double sr = 44100.0;
        constexpr int N = 16384;

        beginTest ("M/S codec is lossless: bypass and unity-gain nulls still hold");

        for (int stage = 0; stage < 2; ++stage)
        {
            auto* proc = makeProc (sr, 512);
            setP (*proc, "midSide", 1.0f);

            if (stage == 0)
            {
                setP (*proc, "bypass", 1.0f);
            }
            else
            {
                setP (*proc, "bypass", 0.0f);
                setP (*proc, "sensitivity", 0.0f);
                setP (*proc, "range", 0.0f);
            }

            const int latency = proc->getLatencySamples();

            // Decorrelated stereo material so an L/R swap or sign error would show up.
            auto input = makeNoise (N, 0.3f, 99);
            for (int s = 0; s < N; ++s)
                input.setSample (1, s, input.getSample (1, s) * 0.6f
                                        + 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi
                                                                   * 700.0 * s / sr));

            auto output = input;
            processBlocks (*proc, output, 512);

            double maxDiff = 0.0;
            for (int s = latency; s < N; ++s)
                for (int ch = 0; ch < 2; ++ch)
                    maxDiff = juce::jmax (maxDiff, (double) std::abs (output.getSample (ch, s)
                                                                     - input.getSample (ch, s - latency)));

            expectLessThan ((float) maxDiff, 1.0e-5f,
                            "M/S " + juce::String (stage == 0 ? "bypass" : "unity-gain")
                            + " is not lossless (maxDiff " + juce::String (maxDiff) + ")");
        }

        beginTest ("Centred resonance is tamed and the stereo image stays centred");

        auto* proc = makeProc (sr, 512);
        setP (*proc, "midSide", 1.0f);
        setP (*proc, "bypass", 0.0f);
        setP (*proc, "sensitivity", 80.0f);
        setP (*proc, "range", 18.0f);
        setP (*proc, "attack", 0.1f);

        auto centred = makeNoise (N, 0.15f, 5);
        for (int s = 0; s < N; ++s)
        {
            const auto ring = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi
                                                       * 4000.0 * s / sr);
            centred.setSample (0, s, centred.getSample (0, s) + ring);
            centred.setSample (1, s, centred.getSample (1, s) + ring);
        }

        auto out = centred;
        processBlocks (*proc, out, 512);

        const int latency = proc->getLatencySamples();

        float maxSide = 0.0f;
        for (int s = latency + 2048; s < N; ++s)
            maxSide = juce::jmax (maxSide, std::abs (out.getSample (0, s) - out.getSample (1, s)));

        expectLessThan (maxSide, 1.0e-4f,
                        "M/S mode broke the centred image (L-R residual "
                        + juce::String (maxSide) + ")");

        const auto ringOut = toneAmpDb (out, 0, latency + 4096, 4096, 4000.0, sr);
        const auto ringIn  = toneAmpDb (centred, 0, latency + 4096, 4096, 4000.0, sr);

        expectLessThan (ringOut, ringIn - 6.0f,
                        "Centred resonance was not tamed in M/S mode: in="
                        + juce::String (ringIn, 1) + " dB, out=" + juce::String (ringOut, 1) + " dB");
    }
};

//==============================================================================
void registerCorrectnessTests()
{
    static ZTameImpulseTest        test001;
    static ZTameFreqResponseTest   test002;
    static ZTameLatencyNullTest    test003;
    static ZTameBypassTest         test004;
    static ZTameBoundaryTest       test005;
    static ZTameChannelIndependenceTest test006;
    static ZTameSampleRateTest     test007;
    static ZTameListenModeTest     test008;
    static ZTameDeterminismTest    test009;
    static ZTameFocusBandTest      test010;
    static ZTameSidechainTest      test011;
    static ZTameResolutionTest     test012;
    static ZTameMeterTest          test013;
    static ZTameDisplayMappingTest test014;
    static ZTameTelemetryTest      test015;
    static ZTameMidSideTest        test016;
}
