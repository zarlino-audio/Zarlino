//==============================================================================
// PluginSpec schema validation.
//
// Every rejection must be deterministic: the same document always produces the
// same error code, the same JSON path and the same message. That property is
// asserted explicitly, because it is what lets a caller (or an automated
// generator) act on a failure.
//==============================================================================

#include "TestSupport.h"

using namespace zfactory;
using namespace zfactory::test;

namespace
{

std::string moduleJson (const char* instanceId,
                        const char* moduleId,
                        const char* version = "1.0.0",
                        const char* parameters = "{}")
{
    return std::string ("{\"instanceId\": \"") + instanceId
           + "\", \"moduleId\": \"" + moduleId
           + "\", \"moduleVersion\": \"" + version
           + "\", \"parameters\": " + parameters + "}";
}

std::string makeDocument (const std::string& modules,
                          const std::string& parameters = "[]",
                          const std::string& macros = "[]")
{
    return std::string ("{")
           + "\"factorySchemaVersion\": 1,"
           + "\"plugin\": {\"id\": \"zarlino.test.doc\", \"name\": \"Doc\", \"vendor\": \"Zarlino Audio\","
             " \"version\": \"1.0.0\", \"effectType\": \"utility\", \"channels\": \"stereo\"},"
           + "\"graph\": {\"modules\": [" + modules + "]},"
           + "\"parameters\": " + parameters + ","
           + "\"macros\": " + macros + ","
           + "\"dryWet\": {\"enabled\": false, \"defaultMix\": 1.0},"
           + "\"bypass\": {\"mode\": \"hard\", \"defaultBypassed\": false},"
           + "\"ui\": {\"accentColor\": \"#101010\", \"category\": \"Test\", \"layout\": \"vertical\"}"
           + "}";
}

/** A document whose schema version is a string rather than a number. */
std::string documentWithStringSchemaVersion()
{
    return std::string ("{\"factorySchemaVersion\": \"1\",")
           + "\"plugin\": {\"id\": \"zarlino.test.doc\", \"name\": \"Doc\", \"vendor\": \"Zarlino Audio\","
             " \"version\": \"1.0.0\", \"effectType\": \"utility\", \"channels\": \"stereo\"},"
           + "\"graph\": {\"modules\": [" + moduleJson ("g", "zarlino.gain") + "]}}";
}

/** The same document, but with a different schema version and an extra field, so
    the "unsupported version wins" rule can be checked. */
std::string documentWithSchemaVersion (int version)
{
    return std::string ("{\"factorySchemaVersion\": ") + std::to_string (version)
           + ", \"futureField\": true,"
           + "\"plugin\": {\"id\": \"zarlino.test.doc\", \"name\": \"Doc\", \"vendor\": \"Zarlino Audio\","
             " \"version\": \"1.0.0\", \"effectType\": \"utility\", \"channels\": \"stereo\"},"
           + "\"graph\": {\"modules\": [" + moduleJson ("g", "zarlino.gain") + "]}"
           + "}";
}

std::string parameterJson (const char* id, const char* target, const char* range, const char* extra = "")
{
    return std::string ("{\"id\": \"") + id + "\", \"name\": \"" + id + "\", \"target\": \"" + target
           + "\", \"range\": " + range + extra + "}";
}

const char* const kGainModule = "{\"instanceId\": \"g\", \"moduleId\": \"zarlino.gain\","
                                " \"moduleVersion\": \"1.0.0\", \"parameters\": {}}";

const char* const kValidRange = "{\"min\": -18.0, \"max\": 18.0, \"default\": 0.0}";

class SpecValidationTests : public juce::UnitTest
{
public:
    SpecValidationTests() : juce::UnitTest ("Factory Spec Validation") {}

    void runTest() override
    {
        testValidDocument();
        testDeterministicErrors();
        testMalformedDocuments();
        testStructuralErrors();
        testIdentifierAndReferenceErrors();
        testNumericDomains();
        testFactoryControls();
        testSemVerRules();
        testRoundTrip();
    }

private:
    void testValidDocument()
    {
        beginTest ("A well formed document parses and reports its shape");

        auto parsed = PluginSpec::fromJsonText (makeDocument (kGainModule));

        if (! expectSuccess (*this, parsed, "valid document"))
            return;

        const auto& spec = parsed.value();

        expectEquals (spec.schemaVersion, kSchemaVersion);
        expectEquals (jstr (spec.pluginId), juce::String ("zarlino.test.doc"));
        expectEquals (jstr (spec.pluginVersion), juce::String ("1.0.0"));
        expect (spec.effectType == EffectType::Utility);
        expect (spec.channels == ChannelMode::Stereo);
        expectEquals (static_cast<int> (spec.modules.size()), 1);
        expectEquals (static_cast<int> (spec.parameters.size()), 0);
        expect (spec.bypass.mode == "hard");
        expect (! spec.dryWet.enabled);
        expectEquals (spec.channelCount(), 2);
    }

    void testDeterministicErrors()
    {
        beginTest ("Repeated validation of a bad document returns the identical error");

        const std::string document = makeDocument ("{\"instanceId\": \"g\"}");

        const auto first = PluginSpec::fromJsonText (document);
        const auto second = PluginSpec::fromJsonText (document);

        expect (first.error() == second.error(), "expected identical errors");
        expect (! first.error().describe().empty(), "error must have a description");
        expectEquals (jstr (errorCodeName (first.error().code)), juce::String ("MISSING_REQUIRED_FIELD"));
        expectEquals (jstr (first.error().path), juce::String ("$/graph/modules[0]/moduleId"));

        // Key order inside an object must not change the reported error, because
        // the unknown-key check reports the lexicographically smallest offender.
        const std::string a = makeDocument ("{\"instanceId\": \"g\", \"moduleId\": \"zarlino.gain\","
                                           " \"moduleVersion\": \"1.0.0\", \"parameters\": {},"
                                           " \"zeta\": 1, \"alpha\": 2}");
        const std::string b = makeDocument ("{\"alpha\": 2, \"parameters\": {}, \"moduleVersion\": \"1.0.0\","
                                           " \"moduleId\": \"zarlino.gain\", \"instanceId\": \"g\","
                                           " \"zeta\": 1}");

        const auto firstOrder = PluginSpec::fromJsonText (a);
        const auto secondOrder = PluginSpec::fromJsonText (b);

        expect (firstOrder.error() == secondOrder.error(), "key order must not change the error");
        expectEquals (jstr (firstOrder.error().path), juce::String ("$/graph/modules[0]/alpha"));
    }

    void testMalformedDocuments()
    {
        beginTest ("Malformed JSON, non-object roots and bad schema versions are rejected");

        expectFailure (*this, PluginSpec::fromJsonText ("{ this is not json }"),
                       ErrorCode::JsonParseFailed, "broken JSON");

        expectFailure (*this, PluginSpec::fromJsonText ("[1, 2, 3]"),
                       ErrorCode::RootNotObject, "array root");

        expectFailure (*this, PluginSpec::fromJsonText ("42"),
                       ErrorCode::JsonParseFailed, "scalar root");

        // A root that is not an object is reported as such when the value already
        // came through the JSON layer (for example from an in-memory var).
        expectFailure (*this, PluginSpec::fromJson (juce::var (42.0)),
                       ErrorCode::RootNotObject, "non-object root var");

        expectFailure (*this, PluginSpec::fromJson (juce::var()),
                       ErrorCode::RootNotObject, "void root var");

        expectFailure (*this, PluginSpec::fromJsonText (documentWithSchemaVersion (2)),
                       ErrorCode::UnsupportedSchemaVersion, "schema v2 document");

        expectFailure (*this, PluginSpec::fromJsonText (documentWithSchemaVersion (0)),
                       ErrorCode::UnsupportedSchemaVersion, "schema v0 document");

        expectFailure (*this, PluginSpec::fromJsonText (documentWithStringSchemaVersion()),
                       ErrorCode::TypeMismatch, "string schema version");

        expectFailure (*this, PluginSpec::fromJsonText (R"({"plugin": {}})"),
                       ErrorCode::MissingRequiredField, "document without a schema version");
    }

    void testStructuralErrors()
    {
        beginTest ("Unknown fields, wrong types and empty collections are rejected");

        expectFailure (*this, PluginSpec::fromJsonText (makeDocument (kGainModule) + "extra"),
                       ErrorCode::JsonParseFailed, "trailing content");

        expectFailure (*this,
                       PluginSpec::fromJsonText (makeDocument (kGainModule).replace (
                           makeDocument (kGainModule).find ("\"ui\""), 4, "\"uiX\"")),
                       ErrorCode::UnexpectedField, "ui renamed to an unknown field");

        expectFailure (*this, PluginSpec::fromJsonText (makeDocument (kGainModule, "{}")),
                       ErrorCode::TypeMismatch, "parameters not an array");

        expectFailure (*this, PluginSpec::fromJsonText (makeDocument ("")),
                       ErrorCode::EmptyCollection, "empty graph");

        expectFailure (*this,
                       PluginSpec::fromJsonText (makeDocument (kGainModule, "[]", "[]").insert (1, "\"unknownTop\": 1,")),
                       ErrorCode::UnexpectedField, "unknown top level field");

        expectFailure (*this,
                       PluginSpec::fromJsonText (makeDocument (moduleJson ("g", "zarlino.gain", "1.0", "{}"))),
                       ErrorCode::TypeMismatch, "two component module version");

        expectFailure (*this,
                       PluginSpec::fromJsonText (makeDocument (moduleJson ("g", "zarlino.gain", "1.0.0", "{\"gainDb\": \"loud\"}"))),
                       ErrorCode::TypeMismatch, "string parameter value");

        std::string tooManyModules;

        for (int i = 0; i < kMaxGraphModules + 1; ++i)
        {
            if (i > 0)
                tooManyModules += ",";

            tooManyModules += moduleJson (("m" + std::to_string (i)).c_str(), "zarlino.gain");
        }

        expectFailure (*this, PluginSpec::fromJsonText (makeDocument (tooManyModules)),
                       ErrorCode::CollectionTooLarge, "graph over the module limit");
    }

    void testIdentifierAndReferenceErrors()
    {
        beginTest ("Duplicate ids, bad identifiers and dangling references are rejected");

        expectFailure (*this,
                       PluginSpec::fromJsonText (makeDocument (moduleJson ("g", "zarlino.gain") + ","
                                                                + moduleJson ("g", "zarlino.gain"))),
                       ErrorCode::DuplicateIdentifier, "duplicate instance id");

        expectFailure (*this,
                       PluginSpec::fromJsonText (makeDocument (moduleJson ("1bad", "zarlino.gain"))),
                       ErrorCode::InvalidIdentifier, "instance id starting with a digit");

        expectFailure (*this,
                       PluginSpec::fromJsonText (makeDocument (
                           kGainModule, "[" + parameterJson ("trim", "nope.gainDb", kValidRange) + "]")),
                       ErrorCode::DanglingReference, "target on a missing instance");

        const std::string duplicated = "[" + parameterJson ("trim", "g.gainDb", kValidRange) + ","
                                       + parameterJson ("trim", "g.gainDb", kValidRange) + "]";

        expectFailure (*this, PluginSpec::fromJsonText (makeDocument (kGainModule, duplicated)),
                       ErrorCode::DuplicateIdentifier, "duplicate parameter id");

        const std::string badMacro = "[{\"id\": \"m\", \"name\": \"M\", \"defaultValue\": 0.0,"
                                     " \"mappings\": [{\"parameter\": \"nope\"}]}]";

        expectFailure (*this, PluginSpec::fromJsonText (makeDocument (kGainModule, "[]", badMacro)),
                       ErrorCode::DanglingReference, "macro mapping to an undeclared parameter");

        expectFailure (*this,
                       PluginSpec::fromJsonText (makeDocument (
                           kGainModule, "[]",
                           "[{\"id\": \"m\", \"name\": \"M\", \"defaultValue\": 0.0, \"mappings\": []}]")),
                       ErrorCode::EmptyCollection, "macro with no mappings");

        expectFailure (*this,
                       PluginSpec::fromJsonText (makeDocument (
                           kGainModule, "[" + parameterJson ("trim", "g.gainDb", kValidRange) + "]",
                           "[{\"id\": \"m\", \"name\": \"M\", \"defaultValue\": 0.0, \"mappings\":"
                           " [{\"parameter\": \"trim\"}, {\"parameter\": \"trim\"}]}]")),
                       ErrorCode::DuplicateIdentifier, "macro mapping a parameter twice");
    }

    void testNumericDomains()
    {
        beginTest ("Value domains are enforced");

        const auto rangeDocument = [] (const char* min, const char* max, const char* def, const char* extra)
        {
            return makeDocument (kGainModule,
                                 std::string ("[{\"id\": \"trim\", \"name\": \"Trim\", \"target\": \"g.gainDb\","
                                              " \"range\": {\"min\": ") + min + ", \"max\": " + max
                                     + ", \"default\": " + def + "}" + extra + "}]");
        };

        expectFailure (*this, PluginSpec::fromJsonText (rangeDocument ("1.0", "1.0", "1.0", "")),
                       ErrorCode::ValueOutOfRange, "empty range");

        expectFailure (*this, PluginSpec::fromJsonText (rangeDocument ("18.0", "-18.0", "0.0", "")),
                       ErrorCode::ValueOutOfRange, "inverted range");

        expectFailure (*this, PluginSpec::fromJsonText (rangeDocument ("-18.0", "18.0", "30.0", "")),
                       ErrorCode::ValueOutOfRange, "default outside range");

        expectFailure (*this, PluginSpec::fromJsonText (rangeDocument ("0.0", "18.0", "1.0", ", \"logarithmic\": true")),
                       ErrorCode::ValueOutOfRange, "logarithmic range with a zero minimum");

        expectFailure (*this, PluginSpec::fromJsonText (rangeDocument ("-18.0", "18.0", "0.0", ", \"unknownKey\": 1")),
                       ErrorCode::UnexpectedField, "unknown parameter field");

        // Values inside the document are fine, but a range wider than the module
        // parameter's own domain is a resolution error (covered by the graph tests).
        expectSuccess (*this, PluginSpec::fromJsonText (rangeDocument ("-18.0", "18.0", "0.0", "")),
                       "valid range");
    }

    void testFactoryControls()
    {
        beginTest ("Factory control targets are validated explicitly");

        const char* const dryWetParameter =
            "[{\"id\": \"mix\", \"name\": \"Mix\", \"target\": \"$factory.dryWet\","
            " \"range\": {\"min\": 0.0, \"max\": 1.0, \"default\": 1.0}}]";

        expectFailure (*this, PluginSpec::fromJsonText (makeDocument (kGainModule, dryWetParameter)),
                       ErrorCode::UnsupportedValue, "dryWet target without dryWet.enabled");

        const char* const unknownControl =
            "[{\"id\": \"mix\", \"name\": \"Mix\", \"target\": \"$factory.reverb\","
            " \"range\": {\"min\": 0.0, \"max\": 1.0, \"default\": 1.0}}]";

        expectFailure (*this, PluginSpec::fromJsonText (makeDocument (kGainModule, unknownControl)),
                       ErrorCode::UnsupportedValue, "unknown factory control");

        const char* const outOfBounds =
            "[{\"id\": \"bypassed\", \"name\": \"Bypass\", \"target\": \"$factory.bypass\","
            " \"range\": {\"min\": -1.0, \"max\": 2.0, \"default\": 0.0}}]";

        expectFailure (*this, PluginSpec::fromJsonText (makeDocument (kGainModule, outOfBounds)),
                       ErrorCode::ValueOutOfRange, "factory control outside 0..1");

        const char* const bypassParameter =
            "[{\"id\": \"bypassed\", \"name\": \"Bypass\", \"target\": \"$factory.bypass\","
            " \"range\": {\"min\": 0.0, \"max\": 1.0, \"default\": 0.0}}]";

        expectSuccess (*this, PluginSpec::fromJsonText (makeDocument (kGainModule, bypassParameter)),
                       "bypass control");
    }

    void testSemVerRules()
    {
        beginTest ("Version parsing and compatibility are strict");

        SemVer version;

        expect (parseSemVer ("1.2.3", version));
        expectEquals (version.major, 1);
        expectEquals (version.minor, 2);
        expectEquals (version.patch, 3);
        expectEquals (jstr (version.to_string()), juce::String ("1.2.3"));

        expect (! parseSemVer ("1.2", version), "two components must be rejected");
        expect (! parseSemVer ("1.2.3.4", version), "four components must be rejected");
        expect (! parseSemVer ("01.2.3", version), "leading zeros must be rejected");
        expect (! parseSemVer ("1.2.x", version), "non numeric must be rejected");
        expect (! parseSemVer ("", version), "empty must be rejected");
        expect (! parseSemVer ("1.2.3-beta", version), "suffixes must be rejected");

        const SemVer oneTwo { 1, 2, 0 };
        const SemVer oneThree { 1, 3, 0 };
        const SemVer twoZero { 2, 0, 0 };
        const SemVer oneOne { 1, 1, 0 };

        expect (isVersionCompatible (oneTwo, oneThree), "newer minor is compatible");
        expect (isVersionCompatible (oneTwo, oneTwo), "same version is compatible");
        expect (! isVersionCompatible (oneTwo, oneOne), "older registry version is incompatible");
        expect (! isVersionCompatible (oneTwo, twoZero), "different major is incompatible");

        expect (meetsCertification (CertificationState::Certified, CertificationState::Verified));
        expect (meetsCertification (CertificationState::Certified, CertificationState::Certified));
        expect (! meetsCertification (CertificationState::Verified, CertificationState::Certified));
        expect (! meetsCertification (CertificationState::Experimental, CertificationState::Verified));
        expect (! meetsCertification (CertificationState::Deprecated, CertificationState::Certified),
                "deprecated modules are usable but never count as certified");
    }

    void testRoundTrip()
    {
        beginTest ("Serializing a parsed document reproduces the same spec");

        const std::string document = makeDocument (
            kGainModule,
            "[{\"id\": \"trim\", \"name\": \"Trim\", \"target\": \"g.gainDb\","
            " \"range\": {\"min\": -12.0, \"max\": 12.0, \"default\": 0.0}, \"unit\": \"dB\","
            " \"group\": \"Input\", \"exposeToHost\": true}]",
            "[{\"id\": \"heat\", \"name\": \"Heat\", \"defaultValue\": 0.25,"
            " \"mappings\": [{\"parameter\": \"trim\", \"rangeStart\": 0.5, \"rangeEnd\": 0.75,"
            " \"curve\": 1.5, \"scale\": 1.0, \"invert\": true}]}]");

        auto first = PluginSpec::fromJsonText (document);

        if (! expectSuccess (*this, first, "round trip source"))
            return;

        auto second = PluginSpec::fromJsonText (first.value().toJsonText());

        if (! expectSuccess (*this, second, "round trip target"))
            return;

        expectEquals (second.value().fingerprint(), first.value().fingerprint(),
                      "fingerprint must survive a serialize/parse round trip");
    }
};

SpecValidationTests specValidationTests;

} // namespace

void registerSpecValidationTests() {}
