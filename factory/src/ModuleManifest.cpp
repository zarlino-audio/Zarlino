#include "zfactory/ModuleManifest.h"

#include <algorithm>
#include <cmath>

namespace zfactory
{

float ParameterManifest::clampValue (float value) const
{
    if (! (value == value))          // NaN
        return defaultValue;

    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

float ParameterManifest::denormalise (float normalised) const
{
    float n = normalised;

    if (! (n == n))
        n = 0.0f;

    n = std::min (1.0f, std::max (0.0f, n));

    float value;

    if (stepped && stepCount >= 2)
    {
        const int lastStep = stepCount - 1;
        const int step = static_cast<int> (std::lround (n * static_cast<float> (lastStep)));
        value = minValue + (maxValue - minValue) * (static_cast<float> (step) / static_cast<float> (lastStep));
    }
    else if (logarithmic && minValue > 0.0f && maxValue > minValue)
    {
        value = minValue * std::pow (maxValue / minValue, n);
    }
    else
    {
        value = minValue + (maxValue - minValue) * n;
    }

    return clampValue (value);
}

float ParameterManifest::normalise (float value) const
{
    const float v = clampValue (value);

    if (stepped && stepCount >= 2 && maxValue > minValue)
    {
        const float position = (v - minValue) / (maxValue - minValue);
        return std::min (1.0f, std::max (0.0f, position));
    }

    if (logarithmic && minValue > 0.0f && maxValue > minValue)
        return std::min (1.0f, std::max (0.0f, std::log (v / minValue) / std::log (maxValue / minValue)));

    if (maxValue <= minValue)
        return 0.0f;

    return std::min (1.0f, std::max (0.0f, (v - minValue) / (maxValue - minValue)));
}

const ParameterManifest* ModuleManifest::findParameter (const std::string& parameterId) const
{
    const int index = indexOfParameter (parameterId);
    return index >= 0 ? &parameters[static_cast<size_t> (index)] : nullptr;
}

int ModuleManifest::indexOfParameter (const std::string& parameterId) const
{
    for (size_t i = 0; i < parameters.size(); ++i)
        if (parameters[i].id == parameterId)
            return static_cast<int> (i);

    return -1;
}

bool ModuleManifest::supportsChannels (ChannelMode mode) const
{
    return std::find (supportedChannels.begin(), supportedChannels.end(), mode) != supportedChannels.end();
}

bool ModuleManifest::supportsSampleRate (double sampleRate) const
{
    if (supportedSampleRates.empty())
        return true;

    for (double rate : supportedSampleRates)
        if (std::abs (rate - sampleRate) < 0.5)
            return true;

    return false;
}

//==============================================================================
uint64_t hashBytes (uint64_t seed, const void* data, size_t numBytes)
{
    constexpr uint64_t kPrime = 1099511628211ull;
    const auto* bytes = static_cast<const unsigned char*> (data);

    for (size_t i = 0; i < numBytes; ++i)
    {
        seed ^= static_cast<uint64_t> (bytes[i]);
        seed *= kPrime;
    }

    return seed;
}

uint64_t hashString (uint64_t seed, const std::string& text)
{
    seed = hashBytes (seed, text.data(), text.size());
    const unsigned char terminator = 0;
    return hashBytes (seed, &terminator, 1);
}

uint64_t hashFloat (uint64_t seed, float value)
{
    // Normalise -0.0 to +0.0 so that numerically identical values hash equally.
    if (value == 0.0f)
        value = 0.0f;

    return hashBytes (seed, &value, sizeof (value));
}

uint64_t hashInt (uint64_t seed, int64_t value)
{
    return hashBytes (seed, &value, sizeof (value));
}

//==============================================================================
uint64_t ModuleManifest::fingerprint() const
{
    constexpr uint64_t kSeed = 1469598103934665603ull;

    uint64_t h = kSeed;
    h = hashString (h, id);
    h = hashInt (h, version.major);
    h = hashInt (h, version.minor);
    h = hashInt (h, version.patch);
    h = hashString (h, displayName);
    h = hashString (h, description);
    h = hashInt (h, static_cast<int64_t> (category));
    h = hashInt (h, static_cast<int64_t> (certification));

    for (ChannelMode mode : supportedChannels)
        h = hashInt (h, static_cast<int64_t> (mode));

    for (double rate : supportedSampleRates)
        h = hashFloat (h, static_cast<float> (rate));

    h = hashInt (h, parameters.size());

    for (const auto& p : parameters)
    {
        h = hashString (h, p.id);
        h = hashString (h, p.displayName);
        h = hashString (h, p.unit);
        h = hashFloat (h, p.minValue);
        h = hashFloat (h, p.maxValue);
        h = hashFloat (h, p.defaultValue);
        h = hashInt (h, p.logarithmic ? 1 : 0);
        h = hashInt (h, p.stepped ? 1 : 0);
        h = hashInt (h, p.stepCount);
        h = hashFloat (h, p.smoothingMs);
        h = hashString (h, p.displayGroup);
    }

    h = hashInt (h, referenceLatencySamples);
    h = hashInt (h, latencySampleRateDependent ? 1 : 0);

    for (const auto& dependency : dependencies)
        h = hashString (h, dependency);

    h = hashFloat (h, cpuCostPerSample);
    h = hashInt (h, realtimeSafe ? 1 : 0);

    return h;
}

} // namespace zfactory
