/*
    ZVOCAL-DSP - correctness battery for the 0.2.0 prototype engine.

    Contract under test (see specs/ZVocal_Gate1_Spec_and_Differentiators.md, section 12.1):

      1. Parameter contract: the five macro parameters (macro.colour ... macro.focus) and the
         six stage bypass switches exist with the documented range and defaults. The macro ids
         are load-bearing: decision D-1 renamed the product but deliberately kept them so that
         existing host automation keeps working.
      2. State round-trip: getStateInformation -> setStateInformation restores macro values.
      3. Latency honesty: the value reported to the host equals the measured impulse delay.
      4. Silence in => digital silence out, and the output is always finite.
      5. Sample-rate independence: 44.1 / 48 / 96 / 192 kHz stay finite and bounded.
      6. Channel linking: identical L/R input produces identical L/R output.
      7. Determinism: same input + same parameters => bit-identical output.

    NOT YET COVERED (deliberately): null-vs-golden-reference and the five golden program-material
    comparisons from the dsp-correctness skill. ZVocal has no approved reference render yet, so
    those tests arrive with the P2 main-path work when a reference build exists.
*/

#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

//==============================================================================
namespace
{
    // Diagnostics: set ZVOCAL_TRACE=1 in the environment to print each step of a
    // suite as it runs. Used to localise a crash when the runner dies silently.
    void trace (const char* what)
    {
        if (std::getenv ("ZVOCAL_TRACE") != nullptr)
            std::cout << "[trace] " << what << std::endl;
    }

    // NOTE: raw pointer returned on purpose - processors are intentionally leaked.
    ZVocalAudioProcessor* makeProc (double sr = 44100.0, int blockSize = 512)
    {
        trace ("makeProc: construct");
        auto* proc = new ZVocalAudioProcessor();
        trace ("makeProc: prepareToPlay");
        proc->prepareToPlay (sr, blockSize);
        trace ("makeProc: ready");
        return proc;
    }

    void setP (ZVocalAudioProcessor& p, const char* id, float value)
    {
        auto& vts = p.getValueTreeState();
        if (auto* param = vts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    float getP (ZVocalAudioProcessor& p, const char* id, float fallback = -999.0f)
    {
        auto& vts = p.getValueTreeState();
        if (auto* param = vts.getParameter (id))
            return param->convertFrom0to1 (param->getValue());
        return fallback;
    }

    juce::AudioBuffer<float> makeSine (double freq, double sr, int numSamples,
                                       float amp = 0.25f, int numChannels = 2)
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

    // Process a buffer in fixed-size chunks, returning the fully processed copy.
    juce::AudioBuffer<float> processBlockwise (ZVocalAudioProcessor& p,
                                               const juce::AudioBuffer<float>& in,
                                               int blockSize)
    {
        juce::AudioBuffer<float> out (in.getNumChannels(), in.getNumSamples());
        out.clear();

        juce::MidiBuffer midi;
        const int total = in.getNumSamples();

        for (int pos = 0; pos < total; pos += blockSize)
        {
            const int n = std::min (blockSize, total - pos);

            juce::AudioBuffer<float> chunk (in.getNumChannels(), n);
            for (int ch = 0; ch < in.getNumChannels(); ++ch)
                chunk.copyFrom (ch, 0, in, ch, pos, n);

            p.processBlock (chunk, midi);

            for (int ch = 0; ch < in.getNumChannels(); ++ch)
                out.copyFrom (ch, pos, chunk, ch, 0, n);
        }

        return out;
    }
}

//==============================================================================
// ZVOCAL-DSP-PARAMS-001: Parameter layout contract
class ZVocalParameterLayoutTest : public juce::UnitTest
{
public:
    ZVocalParameterLayoutTest() : UnitTest ("ZVOCAL-DSP-PARAMS-001: Parameter Layout Contract") {}

    void runTest() override
    {
        auto* proc = new ZVocalAudioProcessor();
        auto& vts = proc->getValueTreeState();

        beginTest ("five macro parameters exist with the documented range and defaults");

        struct MacroExpect { const char* id; const char* name; float def; };
        const MacroExpect macros[] =
        {
            { "macro.colour",    "Colour",    58.0f },
            { "macro.texture",   "Texture",   35.0f },
            { "macro.thickness", "Thickness", 48.0f },
            { "macro.punch",     "Punch",     42.0f },
            { "macro.focus",     "Focus",     62.0f },
        };

        for (const auto& m : macros)
        {
            auto* p = vts.getParameter (m.id);
            expect (p != nullptr, juce::String ("missing macro parameter: ") + m.id);

            if (p == nullptr)
                continue;

            expectEquals (p->getName (64), juce::String (m.name), "macro name");

            const auto range = p->getNormalisableRange();
            expectWithinAbsoluteError (range.start, 0.0f, 1.0e-4f);
            expectWithinAbsoluteError (range.end, 100.0f, 1.0e-4f);

            const auto def = p->convertFrom0to1 (p->getDefaultValue());
            expectWithinAbsoluteError (def, m.def, 0.01f, "macro default");
        }

        beginTest ("six stage bypass switches exist and are boolean");

        const char* bypasses[] = { "stageA.bypass", "stageB.bypass", "stageC.bypass",
                                   "stageD.bypass", "stageE.bypass", "stageF.bypass" };

        for (auto* id : bypasses)
            expect (dynamic_cast<juce::AudioParameterBool*> (vts.getParameter (id)) != nullptr,
                    juce::String ("expected a boolean stage bypass: ") + id);

        beginTest ("global trims exist");

        expect (vts.getParameter ("global.input_trim") != nullptr, "missing global.input_trim");
        expect (vts.getParameter ("global.output_trim") != nullptr, "missing global.output_trim");
    }
};

//==============================================================================
// ZVOCAL-DSP-STATE-002: State save / load round-trip
class ZVocalStateRoundTripTest : public juce::UnitTest
{
public:
    ZVocalStateRoundTripTest() : UnitTest ("ZVOCAL-DSP-STATE-002: State Round-Trip") {}

    void runTest() override
    {
        trace ("STATE-002: begin");
        auto* a = makeProc();
        trace ("STATE-002: proc a ready");
        auto* b = makeProc();
        trace ("STATE-002: proc b ready");

        struct Setting { const char* id; float value; };
        const Setting settings[] =
        {
            { "macro.colour",    12.0f },
            { "macro.texture",   77.0f },
            { "macro.thickness", 3.0f  },
            { "macro.punch",     91.0f },
            { "macro.focus",     44.0f },
        };

        for (const auto& s : settings)
            setP (*a, s.id, s.value);

        trace ("STATE-002: parameters set");

        juce::MemoryBlock state;
        a->getStateInformation (state);

        trace ("STATE-002: getStateInformation done");

        // NOTE: beginTest() must come before the first expect() in a suite - JUCE's
        // UnitTestRunner clears its result list at the start of each test, so an
        // expect() before beginTest() dereferences a null result and crashes.
        beginTest ("getStateInformation produced data and the value survives the round-trip");

        expect (state.getSize() > 0, "getStateInformation produced no data");

        trace ("STATE-002: round-trip on the SAME instance (a)");
        a->setStateInformation (state.getData(), (int) state.getSize());
        trace ("STATE-002: same-instance round-trip survived");

        trace ("STATE-002: round-trip onto a SECOND instance (b)");
        b->setStateInformation (state.getData(), (int) state.getSize());

        trace ("STATE-002: setStateInformation done");

        beginTest ("macro values survive a save -> load round-trip");

        for (const auto& s : settings)
            expectWithinAbsoluteError (getP (*b, s.id), s.value, 0.01f,
                                       juce::String ("macro value after load: ") + s.id);

        beginTest ("reload into a second processor does not disturb its own instance");

        expectWithinAbsoluteError (getP (*a, "macro.colour"), 12.0f, 0.01f,
                                   "source processor changed by saving");
    }
};

//==============================================================================
// ZVOCAL-DSP-LAT-003: Latency is reported honestly
class ZVocalLatencyTest : public juce::UnitTest
{
public:
    ZVocalLatencyTest() : UnitTest ("ZVOCAL-DSP-LAT-003: Latency / Impulse Alignment") {}

    void runTest() override
    {
        auto* proc = makeProc (48000.0, 512);
        const int reported = proc->getLatencySamples();

        beginTest ("reported latency equals the measured impulse delay");

        expect (reported >= 0, "reported latency must not be negative");

        beginTest ("reported latency equals the measured impulse delay");

        juce::AudioBuffer<float> buf (2, 512);
        buf.clear();
        buf.setSample (0, 0, 1.0f);
        buf.setSample (1, 0, 1.0f);

        juce::MidiBuffer midi;
        proc->processBlock (buf, midi);

        int firstNonZero = -1;

        for (int i = 0; i < buf.getNumSamples() && firstNonZero < 0; ++i)
            if (std::abs (buf.getSample (0, i)) > 1.0e-6f)
                firstNonZero = i;

        expect (firstNonZero >= 0, "impulse produced no output at all");
        expectEquals (firstNonZero, reported,
                      "measured impulse delay must match the reported latency");
    }
};

//==============================================================================
// ZVOCAL-DSP-SILENCE-004: Digital silence in => digital silence out
class ZVocalSilenceTest : public juce::UnitTest
{
public:
    ZVocalSilenceTest() : UnitTest ("ZVOCAL-DSP-SILENCE-004: Silence In / Silence Out") {}

    void runTest() override
    {
        auto* proc = makeProc (48000.0, 512);

        juce::AudioBuffer<float> buf (2, 4096);
        buf.clear();

        juce::MidiBuffer midi;

        for (int pos = 0; pos < buf.getNumSamples(); pos += 512)
        {
            juce::AudioBuffer<float> chunk (2, 512);
            chunk.clear();
            proc->processBlock (chunk, midi);

            for (int ch = 0; ch < 2; ++ch)
                buf.copyFrom (ch, pos, chunk, ch, 0, 512);
        }

        const auto s = stats (buf);

        beginTest ("silence stays silent and finite");

        expectEquals (s.nans, 0, "NaN in silence response");
        expectEquals (s.infs, 0, "Inf in silence response");
        expectLessOrEqual (s.maxAbs, 1.0e-6f,
                                 "silence must produce digital silence (got "
                                 + juce::String (s.maxAbs) + ")");
    }
};

//==============================================================================
// ZVOCAL-DSP-RATE-005: Sample-rate independence
class ZVocalSampleRateTest : public juce::UnitTest
{
public:
    ZVocalSampleRateTest() : UnitTest ("ZVOCAL-DSP-RATE-005: Sample-Rate Sweep") {}

    void runTest() override
    {
        const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };

        for (auto sr : rates)
        {
            auto* proc = makeProc (sr, 512);

            setP (*proc, "macro.colour", 70.0f);
            setP (*proc, "macro.thickness", 65.0f);
            setP (*proc, "macro.punch", 55.0f);

            const auto in = makeSine (220.0, sr, (int) sr, 0.25f);
            const auto out = processBlockwise (*proc, in, 512);
            const auto s = stats (out);

            beginTest (juce::String (sr) + " Hz stays finite and bounded");

            expectEquals (s.nans, 0, "NaN");
            expectEquals (s.infs, 0, "Inf");
            expectGreaterThan (s.finite, 0, "no finite samples produced");
            expectLessOrEqual (s.maxAbs, 4.0f,
                                     "output ran away (maxAbs = " + juce::String (s.maxAbs) + ")");
        }
    }
};

//==============================================================================
// ZVOCAL-DSP-LINK-006: Channel linking (identical L/R in => identical L/R out)
class ZVocalChannelLinkTest : public juce::UnitTest
{
public:
    ZVocalChannelLinkTest() : UnitTest ("ZVOCAL-DSP-LINK-006: Channel Linking") {}

    void runTest() override
    {
        auto* proc = makeProc (48000.0, 512);

        const auto in = makeNoise (8192, 0.3f);
        const auto out = processBlockwise (*proc, in, 512);

        beginTest ("identical channels stay identical");

        float worst = 0.0f;

        for (int i = 0; i < out.getNumSamples(); ++i)
            worst = std::max (worst, std::abs (out.getSample (0, i) - out.getSample (1, i)));

        expectWithinAbsoluteError (worst, 0.0f, 1.0e-7f,
                                   "L/R diverged for identical input (worst delta "
                                   + juce::String (worst) + ")");
    }
};

//==============================================================================
// ZVOCAL-DSP-DET-007: Determinism (bit-exact repeat)
class ZVocalDeterminismTest : public juce::UnitTest
{
public:
    ZVocalDeterminismTest() : UnitTest ("ZVOCAL-DSP-DET-007: Determinism (100 runs)") {}

    void runTest() override
    {
        const auto in = makeNoise (8192, 0.3f, 20260913);

        std::vector<float> reference;
        int runsCompared = 0;
        int firstDiffRun = -1;
        int firstDiffSample = -1;

        beginTest ("100 fresh runs produce bit-identical output");

        for (int run = 0; run < 100; ++run)
        {
            auto* proc = makeProc (48000.0, 512);

            setP (*proc, "macro.colour", 61.0f);
            setP (*proc, "macro.texture", 38.0f);

            const auto out = processBlockwise (*proc, in, 512);

            std::vector<float> flat ((size_t) (out.getNumSamples() * out.getNumChannels()));

            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                std::copy (out.getReadPointer (ch),
                           out.getReadPointer (ch) + out.getNumSamples(),
                           flat.begin() + (size_t) (ch * out.getNumSamples()));

            if (run == 0)
            {
                reference = flat;
                continue;
            }

            ++runsCompared;

            for (size_t i = 0; i < flat.size(); ++i)
            {
                if (flat[i] != reference[i])
                {
                    firstDiffRun = run;
                    firstDiffSample = (int) i;
                    break;
                }
            }

            if (firstDiffRun >= 0)
                break;
        }

        // Positive assertions: without these the suite would report 0/0 and pass
        // vacuously even if the loop never ran.
        expectEquals (runsCompared, 99, "expected 99 comparison runs against the reference");
        expect (firstDiffRun < 0,
                firstDiffRun >= 0
                    ? ("run " + juce::String (firstDiffRun) + " differs at sample "
                       + juce::String (firstDiffSample))
                    : juce::String());
    }
};

//==============================================================================
void registerCorrectnessTests()
{
    static ZVocalParameterLayoutTest   test001;
    static ZVocalStateRoundTripTest    test002;
    static ZVocalLatencyTest           test003;
    static ZVocalSilenceTest           test004;
    static ZVocalSampleRateTest        test005;
    static ZVocalChannelLinkTest       test006;
    static ZVocalDeterminismTest       test007;
}
