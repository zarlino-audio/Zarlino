#include "zfactory/shell/ProductRuntime.h"

#include "zfactory/JsonUtil.h"
#include "zfactory/Version.h"

#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <cctype>

namespace zfactory
{
namespace shell
{

namespace
{

/** The canonical serialisation of a spec: single line, keys in the order the
    kernel emits them. Everything that hashes or embeds a spec goes through here,
    so "the same plugin" always has the same bytes. */
std::string canonicalSpecJson (const PluginSpec& spec)
{
    return juce::JSON::toString (spec.toJson(), true).toStdString();
}

std::string sha256Hex (const std::string& text)
{
    return juce::SHA256 (text.data(), text.size()).toHexString().toStdString();
}

/** Kernel fingerprints are rendered as 16 lower-case hex digits, zero padded. */
std::string hex64 (uint64_t value)
{
    juce::String text ("0x");
    text << juce::String::toHexString (static_cast<juce::int64> (value)).paddedLeft ('0', 16);
    return text.toLowerCase().toStdString();
}

bool sameFingerprint (const std::string& recorded, uint64_t actual)
{
    if (recorded.empty())
        return false;

    return recorded == hex64 (actual) || recorded == hex64 (actual).substr (2);
}

std::string varToString (const juce::var& object, const char* key)
{
    if (auto* dynamic = object.getDynamicObject())
        return dynamic->getProperty (key).toString().toStdString();

    return {};
}

} // namespace

//==============================================================================

//==============================================================================
Result<ProductRuntime> ProductRuntime::create()
{
    ProductRuntime product;

    const auto fail = [] (ErrorCode code, std::string path, std::string message)
    {
        return Result<ProductRuntime>::failure (makeError (code, std::move (path), std::move (message)));
    };

    //--------------------------------------------------------------------------
    // 1. The embedded document must validate exactly as a spec from disk would.
    //--------------------------------------------------------------------------
    const std::string specJson (kGeneratedSpecJson);
    const auto specResult = PluginSpec::fromJsonText (specJson);

    if (! specResult.ok())
    {
        product.m_error = specResult.error();
        return Result<ProductRuntime>::failure (product.m_error);
    }

    product.m_spec = specResult.value();

    // Canonical-form check: re-serialising the validated spec must reproduce the
    // embedded bytes exactly. If it does not, the embedded document was not in
    // canonical form and every hash taken over it - including the one recorded in
    // provenance - would be meaningless.
    if (canonicalSpecJson (product.m_spec) != specJson)
        return fail (ErrorCode::UnsupportedValue, "$",
                     "embedded specification is not in canonical form; the artifact is not reproducible");

    //--------------------------------------------------------------------------
    // 2. Provenance must parse and describe this same product.
    //--------------------------------------------------------------------------
    {
        const std::string provenanceJson (kGeneratedProvenanceJson);
        juce::var root;
        const auto parseResult = juce::JSON::parse (juce::String::fromUTF8 (provenanceJson.c_str()), root);
        auto* dynamic = root.getDynamicObject();

        if (parseResult.failed() || dynamic == nullptr)
            return fail (ErrorCode::JsonParseFailed, "provenance", "embedded provenance is not a JSON object");

        auto& provenance = product.m_provenance;

        provenance.schemaVersion = static_cast<int> (dynamic->getProperty ("provenanceSchemaVersion"));
        provenance.productId = varToString (root, "productId");
        provenance.productName = varToString (root, "productName");
        provenance.productVersion = varToString (root, "productVersion");
        provenance.uiTemplate = varToString (root, "uiTemplate");
        provenance.specSha256 = varToString (root, "specSha256");
        provenance.specFingerprint = varToString (root, "specFingerprint");
        provenance.uiPlanFingerprint = varToString (root, "uiPlanFingerprint");
        provenance.catalogFingerprint = varToString (root, "catalogFingerprint");
        provenance.kernelVersion = varToString (root, "kernelVersion");
        provenance.factorySchemaVersion = static_cast<int> (dynamic->getProperty ("factorySchemaVersion"));
        provenance.sourceCommit = varToString (root, "sourceCommit");
        provenance.buildId = varToString (root, "buildId");
        provenance.buildTimestamp = varToString (root, "buildTimestamp");
        provenance.targetPlatform = varToString (root, "targetPlatform");
        provenance.generatorVersion = varToString (root, "generatorVersion");

        for (const auto& item : *dynamic->getProperty ("modules").getArray())
        {
            Provenance::ModuleRef module;
            module.instanceId = varToString (item, "instanceId");
            module.moduleId = varToString (item, "moduleId");
            module.moduleVersion = varToString (item, "moduleVersion");
            module.manifestFingerprint = varToString (item, "manifestFingerprint");
            provenance.modules.push_back (std::move (module));
        }
    }

    //--------------------------------------------------------------------------
    // 3. Integrity: the embedded document must hash to what was recorded.
    //--------------------------------------------------------------------------
    if (product.m_provenance.specSha256 != sha256Hex (specJson))
        return fail (ErrorCode::UnsupportedValue, "provenance/specSha256",
                     "the embedded specification does not match the hash recorded at build time");

    //--------------------------------------------------------------------------
    // 4. Reproducibility: recompiling the plan from the spec must reproduce the
    //    plan the build recorded. A product whose interface cannot be rebuilt
    //    from its own specification must not load.
    //--------------------------------------------------------------------------
    UiTemplate uiTemplate = UiTemplate::Standard;

    if (! parseUiTemplate (product.m_provenance.uiTemplate, uiTemplate))
        return fail (ErrorCode::UnsupportedValue, "provenance/uiTemplate",
                     "unknown UI template '" + product.m_provenance.uiTemplate + "'");

    const auto planResult = UiPlan::compile (product.m_spec, builtinRegistry(), uiTemplate);

    if (! planResult.ok())
    {
        product.m_error = planResult.error();
        return Result<ProductRuntime>::failure (product.m_error);
    }

    product.m_plan = planResult.value();

    if (! sameFingerprint (product.m_provenance.uiPlanFingerprint, product.m_plan.fingerprint()))
        return fail (ErrorCode::UnsupportedValue, "provenance/uiPlanFingerprint",
                     "the interface recorded at build time cannot be reproduced from the specification");

    // The recorded plan document itself must match too - it is the evidence a
    // reviewer reads, so it may not drift from what the binary draws.
    {
        const std::string recordedPlan (kGeneratedUiPlanJson);

        if (recordedPlan != product.m_plan.toJsonText())
            return fail (ErrorCode::UnsupportedValue, "uiPlan",
                         "the recorded interface plan does not match the interface this build reproduces");
    }

    //--------------------------------------------------------------------------
    // 5. Identity: provenance and specification must agree.
    //--------------------------------------------------------------------------
    if (product.m_provenance.productId != product.m_spec.pluginId)
        return fail (ErrorCode::UnsupportedValue, "provenance/productId",
                     "provenance product id does not match the specification");

    if (product.m_provenance.productVersion != product.m_spec.pluginVersion)
        return fail (ErrorCode::UnsupportedValue, "provenance/productVersion",
                     "provenance product version does not match the specification");

    if (! sameFingerprint (product.m_provenance.specFingerprint, product.m_spec.fingerprint()))
        return fail (ErrorCode::UnsupportedValue, "provenance/specFingerprint",
                     "provenance spec fingerprint does not match the specification");

    if (! sameFingerprint (product.m_provenance.catalogFingerprint, builtinRegistry().catalogFingerprint()))
        return fail (ErrorCode::UnsupportedValue, "provenance/catalogFingerprint",
                     "this build was made against a different module catalogue than the one it links");

    product.m_valid = true;
    return Result<ProductRuntime>::success (std::move (product));
}

//==============================================================================
std::string ProductRuntime::buildStamp() const
{
    std::string stamp = m_spec.pluginId + " v" + m_spec.pluginVersion;

    if (! m_provenance.buildId.empty())
        stamp += " build " + m_provenance.buildId;

    if (! m_provenance.sourceCommit.empty())
        stamp += " @ " + m_provenance.sourceCommit.substr (0, std::min<size_t> (7, m_provenance.sourceCommit.size()));

    return stamp;
}

std::string ProductRuntime::chainDescription() const
{
    std::string text;

    for (const auto& module : m_provenance.modules)
    {
        if (! text.empty())
            text += " -> ";

        text += module.moduleId;
    }

    return text;
}

} // namespace shell
} // namespace zfactory
