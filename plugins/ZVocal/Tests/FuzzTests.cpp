/*
    ZVOCAL-FUZZ - adversarial / fuzz battery for the 0.2.0 prototype engine.

    Eight suites, per the dsp-correctness skill. Each suite is a single pass/fail unit:
    pass = zero occurrences of the target failure across the whole run.

      001 NaN injection      - NaN enters, no NaN leaves (state must not be poisoned either)
      002 Inf injection      - Inf enters, output stays finite
      003 Denormal tail      - a decaying tail to -200 dBFS produces no NaN/hang
      004 Rapid automation   - thousands of parameter changes: no crash, finite output
      005 Buffer-size sweep  - 16 -> 4096 samples per block, prepared per size
      006 Sample-rate change - 44.1k -> 96k -> 44.1k mid-stream
      007 Random parameters  - full parameter set randomised per block
      008 Soak               - extended run with periodic automation

    CPU timing (the skill's denormal "spike" criterion) is NOT asserted here; it needs the
    release-time performance harness on a fixed reference machine (spec section 9.3).
*/

#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <vector>

//==============================================================================
namespace
{
    // NOTE: raw pointer returned on purpose - processors are intentionally leaked.
    ZVocalAudioProcessor* makeProc (double sr = 48000.0, int blockSize = 512)
    {
        auto* proc = new ZVocalAudioProcessor();
        proc->prepareToPlay (sr, blockSize);
        return proc;
    }

    struct SampleStats
    {
        int finite = 0, nans = 0, infs = 0;
        float maxAbs = 0.0f;
    };

    SampleStats stats (const juce::AudioBuffer<float>& buf)
    {
        SampleStats s;

        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                const auto v = buf.getSample (ch, i);

                if (std::isnan (v))      ++s.nans;
                else if (std::isinf (v)) ++s.infs;
                else                     { ++s.finite; s.maxAbs = std::max (s.maxAbs, std::abs (v)); }
            }
        }

        return s;
    }

    void fillNoise (juce::AudioBuffer<float>& buf, float amp, int seed = 99)
    {
        juce::Random rng (seed);

        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            const auto v = (rng.nextFloat() * 2.0f - 1.0f) * amp;

            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                buf.setSample (ch, i, v);
        }
    }

    void randomiseAllParameters (ZVocalAudioProcessor& p, juce::Random& rng)
    {
        for (auto* param : p.getParameters())
            param->setValueNotifyingHost (rng.nextFloat());
    }
}

//==============================================================================
// ZVOCAL-FUZZ-NAN-001: NaN injection
class ZVocalFuzzNaN : public juce::UnitTest
{
public:
    ZVocalFuzzNaN() : UnitTest ("ZVOCAL-FUZZ-NAN-001: NaN Injection") {}

    void runTest() override
    {
        auto* proc = makeProc (48000.0, 512);

        juce::MidiBuffer midi;
        int totalNaN = 0, totalInf = 0;

        // Poison the input, then keep feeding clean audio to see whether the
        // internal state was poisoned too.
        for (int block = 0; block < 40; ++block)
        {
            juce::AudioBuffer<float> buf (2, 512);
            fillNoise (buf, 0.3f, 1000 + block);

            if (block == 0 || block == 1)
            {
                buf.setSample (0, 10, std::numeric_limits<float>::quiet_NaN());
                buf.setSample (1, 200, std::numeric_limits<float>::quiet_NaN());
                buf.setSample (0, 511, std::numeric_limits<float>::quiet_NaN());
            }

            proc->processBlock (buf, midi);

            const auto s = stats (buf);
            totalNaN += s.nans;
            totalInf += s.infs;
        }

        beginTest ("NaN in => no NaN and no Inf out, across 40 blocks");

        expectEquals (totalNaN, 0, "NaN propagated to the output");
        expectEquals (totalInf, 0, "Inf appeared in the output");
    }
};

//==============================================================================
// ZVOCAL-FUZZ-INF-002: Infinity injection
class ZVocalFuzzInf : public juce::UnitTest
{
public:
    ZVocalFuzzInf() : UnitTest ("ZVOCAL-FUZZ-INF-002: Infinity Injection") {}

    void runTest() override
    {
        auto* proc = makeProc (48000.0, 512);

        juce::MidiBuffer midi;
        int totalNaN = 0, totalInf = 0;
        float maxAbs = 0.0f;

        for (int block = 0; block < 40; ++block)
        {
            juce::AudioBuffer<float> buf (2, 512);
            fillNoise (buf, 0.3f, 2000 + block);

            if (block == 0)
            {
                buf.setSample (0, 5, std::numeric_limits<float>::infinity());
                buf.setSample (1, 100, -std::numeric_limits<float>::infinity());
            }

            proc->processBlock (buf, midi);

            const auto s = stats (buf);
            totalNaN += s.nans;
            totalInf += s.infs;
            maxAbs = std::max (maxAbs, s.maxAbs);
        }

        beginTest ("Inf in => finite, bounded output");

        expectEquals (totalNaN, 0, "NaN produced from Inf input");
        expectEquals (totalInf, 0, "Inf propagated to the output");
        expectLessOrEqual (maxAbs, 4.0f,
                                 "output ran away after Inf input (maxAbs = "
                                 + juce::String (maxAbs) + ")");
    }
};

//==============================================================================
// ZVOCAL-FUZZ-DENORM-003: denormal-heavy decaying tail
class ZVocalFuzzDenormal : public juce::UnitTest
{
public:
    ZVocalFuzzDenormal() : UnitTest ("ZVOCAL-FUZZ-DENORM-003: Denormal Tail") {}

    void runTest() override
    {
        auto* proc = makeProc (48000.0, 512);

        const int totalSamples = 48000 * 5;
        int totalNaN = 0, totalInf = 0;
        float maxAbs = 0.0f;

        juce::MidiBuffer midi;

        for (int pos = 0; pos < totalSamples; pos += 512)
        {
            juce::AudioBuffer<float> buf (2, 512);

            for (int i = 0; i < 512; ++i)
            {
                const auto t = (double) (pos + i) / (double) totalSamples;

                // -6 dBFS decaying to roughly -200 dBFS: deep into denormal range.
                const auto amp = 0.5 * std::pow (10.0, -10.0 * t);
                const auto v = (float) (amp * std::sin (juce::MathConstants<double>::twoPi
                                                        * 997.0 * (double) (pos + i) / 48000.0));

                for (int ch = 0; ch < 2; ++ch)
                    buf.setSample (ch, i, v);
            }

            proc->processBlock (buf, midi);

            const auto s = stats (buf);
            totalNaN += s.nans;
            totalInf += s.infs;
            maxAbs = std::max (maxAbs, s.maxAbs);
        }

        beginTest ("5 s decaying tail stays finite");

        expectEquals (totalNaN, 0, "NaN during denormal tail");
        expectEquals (totalInf, 0, "Inf during denormal tail");
        expectLessOrEqual (maxAbs, 4.0f, "output ran away during denormal tail");
    }
};

//==============================================================================
// ZVOCAL-FUZZ-RANDOM-007: full random parameter set per block
class ZVocalFuzzRandomParams : public juce::UnitTest
{
public:
    ZVocalFuzzRandomParams() : UnitTest ("ZVOCAL-FUZZ-007: Random Parameter Fuzz") {}

    void runTest() override
    {
        auto* proc = makeProc (48000.0, 512);

        juce::Random rng (20260913);
        juce::MidiBuffer midi;

        int totalNaN = 0, totalInf = 0;
        float maxAbs = 0.0f;

        for (int block = 0; block < 2000; ++block)
        {
            randomiseAllParameters (*proc, rng);

            juce::AudioBuffer<float> buf (2, 512);
            fillNoise (buf, 0.4f, 3000 + block);

            proc->processBlock (buf, midi);

            const auto s = stats (buf);
            totalNaN += s.nans;
            totalInf += s.infs;
            maxAbs = std::max (maxAbs, s.maxAbs);
        }

        beginTest ("2000 blocks x randomised parameter set: no crash, finite output");

        expectEquals (totalNaN, 0, "NaN with randomised parameters");
        expectEquals (totalInf, 0, "Inf with randomised parameters");
        expectLessOrEqual (maxAbs, 8.0f,
                                 "output ran away with randomised parameters (maxAbs = "
                                 + juce::String (maxAbs) + ")");
    }
};

//==============================================================================
// ZVOCAL-FUZZ-AUTO-004: rapid automation
class ZVocalFuzzRapidAutomation : public juce::UnitTest
{
public:
    ZVocalFuzzRapidAutomation() : UnitTest ("ZVOCAL-FUZZ-004: Rapid Automation") {}

    void runTest() override
    {
        auto* proc = makeProc (48000.0, 512);

        const char* ids[] = { "macro.colour", "macro.texture", "macro.thickness",
                              "macro.punch", "macro.focus" };

        juce::MidiBuffer midi;
        int totalNaN = 0, totalInf = 0;
        float maxAbs = 0.0f;

        // Full-range swings every block for 3000 blocks (~32 s at 48 kHz).
        for (int block = 0; block < 3000; ++block)
        {
            const auto phase = (block % 20) / 19.0f;

            int i = 0;

            for (auto* id : ids)
            {
                auto& vts = proc->getValueTreeState();

                if (auto* param = vts.getParameter (id))
                {
                    const auto target = (i % 2 == 0) ? phase : 1.0f - phase;
                    param->setValueNotifyingHost (target);
                }

                ++i;
            }

            juce::AudioBuffer<float> buf (2, 512);
            fillNoise (buf, 0.3f, 4000 + block);

            proc->processBlock (buf, midi);

            const auto s = stats (buf);
            totalNaN += s.nans;
            totalInf += s.infs;
            maxAbs = std::max (maxAbs, s.maxAbs);
        }

        beginTest ("parameter storm: no crash, finite output");

        expectEquals (totalNaN, 0, "NaN during rapid automation");
        expectEquals (totalInf, 0, "Inf during rapid automation");
        expectLessOrEqual (maxAbs, 8.0f, "output ran away during rapid automation");
    }
};

//==============================================================================
// ZVOCAL-FUZZ-BUF-005: buffer-size sweep
class ZVocalFuzzBufferSize : public juce::UnitTest
{
public:
    ZVocalFuzzBufferSize() : UnitTest ("ZVOCAL-FUZZ-005: Buffer-Size Sweep") {}

    void runTest() override
    {
        const int sizes[] = { 16, 32, 48, 64, 128, 256, 512, 1024, 2048, 4096 };

        int totalNaN = 0, totalInf = 0;
        float maxAbs = 0.0f;

        for (auto n : sizes)
        {
            // A host re-prepares the processor when the block size changes.
            auto* proc = makeProc (48000.0, n);

            juce::MidiBuffer midi;

            for (int block = 0; block < 8; ++block)
            {
                juce::AudioBuffer<float> buf (2, n);
                fillNoise (buf, 0.3f, 5000 + n + block);

                proc->processBlock (buf, midi);

                const auto s = stats (buf);
                totalNaN += s.nans;
                totalInf += s.infs;
                maxAbs = std::max (maxAbs, s.maxAbs);
            }

            beginTest (juce::String (n) + " samples per block");

            expectEquals (totalNaN, 0, "NaN");
            expectEquals (totalInf, 0, "Inf at this block size");
            expectLessOrEqual (maxAbs, 8.0f,
                                     "output ran away at block size " + juce::String (n));
        }
    }
};

//==============================================================================
// ZVOCAL-FUZZ-SR-006: sample-rate change mid-stream
class ZVocalFuzzSampleRate : public juce::UnitTest
{
public:
    ZVocalFuzzSampleRate() : UnitTest ("ZVOCAL-FUZZ-006: Sample-Rate Change Mid-Stream") {}

    void runTest() override
    {
        auto* proc = new ZVocalAudioProcessor();

        const double rates[] = { 44100.0, 96000.0, 44100.0, 192000.0, 48000.0 };

        int totalNaN = 0, totalInf = 0;
        float maxAbs = 0.0f;

        for (auto sr : rates)
        {
            proc->prepareToPlay (sr, 512);

            juce::MidiBuffer midi;

            for (int block = 0; block < 16; ++block)
            {
                juce::AudioBuffer<float> buf (2, 512);
                fillNoise (buf, 0.3f, 6000 + block);

                proc->processBlock (buf, midi);

                const auto s = stats (buf);
                totalNaN += s.nans;
                totalInf += s.infs;
                maxAbs = std::max (maxAbs, s.maxAbs);
            }
        }

        beginTest ("rate changes without stale state or blow-up");

        expectEquals (totalNaN, 0, "NaN after a sample-rate change");
        expectEquals (totalInf, 0, "Inf after a sample-rate change");
        expectLessOrEqual (maxAbs, 8.0f, "output ran away after a sample-rate change");
    }
};

//==============================================================================
// ZVOCAL-FUZZ-SOAK-008: extended soak with periodic automation
class ZVocalFuzzSoak : public juce::UnitTest
{
public:
    ZVocalFuzzSoak() : UnitTest ("ZVOCAL-FUZZ-008: Extended Soak") {}

    void runTest() override
    {
        auto* proc = makeProc (48000.0, 512);

        juce::Random rng (777);
        juce::MidiBuffer midi;

        int totalNaN = 0, totalInf = 0;
        float maxAbs = 0.0f;

        for (int block = 0; block < 4000; ++block)
        {
            if (block % 16 == 0)
                randomiseAllParameters (*proc, rng);

            juce::AudioBuffer<float> buf (2, 512);
            fillNoise (buf, 0.25f, 7000 + block);

            proc->processBlock (buf, midi);

            const auto s = stats (buf);
            totalNaN += s.nans;
            totalInf += s.infs;
            maxAbs = std::max (maxAbs, s.maxAbs);
        }

        beginTest ("~43 s soak with automation: finite and bounded throughout");

        expectEquals (totalNaN, 0, "NaN during soak");
        expectEquals (totalInf, 0, "Inf during soak");
        expectLessOrEqual (maxAbs, 8.0f, "output drifted during soak");
    }
};

//==============================================================================
void registerFuzzTests()
{
    static ZVocalFuzzNaN              fuzz001;
    static ZVocalFuzzInf              fuzz002;
    static ZVocalFuzzDenormal         fuzz003;
    static ZVocalFuzzRapidAutomation  fuzz004;
    static ZVocalFuzzBufferSize       fuzz005;
    static ZVocalFuzzSampleRate       fuzz006;
    static ZVocalFuzzRandomParams     fuzz007;
    static ZVocalFuzzSoak             fuzz008;
}
