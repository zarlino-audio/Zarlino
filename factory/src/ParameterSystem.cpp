#include "zfactory/ParameterSystem.h"
#include "zfactory/ModuleManifest.h"

#include <algorithm>
#include <cmath>

namespace zfactory
{

namespace
{
float clamp01 (float value) noexcept
{
    if (! (value == value))
        return 0.0f;

    return std::min (1.0f, std::max (0.0f, value));
}
} // namespace

//==============================================================================
float ParameterSystem::Binding::denormalise (float normalised) const
{
    const float n = clamp01 (normalised);

    if (range.maxValue <= range.minValue)
        return range.minValue;

    float value;

    if (logarithmic && range.minValue > 0.0f)
        value = range.minValue * std::pow (range.maxValue / range.minValue, n);
    else
        value = range.minValue + (range.maxValue - range.minValue) * n;

    return std::min (range.maxValue, std::max (range.minValue, value));
}

float ParameterSystem::Binding::normalise (float value) const
{
    if (range.maxValue <= range.minValue)
        return 0.0f;

    const float clamped = std::min (range.maxValue, std::max (range.minValue, value));

    if (logarithmic && range.minValue > 0.0f)
        return clamp01 (std::log (clamped / range.minValue) / std::log (range.maxValue / range.minValue));

    return clamp01 ((clamped - range.minValue) / (range.maxValue - range.minValue));
}

//==============================================================================
int ParameterSystem::indexOfBinding (const std::string& id) const
{
    for (size_t i = 0; i < m_bindings.size(); ++i)
        if (m_bindings[i].id == id)
            return static_cast<int> (i);

    return -1;
}

int ParameterSystem::indexOfMacro (const std::string& id) const
{
    for (size_t i = 0; i < m_macros.size(); ++i)
        if (m_macros[i].id == id)
            return static_cast<int> (i);

    return -1;
}

const ParameterSystem::Binding* ParameterSystem::bindingAt (int index) const
{
    if (index < 0 || index >= static_cast<int> (m_bindings.size()))
        return nullptr;

    return &m_bindings[static_cast<size_t> (index)];
}

//==============================================================================
void ParameterSystem::setBaseNormalised (int parameterIndex, float normalised)
{
    if (parameterIndex < 0 || parameterIndex >= static_cast<int> (m_bindings.size()))
        return;

    m_bindings[static_cast<size_t> (parameterIndex)].baseNormalised = clamp01 (normalised);
}

void ParameterSystem::setBaseValue (int parameterIndex, float value)
{
    if (parameterIndex < 0 || parameterIndex >= static_cast<int> (m_bindings.size()))
        return;

    auto& binding = m_bindings[static_cast<size_t> (parameterIndex)];
    binding.baseNormalised = binding.normalise (value);
}

void ParameterSystem::setMacroValue (int macroIndex, float value)
{
    if (macroIndex < 0 || macroIndex >= static_cast<int> (m_macros.size()))
        return;

    m_macros[static_cast<size_t> (macroIndex)].value = clamp01 (value);
}

void ParameterSystem::setMacroValue (const std::string& macroId, float value)
{
    setMacroValue (indexOfMacro (macroId), value);
}

float ParameterSystem::getMacroValue (int macroIndex) const
{
    if (macroIndex < 0 || macroIndex >= static_cast<int> (m_macros.size()))
        return 0.0f;

    return m_macros[static_cast<size_t> (macroIndex)].value;
}

float ParameterSystem::getEffectiveValue (int parameterIndex) const
{
    if (parameterIndex < 0 || parameterIndex >= static_cast<int> (m_bindings.size()))
        return 0.0f;

    return m_bindings[static_cast<size_t> (parameterIndex)].effectiveValue;
}

float ParameterSystem::getEffectiveNormalised (int parameterIndex) const
{
    if (parameterIndex < 0 || parameterIndex >= static_cast<int> (m_bindings.size()))
        return 0.0f;

    return m_bindings[static_cast<size_t> (parameterIndex)].effectiveNormalised;
}

//==============================================================================
void ParameterSystem::recompute()
{
    // Step 1: base layer.
    for (auto& binding : m_bindings)
    {
        binding.effectiveNormalised = binding.baseNormalised;
        binding.macroDriven = false;
    }

    // Step 2: macro layer, in macro-then-mapping spec order.
    for (const auto& macro : m_macros)
    {
        const float macroValue = clamp01 (macro.value);

        for (int mappingIndex : macro.mappingIndices)
        {
            if (mappingIndex < 0 || mappingIndex >= static_cast<int> (m_mappings.size()))
                continue;

            const auto& mapping = m_mappings[static_cast<size_t> (mappingIndex)];

            if (mapping.parameterIndex < 0 || mapping.parameterIndex >= static_cast<int> (m_bindings.size()))
                continue;

            float m = macroValue;

            if (mapping.invert)
                m = 1.0f - m;

            if (mapping.curve != 1.0f)
                m = std::pow (clamp01 (m), mapping.curve);

            float p = mapping.rangeStart + (mapping.rangeEnd - mapping.rangeStart) * m;

            if (mapping.scale != 1.0f)
                p *= mapping.scale;

            auto& binding = m_bindings[static_cast<size_t> (mapping.parameterIndex)];
            binding.effectiveNormalised = clamp01 (p);
            binding.macroDriven = true;
        }
    }

    // Step 3: engineering values, clamped by the binding's own domain.
    for (auto& binding : m_bindings)
        binding.effectiveValue = binding.denormalise (binding.effectiveNormalised);
}

void ParameterSystem::resetToDefaults()
{
    for (auto& macro : m_macros)
        macro.value = 0.0f;

    for (auto& binding : m_bindings)
    {
        binding.baseNormalised = binding.normalise (binding.range.defaultValue);
        binding.effectiveNormalised = binding.baseNormalised;
        binding.effectiveValue = binding.range.defaultValue;
    }
}

//==============================================================================
uint64_t ParameterSystem::fingerprint() const
{
    constexpr uint64_t kSeed = 1469598103934665603ull;

    uint64_t h = hashInt (kSeed, static_cast<int64_t> (m_bindings.size()));

    for (const auto& binding : m_bindings)
    {
        h = hashString (h, binding.id);
        h = hashString (h, binding.unit);
        h = hashString (h, binding.group);
        h = hashFloat (h, binding.range.minValue);
        h = hashFloat (h, binding.range.maxValue);
        h = hashFloat (h, binding.range.defaultValue);
        h = hashInt (h, binding.logarithmic ? 1 : 0);
        h = hashInt (h, binding.exposeToHost ? 1 : 0);
        h = hashInt (h, binding.moduleIndex);
        h = hashInt (h, binding.moduleParameterIndex);
    }

    h = hashInt (h, static_cast<int64_t> (m_macros.size()));

    for (const auto& macro : m_macros)
    {
        h = hashString (h, macro.id);
        h = hashFloat (h, macro.value);
        h = hashInt (h, static_cast<int64_t> (macro.mappingIndices.size()));

        for (int index : macro.mappingIndices)
            h = hashInt (h, index);
    }

    h = hashInt (h, static_cast<int64_t> (m_mappings.size()));

    for (const auto& mapping : m_mappings)
    {
        h = hashInt (h, mapping.parameterIndex);
        h = hashFloat (h, mapping.rangeStart);
        h = hashFloat (h, mapping.rangeEnd);
        h = hashFloat (h, mapping.curve);
        h = hashFloat (h, mapping.scale);
        h = hashInt (h, mapping.invert ? 1 : 0);
    }

    return h;
}

} // namespace zfactory
