#pragma once

#include <string>

namespace zfactory
{

//==============================================================================
// Channel configuration. PluginSpec and ModuleManifest speak the same
// vocabulary so that compatibility can be checked structurally.
//==============================================================================
enum class ChannelMode : int
{
    Mono   = 0,
    Stereo = 1
};

const char* channelModeName (ChannelMode mode) noexcept;
bool parseChannelMode (const std::string& text, ChannelMode& out) noexcept;
int numChannels (ChannelMode mode) noexcept;

//==============================================================================
// Audio effect type. Purely descriptive for the host/UI layer; the DSP graph
// itself is defined by the ordered module list.
//==============================================================================
enum class EffectType : int
{
    Utility          = 0,
    Filter           = 1,
    Equalizer        = 2,
    Dynamics         = 3,
    Saturation       = 4,
    Limiter          = 5,
    TransientShaper  = 6,
    StereoUtility    = 7,
    ChannelStrip     = 8,
    VocalProcessor   = 9
};

const char* effectTypeName (EffectType type) noexcept;
bool parseEffectType (const std::string& text, EffectType& out) noexcept;

//==============================================================================
// Module category (registry metadata).
//==============================================================================
enum class ModuleCategory : int
{
    Utility    = 0,
    Filter     = 1,
    Equalizer  = 2,
    Dynamics   = 3,
    Saturation = 4,
    Limiter    = 5,
    Shaping    = 6,
    Stereo     = 7,
    Mixing     = 8
};

const char* moduleCategoryName (ModuleCategory category) noexcept;

//==============================================================================
// Certification lifecycle. V1 user-generated plugins may only reference
// CERTIFIED modules.
//==============================================================================
enum class CertificationState : int
{
    Experimental = 0,
    Verified     = 1,
    Certified    = 2,
    Deprecated   = 3
};

const char* certificationStateName (CertificationState state) noexcept;

/** Ordering helper: true when `actual` is at least as strong as `required`. */
bool meetsCertification (CertificationState actual, CertificationState required) noexcept;

//==============================================================================

struct SemVer
{
    int major = 0;
    int minor = 0;
    int patch = 0;

    std::string to_string() const;

    bool operator== (const SemVer& other) const noexcept
    {
        return major == other.major && minor == other.minor && patch == other.patch;
    }
    bool operator!= (const SemVer& other) const noexcept { return ! (*this == other); }
    bool operator< (const SemVer& other) const noexcept;
    bool operator>= (const SemVer& other) const noexcept { return ! (*this < other); }
};

/** Parse "MAJOR.MINOR.PATCH" (each part 0..9999). Returns false on any other
    shape - major-only, leading zeros, suffixes and negative numbers are all
    rejected so that version comparisons stay unambiguous. */
bool parseSemVer (const std::string& text, SemVer& out) noexcept;

/** A requested version is compatible with a registry version when the major
    numbers match and the registry is not older than requested. */
bool isVersionCompatible (const SemVer& requested, const SemVer& available) noexcept;

} // namespace zfactory
