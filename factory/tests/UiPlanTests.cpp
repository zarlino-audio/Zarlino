//==============================================================================
// The UI compiler: PluginSpec + certified module metadata -> UiPlan.
//
// These tests pin the three properties the universal plugin shell depends on:
//   1. the plan is a pure function of (spec, catalog, template) - identical
//      inputs produce identical plans, on any machine, in any order;
//   2. the three templates are strictly nested and strictly ordered by density,
//      so a product cannot "look different" in an incoherent way;
//   3. the plan can only ever contain controls derived from a validated spec or
//      a certified module manifest - there is no path by which user text becomes
//      a control, a label, a range or a piece of UI code.
//==============================================================================

#include "TestSupport.h"

#include "zfactory/UiPlan.h"

#include <map>
#include <set>

using namespace zfactory;
using namespace zfactory::test;

namespace
{

const char* const kExampleSpecs[] = {
    "channel-strip.zfactory.json",
    "saturation.zfactory.json",
    "vocal.zfactory.json"
};

constexpr int kNumExampleSpecs = 3;

class UiPlanTests : public juce::UnitTest
{
public:
    UiPlanTests() : juce::UnitTest ("Factory UiPlan") {}

    void runTest() override
    {
        testCompilesForEveryExampleSpec();
        testTemplatesAreNested();
        testTemplatesAreStrictlyDenser();
        testDeterministic();
        testHostParameterNumbering();
        testStableIdentity();
        testGroupsAreTotal();
        testControlKindsFollowModuleMetadata();
        testModuleLevelControlsOnlyInAdvanced();
        testRegistryFailureIsPropagated();
        testNoUserTextBecomesAValue();
    }

private:
    //==========================================================================
    PluginSpec loadSpec (const char* fileName)
    {
        const auto result = PluginSpec::fromJsonFile (specPath (fileName));
        expect (result.ok(), "example spec should load: " + jstr (fileName));
        return result.ok() ? result.value() : PluginSpec();
    }

    static UiPlan compileOrFail (juce::UnitTest& test,
                                 const PluginSpec& spec,
                                 UiTemplate uiTemplate,
                                 const juce::String& what)
    {
        const auto result = UiPlan::compile (spec, builtinRegistry(), uiTemplate);

        if (! result.ok())
        {
            test.expect (false, what + " failed to compile: " + jstr (result.error().describe()));
            return UiPlan();
        }

        return result.value();
    }

    static std::set<std::string> idsOf (const UiPlan& plan)
    {
        std::set<std::string> ids;

        for (const auto& control : plan.controls)
            ids.insert (control.id);

        return ids;
    }

    static bool isSubset (const std::set<std::string>& small, const std::set<std::string>& large)
    {
        for (const auto& id : small)
            if (large.find (id) == large.end())
                return false;

        return true;
    }

    //==========================================================================
    void testCompilesForEveryExampleSpec()
    {
        beginTest ("Every committed example spec compiles under every template");

        const UiTemplate templates[] = { UiTemplate::Minimal, UiTemplate::Standard, UiTemplate::Advanced };

        for (int s = 0; s < kNumExampleSpecs; ++s)
        {
            const auto spec = loadSpec (kExampleSpecs[s]);

            for (const auto uiTemplate : templates)
            {
                const auto result = UiPlan::compile (spec, builtinRegistry(), uiTemplate);

                if (! expectSuccess (*this, result, jstr (kExampleSpecs[s])))
                    continue;

                const auto& plan = result.value();

                expect (plan.drawnControlCount > 0,
                        jstr (kExampleSpecs[s]) + " / " + uiTemplateName (uiTemplate)
                            + " produced an empty interface");

                expectEquals (plan.drawnControlCount, static_cast<int> (plan.controls.size()));
                expectEquals (plan.groupCount, static_cast<int> (plan.groups.size()));

                // Identity comes from the spec, not from a constant.
                expectEquals (jstr (spec.pluginName), jstr (plan.title));
                expectEquals (jstr (spec.vendor), jstr (plan.vendor));
                expectEquals (jstr (spec.ui.accentColor), jstr (plan.accentColor));
            }
        }
    }

    //==========================================================================
    void testTemplatesAreNested()
    {
        beginTest ("Minimal is a subset of Standard, and Standard of Advanced");

        for (int s = 0; s < kNumExampleSpecs; ++s)
        {
            const auto spec = loadSpec (kExampleSpecs[s]);

            const auto minimal = idsOf (compileOrFail (*this, spec, UiTemplate::Minimal, "minimal"));
            const auto standard = idsOf (compileOrFail (*this, spec, UiTemplate::Standard, "standard"));
            const auto advanced = idsOf (compileOrFail (*this, spec, UiTemplate::Advanced, "advanced"));

            expect (isSubset (minimal, standard),
                    jstr (kExampleSpecs[s]) + ": Minimal drew a control Standard hides");
            expect (isSubset (standard, advanced),
                    jstr (kExampleSpecs[s]) + ": Standard drew a control Advanced hides");
        }
    }

    //==========================================================================
    void testTemplatesAreStrictlyDenser()
    {
        beginTest ("The three templates produce three genuinely different densities");

        for (int s = 0; s < kNumExampleSpecs; ++s)
        {
            const auto spec = loadSpec (kExampleSpecs[s]);

            const auto minimal = compileOrFail (*this, spec, UiTemplate::Minimal, "minimal");
            const auto standard = compileOrFail (*this, spec, UiTemplate::Standard, "standard");
            const auto advanced = compileOrFail (*this, spec, UiTemplate::Advanced, "advanced");

            // Every template is measured against the same available surface, so
            // the three numbers are directly comparable.
            expectEquals (minimal.availableControlCount(), advanced.availableControlCount());
            expectEquals (standard.availableControlCount(), advanced.availableControlCount());

            expect (minimal.drawnControlCount < standard.drawnControlCount,
                    jstr (kExampleSpecs[s]) + ": Minimal and Standard have the same density ("
                        + juce::String (minimal.drawnControlCount) + ")");

            expect (standard.drawnControlCount < advanced.drawnControlCount,
                    jstr (kExampleSpecs[s]) + ": Standard and Advanced have the same density ("
                        + juce::String (standard.drawnControlCount) + ")");

            expect (minimal.density() < standard.density(), jstr (kExampleSpecs[s]) + ": density not increasing");
            expect (standard.density() < advanced.density(), jstr (kExampleSpecs[s]) + ": density not increasing");

            expect (advanced.density() > 0.999 && advanced.density() < 1.001,
                    "Advanced should draw the whole available surface");

            // Advanced is the only template that reaches module-level controls.
            expect (advanced.moduleLevelControlCount > 0,
                    jstr (kExampleSpecs[s]) + ": Advanced added no module-level controls");
        }
    }

    //==========================================================================
    void testDeterministic()
    {
        beginTest ("Compiling the same spec twice produces a byte-identical plan");

        const UiTemplate templates[] = { UiTemplate::Minimal, UiTemplate::Standard, UiTemplate::Advanced };

        for (int s = 0; s < kNumExampleSpecs; ++s)
        {
            const auto spec = loadSpec (kExampleSpecs[s]);

            for (const auto uiTemplate : templates)
            {
                const auto first = compileOrFail (*this, spec, uiTemplate, "first");
                const auto second = compileOrFail (*this, spec, uiTemplate, "second");

                expectEquals (first.fingerprint(), second.fingerprint());
                expectEquals (jstr (first.toJsonText()), jstr (second.toJsonText()));
                expectEquals (jstr (first.describe()), jstr (second.describe()));

                // The same document parsed a second time must compile identically:
                // nothing may depend on how the spec was built in memory.
                const auto reparsed = loadSpec (kExampleSpecs[s]);
                const auto third = compileOrFail (*this, reparsed, uiTemplate, "reparsed");

                expectEquals (first.fingerprint(), third.fingerprint());
            }
        }

        // Different templates are genuinely different plans.
        const auto spec = loadSpec (kExampleSpecs[0]);
        expect (compileOrFail (*this, spec, UiTemplate::Minimal, "minimal").fingerprint()
                    != compileOrFail (*this, spec, UiTemplate::Standard, "standard").fingerprint(),
                "Minimal and Standard should not fingerprint equally");
        expect (compileOrFail (*this, spec, UiTemplate::Standard, "standard").fingerprint()
                    != compileOrFail (*this, spec, UiTemplate::Advanced, "advanced").fingerprint(),
                "Standard and Advanced should not fingerprint equally");
    }

    //==========================================================================
    void testHostParameterNumbering()
    {
        beginTest ("Host-visible controls are numbered densely, in spec order");

        for (int s = 0; s < kNumExampleSpecs; ++s)
        {
            const auto spec = loadSpec (kExampleSpecs[s]);
            const auto plan = compileOrFail (*this, spec, UiTemplate::Advanced, "advanced");

            std::map<int, std::string> byHostIndex;
            int expectedHostParameters = 0;

            for (const auto& control : plan.controls)
            {
                if (control.isMacro)
                {
                    expect (control.hostParameterIndex >= 0,
                            "a macro is automatable and must have a host index");
                    expectEquals (control.specParameterIndex, -1, "macros have no spec parameter");
                    byHostIndex[control.hostParameterIndex] = control.id;
                    ++expectedHostParameters;
                    continue;
                }

                if (control.moduleLevel)
                {
                    expectEquals (control.hostParameterIndex, -1,
                                  "module-level controls are not host parameters");
                    expect (control.kernelManaged == false,
                            "module-level controls must be persisted by the shell");
                    continue;
                }

                if (! control.exposedToHost)
                {
                    expectEquals (control.hostParameterIndex, -1,
                                  "internal parameters are not host parameters");
                    continue;
                }

                expect (control.hostParameterIndex >= 0,
                        "an exposed parameter must have a host index: " + jstr (control.id));

                byHostIndex[control.hostParameterIndex] = control.id;
                ++expectedHostParameters;
            }

            expectEquals (static_cast<int> (byHostIndex.size()), expectedHostParameters);

            const int macroCount = static_cast<int> (spec.macros.size());

            // Dense 0..n-1: macros first, then the spec's own exposed parameters.
            int next = 0;
            for (const auto& entry : byHostIndex)
            {
                expectEquals (entry.first, next, "host parameter indices must be dense");

                if (next < macroCount)
                {
                    expect (entry.second.rfind ("macro:", 0) == 0,
                            "the first host parameters must be the macros");
                }
                else
                {
                    const auto* parameter = spec.findParameter (entry.second);
                    expect (parameter != nullptr, "host parameter should exist in the spec");
                }

                ++next;
            }
        }
    }

    //==========================================================================
    void testStableIdentity()
    {
        beginTest ("Control identity is stable, unique and template independent");

        const auto spec = loadSpec (kExampleSpecs[0]);

        const auto minimal = compileOrFail (*this, spec, UiTemplate::Minimal, "minimal");
        const auto advanced = compileOrFail (*this, spec, UiTemplate::Advanced, "advanced");

        std::set<uint32_t> hashes;
        std::set<std::string> ids;

        for (const auto& control : advanced.controls)
        {
            expect (hashes.insert (control.stableIdHash).second,
                    "stable id collision in Advanced: " + jstr (control.id));
            expect (ids.insert (control.id).second,
                    "duplicate control id in Advanced: " + jstr (control.id));

            expectEquals (control.stableIdHash, stableIdHash32 (control.id),
                          "the hash must be a pure function of the control id");
        }

        // A control drawn under a different template keeps its identity: an
        // automation lane recorded against Minimal must still find its control.
        for (const auto& control : minimal.controls)
        {
            bool found = false;

            for (const auto& other : advanced.controls)
            {
                if (other.id == control.id)
                {
                    found = true;
                    expectEquals (other.stableIdHash, control.stableIdHash);
                    expectEquals (other.hostParameterIndex, control.hostParameterIndex);
                    break;
                }
            }

            expect (found, "Minimal control missing from Advanced: " + jstr (control.id));
        }

        // The known stable hash of a known id, so a change to the hash function
        // cannot silently invalidate every automation lane in the field.
        expectEquals (stableIdHash32 ("inputTrim"), 0xdf870863u);
    }

    //==========================================================================
    void testGroupsAreTotal()
    {
        beginTest ("Every drawn control belongs to exactly one non-empty group");

        for (int s = 0; s < kNumExampleSpecs; ++s)
        {
            const auto spec = loadSpec (kExampleSpecs[s]);
            const auto plan = compileOrFail (*this, spec, UiTemplate::Advanced, "advanced");

            int memberCount = 0;
            std::set<std::string> groupNames;

            for (const auto& group : plan.groups)
            {
                expect (! group.name.empty(), "a group must have a name");
                expect (groupNames.insert (group.name).second,
                        "duplicate group name: " + jstr (group.name));
                expect (group.controlIndices.size() > 0, "a drawn group must not be empty");

                for (const auto index : group.controlIndices)
                {
                    expect (index >= 0 && index < static_cast<int> (plan.controls.size()),
                            "group member index out of range");

                    if (index < 0 || index >= static_cast<int> (plan.controls.size()))
                        continue;

                    expectEquals (plan.controls[static_cast<size_t> (index)].group, group.name,
                                  "control is filed under the wrong group");

                    ++memberCount;
                }
            }

            expectEquals (memberCount, plan.drawnControlCount,
                          "every control must appear in exactly one group");

            // The design system's reading order: performance macros first.
            if (plan.macroCount > 0)
            {
                expect (plan.groups.size() > 0, "a plan with macros must have groups");
                expectEquals (jstr (plan.groups[0].name), jstr ("Macros"),
                              "macros must be the first group");
            }
        }
    }

    //==========================================================================
    void testControlKindsFollowModuleMetadata()
    {
        beginTest ("Control kinds come from the module manifest, not from the spec's text");

        // zarlino.saturation declares a 6-position stepped "mode" parameter, so
        // the spec's driveMode must be drawn as a Select.
        const auto saturation = loadSpec ("saturation.zfactory.json");
        const auto plan = compileOrFail (*this, saturation, UiTemplate::Standard, "standard");

        bool sawSelect = false;

        for (const auto& control : plan.controls)
        {
            if (control.id == "driveMode")
            {
                sawSelect = true;
                expectEquals (static_cast<int> (control.kind), static_cast<int> (UiControlKind::Select));
                expectEquals (control.stepCount, 6);
            }

            if (control.id == "driveAmount")
                expect (control.kind == UiControlKind::Knob || control.kind == UiControlKind::Fader,
                        "a continuous parameter must not be drawn as a switch or select");
        }

        expect (sawSelect, "the saturation spec should expose driveMode");

        // zarlino.gain declares a 2-position stepped "polarity" the channel strip
        // does not expose; Advanced surfaces it, and it must be a Switch.
        const auto channelStrip = loadSpec ("channel-strip.zfactory.json");
        const auto advanced = compileOrFail (*this, channelStrip, UiTemplate::Advanced, "advanced");

        bool sawSwitch = false;
        bool sawModuleLevelFader = false;

        for (const auto& control : advanced.controls)
        {
            if (control.id == "input.polarity")
            {
                sawSwitch = true;
                expectEquals (static_cast<int> (control.kind), static_cast<int> (UiControlKind::Switch));
                expectEquals (control.stepCount, 2);
            }

            if (control.moduleLevel && control.kind == UiControlKind::Fader)
                sawModuleLevelFader = true;
        }

        expect (sawSwitch, "Advanced should surface the gain module's polarity switch");
        expect (sawModuleLevelFader, "Advanced should surface a logarithmic module parameter as a fader");
    }

    //==========================================================================
    void testModuleLevelControlsOnlyInAdvanced()
    {
        beginTest ("Only Advanced draws module-level or internal controls");

        for (int s = 0; s < kNumExampleSpecs; ++s)
        {
            const auto spec = loadSpec (kExampleSpecs[s]);

            const auto minimal = compileOrFail (*this, spec, UiTemplate::Minimal, "minimal");
            const auto standard = compileOrFail (*this, spec, UiTemplate::Standard, "standard");

            // The candidate set is a property of the spec and the catalog, so
            // every template reports the same available surface.
            expectEquals (minimal.moduleLevelControlCount, standard.moduleLevelControlCount);

            for (const auto* plan : { &minimal, &standard })
                for (const auto& control : plan->controls)
                {
                    expect (! control.moduleLevel, "a non-Advanced template drew a module-level control");
                    expect (control.exposedToHost, "a non-Advanced template drew a non-automatable control");
                }

            // Every module-level control in Advanced really does come from a
            // certified manifest and really is unowned by the spec.
            const auto advanced = compileOrFail (*this, spec, UiTemplate::Advanced, "advanced");

            for (const auto& control : advanced.controls)
            {
                if (! control.moduleLevel)
                    continue;

                expect (control.moduleIndex >= 0 && control.moduleIndex < static_cast<int> (spec.modules.size()),
                        "module-level control with a bad module index");

                const auto* instance = &spec.modules[static_cast<size_t> (control.moduleIndex)];
                const auto entry = builtinRegistry().resolve (instance->moduleId, instance->moduleVersion,
                                                              CertificationState::Certified);

                expect (entry.ok(), "module-level control came from an unresolvable module");

                if (! entry.ok())
                    continue;

                expect (control.moduleParameterIndex >= 0
                            && control.moduleParameterIndex < static_cast<int> (entry.value()->manifest.parameters.size()),
                        "module-level control with a bad parameter index");

                const auto& parameter = entry.value()->manifest.parameters[static_cast<size_t> (control.moduleParameterIndex)];
                expectEquals (control.minValue, parameter.minValue);
                expectEquals (control.maxValue, parameter.maxValue);
                expectEquals (control.defaultValue, parameter.defaultValue);

                // No spec parameter may target it - that is what makes it
                // module-level in the first place.
                for (const auto& specParameter : spec.parameters)
                    expect (! (specParameter.targetInstanceId == instance->instanceId
                               && specParameter.targetParameterId == parameter.id),
                            "a covered module parameter was added twice");
            }
        }
    }

    //==========================================================================
    void testRegistryFailureIsPropagated()
    {
        beginTest ("A spec that cannot be built cannot be drawn");

        auto spec = makeBaseSpec();
        addModule (spec, "gain", "zarlino.gain");
        addParameter (spec, "trim", "gain.gainDb", -18.0f, 18.0f, 0.0f, "dB");

        // An unknown module id.
        auto unknown = spec;
        unknown.modules[0].moduleId = "zarlino.does.not.exist";

        const auto unknownResult = UiPlan::compile (unknown, builtinRegistry(), UiTemplate::Standard);
        expect (! unknownResult.ok(), "an unknown module must fail the plan");
        expectEquals (static_cast<int> (unknownResult.error().code), static_cast<int> (ErrorCode::UnknownModule));

        // A version the registry cannot satisfy.
        auto wrongVersion = spec;
        parseSemVer ("9.0.0", wrongVersion.modules[0].moduleVersion);

        const auto versionResult = UiPlan::compile (wrongVersion, builtinRegistry(), UiTemplate::Standard);
        expect (! versionResult.ok(), "an incompatible module version must fail the plan");
        expectEquals (static_cast<int> (versionResult.error().code),
                      static_cast<int> (ErrorCode::ModuleVersionIncompatible));

        // A dangling parameter target still resolves a module, but produces no
        // module-level match; the plan must still be produced.
        auto dangling = spec;
        dangling.parameters[0].targetParameterId = "doesNotExist";
        expectSuccess (*this, UiPlan::compile (dangling, builtinRegistry(), UiTemplate::Standard), "dangling target");
    }

    //==========================================================================
    void testNoUserTextBecomesAValue()
    {
        beginTest ("Spec text can never become a value, a range or a code path");

        // Hostile-looking display text is carried as a label and nothing else.
        auto spec = makeBaseSpec();
        addModule (spec, "gain", "zarlino.gain");
        addParameter (spec, "trim", "gain.gainDb", -18.0f, 18.0f, 0.0f, "dB");
        spec.parameters[0].displayName = "Trim\"; system(\"calc\"); //";
        spec.ui.category = "<script>alert(1)</script>";

        const auto result = UiPlan::compile (spec, builtinRegistry(), UiTemplate::Standard);
        expectSuccess (*this, result, "hostile label");

        if (! result.ok())
            return;

        const auto& plan = result.value();

        expectEquals (jstr (plan.controls[0].label), jstr (spec.parameters[0].displayName),
                      "the label must survive verbatim as data");

        // ...and the numeric domain still comes from the spec's own range, never
        // from the text.
        expectEquals (plan.controls[0].minValue, -18.0f);
        expectEquals (plan.controls[0].maxValue, 18.0f);
        expectEquals (static_cast<int> (plan.controls[0].kind), static_cast<int> (UiControlKind::Knob));

        // The serialized plan is JSON, so the label is escaped by the serializer;
        // it can never terminate the document.
        const auto text = plan.toJsonText();
        expect (text.find ('\n') == std::string::npos, "the plan JSON must be a single line");

        auto parsed = juce::JSON::parse (juce::String (text));
        expect (! parsed.isVoid(), "the plan must remain valid JSON after a hostile label");
    }
};

UiPlanTests uiPlanTests;

} // namespace

void registerUiPlanTests() {}
