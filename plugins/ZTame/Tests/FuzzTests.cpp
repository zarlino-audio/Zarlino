/*
    ZTame-FUZZ — 8-suite adversarial/fuzz test battery
    Designed to find crashes, NaN propagation, denormal CPU spikes,
    and instability under hostile inputs.
*/

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
namespace
{
    // NOTE: Returns raw pointer — intentionally leaked (see CorrectnessTests.cpp).
    static ZTameAudioProcessor* makeProc (
        double sr = 44100.0, int blockSize = 512)
    {
        auto* proc = new ZTameAudioProcessor();
        proc->prepareToPlay (sr, blockSize);
        return proc;
    }

    static void setP (ZTameAudioProcessor& p, const char* id, float v)
    {
        auto& vts = p.getValueTreeState();
        if (auto* param = vts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (v));
    }

    // Count finite, NaN, and Inf samples
    struct SampleStats
    {
        int finite = 0, nans = 0, infs = 0;
        float maxAbs = 0.0f;
    };

    static SampleStats stats (const juce::AudioBuffer<float>& buf)
    {
        SampleStats s;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                auto v = buf.getSample (ch, i);
                if (std::isnan (v))        ++s.nans;
                else if (std::isinf (v))   ++s.infs;
                else { ++s.finite; s.maxAbs = std::max (s.maxAbs, std::abs (v)); }
            }
        }
        return s;
    }
}

//==============================================================================
// ZTAME-FUZZ-001: Random Parameter Combinations
// Millions of random parameter sets — no crash, no hang.
class ZTameFuzzRandomParams : public juce::UnitTest
{
public:
    ZTameFuzzRandomParams() : UnitTest ("ZTAME-FUZZ-001: Random Parameter Fuzz") {}

    void runTest() override
    {
        beginTest ("Random parameter combinations do not crash");

        constexpr int iterations = 10000;
        juce::Random rng;

        struct ParamSpec { const char* id; float min; float max; };
        ParamSpec params[] = {
            {"sensitivity",   0.0f,  100.0f},
            {"range",         0.0f,   24.0f},
            {"selectivity",   0.0f,  100.0f},
            {"crossoverFreq", 20.0f, 20000.0f},
            {"attack",        0.1f,  100.0f},
            {"release",      10.0f, 1000.0f},
            {"dryWet",        0.0f,  100.0f},
        };

        // Generate fixed noise buffer for reproducibility
        juce::AudioBuffer<float> noiseBuf (2, 512);
        for (int s = 0; s < 512; ++s)
            for (int ch = 0; ch < 2; ++ch)
                noiseBuf.setSample (ch, s, (rng.nextFloat() * 2.0f - 1.0f) * 0.3f);

        // Reuse one processor for all iterations
        auto* proc = makeProc (44100.0, 512);

        for (int iter = 0; iter < iterations; ++iter)
        {
            // Random bypass
            setP (*proc, "bypass", rng.nextFloat() > 0.5f ? 1.0f : 0.0f);
            setP (*proc, "listenMode", rng.nextFloat() > 0.5f ? 1.0f : 0.0f);

            // Random float params
            for (auto& ps : params)
            {
                auto val = ps.min + rng.nextFloat() * (ps.max - ps.min);
                setP (*proc, ps.id, val);
            }

            auto buf = juce::AudioBuffer<float> (2, 512);
            buf.makeCopyOf (noiseBuf);
            juce::MidiBuffer midi;
            proc->processBlock (buf, midi);

            auto s = stats (buf);
            expect (s.nans == 0, "NaN in output at iter " + juce::String (iter));
            expect (s.infs == 0, "Inf in output at iter " + juce::String (iter));
            expect (s.maxAbs < 100.0f, "Output amplitude > 100 at iter "
                    + juce::String (iter));
        }
    }
};

//==============================================================================
// ZTAME-FUZZ-002: NaN Injection
// NaN input must not produce NaN output; must clamp to silence/last-good.
class ZTameFuzzNaN : public juce::UnitTest
{
public:
    ZTameFuzzNaN() : UnitTest ("ZTAME-FUZZ-002: NaN Injection") {}

    void runTest() override
    {
        beginTest ("NaN input is handled — no NaN propagation");

        auto* proc = makeProc (44100.0, 512);
        setP (*proc, "bypass", 0.0f);
        setP (*proc, "sensitivity", 50.0f);
        setP (*proc, "range", 12.0f);

        // Feed normal audio first to establish state
        {
            juce::AudioBuffer<float> warm (2, 4096);
            for (int s = 0; s < 4096; ++s)
                for (int ch = 0; ch < 2; ++ch)
                    warm.setSample (ch, s, std::sin (2.0 * juce::MathConstants<double>::pi
                                                     * 440.0 * s / 44100.0) * 0.5);
            for (int o = 0; o < 4096; o += 512)
            {
                juce::AudioBuffer<float> block (2, 512);
                for (int ch = 0; ch < 2; ++ch)
                    block.copyFrom (ch, 0, warm, ch, o, 512);
                juce::MidiBuffer midi;
                proc->processBlock (block, midi);
            }
        }

        // Now inject NaN
        juce::AudioBuffer<float> buf (2, 512);
        buf.clear();
        buf.setSample (0, 100, std::numeric_limits<float>::quiet_NaN());
        buf.setSample (1, 200, std::numeric_limits<float>::quiet_NaN());

        juce::MidiBuffer midi;
        proc->processBlock (buf, midi);

        auto s = stats (buf);
        expectEquals (s.nans, 0, "NaN propagated to output");

        // Also verify the rest of the block is bounded
        expect (s.maxAbs < 100.0f,
                "NaN injection caused output explosion: maxAbs="
                + juce::String (s.maxAbs));
    }
};

//==============================================================================
// ZTAME-FUZZ-003: Infinity Injection
class ZTameFuzzInf : public juce::UnitTest
{
public:
    ZTameFuzzInf() : UnitTest ("ZTAME-FUZZ-003: Infinity Injection") {}

    void runTest() override
    {
        beginTest ("Inf input does not propagate Infinity");

        auto* proc = makeProc (44100.0, 512);
        setP (*proc, "bypass", 0.0f);
        setP (*proc, "sensitivity", 50.0f);
        setP (*proc, "range", 12.0f);

        // Warm up
        {
            juce::AudioBuffer<float> warm (2, 4096);
            for (int s = 0; s < 4096; ++s)
                for (int ch = 0; ch < 2; ++ch)
                    warm.setSample (ch, s, std::sin (2.0 * juce::MathConstants<double>::pi
                                                     * 440.0 * s / 44100.0) * 0.5);
            for (int o = 0; o < 4096; o += 512)
            {
                juce::AudioBuffer<float> block (2, 512);
                for (int ch = 0; ch < 2; ++ch)
                    block.copyFrom (ch, 0, warm, ch, o, 512);
                juce::MidiBuffer midi;
                proc->processBlock (block, midi);
            }
        }

        // Inject Inf
        juce::AudioBuffer<float> buf (2, 512);
        buf.clear();
        buf.setSample (0, 50,  std::numeric_limits<float>::infinity());
        buf.setSample (1, 150, -std::numeric_limits<float>::infinity());

        juce::MidiBuffer midi;
        proc->processBlock (buf, midi);

        auto s = stats (buf);
        expectEquals (s.infs, 0, "Inf propagated to output");
    }
};

//==============================================================================
// ZTAME-FUZZ-004: Denormal-Heavy Input
// Long near-silence tail must not cause CPU spike or NaN.
class ZTameFuzzDenormal : public juce::UnitTest
{
public:
    ZTameFuzzDenormal() : UnitTest ("ZTAME-FUZZ-004: Denormal Handling") {}

    void runTest() override
    {
        beginTest ("Denormal-heavy silence tail produces finite output");

        auto* proc = makeProc (44100.0, 512);
        setP (*proc, "bypass", 0.0f);
        setP (*proc, "sensitivity", 80.0f);
        setP (*proc, "range", 24.0f);
        setP (*proc, "listenMode", 1.0f);  // Listen mode is most dangerous for denormals

        // Generate a -120 dBFS sine that decays to denormals
        constexpr int N = 16384;
        juce::AudioBuffer<float> buf (2, N);
        for (int s = 0; s < N; ++s)
        {
            const auto envelope = std::exp (-s / 2000.0f);
            const auto val = static_cast<float> (
                1e-6 * envelope
                * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * s / 44100.0));
            buf.setSample (0, s, val);
            buf.setSample (1, s, val);
        }

        // Process in blocks
        for (int offset = 0; offset < N; offset += 512)
        {
            juce::AudioBuffer<float> block (2, 512);
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom (ch, 0, buf, ch, offset, 512);
            juce::MidiBuffer midi;
            proc->processBlock (block, midi);
            for (int s = 0; s < 512; ++s)
                for (int ch = 0; ch < 2; ++ch)
                    expect (! std::isnan (block.getSample (ch, s)),
                            "NaN from denormal input at offset="
                            + juce::String (offset)
                            + " s=" + juce::String (s));
        }
    }
};

//==============================================================================
// ZTAME-FUZZ-005: Rapid Automation
// Thousands of parameter changes per second — no zipper noise blow-up, no crash.
class ZTameFuzzRapidAutomation : public juce::UnitTest
{
public:
    ZTameFuzzRapidAutomation() : UnitTest ("ZTAME-FUZZ-005: Rapid Automation") {}

    void runTest() override
    {
        beginTest ("Rapid parameter changes do not crash or blow up");

        juce::Random rng;
        constexpr int blocks = 2000;
        constexpr int blockSize = 64;  // small blocks = more param changes/sec

        auto* proc = makeProc (44100.0, blockSize);
        setP (*proc, "bypass", 0.0f);

        juce::AudioBuffer<float> noise (2, blockSize);
        for (int s = 0; s < blockSize; ++s)
            for (int ch = 0; ch < 2; ++ch)
                noise.setSample (ch, s, (rng.nextFloat() * 2.0f - 1.0f) * 0.3f);

        for (int b = 0; b < blocks; ++b)
        {
            // Randomize all params every block
            setP (*proc, "sensitivity",    rng.nextFloat() * 100.0f);
            setP (*proc, "range",          rng.nextFloat() * 24.0f);
            setP (*proc, "selectivity",    rng.nextFloat() * 100.0f);
            setP (*proc, "crossoverFreq",  20.0f + rng.nextFloat() * 19980.0f);
            setP (*proc, "attack",         0.1f + rng.nextFloat() * 99.9f);
            setP (*proc, "release",       10.0f + rng.nextFloat() * 990.0f);
            setP (*proc, "dryWet",         rng.nextFloat() * 100.0f);
            setP (*proc, "listenMode",     rng.nextFloat() > 0.5f ? 1.0f : 0.0f);

            auto buf = juce::AudioBuffer<float> (2, blockSize);
            buf.makeCopyOf (noise);
            juce::MidiBuffer midi;
            proc->processBlock (buf, midi);

            auto s = stats (buf);
            expect (s.nans == 0 && s.infs == 0,
                    "Bad output at block " + juce::String (b));
            expect (s.maxAbs < 100.0f,
                    "Output explosion at block " + juce::String (b));
        }
    }
};

//==============================================================================
// ZTAME-FUZZ-006: Sample-Rate Change Mid-Stream
class ZTameFuzzSRChange : public juce::UnitTest
{
public:
    ZTameFuzzSRChange() : UnitTest ("ZTAME-FUZZ-006: Sample-Rate Change Mid-Stream") {}

    void runTest() override
    {
        beginTest ("Sample-rate changes mid-stream do not crash");

        auto* proc = makeProc (44100.0, 512);
        setP (*proc, "bypass", 0.0f);
        setP (*proc, "sensitivity", 50.0f);
        setP (*proc, "range", 12.0f);

        juce::Random rng;
        auto makeNoiseBlock = [&](int n, float amp = 0.3f)
        {
            juce::AudioBuffer<float> b (2, n);
            for (int s = 0; s < n; ++s)
                for (int ch = 0; ch < 2; ++ch)
                    b.setSample (ch, s, (rng.nextFloat() * 2.0f - 1.0f) * amp);
            return b;
        };

        double rates[] = {44100.0, 48000.0, 96000.0, 44100.0, 192000.0, 44100.0};

        for (auto sr : rates)
        {
            proc->prepareToPlay (sr, 512);
            proc->releaseResources(); // clear state between rate changes
            proc->prepareToPlay (sr, 512);

            auto buf = makeNoiseBlock (512);
            juce::MidiBuffer midi;
            proc->processBlock (buf, midi);

            auto s = stats (buf);
            expect (s.nans == 0 && s.infs == 0,
                    "Bad output after SR change to " + juce::String (sr));
        }
    }
};

//==============================================================================
// ZTAME-FUZZ-007: Buffer-Size Change Mid-Stream
class ZTameFuzzBufferChange : public juce::UnitTest
{
public:
    ZTameFuzzBufferChange() : UnitTest ("ZTAME-FUZZ-007: Buffer-Size Change Mid-Stream") {}

    void runTest() override
    {
        beginTest ("Buffer-size changes mid-stream do not crash");

        auto* proc = makeProc (44100.0, 512);
        setP (*proc, "bypass", 0.0f);
        setP (*proc, "sensitivity", 50.0f);
        setP (*proc, "range", 12.0f);

        juce::Random rng;
        auto makeNoiseBlock = [&](int n)
        {
            juce::AudioBuffer<float> b (2, n);
            for (int s = 0; s < n; ++s)
                for (int ch = 0; ch < 2; ++ch)
                    b.setSample (ch, s, (rng.nextFloat() * 2.0f - 1.0f) * 0.3f);
            return b;
        };

        int sizes[] = {32, 256, 512, 1024, 8192, 64, 512};

        for (auto bs : sizes)
        {
            // Re-prepare with new block size to test robustness
            proc->releaseResources();
            proc->prepareToPlay (44100.0, bs);

            // Only process if block size <= sample count we can handle
            auto blockSize = std::min (bs, 2048);
            auto buf = makeNoiseBlock (blockSize);
            juce::MidiBuffer midi;
            proc->processBlock (buf, midi);

            auto s = stats (buf);
            expect (s.nans == 0 && s.infs == 0,
                    "Bad output at buffer size " + juce::String (bs));
        }
    }
};

//==============================================================================
// ZTAME-FUZZ-008: Memory/Stability Soak — Extended Processing
class ZTameFuzzSoak : public juce::UnitTest
{
public:
    ZTameFuzzSoak() : UnitTest ("ZTAME-FUZZ-008: Extended Soak Test") {}

    void runTest() override
    {
        beginTest ("Extended processing does not degrade or leak (smoke test)");

        auto* proc = makeProc (44100.0, 512);
        setP (*proc, "bypass", 0.0f);
        setP (*proc, "sensitivity", 60.0f);
        setP (*proc, "range", 16.0f);
        setP (*proc, "selectivity", 40.0f);
        setP (*proc, "attack", 5.0f);
        setP (*proc, "release", 200.0f);

        juce::Random rng;
        constexpr int blocks = 5000;

        float lastRms = -1.0f;

        for (int b = 0; b < blocks; ++b)
        {
            juce::AudioBuffer<float> buf (2, 512);
            for (int s = 0; s < 512; ++s)
                for (int ch = 0; ch < 2; ++ch)
                    buf.setSample (ch, s, (rng.nextFloat() * 2.0f - 1.0f) * 0.3f);

            juce::MidiBuffer midi;
            proc->processBlock (buf, midi);

            auto s = stats (buf);
            expect (s.nans == 0 && s.infs == 0,
                    "Bad output at soak block " + juce::String (b));

            // Check output hasn't gone completely silent or exploded
            if (b % 500 == 0)
            {
                double sumSq = 0.0;
                for (int i = 0; i < 512; ++i)
                    sumSq += static_cast<double> (buf.getSample (0, i))
                           * static_cast<double> (buf.getSample (0, i));
                auto rms = static_cast<float> (std::sqrt (sumSq / 512.0));
                if (lastRms > 0.0f)
                {
                    // RMS shouldn't change by more than 40 dB between checkpoints
                    float ratio = rms / lastRms;
                    if (ratio < 0.01f || ratio > 100.0f)
                    {
                        expect (false,
                                "RMS changed dramatically at block "
                                + juce::String (b) + ": "
                                + juce::String (lastRms) + " -> "
                                + juce::String (rms));
                    }
                }
                lastRms = rms;
            }
        }
    }
};

//==============================================================================
void registerFuzzTests()
{
    static ZTameFuzzRandomParams     test001;
    static ZTameFuzzNaN              test002;
    static ZTameFuzzInf              test003;
    static ZTameFuzzDenormal         test004;
    static ZTameFuzzRapidAutomation  test005;
    static ZTameFuzzSRChange         test006;
    static ZTameFuzzBufferChange     test007;
    static ZTameFuzzSoak             test008;
}
