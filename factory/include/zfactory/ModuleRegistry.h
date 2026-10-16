#pragma once

#include "zfactory/DspModule.h"
#include "zfactory/Error.h"

#include <memory>
#include <string>
#include <vector>

namespace zfactory
{

//==============================================================================
// ModuleRegistry - resolves certified DSP modules by stable id and version.
//
// The registry is a plain, deterministic container: entries are kept sorted by
// (id, version) so that iteration order, and therefore every fingerprint and
// error message, never depends on registration order.
//==============================================================================
class ModuleRegistry
{
public:
    struct Entry
    {
        ModuleManifest manifest;
        DspModule* (*create)() = nullptr;
    };

    ModuleRegistry() = default;

    ModuleRegistry (const ModuleRegistry&) = delete;
    ModuleRegistry& operator= (const ModuleRegistry&) = delete;

    /** Register a module. Rejects an invalid manifest, an unusable factory
        pointer, a duplicate (id, version) pair, and a module that declares
        itself not realtime safe. */
    FactoryError add (ModuleDescriptor descriptor);

    /** Resolve `moduleId` at (or above) `requestedVersion`.
        Rejects: unknown module id, incompatible version, module below the
        certification policy, and unsatisfied module dependencies. */
    Result<const Entry*> resolve (const std::string& moduleId,
                                  const SemVer& requestedVersion,
                                  CertificationState requiredCertification) const;

    /** Best available version of `moduleId` regardless of version request. */
    const Entry* find (const std::string& moduleId) const;

    const std::vector<Entry>& entries() const noexcept { return m_entries; }
    size_t size() const noexcept { return m_entries.size(); }
    bool empty() const noexcept { return m_entries.empty(); }

    /** XOR-fold of every manifest fingerprint, in sorted catalog order. */
    uint64_t catalogFingerprint() const;

    /** Deterministic, human readable module catalogue (used by the CLI tool
        and by CI evidence). */
    std::string describeCatalog() const;

private:
    void sort();

    std::vector<Entry> m_entries;
};

/** Registers the Zarlino factory module library. Returns the number of modules
    added, or a deterministic error describing the first rejected module. */
Result<size_t> registerBuiltinModules (ModuleRegistry& registry);

/** The process-wide built-in catalogue (certified modules only). */
ModuleRegistry& builtinRegistry();

} // namespace zfactory
