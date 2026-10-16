#include "zfactory/Types.h"

#include <cctype>
#include <cstdio>

namespace zfactory
{

const char* channelModeName (ChannelMode mode) noexcept
{
    switch (mode)
    {
        case ChannelMode::Mono:   return "mono";
        case ChannelMode::Stereo: return "stereo";
    }
    return "unknown";
}

bool parseChannelMode (const std::string& text, ChannelMode& out) noexcept
{
    if (text == "mono")   { out = ChannelMode::Mono;   return true; }
    if (text == "stereo") { out = ChannelMode::Stereo; return true; }
    return false;
}

int numChannels (ChannelMode mode) noexcept
{
    return mode == ChannelMode::Mono ? 1 : 2;
}

const char* effectTypeName (EffectType type) noexcept
{
    switch (type)
    {
        case EffectType::Utility:         return "utility";
        case EffectType::Filter:          return "filter";
        case EffectType::Equalizer:       return "equalizer";
        case EffectType::Dynamics:        return "dynamics";
        case EffectType::Saturation:      return "saturation";
        case EffectType::Limiter:         return "limiter";
        case EffectType::TransientShaper: return "transient-shaper";
        case EffectType::StereoUtility:   return "stereo-utility";
        case EffectType::ChannelStrip:    return "channel-strip";
        case EffectType::VocalProcessor:  return "vocal-processor";
    }
    return "unknown";
}

bool parseEffectType (const std::string& text, EffectType& out) noexcept
{
    for (int i = 0; i <= static_cast<int> (EffectType::VocalProcessor); ++i)
    {
        const auto candidate = static_cast<EffectType> (i);
        if (text == effectTypeName (candidate))
        {
            out = candidate;
            return true;
        }
    }
    return false;
}

const char* moduleCategoryName (ModuleCategory category) noexcept
{
    switch (category)
    {
        case ModuleCategory::Utility:    return "utility";
        case ModuleCategory::Filter:     return "filter";
        case ModuleCategory::Equalizer:  return "equalizer";
        case ModuleCategory::Dynamics:   return "dynamics";
        case ModuleCategory::Saturation: return "saturation";
        case ModuleCategory::Limiter:    return "limiter";
        case ModuleCategory::Shaping:    return "shaping";
        case ModuleCategory::Stereo:     return "stereo";
        case ModuleCategory::Mixing:     return "mixing";
    }
    return "unknown";
}

const char* certificationStateName (CertificationState state) noexcept
{
    switch (state)
    {
        case CertificationState::Experimental: return "EXPERIMENTAL";
        case CertificationState::Verified:     return "VERIFIED";
        case CertificationState::Certified:    return "CERTIFIED";
        case CertificationState::Deprecated:   return "DEPRECATED";
    }
    return "UNKNOWN";
}

bool meetsCertification (CertificationState actual, CertificationState required) noexcept
{
    // DEPRECATED is usable (it still ships) but never counts as CERTIFIED.
    const int rank = (actual == CertificationState::Deprecated)
                        ? static_cast<int> (CertificationState::Verified)
                        : static_cast<int> (actual);
    return rank >= static_cast<int> (required);
}

std::string SemVer::to_string() const
{
    return std::to_string (major) + "." + std::to_string (minor) + "." + std::to_string (patch);
}

bool SemVer::operator< (const SemVer& other) const noexcept
{
    if (major != other.major) return major < other.major;
    if (minor != other.minor) return minor < other.minor;
    return patch < other.patch;
}

namespace
{
bool parseVersionComponent (const std::string& text, size_t& pos, int& out)
{
    const size_t start = pos;

    while (pos < text.size() && std::isdigit (static_cast<unsigned char> (text[pos])) != 0)
        ++pos;

    if (pos == start)
        return false;

    std::string digits = text.substr (start, pos - start);

    if (digits.size() > 1 && digits[0] == '0')   // reject "01"
        return false;

    if (digits.size() > 4)
        return false;

    out = std::stoi (digits);
    return true;
}
} // namespace

bool parseSemVer (const std::string& text, SemVer& out) noexcept
{
    SemVer parsed;
    size_t pos = 0;

    if (! parseVersionComponent (text, pos, parsed.major))  return false;
    if (pos >= text.size() || text[pos] != '.')             return false;
    ++pos;
    if (! parseVersionComponent (text, pos, parsed.minor))  return false;
    if (pos >= text.size() || text[pos] != '.')             return false;
    ++pos;
    if (! parseVersionComponent (text, pos, parsed.patch))  return false;
    if (pos != text.size())                                 return false;

    out = parsed;
    return true;
}

bool isVersionCompatible (const SemVer& requested, const SemVer& available) noexcept
{
    if (requested.major != available.major)
        return false;

    return ! (available < requested);
}

} // namespace zfactory
