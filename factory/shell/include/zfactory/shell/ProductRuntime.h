#pragma once

#include "zfactory/Error.h"
#include "zfactory/ModuleRegistry.h"
#include "zfactory/PluginSpec.h"
#include "zfactory/UiPlan.h"

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

namespace zfactory
{
namespace shell
{

//==============================================================================
// The universal Zarlino plugin shell.
//
// ONE hand-written JUCE plugin implementation serves every generated product.
// A product is a *data* difference, never a code difference:
//
//   generated/ProductData.cpp              <- written by ZarlinoFactory_Generate
//     kGeneratedSpecJson                     the canonical PluginSpec document
//     kGeneratedUiPlanJson                   the UI plan compiled at build time
//     kGeneratedProvenanceJson               build provenance
//
//                  +  this shell (processor, editor, three UI templates, QA)
//
//                  =  an installable Windows VST3
//
// Nothing in the generated translation unit is executable logic: three string
// literals. The shell refuses to run if the plan it recompiles from the spec
// does not reproduce the plan that was recorded at build time, so a product that
// is not reproducible cannot load.
//==============================================================================

//==============================================================================
/** The three data symbols a generated product must define. */
extern const char* const kGeneratedSpecJson;
extern const char* const kGeneratedUiPlanJson;
extern const char* const kGeneratedProvenanceJson;

//==============================================================================
/** Build provenance, as recorded by the generator and enriched by the build. */
struct Provenance
{
    int schemaVersion = 1;

    std::string productId;
    std::string productName;
    std::string productVersion;
    std::string uiTemplate;               // "minimal" | "standard" | "advanced"

    std::string specSha256;
    std::string specFingerprint;          // "0x...."
    std::string uiPlanFingerprint;
    std::string catalogFingerprint;

    std::string kernelVersion;
    int factorySchemaVersion = 0;

    struct ModuleRef
    {
        std::string instanceId;
        std::string moduleId;
        std::string moduleVersion;
        std::string manifestFingerprint;
    };

    std::vector<ModuleRef> modules;

    std::string sourceCommit;
    std::string buildId;
    std::string buildTimestamp;
    std::string targetPlatform;
    std::string generatorVersion;

    juce::var toJson() const;
    std::string toJsonText() const;
};

//==============================================================================
/** The parsed, validated product this binary *is*.
 *
 *  Constructed once per process. `create()` fails - and the plugin refuses to
 *  initialise - when the embedded spec does not validate, when the recorded UI
 *  plan cannot be reproduced from it, or when provenance and specification
 *  disagree about identity.
 */
class ProductRuntime
{
public:
    ProductRuntime() = default;
    ProductRuntime (const ProductRuntime&) = default;
    ProductRuntime (ProductRuntime&&) noexcept = default;
    ProductRuntime& operator= (const ProductRuntime&) = delete;
    ProductRuntime& operator= (ProductRuntime&&) noexcept = default;

    static Result<ProductRuntime> create();

    //--------------------------------------------------------------------------
    bool isValid() const noexcept { return m_valid; }
    const FactoryError& error() const noexcept { return m_error; }

    const PluginSpec& spec() const noexcept { return m_spec; }
    const UiPlan& plan() const noexcept { return m_plan; }
    const Provenance& provenance() const noexcept { return m_provenance; }

    UiTemplate uiTemplate() const noexcept { return m_plan.uiTemplate; }

    // ---- identity, resolved from the spec ------------------------------------
    const std::string& pluginId() const noexcept { return m_spec.pluginId; }
    const std::string& name() const noexcept { return m_spec.pluginName; }
    const std::string& vendor() const noexcept { return m_spec.vendor; }
    const std::string& version() const noexcept { return m_spec.pluginVersion; }
    const std::string& category() const noexcept { return m_spec.ui.category; }
    const std::string& description() const noexcept { return m_spec.ui.shortDescription; }
    const std::string& accentColor() const noexcept { return m_spec.ui.accentColor; }
    int channelCount() const noexcept { return m_spec.channelCount(); }

    /** Deterministic human-readable build stamp, e.g.
        "zarlino.example.channel-strip v1.0.0 build 20261016-… @ abc1234". */
    std::string buildStamp() const;

    /** One-line description of the constructed chain (evidence / editor footer). */
    std::string chainDescription() const;

    /** True when this product is unlicensed (trial / expired): the shell must
        stay usable but must not advertise itself as purchased. */
    bool isTrial() const noexcept { return m_trial; }
    void setTrial (bool trial) noexcept { m_trial = trial; }

private:
    bool m_valid = false;
    bool m_trial = true;
    FactoryError m_error;
    PluginSpec m_spec;
    UiPlan m_plan;
    Provenance m_provenance;
};

//==============================================================================
/** Deterministic four-character VST3/AU plugin code derived from a plugin id.
    Always four ASCII alphanumerics, always starting with a letter, so a product
    id can never collide with, or escape from, the plugin-code namespace. */
std::string pluginCodeFor (const std::string& pluginId);

/** Deterministic bundle identifier derived from a plugin id. */
std::string bundleIdFor (const std::string& pluginId);

/** Lower-case, path-safe slug of a plugin id (used for file names). */
std::string slugFor (const std::string& pluginId);

} // namespace shell
} // namespace zfactory
