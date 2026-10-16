//==============================================================================
// Graph construction: resolution errors, compatibility checks, latency
// propagation, chunking and the deterministic module order.
//==============================================================================

#include "TestSupport.h"

using namespace zfactory;
using namespace zfactory::test;

namespace
{

class GraphConstructionTests : public juce::UnitTest
{
public:
    GraphConstructionTests() : juce::UnitTest ("Factory Graph Construction") {}

    void runTest() override
    {
        testSuccessfulBuild();
        testResolutionFailures();
        testParameterResolutionFailures();
        testLatencyAndPreparation();
        testChunkingAndGuards();
    }

private:
    void testSuccessfulBuild()
    {
        beginTest ("A chain builds with the spec's order, channels and latency");

        auto spec = makeChainSpec();
        auto engine = GraphEngine::create (spec, builtinRegistry());

        if (! expectSuccess (*this, engine, "chain graph"))
            return;

        const auto& built = engine.value();

        expectEquals (static_cast<int> (built->modules().size()), 4);
        expectEquals (jstr (built->modules()[0].instanceId), juce::String ("gain"));
        expectEquals (jstr (built->modules()[1].manifest.id), juce::String ("zarlino.filter.multimode"));
        expectEquals (jstr (built->modules()[2].manifest.id), juce::String ("zarlino.dynamics.compressor"));
        expectEquals (jstr (built->modules()[3].manifest.id), juce::String ("zarlino.eq.tilt"));
        expectEquals (built->getChannelCount(), 2);
        expectEquals (built->getLatencySamples(), 0);
        expectEquals (built->parameters().numBindings(), 3);
        expect (! built->isPrepared());

        if (auto error = built->prepare (48000.0, 512); error.failed())
            expect (false, "prepare failed: " + jstr (error.describe()));

        expect (built->isPrepared());
        expectEquals (built->getMaxBlockSize(), 512);
        expect (! built->isBypassed());

        // The spec's initial values must have reached the modules.
        expectWithinAbsoluteError (built->modules()[2].module->getParameter (0), -18.0f, 1.0e-4f,
                                   "compressor threshold");

        const auto description = built->describeGraph();
        expect (description.find ("zarlino.dynamics.compressor") != std::string::npos);
        expect (description.find ("macros=0") != std::string::npos);
    }

    void testResolutionFailures()
    {
        beginTest ("Unknown modules, bad versions and channel mismatches are rejected");

        {
            auto spec = makeBaseSpec();
            addModule (spec, "x", "zarlino.does.not.exist");

            auto engine = GraphEngine::create (spec, builtinRegistry());
            expectFailure (*this, engine, ErrorCode::UnknownModule, "unknown module");
            expectEquals (jstr (engine.error().path), juce::String ("$/graph/modules[0]/moduleId"));
        }

        {
            auto spec = makeBaseSpec();
            addModule (spec, "x", "zarlino.gain", "3.0.0");

            auto engine = GraphEngine::create (spec, builtinRegistry());
            expectFailure (*this, engine, ErrorCode::ModuleVersionIncompatible, "future major version");
        }

        {
            auto spec = makeBaseSpec();
            addModule (spec, "x", "zarlino.gain", "1.9.9");

            auto engine = GraphEngine::create (spec, builtinRegistry());
            expectFailure (*this, engine, ErrorCode::ModuleVersionIncompatible, "future minor version");
        }

        {
            // The stereo-width module declares stereo only.
            auto spec = makeBaseSpec (ChannelMode::Mono);
            addModule (spec, "width", "zarlino.stereo.width");

            auto engine = GraphEngine::create (spec, builtinRegistry());
            expectFailure (*this, engine, ErrorCode::ModuleChannelUnsupported, "stereo-only module in mono");
        }

        {
            auto spec = makeBaseSpec();
            auto& module = addModule (spec, "g", "zarlino.gain");
            setModuleParameter (module, "gainDb", 100.0f);

            auto engine = GraphEngine::create (spec, builtinRegistry());
            expectFailure (*this, engine, ErrorCode::ValueOutOfRange, "module value above its domain");
        }

        {
            auto spec = makeBaseSpec();
            auto& module = addModule (spec, "g", "zarlino.gain");
            setModuleParameter (module, "notAParameter", 1.0f);

            auto engine = GraphEngine::create (spec, builtinRegistry());
            expectFailure (*this, engine, ErrorCode::ModuleParameterUnknown, "unknown module parameter");
        }

        {
            // The kernel's default policy is certified-only; the experimental
            // rejection path itself is asserted by the registry suite.
            expect (GraphOptions().requiredCertification == CertificationState::Certified,
                    "the default policy must be certified-only");

            ModuleRegistry registry;
            auto count = registerBuiltinModules (registry);
            expectSuccess (*this, count, "private registry");
            expectEquals (registry.catalogFingerprint(), builtinRegistry().catalogFingerprint());
        }
    }

    void testParameterResolutionFailures()
    {
        beginTest ("Parameter domain, unit and ambiguity problems are rejected");

        {
            auto spec = makeBaseSpec();
            addModule (spec, "g", "zarlino.gain");
            addParameter (spec, "trim", "g.gainDb", -40.0f, 40.0f, 0.0f, "dB");

            auto engine = GraphEngine::create (spec, builtinRegistry());
            expectFailure (*this, engine, ErrorCode::ValueOutOfRange, "range wider than the module domain");
            expectEquals (jstr (engine.error().path), juce::String ("$/parameters[0]/range"));
        }

        {
            auto spec = makeBaseSpec();
            addModule (spec, "g", "zarlino.gain");
            addParameter (spec, "trim", "g.gainDb", -12.0f, 12.0f, 0.0f, "Hz");

            auto engine = GraphEngine::create (spec, builtinRegistry());
            expectFailure (*this, engine, ErrorCode::TypeMismatch, "unit that disagrees with the module");
        }

        {
            auto spec = makeBaseSpec();
            addModule (spec, "g", "zarlino.gain");
            addParameter (spec, "trim", "g.gainDb", -12.0f, 12.0f, 0.0f, "dB");
            addParameter (spec, "alsoTrim", "g.gainDb", -6.0f, 6.0f, 0.0f, "dB");

            auto engine = GraphEngine::create (spec, builtinRegistry());
            expectFailure (*this, engine, ErrorCode::DuplicateIdentifier,
                           "two parameters on one module parameter");
        }

        {
            auto spec = makeBaseSpec();
            addModule (spec, "g", "zarlino.gain");
            addParameter (spec, "trim", "g.nope", -12.0f, 12.0f, 0.0f, "dB");

            auto engine = GraphEngine::create (spec, builtinRegistry());
            expectFailure (*this, engine, ErrorCode::ModuleParameterUnknown, "unknown module parameter target");
        }

        {
            auto spec = makeBaseSpec();
            addModule (spec, "g", "zarlino.gain");
            addParameter (spec, "trim", "g.gainDb", -12.0f, 12.0f, 0.0f, "dB");
            addParameter (spec, "mix", "$factory.dryWet", 0.0f, 1.0f, 1.0f);
            addParameter (spec, "mixAgain", "$factory.dryWet", 0.0f, 1.0f, 1.0f);
            spec.dryWet.enabled = true;

            auto engine = GraphEngine::create (spec, builtinRegistry());
            expectFailure (*this, engine, ErrorCode::DuplicateIdentifier, "two dry/wet controls");
        }

        {
            // A graph with only factory controls and no module parameters is legal.
            auto spec = makeBaseSpec();
            addModule (spec, "g", "zarlino.gain");
            addParameter (spec, "mix", "$factory.dryWet", 0.0f, 1.0f, 1.0f);
            spec.dryWet.enabled = true;

            auto engine = GraphEngine::create (spec, builtinRegistry());
            expectSuccess (*this, engine, "factory-only parameter set");
        }
    }

    void testLatencyAndPreparation()
    {
        beginTest ("Latency is summed from the modules and reported after prepare");

        auto spec = makeBaseSpec();
        addModule (spec, "limit", "zarlino.limiter");

        auto engine = GraphEngine::create (spec, builtinRegistry());

        if (! expectSuccess (*this, engine, "limiter graph"))
            return;

        expectEquals (engine.value()->getLatencySamples(), 0, "latency is unknown before prepare");

        if (auto error = engine.value()->prepare (48000.0, 512); error.failed())
        {
            expect (false, "prepare failed: " + jstr (error.describe()));
            return;
        }

        // 1 ms lookahead at 48 kHz.
        expectEquals (engine.value()->getLatencySamples(), 48);
        expectEquals (engine.value()->modules()[0].latencySamples, 48);

        if (auto error = engine.value()->prepare (96000.0, 512); error.failed())
            expect (false, "prepare at 96 kHz failed: " + jstr (error.describe()));

        expectEquals (engine.value()->getLatencySamples(), 96);

        expectError (*this, engine.value()->prepare (10.0, 512),
                     ErrorCode::ValueOutOfRange, "sample rate below the supported minimum");
        expectError (*this, engine.value()->prepare (48000.0, 0),
                     ErrorCode::ValueOutOfRange, "zero block size");
        expectError (*this, engine.value()->prepare (48000.0, kMaxBlockSize + 1),
                     ErrorCode::ValueOutOfRange, "block size above the supported maximum");
    }

    void testChunkingAndGuards()
    {
        beginTest ("Oversized blocks are chunked and unprepared graphs stay silent");

        auto spec = makeStereoGainSpec();
        auto engine = GraphEngine::create (spec, builtinRegistry());

        if (! expectSuccess (*this, engine, "gain graph"))
            return;

        auto& built = *engine.value();

        Block unprepared (2, 256);
        unprepared.sine (440.0, 48000.0, 0.5f);
        const Block copy = unprepared.duplicate();

        built.process (unprepared.pointers(), 2, 256);

        expect (unprepared.maxDifference (copy) == 0.0f, "an unprepared graph must not touch the buffer");

        if (auto error = built.prepare (48000.0, 64); error.failed())
            expect (false, "prepare failed: " + jstr (error.describe()));

        // 1000 samples through a graph prepared for 64-sample blocks.
        Block big (2, 1000);
        big.sine (440.0, 48000.0, 0.5f);
        built.process (big.pointers(), 2, 1000);

        expect (big.allFinite(), "chunked processing must stay finite");
        expect (big.peak() > 0.1f, "chunked processing must produce audio");

        // A narrower buffer than the graph expects is left untouched.
        Block mono (1, 64);
        mono.sine (440.0, 48000.0, 0.5f);
        const Block monoCopy = mono.duplicate();
        built.process (mono.pointers(), 1, 64);
        expect (mono.maxDifference (monoCopy) == 0.0f, "a too-narrow buffer must not be processed");
    }

    /** Gain -> limiter, stereo, with a unit gain so the output is predictable. */
    PluginSpec makeStereoGainSpec()
    {
        auto spec = makeBaseSpec();
        addModule (spec, "g", "zarlino.gain");
        addParameter (spec, "trim", "g.gainDb", -12.0f, 12.0f, 0.0f, "dB");
        return spec;
    }
};

GraphConstructionTests graphConstructionTests;

} // namespace

void registerGraphConstructionTests() {}
