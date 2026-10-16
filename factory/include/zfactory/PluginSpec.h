#pragma once

#include "zfactory/Error.h"
#include "zfactory/Types.h"
#include "zfactory/Version.h"

#include <juce_core/juce_core.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace zfactory
{

//==============================================================================
// PluginSpec - the strict, versioned, machine-readable description of a plugin.
//
// A PluginSpec is pure data. It can never contain C++, an expression, a script
// or a reference to an external resource: the only DSP that can run is what the
// ordered module list plus the documented macro/parameter algebra produce.
//
// The document is checked in two phases, each with deterministic errors:
//   1. Schema validation (this file) - shape, types, ranges, references
//      *inside* the document. Registry independent.
//   2. Module resolution (ModuleRegistry) - the modules exist, the versions are
//      compatible, the certification policy is satisfied, the channel and
//      sample-rate domains match.
//==============================================================================

/** Value domain of an exposed parameter, in engineering units. */
struct SpecParameterRange
{
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.0f;
};

/** A parameter the factory exposes. It always targets exactly one control:
    either a parameter of a module instance, or one of the factory's two virtual
    controls ($factory.dryWet, $factory.bypass). The spec never invents signal
    routing. */
struct SpecParameter
{
    std::string id;                  // unique within the spec
    std::string displayName;
    std::string unit;                // may be empty; must match the module's unit when set
    std::string group;               // UI grouping hint

    std::string targetInstanceId;    // module instance id, or "$factory"
    std::string targetParameterId;   // module parameter id, or "dryWet" / "bypass"

    SpecParameterRange range;
    bool logarithmic = false;

    /** false = internal parameter: drivable by macros, hidden from host automation. */
    bool exposeToHost = true;

    /** "instance.parameter", the canonical reference used in errors and UI. */
    std::string targetRef() const { return targetInstanceId + "." + targetParameterId; }

    /** True when this parameter drives a factory control rather than a module. */
    bool isFactoryTarget() const { return targetInstanceId == "$factory"; }
};

//==============================================================================
/** One macro -> parameter mapping. The mapping algebra is fixed and total:

        m = 0..1 macro value
        m = invert ? 1 - m : m
        m = pow (m, curve)
        p = rangeStart + (rangeEnd - rangeStart) * m
        p = p * scale
        value = parameterRange.denormalise (clamp01 (p))

    Every step is bounded, so a mapping can never produce a value outside the
    parameter's declared range, and the same macro value always produces the
    same parameter value. */
struct SpecMacroMapping
{
    std::string parameterId;
    float rangeStart = 0.0f;
    float rangeEnd = 1.0f;
    float curve = 1.0f;
    float scale = 1.0f;
    bool invert = false;
};

struct SpecMacro
{
    std::string id;
    std::string displayName;
    float defaultValue = 0.0f;      // 0..1
    std::vector<SpecMacroMapping> mappings;
};

//==============================================================================
struct SpecModuleInstance
{
    std::string instanceId;
    std::string moduleId;
    SemVer moduleVersion { 0, 0, 0 };

    /** Values given in the document, in the order they appeared. Missing
        parameters fall back to the manifest default. */
    std::vector<std::pair<std::string, float>> parameterValues;

    float parameterValueOrDefault (const std::string& parameterId, float fallback) const;
};

//==============================================================================
//==============================================================================
/** Global dry/wet. The mix is fixed at `defaultMix` unless the spec declares a
    parameter targeting "$factory.dryWet". */
struct SpecDryWet
{
    bool enabled = false;
    float defaultMix = 1.0f;
};

/** Global bypass. The v1 mode is "hard": the whole chain is skipped and the
    input passes through untouched. A parameter targeting "$factory.bypass"
    drives it (0.5 and above means bypassed). */
struct SpecBypass
{
    std::string mode = "hard";
    bool defaultBypassed = false;
};

struct SpecUi
{
    std::string accentColor;        // "#RRGGBB"
    std::string category;
    std::string shortDescription;
    std::vector<std::string> tags;
    std::string layout;             // "vertical" | "horizontal"
    std::vector<std::string> parameterOrder;
};

//==============================================================================
struct PluginSpec
{
    int schemaVersion = kSchemaVersion;

    std::string pluginId;
    std::string pluginName;
    std::string vendor;
    std::string pluginVersion;      // "MAJOR.MINOR.PATCH"
    EffectType effectType = EffectType::Utility;
    ChannelMode channels = ChannelMode::Stereo;

    std::vector<SpecModuleInstance> modules;   // ordered: index 0 processes first
    std::vector<SpecParameter> parameters;
    std::vector<SpecMacro> macros;

    SpecDryWet dryWet;
    SpecBypass bypass;
    SpecUi ui;

    int channelCount() const { return numChannels (channels); }

    const SpecModuleInstance* findModule (const std::string& instanceId) const;
    const SpecParameter* findParameter (const std::string& parameterId) const;

    /** Count of parameters visible to the host (exposeToHost == true). */
    int exposedParameterCount() const;

    //--------------------------------------------------------------------------
    // Parsing / validation. All three entry points run the identical schema
    // pass; only the source of the bytes differs.
    //--------------------------------------------------------------------------
    static Result<PluginSpec> fromJsonText (const std::string& jsonText);
    static Result<PluginSpec> fromJsonFile (const std::string& filePath);
    static Result<PluginSpec> fromJson (const juce::var& root);

    juce::var toJson() const;
    std::string toJsonText() const;

    /** Order-sensitive, formatting-insensitive fingerprint of the normalized
        spec. Two documents that describe the same plugin fingerprint equally. */
    uint64_t fingerprint() const;
};

//==============================================================================
/** Deterministic schema-level validation of an already-parsed spec. Exposed so
    the test suite can validate structurally-built specs without serializing
    them first. */
FactoryError validatePluginSpecSchema (const PluginSpec& spec);

} // namespace zfactory
