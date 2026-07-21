#include "ParameterControl.h"

void ParameterControl::registerPluginParameters (int slotIndex, const std::vector<ParameterManifest>& manifest)
{
    mSlotManifests[slotIndex] = manifest;
}

ParameterManifest* ParameterControl::fuzzyMatchParameter (int slotIndex, const juce::String& humanName)
{
    auto it = mSlotManifests.find (slotIndex);
    if (it == mSlotManifests.end())
        return nullptr;
    
    auto& manifest = it->second;
    int bestDistance = INT_MAX;
    ParameterManifest* bestMatch = nullptr;
    
    for (auto& param : manifest)
    {
        int distance = levenshteinDistance (humanName.toLowerCase(), param.parameterName.toLowerCase());
        if (distance < bestDistance && distance < 3)
        {
            bestDistance = distance;
            bestMatch = &param;
        }
    }
    
    return bestMatch;
}

float ParameterControl::convertToNormalized (int slotIndex, const ParameterManifest& param, float humanValue)
{
    // Convert human-readable value to 0.0-1.0 range
    if (param.unit == "dB")
    {
        // dB range typically -80 to +24 dB
        return (humanValue + 80.0f) / 104.0f;
    }
    
    return juce::jlimit (0.0f, 1.0f, (humanValue - param.minValue) / (param.maxValue - param.minValue));
}

float ParameterControl::convertToHuman (int slotIndex, const ParameterManifest& param, float normalizedValue)
{
    if (param.unit == "dB")
    {
        return normalizedValue * 104.0f - 80.0f;
    }
    
    return param.minValue + normalizedValue * (param.maxValue - param.minValue);
}

float ParameterControl::dBToLinear (float dB)
{
    return std::pow (10.0f, dB / 20.0f);
}

float ParameterControl::linearTodB (float linear)
{
    return 20.0f * std::log10 (linear);
}

float ParameterControl::hzToLogFreq (float hz)
{
    return std::log2 (hz / 20.0f);
}

float ParameterControl::logFreqToHz (float logFreq)
{
    return 20.0f * std::pow (2.0f, logFreq);
}

int ParameterControl::levenshteinDistance (const juce::String& s1, const juce::String& s2) const
{
    int len1 = s1.length();
    int len2 = s2.length();
    std::vector<std::vector<int>> dp (len1 + 1, std::vector<int> (len2 + 1));
    
    for (int i = 0; i <= len1; ++i)
        dp[i][0] = i;
    for (int j = 0; j <= len2; ++j)
        dp[0][j] = j;
    
    for (int i = 1; i <= len1; ++i)
    {
        for (int j = 1; j <= len2; ++j)
        {
            if (s1[i - 1] == s2[j - 1])
                dp[i][j] = dp[i - 1][j - 1];
            else
                dp[i][j] = 1 + std::min ({dp[i - 1][j], dp[i][j - 1], dp[i - 1][j - 1]});
        }
    }
    
    return dp[len1][len2];
}
