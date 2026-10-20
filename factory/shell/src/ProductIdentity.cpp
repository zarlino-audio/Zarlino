//==============================================================================
// Product identity and the build provenance record.
//
// Separate from ProductRuntime.cpp on purpose: these are pure transformations
// with no dependency on the generated payload, so the project generator can use
// them without linking a product's embedded specification. One implementation
// is therefore shared by the generator and by every plugin the generator
// produces, and the two can never disagree about a product's identity or about
// the shape of a provenance record.
//==============================================================================

#include "zfactory/shell/ProductRuntime.h"

#include <cctype>

namespace zfactory
{
namespace shell
{

//==============================================================================
std::string slugFor (const std::string& pluginId)
{
    std::string slug;
    slug.reserve (pluginId.size());

    for (const auto c : pluginId)
    {
        const auto lower = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));

        if ((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9'))
            slug.push_back (lower);
        else if (! slug.empty() && slug.back() != '-')
            slug.push_back ('-');
    }

    while (! slug.empty() && slug.back() == '-')
        slug.pop_back();

    return slug.empty() ? "product" : slug;
}

std::string bundleIdFor (const std::string& pluginId)
{
    return "com.zarlinoaudio." + slugFor (pluginId);
}

std::string pluginCodeFor (const std::string& pluginId)
{
    // The plugin code must be four ASCII alphanumerics and must be unique per
    // product, because the host identifies the plugin by it. It is derived from
    // the plugin id rather than assigned by hand, so two products can never be
    // issued the same code by accident and no human has to remember one.
    const auto hash = stableIdHash32 (pluginId);

    constexpr const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    std::string code;
    code.reserve (4);
    code.push_back ('Z');

    for (int i = 0; i < 3; ++i)
        code.push_back (alphabet[(hash >> (i * 5)) & 0x1fu]);

    return code;
}

//==============================================================================
juce::var Provenance::toJson() const
{
    auto* root = new juce::DynamicObject();

    root->setProperty ("provenanceSchemaVersion", schemaVersion);
    root->setProperty ("productId", juce::String (productId));
    root->setProperty ("productName", juce::String (productName));
    root->setProperty ("productVersion", juce::String (productVersion));
    root->setProperty ("uiTemplate", juce::String (uiTemplate));
    root->setProperty ("specSha256", juce::String (specSha256));
    root->setProperty ("specFingerprint", juce::String (specFingerprint));
    root->setProperty ("uiPlanFingerprint", juce::String (uiPlanFingerprint));
    root->setProperty ("catalogFingerprint", juce::String (catalogFingerprint));
    root->setProperty ("kernelVersion", juce::String (kernelVersion));
    root->setProperty ("factorySchemaVersion", factorySchemaVersion);
    root->setProperty ("sourceCommit", juce::String (sourceCommit));
    root->setProperty ("buildId", juce::String (buildId));
    root->setProperty ("buildTimestamp", juce::String (buildTimestamp));
    root->setProperty ("targetPlatform", juce::String (targetPlatform));
    root->setProperty ("generatorVersion", juce::String (generatorVersion));

    juce::Array<juce::var> moduleArray;

    for (const auto& module : modules)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("instanceId", juce::String (module.instanceId));
        object->setProperty ("moduleId", juce::String (module.moduleId));
        object->setProperty ("moduleVersion", juce::String (module.moduleVersion));
        object->setProperty ("manifestFingerprint", juce::String (module.manifestFingerprint));
        moduleArray.add (juce::var (object));
    }

    root->setProperty ("modules", moduleArray);

    return juce::var (root);
}

std::string Provenance::toJsonText() const
{
    return juce::JSON::toString (toJson(), true).toStdString();
}

} // namespace shell
} // namespace zfactory
