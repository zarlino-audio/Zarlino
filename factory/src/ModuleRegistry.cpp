#include "zfactory/ModuleRegistry.h"
#include "zfactory/JsonUtil.h"

#include <algorithm>
#include <set>

namespace zfactory
{

namespace
{

FactoryError validateManifest (const ModuleManifest& manifest)
{
    if (! json::isValidIdentifier (manifest.id))
        return makeError (ErrorCode::InvalidIdentifier, "manifest/id",
                          "module id '" + manifest.id + "' is not a valid identifier");

    // A version must be an explicit MAJOR.MINOR.PATCH; "0.0.0" is reserved to
    // mean "unversioned" and is rejected so that version comparisons are
    // meaningful.
    if (manifest.version.major == 0 && manifest.version.minor == 0 && manifest.version.patch == 0)
        return makeError (ErrorCode::UnsupportedValue, "manifest/version",
                          "module '" + manifest.id + "' declares version 0.0.0");

    if (manifest.displayName.empty())
        return makeError (ErrorCode::MissingRequiredField, "manifest/displayName",
                          "module '" + manifest.id + "' has no display name");

    if (manifest.supportedChannels.empty())
        return makeError (ErrorCode::EmptyCollection, "manifest/supportedChannels",
                          "module '" + manifest.id + "' declares no supported channel configurations");

    if (! manifest.realtimeSafe)
        return makeError (ErrorCode::UnsupportedValue, "manifest/realtimeSafe",
                          "module '" + manifest.id + "' is not realtime safe and cannot be certified");

    if (manifest.referenceLatencySamples < 0)
        return makeError (ErrorCode::ValueOutOfRange, "manifest/referenceLatencySamples",
                          "module '" + manifest.id + "' declares a negative latency");

    std::set<std::string> parameterIds;

    for (size_t i = 0; i < manifest.parameters.size(); ++i)
    {
        const auto& parameter = manifest.parameters[i];
        const std::string path = "manifest/parameters[" + std::to_string (i) + "]";

        if (! json::isValidIdentifier (parameter.id))
            return makeError (ErrorCode::InvalidIdentifier, path + "/id",
                              "module '" + manifest.id + "' has an invalid parameter id");

        if (! parameterIds.insert (parameter.id).second)
            return makeError (ErrorCode::DuplicateIdentifier, path + "/id",
                              "module '" + manifest.id + "' declares parameter '" + parameter.id
                                  + "' more than once");

        if (! (parameter.minValue < parameter.maxValue))
            return makeError (ErrorCode::ValueOutOfRange, path + "/minValue",
                              "module '" + manifest.id + "' parameter '" + parameter.id
                                  + "' has an empty value domain");

        if (parameter.defaultValue < parameter.minValue || parameter.defaultValue > parameter.maxValue)
            return makeError (ErrorCode::ValueOutOfRange, path + "/defaultValue",
                              "module '" + manifest.id + "' parameter '" + parameter.id
                                  + "' default lies outside its domain");

        if (parameter.logarithmic && parameter.minValue <= 0.0f)
            return makeError (ErrorCode::ValueOutOfRange, path + "/logarithmic",
                              "module '" + manifest.id + "' parameter '" + parameter.id
                                  + "' is logarithmic but its domain is not strictly positive");

        if (parameter.stepped && parameter.stepCount < 2)
            return makeError (ErrorCode::ValueOutOfRange, path + "/stepCount",
                              "module '" + manifest.id + "' parameter '" + parameter.id
                                  + "' is stepped but declares fewer than 2 steps");

        if (parameter.smoothingMs < 0.0f || parameter.smoothingMs > 1000.0f)
            return makeError (ErrorCode::ValueOutOfRange, path + "/smoothingMs",
                              "module '" + manifest.id + "' parameter '" + parameter.id
                                  + "' has an out-of-range smoothing time");
    }

    // Sample-rate domains must be sane.
    for (size_t i = 0; i < manifest.supportedSampleRates.size(); ++i)
    {
        const double rate = manifest.supportedSampleRates[i];

        if (! (rate >= kMinSampleRate && rate <= kMaxSampleRate))
            return makeError (ErrorCode::ValueOutOfRange,
                              "manifest/supportedSampleRates[" + std::to_string (i) + "]",
                              "module '" + manifest.id + "' declares an out-of-range sample rate");
    }

    for (size_t i = 0; i < manifest.dependencies.size(); ++i)
        if (! json::isValidIdentifier (manifest.dependencies[i]))
            return makeError (ErrorCode::InvalidIdentifier,
                              "manifest/dependencies[" + std::to_string (i) + "]",
                              "module '" + manifest.id + "' declares an invalid dependency id");

    if (manifest.dependencies.end() != std::find (manifest.dependencies.begin(),
                                                  manifest.dependencies.end(), manifest.id))
        return makeError (ErrorCode::UnsupportedValue, "manifest/dependencies",
                          "module '" + manifest.id + "' depends on itself");

    return FactoryError();
}

} // namespace

//==============================================================================
FactoryError ModuleRegistry::add (ModuleDescriptor descriptor)
{
    if (! descriptor.valid())
        return makeError (ErrorCode::UnsupportedValue, "registry/add",
                          "module descriptor has no factory function or no id");

    if (auto error = validateManifest (descriptor.manifest); error.failed())
        return error;

    for (const auto& existing : m_entries)
    {
        if (existing.manifest.id == descriptor.manifest.id
            && existing.manifest.version == descriptor.manifest.version)
            return makeError (ErrorCode::ModuleAlreadyRegistered, "registry/add",
                              "module '" + descriptor.manifest.id + "' version "
                                  + descriptor.manifest.version.to_string() + " is already registered");
    }

    Entry entry;
    entry.manifest = std::move (descriptor.manifest);
    entry.create = descriptor.create;

    m_entries.push_back (std::move (entry));
    sort();

    return FactoryError();
}

void ModuleRegistry::sort()
{
    std::stable_sort (m_entries.begin(), m_entries.end(),
                      [] (const Entry& a, const Entry& b)
                      {
                          if (a.manifest.id != b.manifest.id)
                              return a.manifest.id < b.manifest.id;

                          return a.manifest.version < b.manifest.version;
                      });
}

const ModuleRegistry::Entry* ModuleRegistry::find (const std::string& moduleId) const
{
    const Entry* best = nullptr;

    for (const auto& entry : m_entries)
    {
        if (entry.manifest.id != moduleId)
            continue;

        if (best == nullptr || best->manifest.version < entry.manifest.version)
            best = &entry;
    }

    return best;
}

Result<const ModuleRegistry::Entry*> ModuleRegistry::resolve (
    const std::string& moduleId,
    const SemVer& requestedVersion,
    CertificationState requiredCertification) const
{
    const Entry* best = nullptr;

    for (const auto& entry : m_entries)
    {
        if (entry.manifest.id != moduleId)
            continue;

        if (! isVersionCompatible (requestedVersion, entry.manifest.version))
            continue;

        if (best == nullptr || best->manifest.version < entry.manifest.version)
            best = &entry;
    }

    if (best == nullptr)
    {
        // Distinguish "never heard of it" from "wrong version" so the caller
        // gets an actionable error.
        const Entry* anyVersion = find (moduleId);

        if (anyVersion == nullptr)
            return Result<const Entry*>::failure (
                makeError (ErrorCode::UnknownModule, "$",
                           "no module registered with id '" + moduleId + "'"));

        return Result<const Entry*>::failure (
            makeError (ErrorCode::ModuleVersionIncompatible, "$",
                       "module '" + moduleId + "' version " + requestedVersion.to_string()
                           + " is incompatible with the registered version "
                           + anyVersion->manifest.version.to_string()
                           + " (major versions must match and the registry must not be older)"));
    }

    if (! meetsCertification (best->manifest.certification, requiredCertification))
        return Result<const Entry*>::failure (
            makeError (ErrorCode::ModuleNotCertified, "$",
                       "module '" + moduleId + "' is " + certificationStateName (best->manifest.certification)
                           + " but the requested policy requires "
                           + certificationStateName (requiredCertification)));

    for (const auto& dependency : best->manifest.dependencies)
    {
        if (find (dependency) == nullptr)
            return Result<const Entry*>::failure (
                makeError (ErrorCode::ModuleDependencyMissing, "$",
                           "module '" + moduleId + "' depends on unregistered module '"
                               + dependency + "'"));
    }

    return Result<const Entry*>::success (best);
}

uint64_t ModuleRegistry::catalogFingerprint() const
{
    constexpr uint64_t kSeed = 1469598103934665603ull;

    uint64_t h = hashInt (kSeed, static_cast<int64_t> (m_entries.size()));

    for (const auto& entry : m_entries)
        h ^= entry.manifest.fingerprint();

    return h;
}

std::string ModuleRegistry::describeCatalog() const
{
    std::string text;

    for (const auto& entry : m_entries)
    {
        text += entry.manifest.id;
        text += " ";
        text += entry.manifest.version.to_string();
        text += " [";
        text += certificationStateName (entry.manifest.certification);
        text += "] ";
        text += moduleCategoryName (entry.manifest.category);
        text += " latency=";
        text += std::to_string (entry.manifest.referenceLatencySamples);
        text += " params=";
        text += std::to_string (entry.manifest.parameters.size());
        text += "\n";
    }

    return text;
}

//==============================================================================
ModuleRegistry& builtinRegistry()
{
    // Process-lifetime singleton. Built through a pointer because ModuleRegistry
    // is deliberately non-copyable and non-movable: its entries are sorted on
    // insertion so that no catalogue consumer can observe a partial order.
    static ModuleRegistry* registry = []() -> ModuleRegistry*
    {
        auto* created = new ModuleRegistry();

        // A failure here would be a programming error in the built-in library;
        // registerBuiltinModules() is asserted by the factory test suite and by
        // the CLI tool.
        (void) registerBuiltinModules (*created);
        return created;
    }();

    return *registry;
}

} // namespace zfactory
