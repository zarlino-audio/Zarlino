#pragma once

#include "zfactory/PluginSpec.h"

#include <string>
#include <vector>

namespace zfactory
{

//==============================================================================
// ParameterSystem - the factory's parameter and macro algebra.
//
// Resolution happens once, at graph build time: every spec parameter becomes a
// Binding that points at exactly one (module instance, module parameter index)
// pair. Nothing is looked up by string while audio is running.
//
// VALUE ALGEBRA (fixed, total, and the same for every spec):
//
//   1. Each binding starts at its base value (spec default, or whatever the
//      host last wrote through setBaseNormalised / setBaseValue).
//   2. Macros are applied in spec order. For each mapping, in spec order:
//
//          m = clamp01 (macro value)
//          m = invert ? 1 - m : m
//          m = pow (m, curve)
//          p = rangeStart + (rangeEnd - rangeStart) * m
//          p = clamp01 (p * scale)
//
//      and `p` replaces the binding's effective normalised value.
//   3. Effective engineering value = binding.denormalise (effectiveNormalised),
//      which clamps to the parameter's declared range.
//
// Consequences, all asserted by the factory test suite:
//   * a macro can never drive a parameter outside its declared range;
//   * a macro value outside 0..1 is clamped, never extrapolated;
//   * the same inputs always produce the same parameter values;
//   * a parameter driven by macros ignores host writes to its base value
//     (documented behaviour - macros own the parameter).
//==============================================================================
class ParameterSystem
{
public:
    struct Binding
    {
        std::string id;
        std::string displayName;
        std::string unit;
        std::string group;

        SpecParameterRange range;
        bool logarithmic = false;
        bool exposeToHost = true;

        int moduleIndex = -1;            // index into GraphEngine's module list
        int moduleParameterIndex = -1;   // index into the module's manifest

        float baseNormalised = 0.0f;
        float effectiveNormalised = 0.0f;
        float effectiveValue = 0.0f;

        bool macroDriven = false;

        float denormalise (float normalised) const;
        float normalise (float value) const;
    };

    struct Mapping
    {
        int parameterIndex = -1;
        float rangeStart = 0.0f;
        float rangeEnd = 1.0f;
        float curve = 1.0f;
        float scale = 1.0f;
        bool invert = false;
    };

    struct MacroBinding
    {
        std::string id;
        std::string displayName;
        float value = 0.0f;
        std::vector<int> mappingIndices;
    };

    //--------------------------------------------------------------------------
    void addBinding (Binding binding) { m_bindings.push_back (std::move (binding)); }
    void addMacro (MacroBinding macro) { m_macros.push_back (std::move (macro)); }
    void addMapping (Mapping mapping) { m_mappings.push_back (mapping); }

    int numBindings() const noexcept { return static_cast<int> (m_bindings.size()); }
    int numMacros() const noexcept { return static_cast<int> (m_macros.size()); }
    int numMappings() const noexcept { return static_cast<int> (m_mappings.size()); }

    const std::vector<Binding>& bindings() const noexcept { return m_bindings; }
    const std::vector<MacroBinding>& macros() const noexcept { return m_macros; }
    const std::vector<Mapping>& mappings() const noexcept { return m_mappings; }

    int indexOfBinding (const std::string& id) const;   // -1 when absent
    int indexOfMacro (const std::string& id) const;     // -1 when absent

    const Binding* bindingAt (int index) const;

    //--------------------------------------------------------------------------
    // Host-facing parameter access (normalised 0..1, as DAWs speak).
    void setBaseNormalised (int parameterIndex, float normalised);
    void setBaseValue (int parameterIndex, float value);

    /** Set a macro position. Clamped to 0..1. */
    void setMacroValue (int macroIndex, float value);
    void setMacroValue (const std::string& macroId, float value);

    float getMacroValue (int macroIndex) const;
    float getEffectiveValue (int parameterIndex) const;
    float getEffectiveNormalised (int parameterIndex) const;

    /** Recompute every effective value from the base values and macro
        positions. Deterministic; never allocates after the first call. */
    void recompute();

    /** Reset every macro to its declared default and recompute. */
    void resetToDefaults();

    /** Fingerprint of the whole resolved algebra (bindings, domains, mappings).
        Two graphs built from the same spec produce the same value here. */
    uint64_t fingerprint() const;

private:
    std::vector<Binding> m_bindings;
    std::vector<MacroBinding> m_macros;
    std::vector<Mapping> m_mappings;
};

} // namespace zfactory
