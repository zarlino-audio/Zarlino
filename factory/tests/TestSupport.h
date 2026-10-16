#pragma once

#include <JuceHeader.h>

#include "ZStressTestUtils.h"
#include "zfactory/GraphEngine.h"
#include "zfactory/ModuleRegistry.h"
#include "zfactory/PluginSpec.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

//==============================================================================
// Shared helpers for the factory test suites.
//==============================================================================

namespace zfactory
{
namespace test
{

inline juce::String jstr (const std::string& text)
{
    return juce::String::fromUTF8 (text.c_str());
}

//==============================================================================
// Committed example PluginSpecs live in factory/specs; the build passes the
// directory in so the tests exercise the real documents from disk.
//==============================================================================
inline std::string specsDirectory()
{
    return std::string (ZFACTORY_SPEC_DIR);
}

inline std::string specPath (const char* fileName)
{
    return specsDirectory() + "/" + fileName;
}

//==============================================================================
// Programmatic spec construction. Used by the tests that need a controlled
// graph rather than a committed document.
//==============================================================================
inline PluginSpec makeBaseSpec (ChannelMode channels = ChannelMode::Stereo)
{
    PluginSpec spec;
    spec.schemaVersion = kSchemaVersion;
    spec.pluginId = "zarlino.test.fixture";
    spec.pluginName = "Factory Test Fixture";
    spec.vendor = "Zarlino Audio";
    spec.pluginVersion = "1.0.0";
    spec.effectType = EffectType::Utility;
    spec.channels = channels;

    spec.ui.accentColor = "#123456";
    spec.ui.category = "Test";
    spec.ui.layout = "vertical";

    spec.bypass.mode = "hard";
    spec.bypass.defaultBypassed = false;
    spec.dryWet.enabled = false;
    spec.dryWet.defaultMix = 1.0f;

    return spec;
}

inline SpecModuleInstance& addModule (PluginSpec& spec,
                                      const std::string& instanceId,
                                      const std::string& moduleId,
                                      const std::string& version = "1.0.0")
{
    SpecModuleInstance instance;
    instance.instanceId = instanceId;
    instance.moduleId = moduleId;
    parseSemVer (version, instance.moduleVersion);
    spec.modules.push_back (std::move (instance));
    return spec.modules.back();
}

inline void setModuleParameter (SpecModuleInstance& instance, const std::string& parameterId, float value)
{
    for (auto& entry : instance.parameterValues)
    {
        if (entry.first == parameterId)
        {
            entry.second = value;
            return;
        }
    }

    instance.parameterValues.emplace_back (parameterId, value);
}

inline SpecParameter& addParameter (PluginSpec& spec,
                                    const std::string& id,
                                    const std::string& target,
                                    float minValue,
                                    float maxValue,
                                    float defaultValue,
                                    const std::string& unit = std::string(),
                                    bool exposeToHost = true)
{
    SpecParameter parameter;
    parameter.id = id;
    parameter.displayName = id;
    parameter.unit = unit;
    parameter.targetInstanceId = target.substr (0, target.find ('.'));
    parameter.targetParameterId = target.substr (target.find ('.') + 1);
    parameter.range.minValue = minValue;
    parameter.range.maxValue = maxValue;
    parameter.range.defaultValue = defaultValue;
    parameter.exposeToHost = exposeToHost;
    spec.parameters.push_back (std::move (parameter));
    return spec.parameters.back();
}

inline void addMacro (PluginSpec& spec,
                      const std::string& id,
                      float defaultValue,
                      std::vector<SpecMacroMapping> mappings)
{
    SpecMacro macro;
    macro.id = id;
    macro.displayName = id;
    macro.defaultValue = defaultValue;
    macro.mappings = std::move (mappings);
    spec.macros.push_back (std::move (macro));
}

inline SpecMacroMapping mapping (const std::string& parameterId,
                                 float rangeStart = 0.0f,
                                 float rangeEnd = 1.0f,
                                 bool invert = false,
                                 float curve = 1.0f,
                                 float scale = 1.0f)
{
    SpecMacroMapping result;
    result.parameterId = parameterId;
    result.rangeStart = rangeStart;
    result.rangeEnd = rangeEnd;
    result.invert = invert;
    result.curve = curve;
    result.scale = scale;
    return result;
}

/** Gain -> high-pass -> compressor -> tilt EQ. Enough shape to exercise the
    chain, parameters and macros without depending on a document. */
inline PluginSpec makeChainSpec (ChannelMode channels = ChannelMode::Stereo)
{
    auto spec = makeBaseSpec (channels);

    addModule (spec, "gain", "zarlino.gain");
    addModule (spec, "highpass", "zarlino.filter.multimode");
    addModule (spec, "comp", "zarlino.dynamics.compressor");
    addModule (spec, "tone", "zarlino.eq.tilt");

    setModuleParameter (spec.modules[1], "shape", 1.0f);
    setModuleParameter (spec.modules[1], "frequency", 80.0f);
    setModuleParameter (spec.modules[2], "threshold", -18.0f);
    setModuleParameter (spec.modules[2], "ratio", 4.0f);

    addParameter (spec, "trim", "gain.gainDb", -18.0f, 18.0f, 0.0f, "dB");
    addParameter (spec, "threshold", "comp.threshold", -36.0f, -6.0f, -18.0f, "dB");
    addParameter (spec, "tone", "tone.tiltDb", -6.0f, 6.0f, 0.0f, "dB");

    return spec;
}

//==============================================================================
// Result assertions
//==============================================================================
template <typename T>
bool expectSuccess (juce::UnitTest& test, const Result<T>& result, const juce::String& what)
{
    if (result.ok())
        return true;

    test.expect (false, what + " should have succeeded but returned: "
                            + jstr (result.error().describe()));
    return false;
}

template <typename T>
bool expectFailure (juce::UnitTest& test,
                    const Result<T>& result,
                    ErrorCode expectedCode,
                    const juce::String& what)
{
    if (result.ok())
    {
        test.expect (false, what + " should have failed with "
                                + juce::String (errorCodeName (expectedCode)) + " but succeeded");
        return false;
    }

    const bool matches = result.error().code == expectedCode;

    test.expect (matches, what + " expected " + juce::String (errorCodeName (expectedCode))
                              + " but got " + jstr (result.error().describe()));

    return matches;
}

inline bool expectNoError (juce::UnitTest& test, const FactoryError& error, const juce::String& what)
{
    test.expect (error.ok(), what + " should have succeeded but returned: " + jstr (error.describe()));
    return error.ok();
}

inline bool expectError (juce::UnitTest& test,
                         const FactoryError& error,
                         ErrorCode expectedCode,
                         const juce::String& what)
{
    const bool matches = error.code == expectedCode;

    test.expect (matches, what + " expected " + juce::String (errorCodeName (expectedCode))
                              + " but got " + jstr (error.describe()));

    return matches;
}

//==============================================================================
// Audio helpers
//==============================================================================
class Block
{
public:
    Block (int numChannels, int numSamples)
        : m_channels (static_cast<size_t> (numChannels)),
          m_pointers (static_cast<size_t> (numChannels)),
          m_numSamples (numSamples)
    {
        for (auto& channel : m_channels)
            channel.assign (static_cast<size_t> (numSamples), 0.0f);

        for (size_t i = 0; i < m_channels.size(); ++i)
            m_pointers[i] = m_channels[i].data();
    }

    int numChannels() const noexcept { return static_cast<int> (m_channels.size()); }
    int numSamples() const noexcept { return m_numSamples; }

    /** Deep copy with the channel pointers re-pointed at the copy, so a snapshot
        can be compared against the processed block safely. */
    Block duplicate() const
    {
        Block copy (numChannels(), m_numSamples);

        for (int ch = 0; ch < numChannels(); ++ch)
            copy.m_channels[static_cast<size_t> (ch)] = m_channels[static_cast<size_t> (ch)];

        return copy;
    }

    Block (const Block&) = delete;
    Block& operator= (const Block&) = delete;
    Block (Block&&) = default;
    Block& operator= (Block&&) = default;

    float* channel (int index) noexcept { return m_channels[static_cast<size_t> (index)].data(); }
    const float* channel (int index) const noexcept { return m_channels[static_cast<size_t> (index)].data(); }

    float* const* pointers() noexcept { return m_pointers.data(); }

    void clear() noexcept
    {
        for (auto& channel : m_channels)
            std::fill (channel.begin(), channel.end(), 0.0f);
    }

    void impulse (int channelIndex, int position, float amplitude)
    {
        clear();
        m_channels[static_cast<size_t> (channelIndex)][static_cast<size_t> (position)] = amplitude;
    }

    void sine (double frequency, double sampleRate, float amplitude, long long startIndex = 0)
    {
        for (auto& channel : m_channels)
            for (int i = 0; i < m_numSamples; ++i)
            {
                const auto index = startIndex + i;
                channel[static_cast<size_t> (i)] = amplitude * static_cast<float> (
                    std::sin (2.0 * 3.14159265358979 * frequency
                              * static_cast<double> (index) / sampleRate));
            }
    }

    /** Deterministic broadband noise. Every channel gets a different stream, so
        stereo material has a real side component. */
    void noise (unsigned int seed, float amplitude)
    {
        for (size_t ch = 0; ch < m_channels.size(); ++ch)
        {
            unsigned int state = seed + static_cast<unsigned int> (ch) * 7919u;

            for (int i = 0; i < m_numSamples; ++i)
            {
                state = state * 1664525u + 1013904223u;
                const float unit = static_cast<float> ((state >> 9) & 0x7FFFFFu) / 4194304.0f - 1.0f;
                m_channels[ch][static_cast<size_t> (i)] = amplitude * unit;
            }
        }
    }

    /** Copy in a stimulus built with the shared Zarlino test generators
        (shared/testing/ZStressTestUtils.h), so the factory tests exercise the same
        hostile and analytic signals as the plugin suites. */
    void copyFrom (const juce::AudioBuffer<float>& source)
    {
        const int channels = std::min (numChannels(), source.getNumChannels());
        const int samples = std::min (m_numSamples, source.getNumSamples());

        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < samples; ++i)
                m_channels[static_cast<size_t> (ch)][static_cast<size_t> (i)] = source.getSample (ch, i);
    }

    bool allFinite() const
    {
        for (const auto& channel : m_channels)
            for (float value : channel)
                if (! (value > -1.0e30f && value < 1.0e30f))
                    return false;

        return true;
    }

    bool isSilent() const
    {
        for (const auto& channel : m_channels)
            for (float value : channel)
                if (value != 0.0f)
                    return false;

        return true;
    }

    float peak() const
    {
        float result = 0.0f;

        for (const auto& channel : m_channels)
            for (float value : channel)
                result = std::max (result, std::abs (value));

        return result;
    }

    double rms() const
    {
        double sum = 0.0;
        long long count = 0;

        for (const auto& channel : m_channels)
            for (float value : channel)
            {
                sum += static_cast<double> (value) * static_cast<double> (value);
                ++count;
            }

        return count > 0 ? std::sqrt (sum / static_cast<double> (count)) : 0.0;
    }

    /** Largest absolute difference between two blocks of the same shape. */
    float maxDifference (const Block& other) const
    {
        float result = 0.0f;

        for (int ch = 0; ch < numChannels(); ++ch)
            for (int i = 0; i < m_numSamples; ++i)
                result = std::max (result, std::abs (channel (ch)[i] - other.channel (ch)[i]));

        return result;
    }

private:
    std::vector<std::vector<float>> m_channels;
    std::vector<float*> m_pointers;
    int m_numSamples = 0;
};

} // namespace test
} // namespace zfactory
