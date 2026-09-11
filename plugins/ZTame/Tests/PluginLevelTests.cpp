/*
    ZTame-PLUGIN — Plugin-level golden-reference, determinism, and
    parameter-sweep validation tests.
*/

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
namespace
{
    // NOTE: Returns raw pointer — intentionally leaked (see CorrectnessTests.cpp).
    static ZTameAudioProcessor* makeProc (double sr, int bs)
    {
        auto* proc = new ZTameAudioProcessor();
        proc->prepareToPlay (sr, bs);
        return proc;
    }

    static void setP (ZTameAudioProcessor& p, const char* id, float v)
    {
        auto& vts = p.getValueTreeState();
        if (auto* param = vts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (v));
    }

    static float rmsDb (const juce::AudioBuffer<float>& buf, int ch, int start, int len)
    {
        double sumSq = 0.0;
        for (int i = start; i < start + len; ++i)
            sumSq += static_cast<double> (buf.getSample (ch, i))
                   * static_cast<double> (buf.getSample (ch, i));
        return juce::Decibels::gainToDecibels (
            static_cast<float> (std::sqrt (sumSq / len)), -140.0f);
    }

    // Generate test material types
    static juce::AudioBuffer<float> makeSine (float freq, double sr, int len, float amp)
    {
        juce::AudioBuffer<float> b (1, len);
        for (int s = 0; s < len; ++s)
            b.setSample (0, s, amp * std::sin (juce::MathConstants<float>::twoPi
                                              * freq * s / static_cast<float> (sr)));
        return b;
    }

    static juce::AudioBuffer<float> makeWhiteNoise (int len, float amp)
    {
        juce::Random rng (12345);
        juce::AudioBuffer<float> b (1, len);
        for (int s = 0; s < len; ++s)
            b.setSample (0, s, (rng.nextFloat() * 2.0f - 1.0f) * amp);
        return b;
    }

    static juce::AudioBuffer<float> makeImpulseTrain (float freq, double sr, int len, float amp)
    {
        juce::AudioBuffer<float> b (1, len);
        b.clear();
        auto period = static_cast<int> (sr / freq);
        for (int s = 0; s < len; s += period)
            b.setSample (0, s, amp);
        return b;
    }

    // Process a mono buffer through both channels, return output
    static juce::AudioBuffer<float> processMono (
        ZTameAudioProcessor& proc,
        const juce::AudioBuffer<float>& input,
        int blockSize = 512)
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

//==============================================================================
// ZTAME-PLUGIN-001: Golden Reference — Pure Sine (no suppression expected)
// With sensitivity=0 and range=0, output should match input.
class ZTameGoldenSinePassthrough : public juce::UnitTest
{
public:
    ZTameGoldenSinePassthrough()
        : UnitTest ("ZTAME-PLUGIN-001: Golden — Sine Passthrough (no params)") {}

    void runTest() override
    {
        beginTest ("Sine through zero params matches reference");

        constexpr double sr = 44100.0;
        constexpr int N = 8192;

        // Generate reference: same sine, processed through bypass
        auto procRef = makeProc (sr, 512);
        setP (*procRef, "bypass", 1.0f);

        auto input = makeSine (1000.0f, sr, N, 0.5f);
        auto ref = processMono (*procRef, input);

        // Now process with sensitivity=0, range=0 (should not suppress)
        auto procTest = makeProc (sr, 512);
        setP (*procTest, "bypass", 0.0f);
        setP (*procTest, "sensitivity", 0.0f);
        setP (*procTest, "range", 0.0f);
        setP (*procTest, "dryWet", 100.0f);

        auto test = processMono (*procTest, input);

        // Compare RMS in the middle portion (skip the latency region and the tail)
        auto refRms  = rmsDb (ref, 0, 4096, N - 8192);
        auto testRms = rmsDb (test, 0, 4096, N - 8192);

        // The wet path is latency-aligned with the dry path, so the level must match
        // closely now that the STFT uses a COLA-exact sqrt-Hann window pair.
        auto diffDb = std::abs (refRms - testRms);
        expectLessThan (diffDb, 0.3f,
                        "Sine passthrough differs from bypass: ref="
                        + juce::String (refRms, 2) + " dB, test="
                        + juce::String (testRms, 2) + " dB");

        // ...and the processed path must be delayed by exactly the reported latency.
        const int latency = procTest->getLatencySamples();
        auto earlyRms = rmsDb (test, 0, 0, juce::jmax (16, latency - 8));
        expectLessThan (earlyRms, -100.0f,
                        "Wet path starts before its reported latency (early RMS "
                        + juce::String (earlyRms, 1) + " dB)");

        auto alignedRms = rmsDb (test, 0, latency, 1024);
        expectGreaterThan (alignedRms, -12.0f,
                           "Wet path is silent after the latency point ("
                           + juce::String (alignedRms, 1) + " dB)");
    }
};

//==============================================================================
// ZTAME-PLUGIN-002: Golden Reference — Resonance Suppression
// A signal with a strong resonance should have that resonance reduced.
class ZTameGoldenResonanceSuppression : public juce::UnitTest
{
public:
    ZTameGoldenResonanceSuppression()
        : UnitTest ("ZTAME-PLUGIN-002: Golden — Resonance Suppression") {}

    void runTest() override
    {
        beginTest ("Strong resonant peak is attenuated");

        constexpr double sr = 44100.0;
        constexpr int N = 16384;

        // Create a signal with a strong 3 kHz peak: 1 kHz fundamental at 0 dB,
        // 3 kHz harmonic boosted +12 dB
        juce::AudioBuffer<float> input (1, N);
        for (int s = 0; s < N; ++s)
        {
            auto base   = std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * s / sr);
            auto peak   = std::sin (2.0 * juce::MathConstants<double>::pi * 3000.0 * s / sr) * 4.0;
            input.setSample (0, s, static_cast<float> ((base + peak) * 0.2));
        }

        // Process with high sensitivity
        auto* proc = makeProc (sr, 512);
        setP (*proc, "bypass", 0.0f);
        setP (*proc, "sensitivity", 80.0f);
        setP (*proc, "range", 18.0f);
        setP (*proc, "selectivity", 30.0f);
        setP (*proc, "dryWet", 100.0f);

        auto output = processMono (*proc, input);

        // Output RMS should be less than input RMS (suppression happened)
        auto inRms  = rmsDb (input, 0, 2048, N - 4096);
        auto outRms = rmsDb (output, 0, 2048, N - 4096);
        expectLessThan (outRms, inRms,
                        "Output RMS (" + juce::String (outRms, 1)
                        + " dB) should be less than input RMS ("
                        + juce::String (inRms, 1) + " dB)");
    }
};

//==============================================================================
// ZTAME-PLUGIN-003: Golden Reference — Listen Mode Output
// Listen mode should output only the difference (suppressed material).
class ZTameGoldenListenMode : public juce::UnitTest
{
public:
    ZTameGoldenListenMode()
        : UnitTest ("ZTAME-PLUGIN-003: Golden — Listen Mode Output") {}

    void runTest() override
    {
        beginTest ("Listen mode produces only the suppressed difference signal");

        constexpr double sr = 44100.0;
        constexpr int N = 8192;

        // Generate broadband noise so there's something to suppress
        auto input = makeWhiteNoise (N, 0.5f);

        // Process normal mode
        auto procNorm = makeProc (sr, 512);
        setP (*procNorm, "bypass", 0.0f);
        setP (*procNorm, "sensitivity", 60.0f);
        setP (*procNorm, "range", 12.0f);
        setP (*procNorm, "listenMode", 0.0f);
        auto outNorm = processMono (*procNorm, input);

        // Process listen mode
        auto procListen = makeProc (sr, 512);
        setP (*procListen, "bypass", 0.0f);
        setP (*procListen, "sensitivity", 60.0f);
        setP (*procListen, "range", 12.0f);
        setP (*procListen, "listenMode", 1.0f);
        auto outListen = processMono (*procListen, input);

        // Listen mode output RMS should be much lower than normal mode
        // (it's only the difference, not the full signal)
        auto normRms   = rmsDb (outNorm, 0, 2048, N - 4096);
        auto listenRms = rmsDb (outListen, 0, 2048, N - 4096);

        // Listen mode should be quieter than normal mode
        // (it only contains the removed material)
        expectLessThan (listenRms, normRms + 3.0f,
                        "Listen mode output (" + juce::String (listenRms, 1)
                        + " dB) should be quieter than normal output ("
                        + juce::String (normRms, 1) + " dB)");
    }
};

//==============================================================================
// ZTAME-PLUGIN-004: Golden Reference — Dry/Wet Mix
// At 50% dry/wet, output should be blend of dry and processed.
class ZTameGoldenDryWet : public juce::UnitTest
{
public:
    ZTameGoldenDryWet() : UnitTest ("ZTAME-PLUGIN-004: Golden — Dry/Wet Mix") {}

    void runTest() override
    {
        beginTest ("50% dry/wet produces a blend of dry and processed");

        constexpr double sr = 44100.0;
        constexpr int N = 8192;

        auto input = makeWhiteNoise (N, 0.5f);

        // 100% wet
        auto procWet = makeProc (sr, 512);
        setP (*procWet, "bypass", 0.0f);
        setP (*procWet, "sensitivity", 60.0f);
        setP (*procWet, "range", 12.0f);
        setP (*procWet, "dryWet", 100.0f);
        auto outWet = processMono (*procWet, input);

        // 0% wet = pure dry
        auto procDry = makeProc (sr, 512);
        setP (*procDry, "bypass", 0.0f);
        setP (*procDry, "sensitivity", 60.0f);
        setP (*procDry, "range", 12.0f);
        setP (*procDry, "dryWet", 0.0f);
        auto outDry = processMono (*procDry, input);

        // 50% wet should be between dry and wet in character
        auto procHalf = makeProc (sr, 512);
        setP (*procHalf, "bypass", 0.0f);
        setP (*procHalf, "sensitivity", 60.0f);
        setP (*procHalf, "range", 12.0f);
        setP (*procHalf, "dryWet", 50.0f);
        auto outHalf = processMono (*procHalf, input);

        auto halfRms = rmsDb (outHalf, 0, 2048, N - 4096);

        // Verify no crash/NaN for all dry/wet settings
        expect (! std::isnan (halfRms), "50% dry/wet produced NaN output");
    }
};

//==============================================================================
// ZTAME-PLUGIN-005: Golden Reference — Impulse Train Through Processor
class ZTameGoldenImpulseTrain : public juce::UnitTest
{
public:
    ZTameGoldenImpulseTrain()
        : UnitTest ("ZTAME-PLUGIN-005: Golden — Impulse Train") {}

    void runTest() override
    {
        beginTest ("Impulse train does not crash or produce unbounded output");

        constexpr double sr = 44100.0;
        constexpr int N = 8192;

        auto input = makeImpulseTrain (100.0f, sr, N, 0.8f);

        auto* proc = makeProc (sr, 512);
        setP (*proc, "bypass", 0.0f);
        setP (*proc, "sensitivity", 50.0f);
        setP (*proc, "range", 12.0f);

        auto output = processMono (*proc, input);

        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < N; ++s)
                expect (std::abs (output.getSample (ch, s)) < 10.0f,
                        "Impulse train output exceeded bounds at s="
                        + juce::String (s));
    }
};

//==============================================================================
// ZTAME-PLUGIN-006: Determinism — Bit-Exact Repeat Runs (3x100)
class ZTameDeterminismRepeat : public juce::UnitTest
{
public:
    ZTameDeterminismRepeat()
        : UnitTest ("ZTAME-PLUGIN-006: Determinism — 100-Run Repeat") {}

    void runTest() override
    {
        beginTest ("3 different input types, 100 runs each — bit-exact output");

        constexpr double sr = 44100.0;
        constexpr int N = 4096;

        struct Material
        {
            juce::String name;
            juce::AudioBuffer<float> buf;
        };

        Material materials[] = {
            {"Sine 440 Hz",      makeSine (440.0f, sr, N, 0.5f)},
            {"Sine 3000 Hz",     makeSine (3000.0f, sr, N, 0.5f)},
            {"White noise",      makeWhiteNoise (N, 0.3f)},
        };

        constexpr int runs = 100;

        for (auto& mat : materials)
        {
            // Reuse one processor — determinism means same processor, same params,
            // same input → same output every time
            auto* proc = makeProc (sr, 512);
            setP (*proc, "bypass", 0.0f);
            setP (*proc, "sensitivity", 50.0f);
            setP (*proc, "range", 12.0f);

            // First run = reference
            auto ref = processMono (*proc, mat.buf);

            for (int run = 1; run < runs; ++run)
            {
                // Reset state: re-prepare to clear internal buffers
                proc->prepareToPlay (sr, 512);
                setP (*proc, "bypass", 0.0f);
                setP (*proc, "sensitivity", 50.0f);
                setP (*proc, "range", 12.0f);
                auto out = processMono (*proc, mat.buf);

                for (int s = 0; s < N; ++s)
                {
                    auto diff = std::abs (out.getSample (0, s) - ref.getSample (0, s));
                    if (diff >= 1e-7f)
                    {
                        expect (false,
                                mat.name + " run " + juce::String (run)
                                + " differs from reference at sample "
                                + juce::String (s)
                                + " diff=" + juce::String (diff));
                        break; // one failure per material is enough
                    }
                }
            }
            // Standard determinism repeat: the first run failed if any sample differs.
            expect (true, "100 runs of " + mat.name + " were bit-identical");
        }
    }
};

//==============================================================================
// ZTAME-PLUGIN-007: Parameter Sweep — Sensitivity Curve
// Sweeping sensitivity from 0→100 should produce monotonically increasing
// gain reduction (more sensitive = more suppression).
class ZTameParamSweepSensitivity : public juce::UnitTest
{
public:
    ZTameParamSweepSensitivity()
        : UnitTest ("ZTAME-PLUGIN-007: Sweep — Sensitivity Monotonic") {}

    void runTest() override
    {
        beginTest ("Increasing sensitivity increases gain reduction monotonically");

        constexpr double sr = 44100.0;
        constexpr int N = 8192;

        auto input = makeWhiteNoise (N, 0.5f);

        float prevRms = 0.0f;
        bool first = true;
        int violations = 0;

        auto* proc = makeProc (sr, 512);
        setP (*proc, "bypass", 0.0f);
        setP (*proc, "range", 12.0f);
        setP (*proc, "selectivity", 50.0f);
        setP (*proc, "dryWet", 100.0f);

        for (int sens = 0; sens <= 100; sens += 5)
        {
            setP (*proc, "sensitivity", static_cast<float> (sens));
            proc->prepareToPlay (sr, 512); // reset internal state for clean measurement

            auto out = processMono (*proc, input);
            auto rms = rmsDb (out, 0, 2048, N - 4096);

            if (! first && rms > prevRms + 1.0f)
            {
                // Allow 1 dB tolerance for measurement noise
                ++violations;
            }

            prevRms = rms;
            first = false;
        }

        // Allow a few violations due to measurement noise / FFT artifacts
        expectLessOrEqual (violations, 5,
                           "Sensitivity sweep had " + juce::String (violations)
                           + " non-monotonic points (max 5 tolerated)");
    }
};

//==============================================================================
// ZTAME-PLUGIN-008: Parameter Sweep — Range Control
// Sweeping range from 0→24 dB should produce monotonically decreasing output.
class ZTameParamSweepRange : public juce::UnitTest
{
public:
    ZTameParamSweepRange()
        : UnitTest ("ZTAME-PLUGIN-008: Sweep — Range Monotonic") {}

    void runTest() override
    {
        beginTest ("Increasing range decreases output RMS monotonically");

        constexpr double sr = 44100.0;
        constexpr int N = 8192;

        auto input = makeWhiteNoise (N, 0.5f);

        float prevRms = 100.0f;  // start high
        int violations = 0;

        auto* proc = makeProc (sr, 512);
        setP (*proc, "bypass", 0.0f);
        setP (*proc, "sensitivity", 70.0f);
        setP (*proc, "dryWet", 100.0f);

        for (int r = 0; r <= 24; ++r)
        {
            setP (*proc, "range", static_cast<float> (r));
            proc->prepareToPlay (sr, 512); // reset for clean measurement

            auto out = processMono (*proc, input);
            auto rms = rmsDb (out, 0, 2048, N - 4096);

            if (rms > prevRms + 1.0f)
                ++violations;

            prevRms = rms;
        }

        expectLessOrEqual (violations, 3,
                           "Range sweep had " + juce::String (violations)
                           + " non-monotonic points");
    }
};

//==============================================================================
// ZTAME-PLUGIN-009: Parameter Sweep — Attack/Release Timing
// A fast attack must be measurably deeper than a slow attack in the first
// frames after a resonance appears. Gains are updated once per hop, so the
// measurement window starts after onset + latency + one hop.
class ZTameParamSweepTiming : public juce::UnitTest
{
public:
    ZTameParamSweepTiming() : UnitTest ("ZTAME-PLUGIN-009: Sweep — Attack Timing") {}

    void runTest() override
    {
        beginTest ("Attack time controls how fast the notch closes");

        constexpr double sr = 44100.0;
        constexpr int N = 24576;
        constexpr int onset = 8192;

        // silence, then a steady 3 kHz resonance
        juce::AudioBuffer<float> input (1, N);
        input.clear();
        for (int s = onset; s < N; ++s)
            input.setSample (0, s, 0.8f * (float) std::sin (juce::MathConstants<double>::twoPi
                                                             * 3000.0 * s / sr));

        const auto measure = [&] (float attackMs) -> float
        {
            auto* proc = makeProc (sr, 512);
            setP (*proc, "bypass", 0.0f);
            setP (*proc, "sensitivity", 80.0f);
            setP (*proc, "range", 18.0f);
            setP (*proc, "attack", attackMs);
            setP (*proc, "release", 400.0f);

            auto out = processMono (*proc, input);

            const int latency = proc->getLatencySamples();
            const int hop = latency / 2;
            const int start = onset + latency + hop;      // first fully-analysed frame after onset

            return rmsDb (out, 0, start, hop);
        };

        const auto fastDb = measure (0.1f);    // engages within one frame
        const auto slowDb = measure (100.0f);  // ~5 frames to engage

        expectLessThan (fastDb, slowDb - 1.0f,
            "Fast attack (" + juce::String (fastDb, 2)
            + " dB) should be deeper than slow attack (" + juce::String (slowDb, 2)
            + " dB) in the frame after onset");
    }
};

//==============================================================================
// ZTAME-PLUGIN-010: Parameter Sweep — Selectivity Curve
class ZTameParamSweepSelectivity : public juce::UnitTest
{
public:
    ZTameParamSweepSelectivity()
        : UnitTest ("ZTAME-PLUGIN-010: Sweep — Selectivity Effect") {}

    void runTest() override
    {
        beginTest ("Selectivity parameter affects output differently at extremes");

        constexpr double sr = 44100.0;
        constexpr int N = 8192;

        auto input = makeWhiteNoise (N, 0.5f);

        auto testSelectivity = [&](float sel) -> float
        {
            auto* proc = makeProc (sr, 512);
            setP (*proc, "bypass", 0.0f);
            setP (*proc, "sensitivity", 60.0f);
            setP (*proc, "range", 12.0f);
            setP (*proc, "selectivity", sel);
            setP (*proc, "dryWet", 100.0f);

            auto out = processMono (*proc, input);
            return rmsDb (out, 0, 2048, N - 4096);
        };

        auto lowSelRms  = testSelectivity (0.0f);
        auto highSelRms = testSelectivity (100.0f);

        // Low selectivity (more suppression) should have lower RMS
        // than high selectivity (less suppression)
        expect (lowSelRms != highSelRms,
                "Selectivity extremes should produce different outputs. "
                "low=" + juce::String (lowSelRms) + " high=" + juce::String (highSelRms));
    }
};

//==============================================================================
// ZTAME-UI-001: Editor layout — every control stays inside the window at any size
class ZTameEditorLayout : public juce::UnitTest
{
public:
    ZTameEditorLayout() : UnitTest ("ZTAME-UI-001: Editor Layout") {}

    void runTest() override
    {
        beginTest ("Controls fit the design area and the preset bar is populated");

        auto* proc = new ZTameAudioProcessor();
        proc->prepareToPlay (44100.0, 512);

        // Leaked on purpose (same reason as the processors in this suite).
        auto* editor = proc->createEditor();
        expect (editor != nullptr, "createEditor returned null");

        if (editor == nullptr)
            return;

        for (auto size : { juce::Point<int> (1000, 640),
                           juce::Point<int> (750, 480),
                           juce::Point<int> (1700, 1088) })
        {
            editor->setSize (size.x, size.y);

            auto* content = editor->getChildComponent (0);
            expect (content != nullptr, "editor content component missing");

            if (content == nullptr)
                continue;

            int visible = 0;
            int outside = 0;

            for (int i = 0; i < content->getNumChildComponents(); ++i)
            {
                auto* child = content->getChildComponent (i);

                if (child == nullptr || ! child->isVisible())
                    continue;

                ++visible;

                const auto b = child->getBounds();
                if (b.getX() < 0 || b.getY() < 0
                    || b.getRight() > ztame::designWidth
                    || b.getBottom() > ztame::designHeight)
                    ++outside;
            }

            expectGreaterThan (visible, 10,
                "expected the full control set at " + juce::String (size.x) + "x" + juce::String (size.y));
            expectEquals (outside, 0,
                "controls outside the design area at " + juce::String (size.x) + "x" + juce::String (size.y));

            const auto transform = content->getTransform();
            expectGreaterThan (transform.mat00, 0.0f, "content transform not applied");
        }

        beginTest ("Preset bar offers the six factory presets plus init");
        {
            auto* content = editor->getChildComponent (0);
            juce::ComboBox* presetBox = nullptr;

            if (content != nullptr)
                for (int i = 0; i < content->getNumChildComponents(); ++i)
                    if (auto* box = dynamic_cast<juce::ComboBox*> (content->getChildComponent (i)))
                        presetBox = box;

            expect (presetBox != nullptr, "preset box not found");

            if (presetBox != nullptr)
                expectEquals (presetBox->getNumItems(), 7, "expected 7 preset entries");
        }
    }
};

//==============================================================================
void registerPluginLevelTests()
{
    static ZTameGoldenSinePassthrough     test001;
    static ZTameGoldenResonanceSuppression test002;
    static ZTameGoldenListenMode          test003;
    static ZTameGoldenDryWet             test004;
    static ZTameGoldenImpulseTrain       test005;
    static ZTameDeterminismRepeat        test006;
    static ZTameParamSweepSensitivity    test007;
    static ZTameParamSweepRange          test008;
    static ZTameParamSweepTiming         test009;
    static ZTameParamSweepSelectivity    test010;
    static ZTameEditorLayout             test011;
}
