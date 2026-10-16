//==============================================================================
// The committed example PluginSpecs.
//
// These three documents are the factory's proof that the same kernel produces
// materially different processors: a conventional channel strip, a saturation
// unit and a mono vocal chain. Each one is loaded from disk, validated, built,
// prepared and rendered here - no hand-built shortcut anywhere.
//==============================================================================

#include "TestSupport.h"

using namespace zfactory;
using namespace zfactory::test;

namespace
{

struct ExampleExpectation
{
    const char* fileName;
    const char* pluginId;
    ChannelMode channels;
    int moduleCount;
    int exposedParameters;
    int macroCount;
};

const ExampleExpectation kExamples[] =
{
    { "channel-strip.zfactory.json", "zarlino.example.channel-strip", ChannelMode::Stereo, 5, 7, 2 },
    { "saturation.zfactory.json",    "zarlino.example.saturation",    ChannelMode::Stereo, 5, 9, 2 },
    { "vocal.zfactory.json",         "zarlino.example.vocal",         ChannelMode::Mono,   6, 8, 3 }
};

class ExampleSpecTests : public juce::UnitTest
{
public:
    ExampleSpecTests() : juce::UnitTest ("Factory Example Specs") {}

    void runTest() override
    {
        testExamplesLoadAndBuild();
        testExamplesProcessAudio();
        testMacrosAffectAudio();
        testBypassAndMix();
        testInvalidVariantsAreRejected();
    }

private:
    void testExamplesLoadAndBuild()
    {
        beginTest ("Every committed example loads, validates and builds");

        for (const auto& example : kExamples)
        {
            auto parsed = PluginSpec::fromJsonFile (specPath (example.fileName));

            if (! expectSuccess (*this, parsed, jstr (example.fileName) + " loads"))
                continue;

            const auto& spec = parsed.value();

            expectEquals (jstr (spec.pluginId), juce::String (example.pluginId));
            expect (spec.channels == example.channels, jstr (example.fileName) + " channel mode");
            expectEquals (static_cast<int> (spec.modules.size()), example.moduleCount);
            expectEquals (spec.exposedParameterCount(), example.exposedParameters);
            expectEquals (static_cast<int> (spec.macros.size()), example.macroCount);
            expectEquals (spec.schemaVersion, kSchemaVersion);
            expect (! spec.ui.shortDescription.empty(), "each example carries UI metadata");
            expect (! spec.ui.tags.empty());
            expectEquals (static_cast<int> (spec.ui.parameterOrder.size()),
                          static_cast<int> (spec.parameters.size()));

            auto engine = GraphEngine::create (spec, builtinRegistry());

            if (! expectSuccess (*this, engine, jstr (example.fileName) + " builds"))
                continue;

            const auto& built = *engine.value();

            expectEquals (static_cast<int> (built.modules().size()), example.moduleCount);
            expectEquals (built.getChannelCount(), numChannels (example.channels));
            expectEquals (built.parameters().numBindings(),
                          static_cast<int> (spec.parameters.size()));
            expectEquals (built.parameters().numMacros(), example.macroCount);

            // Every module in the chain resolved to a certified manifest.
            for (const auto& instance : built.modules())
            {
                expect (instance.manifest.certification == CertificationState::Certified,
                        jstr (instance.manifest.id) + " must be certified");
                expect (instance.module != nullptr, "every instance must be constructed");
            }

            expect (built.describeGraph().find (example.pluginId) != std::string::npos);
        }
    }

    void testExamplesProcessAudio()
    {
        beginTest ("Every committed example renders audio at several rates and block sizes");

        for (const auto& example : kExamples)
        {
            auto parsed = PluginSpec::fromJsonFile (specPath (example.fileName));

            if (! expectSuccess (*this, parsed, jstr (example.fileName) + " loads"))
                continue;

            const auto& spec = parsed.value();
            const int channels = spec.channelCount();

            auto engine = GraphEngine::create (spec, builtinRegistry());

            if (! expectSuccess (*this, engine, jstr (example.fileName) + " builds"))
                continue;

            for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
            {
                for (int blockSize : { 64, 257, 1024 })
                {
                    if (auto error = engine.value()->prepare (rate, blockSize); error.failed())
                    {
                        expect (false, jstr (example.fileName) + " prepare failed: "
                                          + jstr (error.describe()));
                        continue;
                    }

                    Block block (channels, blockSize);
                    float peak = 0.0f;

                    for (int i = 0; i < 16; ++i)
                    {
                        block.sine (440.0, rate, 0.5f, static_cast<long long> (i) * blockSize);
                        engine.value()->process (block.pointers(), channels, blockSize);

                        expect (block.allFinite(),
                                jstr (example.fileName) + " must stay finite at "
                                    + juce::String (rate) + " Hz / " + juce::String (blockSize));

                        peak = std::max (peak, block.peak());
                    }

                    expect (peak > 0.001f,
                            jstr (example.fileName) + " must produce audio at "
                                + juce::String (rate) + " Hz / " + juce::String (blockSize));
                }
            }
        }
    }

    void testMacrosAffectAudio()
    {
        beginTest ("Each example's macros change the rendered output");

        for (const auto& example : kExamples)
        {
            auto parsed = PluginSpec::fromJsonFile (specPath (example.fileName));

            if (! expectSuccess (*this, parsed, jstr (example.fileName) + " loads"))
                continue;

            const auto& spec = parsed.value();
            const int channels = spec.channelCount();

            auto engine = GraphEngine::create (spec, builtinRegistry());

            if (! expectSuccess (*this, engine, jstr (example.fileName) + " builds"))
                continue;

            if (auto error = engine.value()->prepare (48000.0, 128); error.failed())
            {
                expect (false, jstr (example.fileName) + " prepare failed");
                continue;
            }

            auto& built = *engine.value();

            // Broadband material, so a change in any band shows up in the energy
            // measurement - a sine would hide a presence-only EQ move.
            const auto render = [&built, channels] ()
            {
                Block block (channels, 128);
                double energy = 0.0;

                for (int i = 0; i < 24; ++i)
                {
                    block.noise (0x5EEDu + static_cast<unsigned int> (i) * 131u, 0.35f);
                    built.process (block.pointers(), channels, 128);

                    for (int ch = 0; ch < channels; ++ch)
                        for (int s = 0; s < 128; ++s)
                            energy += std::abs (static_cast<double> (block.channel (ch)[s]));
                }

                return energy;
            };

            for (int macroIndex = 0; macroIndex < built.parameters().numMacros(); ++macroIndex)
            {
                // Baseline: every macro at its declared default.
                for (size_t i = 0; i < spec.macros.size(); ++i)
                    built.parameters().setMacroValue (static_cast<int> (i), spec.macros[i].defaultValue);

                built.applyParameters();
                built.reset();
                const auto baseline = render();

                // Only this macro moves.
                built.parameters().setMacroValue (macroIndex, 1.0f);
                built.applyParameters();
                built.reset();
                const auto moved = render();

                expect (std::abs (moved - baseline) > 1.0e-4,
                        jstr (example.fileName) + " macro " + juce::String (macroIndex)
                            + " must change the output");
            }
        }
    }

    void testBypassAndMix()
    {
        beginTest ("Bypass is bit-exact and a fully dry mix reproduces the delayed input");

        for (const auto& example : kExamples)
        {
            auto parsed = PluginSpec::fromJsonFile (specPath (example.fileName));

            if (! expectSuccess (*this, parsed, jstr (example.fileName) + " loads"))
                continue;

            const auto& spec = parsed.value();
            const int channels = spec.channelCount();

            auto engine = GraphEngine::create (spec, builtinRegistry());

            if (! expectSuccess (*this, engine, jstr (example.fileName) + " builds"))
                continue;

            if (auto error = engine.value()->prepare (48000.0, 128); error.failed())
            {
                expect (false, jstr (example.fileName) + " prepare failed");
                continue;
            }

            auto& built = *engine.value();

            const int mixIndex = built.parameters().indexOfBinding ("mix");
            const int bypassIndex = built.parameters().indexOfBinding ("bypassed");

            expect (mixIndex >= 0, jstr (example.fileName) + " exposes a mix parameter");
            expect (bypassIndex >= 0, jstr (example.fileName) + " exposes a bypass parameter");

            if (mixIndex < 0 || bypassIndex < 0)
                continue;

            const int latency = built.getLatencySamples();
            expect (latency > 0, jstr (example.fileName) + " reports its limiter latency");
            expect (latency < 128, "the test block must be longer than the latency");

            // --- Fully dry -------------------------------------------------
            built.parameters().setBaseNormalised (mixIndex, 0.0f);
            built.parameters().setBaseNormalised (bypassIndex, 0.0f);
            built.applyParameters();
            built.reset();

            Block input (channels, 128);
            input.sine (440.0, 48000.0, 0.5f);
            const Block reference = input.duplicate();

            built.process (input.pointers(), channels, 128);

            for (int ch = 0; ch < channels; ++ch)
            {
                for (int s = 0; s < latency; ++s)
                    expectEquals (input.channel (ch)[s], 0.0f,
                                  jstr (example.fileName) + " dry path must start with the delay");

                for (int s = latency; s < 128; ++s)
                    expectEquals (input.channel (ch)[s], reference.channel (ch)[s - latency],
                                  jstr (example.fileName) + " dry path must be the delayed input");
            }

            // --- Fully wet -------------------------------------------------
            built.parameters().setBaseNormalised (mixIndex, 1.0f);
            built.applyParameters();
            built.reset();

            Block wet (channels, 128);
            wet.sine (440.0, 48000.0, 0.5f);
            built.process (wet.pointers(), channels, 128);

            expect (wet.allFinite(), jstr (example.fileName) + " wet output must be finite");
            expect (wet.maxDifference (reference) > 1.0e-4f,
                    jstr (example.fileName) + " the processed signal must differ from the input");

            // --- Bypass ----------------------------------------------------
            built.parameters().setBaseNormalised (bypassIndex, 1.0f);
            built.applyParameters();

            expect (built.isBypassed(), jstr (example.fileName) + " must report bypass");

            Block bypassed (channels, 128);
            bypassed.sine (440.0, 48000.0, 0.5f);
            const Block bypassReference = bypassed.duplicate();

            built.process (bypassed.pointers(), channels, 128);
            expectEquals (bypassed.maxDifference (bypassReference), 0.0f,
                          jstr (example.fileName) + " bypass must be bit-exact");
        }
    }

    void testInvalidVariantsAreRejected()
    {
        beginTest ("Specs derived from the examples fail safely when broken");

        // 1. A module the registry does not have.
        {
            auto text = juce::File (jstr (specPath ("channel-strip.zfactory.json"))).loadFileAsString();
            const auto broken = text.replace ("zarlino.eq.tilt", "zarlino.absent.module");

            auto parsed = PluginSpec::fromJsonText (broken.toStdString());

            if (! expectSuccess (*this, parsed, "broken spec still parses"))
                return;

            auto engine = GraphEngine::create (parsed.value(), builtinRegistry());
            expectFailure (*this, engine, ErrorCode::UnknownModule, "unknown module in a derived spec");
        }

        // 2. A module version from the future.
        {
            auto text = juce::File (jstr (specPath ("saturation.zfactory.json"))).loadFileAsString();
            const auto broken = text.replace ("\"moduleVersion\": \"1.0.0\"", "\"moduleVersion\": \"4.0.0\"");

            auto parsed = PluginSpec::fromJsonText (broken.toStdString());
            expectSuccess (*this, parsed, "future-version spec still parses");

            auto engine = GraphEngine::create (parsed.value(), builtinRegistry());
            expectFailure (*this, engine, ErrorCode::ModuleVersionIncompatible,
                           "future module version in a derived spec");
        }

        // 3. A mono graph that asks for the stereo-only width module.
        {
            const auto text = juce::File (jstr (specPath ("vocal.zfactory.json"))).loadFileAsString();

            // Splice a complete stereo-only module in front of the limiter; the
            // document stays valid JSON, so the failure must come from the
            // registry's channel check rather than from the parser.
            const auto broken = text.replace (
                "\"instanceId\": \"ceiling\"",
                "\"instanceId\": \"width\", \"moduleId\": \"zarlino.stereo.width\","
                " \"moduleVersion\": \"1.0.0\", \"parameters\": {} }, { \"instanceId\": \"ceiling\"");

            auto parsed = PluginSpec::fromJsonText (broken.toStdString());

            if (! expectSuccess (*this, parsed, "mono spec with a stereo-only module still parses"))
                return;

            expectEquals (static_cast<int> (parsed.value().modules.size()), 7);

            auto engine = GraphEngine::create (parsed.value(), builtinRegistry());
            expectFailure (*this, engine, ErrorCode::ModuleChannelUnsupported,
                           "stereo-only module inside a mono graph");
        }
    }
};

ExampleSpecTests exampleSpecTests;

} // namespace

void registerExampleSpecTests() {}
