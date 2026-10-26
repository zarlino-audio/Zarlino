#pragma once

#include "zfactory/Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace zfactory
{

//==============================================================================
// Parameter metadata exposed by a module.
//
// Values are always stored in SI-ish engineering units (dB, Hz, seconds, ratio,
// normalised 0..1) so that a PluginSpec never needs to know module internals.
//==============================================================================
struct ParameterManifest
{
    std::string id;                  // stable, unique within the module
    std::string displayName;
    std::string unit;                // "dB", "Hz", "ms", "ratio", "" for 0..1
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.0f;
    bool  logarithmic = false;       // normalised <-> value uses an exponential map
    bool  stepped = false;           // value is one of stepCount discrete positions
    int   stepCount = 0;             // number of positions when stepped (>= 2)
    float smoothingMs = 0.0f;        // 0 = module applies the change immediately
    std::string displayGroup;        // UI grouping hint for the next slice

    /** Normalised [0,1] -> engineering value. Deterministic, clamped. */
    float denormalise (float normalised) const;

    /** Engineering value -> normalised [0,1]. Deterministic, clamped. */
    float normalise (float value) const;

    /** Snap an engineering value into the manifest's legal domain. */
    float clampValue (float value) const;
};

//==============================================================================
// Machine-readable module metadata. One manifest per registry entry.
//==============================================================================
struct ModuleManifest
{
    std::string id;                       // "zarlino.dynamics.compressor"
    SemVer version { 0, 0, 0 };
    std::string displayName;
    std::string description;
    ModuleCategory category = ModuleCategory::Utility;
    CertificationState certification = CertificationState::Experimental;

    std::vector<ChannelMode> supportedChannels;   // never empty
    std::vector<double> supportedSampleRates;     // empty == all rates in range

    std::vector<ParameterManifest> parameters;

    /** Latency at the reference rate below. */
    int referenceLatencySamples = 0;
    bool latencySampleRateDependent = false;
    static constexpr double kReferenceSampleRate = 48000.0;

    /** Module ids that must also be present in the graph. */
    std::vector<std::string> dependencies;

    /** Relative per-sample cost estimate for planning UI / warnings. */
    float cpuCostPerSample = 0.0f;

    /** Declares that process()/setParameter() never allocate, lock or block. */
    bool realtimeSafe = true;

    /** The module generates audio from note events (a sound source) instead of
        processing its input. A spec whose effectType is 'instrument' must carry
        at least one source, and a processor chain must carry none. */
    bool isSource = false;

    const ParameterManifest* findParameter (const std::string& parameterId) const;
    int indexOfParameter (const std::string& parameterId) const;   // -1 when absent
    bool supportsChannels (ChannelMode mode) const;
    bool supportsSampleRate (double sampleRate) const;

    /** Stable 64-bit fingerprint over every field. Used for determinism
        evidence: the registry catalog hash is the XOR-fold of these. */
    uint64_t fingerprint() const;
};

/** FNV-1a 64 mixing helpers shared by manifests, specs and engine state. */
uint64_t hashBytes (uint64_t seed, const void* data, size_t numBytes);
uint64_t hashString (uint64_t seed, const std::string& text);
uint64_t hashFloat (uint64_t seed, float value);
uint64_t hashInt (uint64_t seed, int64_t value);

} // namespace zfactory
