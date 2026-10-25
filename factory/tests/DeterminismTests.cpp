//==============================================================================
// Determinism: the same specification must always produce the same graph, the
// same mappings and bit-identical audio.
//==============================================================================

#include "TestSupport.h"

using namespace zfactory;
using namespace zfactory::test;

namespace
{

class DeterminismTests : public juce::UnitTest
{
public:
    DeterminismTests() : juce::UnitTest ("Factory Determinism") {}

    void runTest() override
    {
        testSpecFingerprints();
        testGraphFingerprints();
        testIdenticalAudio();
        testResolvedOrderIndependence();
        testExamplesAreStable();
    }

private:
    PluginSpec makeSpec()
    {
        auto spec = makeBaseSpec();

        addModule (spec, "gain", "zarlino.gain");
        addModule (spec, "eq", "zarlino.eq.parametric");
        addModule (spec, "comp", "zarlino.dynamics.compressor");
        addModule (spec, "sat", "zarlino.saturation");

        addParameter (spec, "trim", "gain.gainDb", -12.0f, 12.0f, 0.0f, "dB");
        addParameter (spec, "drive", "sat.drive", 0.0f, 1.0f, 0.35f);
        addParameter (spec, "threshold", "comp.threshold", -36.0f, -6.0f, -20.0f, "dB");

        addMacro (spec, "character", 0.5f, { mapping ("drive", 0.0f, 1.0f),
                                             mapping ("threshold", 0.0f, 0.6f, true) });

        return spec;
    }

    void testSpecFingerprints()
    {
        beginTest ("Equal specifications fingerprint equally; different ones do not");

        const auto first = makeSpec();
        const auto second = makeSpec();

        expectEquals (first.fingerprint(), second.fingerprint());

        // A change anywhere in the DSP definition changes the fingerprint.
        auto changed = makeSpec();
        changed.modules[3].parameterValues.clear();
        setModuleParameter (changed.modules[3], "drive", 0.5f);
        expect (changed.fingerprint() != first.fingerprint(), "a module value change must be visible");

        auto reordered = makeSpec();
        std::swap (reordered.modules[0], reordered.modules[1]);
        expect (reordered.fingerprint() != first.fingerprint(), "graph order must be visible");

        // Serializing and re-parsing must not change the fingerprint.
        auto reparsed = PluginSpec::fromJsonText (first.toJsonText());

        if (expectSuccess (*this, reparsed, "re-parse"))
            expectEquals (reparsed.value().fingerprint(), first.fingerprint());
    }

    void testGraphFingerprints()
    {
        beginTest ("Graph fingerprints are stable for a spec and distinct across specs");

        ModuleRegistry firstRegistry;
        ModuleRegistry secondRegistry;
        expectSuccess (*this, registerBuiltinModules (firstRegistry), "first registry");
        expectSuccess (*this, registerBuiltinModules (secondRegistry), "second registry");

        expectEquals (firstRegistry.catalogFingerprint(), secondRegistry.catalogFingerprint());

        auto specA = makeSpec();
        auto specB = makeSpec();
        specB.modules.push_back (SpecModuleInstance());
        specB.modules.back().instanceId = "extra";
        specB.modules.back().moduleId = "zarlino.limiter";
        parseSemVer ("1.0.0", specB.modules.back().moduleVersion);

        auto engineA = GraphEngine::create (specA, firstRegistry);
        auto engineB = GraphEngine::create (specB, firstRegistry);
        auto engineA2 = GraphEngine::create (specA, secondRegistry);

        if (! expectSuccess (*this, engineA, "graph A")
            || ! expectSuccess (*this, engineB, "graph B")
            || ! expectSuccess (*this, engineA2, "graph A from a second registry"))
            return;

        expectEquals (engineA.value()->graphFingerprint(), engineA2.value()->graphFingerprint(),
                      "the same spec must build the same graph fingerprint");
        expect (engineA.value()->graphFingerprint() != engineB.value()->graphFingerprint(),
                "a different spec must build a different graph fingerprint");
        expectEquals (engineA.value()->parameters().fingerprint(),
                      engineA2.value()->parameters().fingerprint(),
                      "the resolved parameter algebra must be identical");
    }

    void testIdenticalAudio()
    {
        beginTest ("Two engines built from one spec render bit-identical audio");

        auto first = GraphEngine::create (makeSpec(), builtinRegistry());
        auto second = GraphEngine::create (makeSpec(), builtinRegistry());

        if (! expectSuccess (*this, first, "first engine")
            || ! expectSuccess (*this, second, "second engine"))
            return;

        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            expectNoError (*this, first.value()->prepare (rate, 128), "first prepare");
            expectNoError (*this, second.value()->prepare (rate, 128), "second prepare");

            Block blockA (2, 128);
            Block blockB (2, 128);
            float worst = 0.0f;
            float peak = 0.0f;

            for (int i = 0; i < 32; ++i)
            {
                blockA.sine (330.0, rate, 0.7f, static_cast<long long> (i) * 128);
                blockB.sine (330.0, rate, 0.7f, static_cast<long long> (i) * 128);

                first.value()->process (blockA.pointers(), 2, 128);
                second.value()->process (blockB.pointers(), 2, 128);

                worst = std::max (worst, blockA.maxDifference (blockB));
                peak = std::max (peak, blockA.peak());
            }

            expectEquals (worst, 0.0f, "renders must match exactly at " + juce::String (rate));
            expect (peak > 0.01f, "the render must not be silent at " + juce::String (rate));
        }
    }

    void testResolvedOrderIndependence()
    {
        beginTest ("Independently built registries expose an identical, sorted catalogue");

        ModuleRegistry first;
        ModuleRegistry second;

        expectSuccess (*this, registerBuiltinModules (first), "first registry");
        expectSuccess (*this, registerBuiltinModules (second), "second registry");

        expectEquals (static_cast<int> (first.size()), 21);
        expectEquals (first.catalogFingerprint(), second.catalogFingerprint());
        expectEquals (jstr (first.describeCatalog()), jstr (second.describeCatalog()));

        // Sorted by id, so the catalogue (and every fingerprint derived from it)
        // is independent of the order in which modules were registered.
        for (size_t i = 1; i < first.entries().size(); ++i)
            expect (first.entries()[i - 1].manifest.id < first.entries()[i].manifest.id,
                    "the catalogue must be sorted by module id");

        expectEquals (first.entries()[0].manifest.id, second.entries()[0].manifest.id);
    }

    void testExamplesAreStable()
    {
        beginTest ("The committed example specs load with stable fingerprints");

        const char* const names[] = { "channel-strip.zfactory.json",
                                     "saturation.zfactory.json",
                                     "vocal.zfactory.json" };

        std::vector<uint64_t> fingerprints;

        for (const auto* name : names)
        {
            auto first = PluginSpec::fromJsonFile (specPath (name));

            if (! expectSuccess (*this, first, jstr (name) + " (first load)"))
                continue;

            auto second = PluginSpec::fromJsonFile (specPath (name));

            if (! expectSuccess (*this, second, jstr (name) + " (second load)"))
                continue;

            expectEquals (first.value().fingerprint(), second.value().fingerprint(),
                          jstr (name) + " must fingerprint identically on every load");

            fingerprints.push_back (first.value().fingerprint());
        }

        expectEquals (static_cast<int> (fingerprints.size()), 3);

        for (size_t i = 0; i < fingerprints.size(); ++i)
            for (size_t j = i + 1; j < fingerprints.size(); ++j)
                expect (fingerprints[i] != fingerprints[j],
                        "each example spec must describe a different processor");
    }
};

DeterminismTests determinismTests;

} // namespace

void registerDeterminismTests() {}
