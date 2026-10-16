//==============================================================================
// Audio processing through the constructed graph: channel configurations,
// sample rates, block sizes, reset cycles, bypass, dry/wet, silence, impulses,
// hostile input and extreme parameter values.
//==============================================================================

#include "TestSupport.h"

using namespace zfactory;
using namespace zfactory::test;

namespace
{

class AudioProcessingTests : public juce::UnitTest
{
public:
    AudioProcessingTests() : juce::UnitTest ("Factory Audio Processing") {}

    void runTest() override
    {
        testChannelConfigurations();
        testSampleRates();
        testBlockSizes();
        testSilence();
        testImpulse();
        testHostileInput();
        testResetAndReprepareCycles();
        testBypass();
        testDryWet();
        testExtremeParameters();
    }

private:
    /** A full chain: gate -> high-pass -> parametric EQ -> compressor ->
        saturation -> width -> limiter. Exercises every module family at once. */
    PluginSpec makeFullSpec (ChannelMode channels)
    {
        auto spec = makeBaseSpec (channels);

        addModule (spec, "gate", "zarlino.dynamics.gate");
        addModule (spec, "hp", "zarlino.filter.multimode");
        addModule (spec, "eq", "zarlino.eq.parametric");
        addModule (spec, "comp", "zarlino.dynamics.compressor");
        addModule (spec, "sat", "zarlino.saturation");
        addModule (spec, "tone", "zarlino.eq.tilt");
        addModule (spec, "shaper", "zarlino.shaping.transient");
        addModule (spec, "limit", "zarlino.limiter");

        if (channels == ChannelMode::Stereo)
            addModule (spec, "width", "zarlino.stereo.width");

        // Band-limit the low end so the chain is a realistic broadband strip.
        setModuleParameter (spec.modules[1], "shape", 1.0f);

        addParameter (spec, "trim", "hp.frequency", 40.0f, 400.0f, 90.0f, "Hz");
        addParameter (spec, "drive", "sat.drive", 0.0f, 1.0f, 0.25f);
        addParameter (spec, "ceiling", "limit.ceiling", -6.0f, 0.0f, -1.0f, "dB");

        addMacro (spec, "intensity", 0.4f, { mapping ("drive", 0.0f, 1.0f) });

        spec.dryWet.enabled = true;
        spec.dryWet.defaultMix = 1.0f;

        return spec;
    }

    std::unique_ptr<GraphEngine> buildPrepared (const PluginSpec& spec,
                                                double sampleRate,
                                                int blockSize)
    {
        auto engine = GraphEngine::create (spec, builtinRegistry());

        if (! engine.ok())
        {
            expect (false, "graph build failed: " + jstr (engine.error().describe()));
            return nullptr;
        }

        if (auto error = engine.value()->prepare (sampleRate, blockSize); error.failed())
        {
            expect (false, "prepare failed: " + jstr (error.describe()));
            return nullptr;
        }

        return std::move (engine.value());
    }

    void testChannelConfigurations()
    {
        beginTest ("Mono and stereo chains both process audio");

        for (auto channels : { ChannelMode::Mono, ChannelMode::Stereo })
        {
            auto spec = makeFullSpec (channels);
            auto engine = buildPrepared (spec, 48000.0, 256);

            if (engine == nullptr)
                continue;

            const int count = numChannels (channels);

            expectEquals (engine->getChannelCount(), count);

            Block block (count, 256);
            block.sine (440.0, 48000.0, 0.5f);
            engine->process (block.pointers(), count, 256);

            expect (block.allFinite(), jstr (channelModeName (channels)) + " output must be finite");
            expect (block.peak() > 0.01f, jstr (channelModeName (channels)) + " output must carry audio");
            expect (block.peak() < 2.0f, jstr (channelModeName (channels)) + " output must stay bounded");
        }
    }

    void testSampleRates()
    {
        beginTest ("The same spec runs at every supported sample rate");

        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };

        for (double rate : rates)
        {
            auto spec = makeFullSpec (ChannelMode::Stereo);
            auto engine = buildPrepared (spec, rate, 512);

            if (engine == nullptr)
                continue;

            Block block (2, 512);
            double total = 0.0;

            for (int i = 0; i < 8; ++i)
            {
                block.sine (440.0, rate, 0.4f, static_cast<long long> (i) * 512);
                engine->process (block.pointers(), 2, 512);
                expect (block.allFinite(), "finite at " + juce::String (rate));
                total += block.rms();
            }

            expect (total > 0.0, "audio at " + juce::String (rate) + " Hz");

            // The only latency-reporting module in this chain is the limiter, so
            // the graph latency must be its 1 ms lookahead at this rate.
            expectEquals (engine->getLatencySamples(),
                          static_cast<int> (rate * 0.001 + 0.5),
                          "lookahead latency must scale with the sample rate");
        }
    }

    void testBlockSizes()
    {
        beginTest ("Every block size from 1 sample up processes correctly");

        const int sizes[] = { 1, 2, 16, 64, 257, 512, 1024, 4096 };

        for (int size : sizes)
        {
            auto spec = makeFullSpec (ChannelMode::Stereo);
            auto engine = buildPrepared (spec, 48000.0, size);

            if (engine == nullptr)
                continue;

            Block block (2, size);
            float peak = 0.0f;

            // The chain contains gate and compressor envelope detectors whose
            // windows are tens of milliseconds, so tiny block sizes need many more
            // blocks before the chain is at level.
            const int blocks = std::max (16, 16384 / size);

            for (int i = 0; i < blocks; ++i)
            {
                block.sine (1000.0, 48000.0, 0.5f, static_cast<long long> (i) * size);
                engine->process (block.pointers(), 2, size);
                peak = std::max (peak, block.peak());
            }

            expect (block.allFinite(), "finite at block size " + juce::String (size));
            expect (peak > 0.01f, "audio at block size " + juce::String (size));
        }

        // A block larger than the prepared maximum is chunked internally.
        auto spec = makeFullSpec (ChannelMode::Stereo);
        auto engine = buildPrepared (spec, 48000.0, 64);

        if (engine != nullptr)
        {
            Block small (2, 64);
            Block large (2, 640);

            for (int i = 0; i < 8; ++i)
            {
                small.sine (1000.0, 48000.0, 0.5f, static_cast<long long> (i) * 64);
                large.sine (1000.0, 48000.0, 0.5f, static_cast<long long> (i) * 640);
                engine->process (small.pointers(), 2, 64);
                engine->process (large.pointers(), 2, 640);
            }

            expect (small.allFinite() && large.allFinite(), "both block regimes must stay finite");
            expect (large.peak() > 0.01f, "oversized blocks must still carry audio");
        }
    }

    void testSilence()
    {
        beginTest ("Silence in produces silence out for every example processor");

        const char* const specs[] = { "channel-strip.zfactory.json", "saturation.zfactory.json", "vocal.zfactory.json" };

        for (const auto* name : specs)
        {
            auto parsed = PluginSpec::fromJsonFile (specPath (name));

            if (! expectSuccess (*this, parsed, jstr (name) + " loads"))
                continue;

            auto engine = buildPrepared (parsed.value(), 48000.0, 128);

            if (engine == nullptr)
                continue;

            const int count = engine->getChannelCount();
            Block block (count, 128);

            for (int i = 0; i < 16; ++i)
            {
                block.clear();
                engine->process (block.pointers(), count, 128);
            }

            expect (block.isSilent(), jstr (name) + " must stay silent for a silent input");
        }
    }

    void testImpulse()
    {
        beginTest ("An impulse produces a finite, non-silent, bounded response");

        auto spec = makeFullSpec (ChannelMode::Stereo);
        auto engine = buildPrepared (spec, 48000.0, 256);

        if (engine == nullptr)
            return;

        // Stimulus from the shared Zarlino test generator (it clears the buffer,
        // so the impulse is mirrored to the second channel afterwards).
        juce::AudioBuffer<float> source (2, 256);
        ZStressTestUtils::generateImpulse (source, 0, 0, 0.8f);
        source.copyFrom (1, 0, source, 0, 0, source.getNumSamples());

        Block block (2, 256);
        block.copyFrom (source);

        engine->process (block.pointers(), 2, 256);

        expect (block.allFinite(), "impulse response must be finite");
        expect (block.peak() > 0.001f, "impulse response must not be silent");
        expect (block.peak() <= 1.5f, "impulse response must stay bounded");
    }

    void testHostileInput()
    {
        beginTest ("NaN and infinity never survive the graph");

        for (bool infinity : { false, true })
        {
            auto spec = makeFullSpec (ChannelMode::Stereo);
            auto engine = buildPrepared (spec, 48000.0, 256);

            if (engine == nullptr)
                continue;

            // Hostile stimuli from the shared Zarlino test generators.
            juce::AudioBuffer<float> source (2, 256);

            if (infinity)
                ZStressTestUtils::generateInfBuffer (source);
            else
                ZStressTestUtils::generateNaNBuffer (source);

            Block block (2, 256);
            block.copyFrom (source);

            engine->process (block.pointers(), 2, 256);

            expect (block.allFinite(),
                    infinity ? "infinite input must be neutralised" : "NaN input must be neutralised");

            // The graph must recover: normal audio afterwards still works.
            block.sine (440.0, 48000.0, 0.5f);
            engine->process (block.pointers(), 2, 256);
            expect (block.allFinite(), "the graph must recover after hostile input");
        }
    }

    void testResetAndReprepareCycles()
    {
        beginTest ("reset() and repeated prepare() produce bit-identical output");

        auto spec = makeFullSpec (ChannelMode::Stereo);
        auto engine = buildPrepared (spec, 48000.0, 256);

        if (engine == nullptr)
            return;

        const auto renderOnce = [&engine] (double rate, long long startIndex)
        {
            Block block (2, 256);
            Block collected (2, 256);

            for (int i = 0; i < 4; ++i)
            {
                block.sine (500.0, rate, 0.6f, startIndex + static_cast<long long> (i) * 256);
                engine->process (block.pointers(), 2, 256);

                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < 256; ++s)
                        collected.channel (ch)[s] += block.channel (ch)[s];
            }

            return collected;
        };

        engine->reset();
        const auto first = renderOnce (48000.0, 0);

        engine->reset();
        const auto second = renderOnce (48000.0, 0);

        expect (first.allFinite() && second.allFinite(), "both renders must be finite");
        expectEquals (first.maxDifference (second), 0.0f,
                      "a reset must restore the identical internal state");

        // Re-preparing at a different rate, then back, must also reproduce.
        expectNoError (*this, engine->prepare (96000.0, 256), "re-prepare at 96 kHz");
        expectNoError (*this, engine->prepare (48000.0, 256), "re-prepare at 48 kHz");
        engine->reset();
        const auto third = renderOnce (48000.0, 0);

        expectEquals (first.maxDifference (third), 0.0f,
                      "re-preparing at the original rate must reproduce the original render");
    }

    void testBypass()
    {
        beginTest ("A bypassed graph passes the input through untouched");

        auto spec = makeFullSpec (ChannelMode::Stereo);
        auto engine = buildPrepared (spec, 48000.0, 256);

        if (engine == nullptr)
            return;

        engine->setBypassed (true);
        expect (engine->isBypassed());

        Block block (2, 256);
        block.sine (440.0, 48000.0, 0.5f);
        const Block reference = block.duplicate();

        engine->process (block.pointers(), 2, 256);

        expectEquals (block.maxDifference (reference), 0.0f, "bypass must be bit-exact");

        engine->setBypassed (false);
        expect (! engine->isBypassed());

        engine->process (block.pointers(), 2, 256);
        expect (block.maxDifference (reference) > 0.0f, "un-bypassing must process again");
        expect (block.allFinite(), "post-bypass processing must stay finite");
    }

    void testDryWet()
    {
        beginTest ("Dry/wet mixing is phase coherent with a latency-reporting module");

        auto spec = makeBaseSpec();
        addModule (spec, "limit", "zarlino.limiter");
        addParameter (spec, "mix", "$factory.dryWet", 0.0f, 1.0f, 0.0f);
        addParameter (spec, "ceiling", "limit.ceiling", -6.0f, 0.0f, 0.0f, "dB");
        spec.dryWet.enabled = true;
        spec.dryWet.defaultMix = 0.0f;

        auto engine = buildPrepared (spec, 48000.0, 256);

        if (engine == nullptr)
            return;

        expectEquals (engine->getLatencySamples(), 48);

        Block block (2, 256);
        block.impulse (0, 0, 0.5f);
        block.channel (1)[0] = 0.5f;

        engine->process (block.pointers(), 2, 256);

        // Fully dry: the impulse is delayed by exactly the reported latency.
        expectWithinAbsoluteError (block.channel (0)[0], 0.0f, 1.0e-6f, "no dry leakage before the delay");
        expectWithinAbsoluteError (block.channel (0)[48], 0.5f, 1.0e-6f,
                                   "the dry path must be delayed by the reported latency");
        expectWithinAbsoluteError (block.channel (0)[49], 0.0f, 1.0e-6f);

        // A partially wet mix stays bounded and finite.
        engine->setDryWetNormalised (0.5f);
        engine->applyParameters();
        block.clear();
        block.sine (440.0, 48000.0, 0.8f);
        engine->process (block.pointers(), 2, 256);
        expect (block.allFinite(), "a half-wet mix must stay finite");
        expect (block.peak() <= 1.0f, "a half-wet mix must respect the limiter ceiling");
    }

    void testExtremeParameters()
    {
        beginTest ("Extreme parameter and macro values stay finite and bounded");

        auto spec = makeFullSpec (ChannelMode::Stereo);
        auto engine = buildPrepared (spec, 48000.0, 256);

        if (engine == nullptr)
            return;

        auto& parameters = engine->parameters();

        for (int sweep = 0; sweep < 3; ++sweep)
        {
            const float position = sweep == 0 ? 0.0f : (sweep == 1 ? 1.0f : 0.5f);

            for (int i = 0; i < parameters.numBindings(); ++i)
                parameters.setBaseNormalised (i, position);

            for (int i = 0; i < parameters.numMacros(); ++i)
                parameters.setMacroValue (i, position);

            engine->applyParameters();

            Block block (2, 256);
            block.sine (2000.0, 48000.0, 1.0f);
            engine->process (block.pointers(), 2, 256);

            expect (block.allFinite(), "sweep " + juce::String (sweep) + " must be finite");
            expect (block.peak() <= 1.2f, "sweep " + juce::String (sweep) + " must stay bounded");
        }
    }
};

AudioProcessingTests audioProcessingTests;

} // namespace

void registerAudioProcessingTests() {}
