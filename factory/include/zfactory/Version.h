#pragma once

//==============================================================================
// Zarlino Plugin Factory - kernel version constants
//
// SCHEMA_VERSION is the version of the PluginSpec JSON document format. It is
// embedded in every spec as "factorySchemaVersion" and in every serialized
// engine state blob. A spec or state blob carrying a schema version the kernel
// does not support is rejected with a deterministic error - never silently
// coerced.
//
// KERNEL_VERSION identifies the factory kernel implementation itself.
//==============================================================================

namespace zfactory
{
constexpr int kSchemaVersion = 1;

constexpr int kKernelVersionMajor = 1;
constexpr int kKernelVersionMinor = 0;
constexpr int kKernelVersionPatch = 0;

constexpr const char* kKernelVersionString = "1.0.0";

// File extension used for committed PluginSpec documents.
constexpr const char* kSpecFileExtension = ".zfactory.json";

// Absolute limits enforced by the validator. These exist so that a hostile or
// broken spec cannot make the kernel allocate unboundedly or build an
// unbounded chain.
constexpr int kMaxGraphModules = 32;
constexpr int kMaxExposedParameters = 64;
constexpr int kMaxMacros = 16;
constexpr int kMaxMacroMappings = 256;
constexpr double kMinSampleRate = 8000.0;
constexpr double kMaxSampleRate = 384000.0;
constexpr int kMaxBlockSize = 65536;
} // namespace zfactory
