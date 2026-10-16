//==============================================================================
// State serialization and restoration.
//
// A state blob is only ever restorable into the graph it was taken from: it
// carries the factory schema version and the fingerprint of the PluginSpec the
// graph was built from. Restoring into a different graph fails deterministically
// instead of silently mis-mapping values.
//==============================================================================

#include "TestSupport.h"

#include <cstring>

using namespace zfactory;
using namespace zfactory::test;

namespace
{

class SerializationTests : public juce::UnitTest
{
public:
    SerializationTests() : juce::UnitTest ("Factory State Serialization") {}

    void runTest() override
    {
        testRoundTrip();
        testAffectsAudio();
        testMismatchedSpec();
        testRejectedBlobs();
    }

private:
    PluginSpec makeSpec (float driveDefault = 0.2f)
    {
        auto spec = makeBaseSpec();

        addModule (spec, "gain", "zarlino.gain");
        addModule (spec, "sat", "zarlino.saturation");
        addModule (spec, "limit", "zarlino.limiter");

        addParameter (spec, "trim", "gain.gainDb", -12.0f, 12.0f, 0.0f, "dB");
        addParameter (spec, "drive", "sat.drive", 0.0f, 1.0f, driveDefault);
        addParameter (spec, "mix", "$factory.dryWet", 0.0f, 1.0f, 1.0f);
        addParameter (spec, "bypassed", "$factory.bypass", 0.0f, 1.0f, 0.0f);

        spec.dryWet.enabled = true;

        // The macro owns the gain parameter; drive, mix and bypass stay
        // host-writable so the round trip can be checked in both directions.
        addMacro (spec, "heat", 0.5f, { mapping ("trim", 0.0f, 1.0f) });

        return spec;
    }

    std::unique_ptr<GraphEngine> buildPrepared (const PluginSpec& spec)
    {
        auto engine = GraphEngine::create (spec, builtinRegistry());

        if (! engine.ok())
        {
            expect (false, "graph build failed: " + jstr (engine.error().describe()));
            return nullptr;
        }

        if (auto error = engine.value()->prepare (48000.0, 128); error.failed())
        {
            expect (false, "prepare failed: " + jstr (error.describe()));
            return nullptr;
        }

        return std::move (engine.value());
    }

    void testRoundTrip()
    {
        beginTest ("State round-trips macros, parameters, bypass and dry/wet");

        auto spec = makeSpec();
        auto source = buildPrepared (spec);
        auto target = buildPrepared (spec);

        if (source == nullptr || target == nullptr)
            return;

        source->parameters().setMacroValue (0, 0.8f);
        source->parameters().setBaseValue (0, 7.5f);
        source->parameters().setBaseNormalised (2, 0.35f);
        source->setBypassed (true);
        source->applyParameters();

        const auto blob = source->saveState();
        expect (blob.getSize() > 0, "the state blob must not be empty");

        expectNoError (*this, target->restoreState (blob.getData(), blob.getSize()), "restore");

        expectEquals (target->parameters().numMacros(), source->parameters().numMacros());
        expectWithinAbsoluteError (target->parameters().getMacroValue (0), 0.8f, 1.0e-6f);
        expectWithinAbsoluteError (target->parameters().bindings()[0].baseNormalised,
                                   source->parameters().bindings()[0].baseNormalised, 1.0e-6f);
        expectWithinAbsoluteError (target->parameters().bindings()[2].baseNormalised, 0.35f, 1.0e-6f);
        expectWithinAbsoluteError (target->getDryWetNormalised(), source->getDryWetNormalised(), 1.0e-6f);
        expect (target->isBypassed(), "bypass must survive the round trip");

        // Restoring twice is idempotent.
        expectNoError (*this, target->restoreState (blob.getData(), blob.getSize()), "restore again");
        expectWithinAbsoluteError (target->parameters().getMacroValue (0), 0.8f, 1.0e-6f);
    }

    void testAffectsAudio()
    {
        beginTest ("A restored state changes the rendered audio back to the saved setting");

        auto spec = makeSpec();
        auto reference = buildPrepared (spec);
        auto other = buildPrepared (spec);

        if (reference == nullptr || other == nullptr)
            return;

        // Save a "bright, driving" state.
        reference->parameters().setBaseNormalised (1, 1.0f);
        reference->applyParameters();

        const auto blob = reference->saveState();

        // The other engine is set to the opposite extreme, then restored.
        other->parameters().setBaseNormalised (1, 0.0f);
        other->applyParameters();

        Block before (2, 128);
        Block after (2, 128);
        Block expected (2, 128);

        for (int i = 0; i < 8; ++i)
        {
            const auto start = static_cast<long long> (i) * 128;

            before.sine (440.0, 48000.0, 0.6f, start);
            after.sine (440.0, 48000.0, 0.6f, start);
            expected.sine (440.0, 48000.0, 0.6f, start);

            other->process (before.pointers(), 2, 128);
            reference->process (expected.pointers(), 2, 128);
        }

        expect (before.maxDifference (expected) > 1.0e-4f, "the two settings must sound different");

        expectNoError (*this, other->restoreState (blob.getData(), blob.getSize()), "restore");
        other->reset();
        reference->reset();

        for (int i = 0; i < 8; ++i)
        {
            const auto start = static_cast<long long> (i) * 128;

            after.sine (440.0, 48000.0, 0.6f, start);
            expected.sine (440.0, 48000.0, 0.6f, start);

            other->process (after.pointers(), 2, 128);
            reference->process (expected.pointers(), 2, 128);
        }

        expectEquals (after.maxDifference (expected), 0.0f,
                      "a restored graph must render identically to the source graph");
    }

    void testMismatchedSpec()
    {
        beginTest ("A blob taken from another spec is refused");

        auto spec = makeSpec();
        auto original = buildPrepared (spec);

        // Same shape, materially different DSP definition.
        auto other = makeSpec (0.9f);
        auto mismatched = buildPrepared (other);

        if (original == nullptr || mismatched == nullptr)
            return;

        const auto blob = original->saveState();

        const auto error = mismatched->restoreState (blob.getData(), blob.getSize());
        expectError (*this, error, ErrorCode::StateSpecMismatch, "spec fingerprint mismatch");
        expect (error.message.find ("PluginSpec") != std::string::npos);

        // A spec that differs only in the graph is refused too.
        auto extra = makeSpec();
        extra.modules.pop_back();
        auto shorter = buildPrepared (extra);

        if (shorter != nullptr)
            expectError (*this, shorter->restoreState (blob.getData(), blob.getSize()),
                         ErrorCode::StateSpecMismatch, "shorter graph");
    }

    void testRejectedBlobs()
    {
        beginTest ("Malformed, empty and future-version blobs are refused");

        auto spec = makeSpec();
        auto engine = buildPrepared (spec);

        if (engine == nullptr)
            return;

        expectError (*this, engine->restoreState (nullptr, 0),
                     ErrorCode::InvalidStateBlob, "null blob");

        const char* const notJson = "this is not a state blob";
        expectError (*this, engine->restoreState (notJson, std::strlen (notJson)),
                     ErrorCode::InvalidStateBlob, "non JSON blob");

        const char* const arrayRoot = "[1, 2, 3]";
        expectError (*this, engine->restoreState (arrayRoot, std::strlen (arrayRoot)),
                     ErrorCode::InvalidStateBlob, "array root");

        const char* const badSchema =
            "{\"factorySchemaVersion\": 99, \"specFingerprint\": \"0000000000000000\"}";
        expectError (*this, engine->restoreState (badSchema, std::strlen (badSchema)),
                     ErrorCode::StateSchemaUnsupported, "future schema version");

        const auto blob = engine->saveState();
        const auto text = juce::String::fromUTF8 (static_cast<const char*> (blob.getData()),
                                                 static_cast<int> (blob.getSize()));

        expect (text.contains ("factorySchemaVersion"), "the blob must carry the schema version");
        expect (text.contains ("specFingerprint"), "the blob must carry the spec fingerprint");

        // An unknown parameter id inside an otherwise valid blob is refused.
        const juce::String tamperedParameters =
            text.replace ("\"trim\"", "\"nonexistent\"");
        expectError (*this,
                     engine->restoreState (tamperedParameters.toRawUTF8(),
                                           static_cast<size_t> (tamperedParameters.getNumBytesAsUTF8())),
                     ErrorCode::InvalidStateBlob, "unknown parameter in a blob");
    }
};

SerializationTests serializationTests;

} // namespace

void registerSerializationTests() {}
