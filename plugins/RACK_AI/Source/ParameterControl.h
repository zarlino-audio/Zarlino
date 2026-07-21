#pragma once

#include <JuceHeader.h>
#include <map>
#include <vector>

struct ParameterManifest
{
    int parameterID = -1;
    juce::String parameterName;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.5f;
    juce::String unit;  // "dB", "Hz", "ms", "%", etc.
};

class ParameterControl
{
public:
    ParameterControl() = default;
    ~ParameterControl() = default;

    // Register a plugin's parameter manifest
    void registerPluginParameters (int slotIndex, const std::vector<ParameterManifest>& manifest);

    // Fuzzy-match a human-readable parameter name to a parameter manifest entry
    ParameterManifest* fuzzyMatchParameter (int slotIndex, const juce::String& humanName);

    // Convert human-readable values to normalized (0.0-1.0)
    float convertToNormalized (int slotIndex, const ParameterManifest& param, float humanValue);

    // Convert normalized values back to human-readable
    float convertToHuman (int slotIndex, const ParameterManifest& param, float normalizedValue);

    // Unit conversion helpers
    static float dBToLinear (float dB);
    static float linearTodB (float linear);
    static float hzToLogFreq (float hz);
    static float logFreqToHz (float logFreq);

private:
    int levenshteinDistance (const juce::String& s1, const juce::String& s2) const;

    std::map<int, std::vector<ParameterManifest>> mSlotManifests; // slotIndex -> manifest

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterControl)
};
