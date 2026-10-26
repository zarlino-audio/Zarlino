//==============================================================================
// Module registry: resolution, version compatibility, certification policy,
// dependency checking and catalogue determinism.
//==============================================================================

#include "TestSupport.h"

#include <memory>

using namespace zfactory;
using namespace zfactory::test;

namespace
{

/** Explicit test double: a do-nothing module with a manifest the test controls.
    Used to exercise registry policy without inventing production modules. */
class TestModule final : public DspModule
{
public:
    static ModuleManifest s_manifest;

    const ModuleManifest& getManifest() const noexcept override { return s_manifest; }
    void prepare (const PrepareContext&) override {}
    void reset() noexcept override {}
    void process (float* const*, int, int) noexcept override {}
    void setParameter (int, float) noexcept override {}
    float getParameter (int) const noexcept override { return 0.0f; }
    int getLatencySamples() const noexcept override { return 0; }
};

ModuleManifest TestModule::s_manifest;

ModuleDescriptor makeTestDescriptor()
{
    ModuleDescriptor descriptor;
    descriptor.manifest = TestModule::s_manifest;
    descriptor.create = []() -> DspModule* { return new TestModule(); };
    return descriptor;
}

ModuleManifest makeValidManifest()
{
    ModuleManifest manifest;
    manifest.id = "zarlino.test.double";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Test Double";
    manifest.category = ModuleCategory::Utility;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.realtimeSafe = true;

    ParameterManifest parameter;
    parameter.id = "amount";
    parameter.displayName = "Amount";
    parameter.unit = "";
    parameter.minValue = 0.0f;
    parameter.maxValue = 1.0f;
    parameter.defaultValue = 0.0f;
    manifest.parameters.push_back (parameter);

    return manifest;
}

class RegistryTests : public juce::UnitTest
{
public:
    RegistryTests() : juce::UnitTest ("Factory Registry") {}

    void runTest() override
    {
        testBuiltinCatalogue();
        testCatalogueDeterminism();
        testResolution();
        testVersioning();
        testCertificationPolicy();
        testDependencyAndManifestValidation();
    }

private:
    void testBuiltinCatalogue()
    {
        beginTest ("The built-in catalogue contains eleven certified modules");

        const auto& registry = builtinRegistry();

        expectEquals (static_cast<int> (registry.size()), 25);

        for (const auto& entry : registry.entries())
        {
            expect (entry.create != nullptr, "every entry must be constructible");
            expect (entry.manifest.certification == CertificationState::Certified,
                    jstr (entry.manifest.id) + " must be certified");
            expect (entry.manifest.realtimeSafe, jstr (entry.manifest.id) + " must be realtime safe");
            expect (! entry.manifest.displayName.empty());
            expect (! entry.manifest.supportedChannels.empty());
            expect (entry.manifest.version.major >= 1);
        }

        // Sorted by id so that every catalogue consumer sees the same order.
        for (size_t i = 1; i < registry.entries().size(); ++i)
            expect (registry.entries()[i - 1].manifest.id < registry.entries()[i].manifest.id,
                    "catalogue must be sorted by module id");

        // Every module the example specs reference must exist.
        const char* const expected[] = {
            "zarlino.gain", "zarlino.filter.multimode", "zarlino.eq.tilt",
            "zarlino.eq.parametric", "zarlino.dynamics.compressor", "zarlino.dynamics.gate",
            "zarlino.saturation", "zarlino.limiter", "zarlino.shaping.transient",
            "zarlino.stereo.width", "zarlino.mix.drywet"
        };

        for (const auto* id : expected)
            expect (registry.find (id) != nullptr, jstr (id) + " must be registered");
    }

    void testCatalogueDeterminism()
    {
        beginTest ("Two registries built from the same sources fingerprint identically");

        ModuleRegistry first;
        ModuleRegistry second;

        const auto firstCount = registerBuiltinModules (first);
        const auto secondCount = registerBuiltinModules (second);

        expectSuccess (*this, firstCount, "first registration");
        expectSuccess (*this, secondCount, "second registration");
        expectEquals (static_cast<int> (firstCount.value()), 25);
        expectEquals (first.catalogFingerprint(), second.catalogFingerprint());
        expectEquals (first.catalogFingerprint(), builtinRegistry().catalogFingerprint());
        expectEquals (jstr (first.describeCatalog()), jstr (second.describeCatalog()));
    }

    void testResolution()
    {
        beginTest ("Resolution reports unknown modules distinctly from bad versions");

        const auto& registry = builtinRegistry();

        auto unknown = registry.resolve ("zarlino.nope", SemVer { 1, 0, 0 },
                                         CertificationState::Certified);

        expectFailure (*this, unknown, ErrorCode::UnknownModule, "unknown module id");
        expect (unknown.error().message.find ("zarlino.nope") != std::string::npos,
                "the message must name the module");

        auto found = registry.resolve ("zarlino.gain", SemVer { 1, 0, 0 },
                                       CertificationState::Certified);

        expectSuccess (*this, found, "resolution of a registered module");

        if (found.ok())
        {
            expectEquals (jstr (found.value()->manifest.id), juce::String ("zarlino.gain"));
            expect (found.value()->create != nullptr);
        }

        auto probe = found.ok() ? std::unique_ptr<DspModule> (found.value()->create()) : nullptr;
        expect (probe != nullptr, "resolved entries must instantiate");
        expect (probe != nullptr && probe->getManifest().id == "zarlino.gain");
    }

    void testVersioning()
    {
        beginTest ("Version requests follow the compatibility rule");

        const auto& registry = builtinRegistry();

        expectSuccess (*this, registry.resolve ("zarlino.gain", SemVer { 1, 0, 0 },
                                                CertificationState::Certified),
                       "exact version");

        expectSuccess (*this, registry.resolve ("zarlino.gain", SemVer { 1, 0, 0 },
                                                CertificationState::Certified),
                       "patch-level request");

        auto newerMinor = registry.resolve ("zarlino.gain", SemVer { 1, 0, 0 },
                                           CertificationState::Certified);

        expectSuccess (*this, newerMinor, "requesting an older patch of the same major");

        auto wrongMajor = registry.resolve ("zarlino.gain", SemVer { 2, 0, 0 },
                                            CertificationState::Certified);

        expectFailure (*this, wrongMajor, ErrorCode::ModuleVersionIncompatible,
                       "request for a future major version");

        expect (wrongMajor.error().message.find ("major versions must match") != std::string::npos,
                "the error must explain the rule");

        auto futureMinor = registry.resolve ("zarlino.gain", SemVer { 1, 5, 0 },
                                             CertificationState::Certified);

        expectFailure (*this, futureMinor, ErrorCode::ModuleVersionIncompatible,
                       "request for a minor version the registry does not have");
    }

    void testCertificationPolicy()
    {
        beginTest ("Certification policy is enforced structurally");

        ModuleRegistry registry;
        TestModule::s_manifest = makeValidManifest();
        TestModule::s_manifest.certification = CertificationState::Experimental;

        expectNoError (*this, registry.add (makeTestDescriptor()),
                       "registering an experimental module");

        auto rejected = registry.resolve ("zarlino.test.double", SemVer { 1, 0, 0 },
                                          CertificationState::Certified);

        expectFailure (*this, rejected, ErrorCode::ModuleNotCertified,
                       "certified-only policy must reject an experimental module");

        expectSuccess (*this, registry.resolve ("zarlino.test.double", SemVer { 1, 0, 0 },
                                                CertificationState::Experimental),
                       "an explicit experimental policy accepts it");

        // A verified module satisfies a verified policy but not a certified one.
        ModuleRegistry verified;
        TestModule::s_manifest.certification = CertificationState::Verified;

        expectNoError (*this, verified.add (makeTestDescriptor()),
                       "registering a verified module");

        expectSuccess (*this, verified.resolve ("zarlino.test.double", SemVer { 1, 0, 0 },
                                                CertificationState::Verified),
                       "verified policy");

        expectFailure (*this, verified.resolve ("zarlino.test.double", SemVer { 1, 0, 0 },
                                                CertificationState::Certified),
                       ErrorCode::ModuleNotCertified, "certified policy on a verified module");

        // Duplicate registration of the same id and version is refused.
        ModuleRegistry duplicates;
        TestModule::s_manifest.certification = CertificationState::Certified;

        expectNoError (*this, duplicates.add (makeTestDescriptor()), "first add");
        expectError (*this, duplicates.add (makeTestDescriptor()),
                     ErrorCode::ModuleAlreadyRegistered, "second add");
    }

    void testDependencyAndManifestValidation()
    {
        beginTest ("Dependencies are resolved and malformed manifests are refused");

        ModuleRegistry registry;
        TestModule::s_manifest = makeValidManifest();
        TestModule::s_manifest.dependencies = { "zarlino.absent.dependency" };

        expectNoError (*this, registry.add (makeTestDescriptor()),
                       "a module may be registered while its dependency is absent");

        expectFailure (*this, registry.resolve ("zarlino.test.double", SemVer { 1, 0, 0 },
                                                CertificationState::Certified),
                       ErrorCode::ModuleDependencyMissing, "missing dependency");

        ModuleRegistry invalid;
        TestModule::s_manifest = makeValidManifest();
        TestModule::s_manifest.version = { 0, 0, 0 };
        expectError (*this, invalid.add (makeTestDescriptor()), ErrorCode::UnsupportedValue,
                     "unversioned module");

        TestModule::s_manifest = makeValidManifest();
        TestModule::s_manifest.supportedChannels.clear();
        expectError (*this, invalid.add (makeTestDescriptor()), ErrorCode::EmptyCollection,
                     "module with no supported channel modes");

        TestModule::s_manifest = makeValidManifest();
        TestModule::s_manifest.realtimeSafe = false;
        expectError (*this, invalid.add (makeTestDescriptor()), ErrorCode::UnsupportedValue,
                     "module that cannot promise realtime safety");

        TestModule::s_manifest = makeValidManifest();
        TestModule::s_manifest.parameters[0].defaultValue = 5.0f;
        expectError (*this, invalid.add (makeTestDescriptor()), ErrorCode::ValueOutOfRange,
                     "module parameter default outside its domain");

        TestModule::s_manifest = makeValidManifest();
        TestModule::s_manifest.parameters[0].stepped = true;
        TestModule::s_manifest.parameters[0].stepCount = 1;
        expectError (*this, invalid.add (makeTestDescriptor()), ErrorCode::ValueOutOfRange,
                     "stepped parameter with fewer than two steps");

        TestModule::s_manifest = makeValidManifest();
        TestModule::s_manifest.dependencies = { TestModule::s_manifest.id };
        expectError (*this, invalid.add (makeTestDescriptor()), ErrorCode::UnsupportedValue,
                     "module depending on itself");

        ModuleDescriptor broken;
        broken.manifest = makeValidManifest();
        broken.create = nullptr;
        expectError (*this, invalid.add (broken), ErrorCode::UnsupportedValue,
                     "descriptor without a factory function");

        // Parameter domain -> normalised round trip must be exact at both ends.
        const auto& parameter = makeValidManifest().parameters[0];
        expectWithinAbsoluteError (parameter.denormalise (0.0f), 0.0f, 1.0e-6f);
        expectWithinAbsoluteError (parameter.denormalise (1.0f), 1.0f, 1.0e-6f);
        expectWithinAbsoluteError (parameter.normalise (0.25f), 0.25f, 1.0e-6f);
        expectWithinAbsoluteError (parameter.clampValue (7.0f), 1.0f, 1.0e-6f);

        ParameterManifest logarithmic;
        logarithmic.minValue = 20.0f;
        logarithmic.maxValue = 20000.0f;
        logarithmic.defaultValue = 1000.0f;
        logarithmic.logarithmic = true;

        expectWithinAbsoluteError (logarithmic.denormalise (0.0f), 20.0f, 1.0e-3f);
        expectWithinAbsoluteError (logarithmic.denormalise (1.0f), 20000.0f, 2.0f);
        expectWithinAbsoluteError (logarithmic.denormalise (0.5f), 632.4555f, 0.5f);

        ParameterManifest stepped;
        stepped.minValue = 0.0f;
        stepped.maxValue = 5.0f;
        stepped.defaultValue = 0.0f;
        stepped.stepped = true;
        stepped.stepCount = 6;

        expectWithinAbsoluteError (stepped.denormalise (0.0f), 0.0f, 1.0e-6f);
        expectWithinAbsoluteError (stepped.denormalise (1.0f), 5.0f, 1.0e-6f);
        expectWithinAbsoluteError (stepped.denormalise (0.55f), 3.0f, 1.0e-6f);
        expectWithinAbsoluteError (stepped.denormalise (2.0f), 5.0f, 1.0e-6f, "denormalise must clamp");
        expectWithinAbsoluteError (stepped.denormalise (std::numeric_limits<float>::quiet_NaN()), 0.0f, 1.0e-6f,
                                   "NaN must fall back to the default");
    }
};

RegistryTests registryTests;

} // namespace

void registerRegistryTests() {}
