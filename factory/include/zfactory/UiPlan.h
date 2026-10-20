#pragma once

#include "zfactory/Error.h"
#include "zfactory/ModuleRegistry.h"
#include "zfactory/PluginSpec.h"

#include <juce_core/juce_core.h>

#include <cstdint>
#include <string>
#include <vector>

namespace zfactory
{

//==============================================================================
// UiPlan - the compiled, deterministic description of a plugin's user interface.
//
// WHY THIS EXISTS
//   The factory must never accept UI code from a user, and must never hand-write
//   a different editor for every product. So the UI is compiled, not authored:
//
//     PluginSpec + certified module manifests + one of three fixed templates
//                     |
//                     v
//                  UiPlan                <- this file. Pure data, no code.
//                     |
//                     v
//     one of three hand-authored JUCE layout engines in the plugin shell
//
//   The plan decides WHICH controls exist, what they are called, which unit
//   they carry, which group they belong to, what order they appear in, how
//   important they are and whether they are a macro. The template decides HOW
//   MUCH of the plan is drawn and how densely. Neither can be supplied by a
//   user: the inputs are a validated PluginSpec and the certified catalog.
//
//   Nothing here is random, environment dependent or order dependent: the same
//   spec + registry + template always produces a byte-identical plan.
//==============================================================================

//==============================================================================
/** The three constrained Zarlino UI templates.
 *
 *  A template is a fixed, hand-authored layout engine inside the universal
 *  plugin shell - not a stylesheet, not generated code, and not selectable
 *  beyond these three values.
 *
 *  Each template draws a strictly larger prefix of the same priority order, so
 *  the three interfaces are nested: Minimal is always a subset of Standard, and
 *  Standard of Advanced. They therefore differ in density, never in coherence.
 *
 *    Minimal   - macros + the most important controls. One column, label only.
 *    Standard  - every control the spec exposes. Grouped, two columns, units.
 *    Advanced  - Standard plus a per-module control for every certified module
 *                parameter the spec does not itself expose, plus value and
 *                range readouts. The full-access surface. */
enum class UiTemplate : int
{
    Minimal  = 0,
    Standard = 1,
    Advanced = 2
};

const char* uiTemplateName (UiTemplate type) noexcept;

/** Parse "minimal" | "standard" | "advanced" (case-insensitive, ASCII only). */
bool parseUiTemplate (const std::string& text, UiTemplate& out) noexcept;

//==============================================================================
/** Visual weight of a control, derived deterministically from the spec:
 *
 *    Primary   - the parameter is driven by at least one macro. The spec author
 *                marked it as a performance control.
 *    Secondary - the parameter is explicitly ordered by `ui.parameterOrder`, or
 *                it is one of the factory's own controls ($factory.dryWet).
 *    Tertiary  - every other parameter, including the ones hidden from the host.
 */
enum class UiImportance : int
{
    Primary   = 0,
    Secondary = 1,
    Tertiary  = 2
};

const char* uiImportanceName (UiImportance importance) noexcept;

//==============================================================================
/** How the shell draws a control. Derived from the parameter's resolved domain,
    never from the spec's free-form text. */
enum class UiControlKind : int
{
    Knob   = 0,   // continuous, non-logarithmic
    Fader  = 1,   // continuous, logarithmic (frequencies and similar)
    Switch = 2,   // stepped, exactly 2 positions
    Select = 3    // stepped, 3 or more positions
};

const char* uiControlKindName (UiControlKind kind) noexcept;

//==============================================================================
struct UiControl
{
    /** Stable identity of the control:
          - a spec parameter  -> the parameter id
          - a macro           -> "macro:" + macro id
        Two controls in one plan never share an id. */
    std::string id;

    std::string label;        // display name
    std::string unit;         // may be empty
    std::string group;        // resolved group name, never empty
    std::string tooltip;      // deterministic, derived from label + unit + range

    UiImportance importance = UiImportance::Tertiary;
    UiControlKind kind = UiControlKind::Knob;

    bool isMacro = false;
    bool isFactoryControl = false;    // $factory.dryWet / $factory.bypass
    bool exposedToHost = true;        // false => macro-only or internal
    bool macroDriven = false;         // a macro writes this parameter

    float minValue = 0.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.0f;
    float defaultValueNormalised = 0.0f;
    bool logarithmic = false;
    int stepCount = 0;                // 0 = continuous

    /** Index into PluginSpec::parameters, or -1 for a macro control. */
    int specParameterIndex = -1;

    /** Ordinal among the instrument's HOST parameters, or -1 when the control is
        not automatable. The host parameter list is the macros first, in spec
        order, followed by the spec parameters with `exposeToHost == true`, also
        in spec order. This is the index the shell's AudioProcessorParameter
        list must use, and it is what makes an automation lane recorded against
        one template resolve in another. */
    int hostParameterIndex = -1;

    /** Stable 32-bit identity derived from `id`. Recorded as build provenance
        and used to prove the host-visible parameter set never renumbers when an
        unrelated parameter is added. */
    uint32_t stableIdHash = 0;

    /** Module instance the control ultimately drives ("$factory" for factory
        controls). Empty for macros. */
    std::string targetInstanceId;
    std::string targetRef;            // "instance.parameter"

    //--------------------------------------------------------------------------
    // Provenance
    //--------------------------------------------------------------------------
    /** True when this control was derived from a certified module manifest
        rather than from a declared PluginSpec parameter. The Advanced template
        adds one of these for every module parameter the spec leaves unexposed,
        which is what makes its information density genuinely higher than the
        other two templates rather than merely rearranged. */
    bool moduleLevel = false;

    /** Index into PluginSpec::modules, for both module-level controls and
        controls that target a module parameter. -1 for macros. */
    int moduleIndex = -1;

    /** Index into the module manifest's parameter list. -1 for macros and for
        factory controls. */
    int moduleParameterIndex = -1;

    /** True when GraphEngine's own state blob persists this control's value.
        False for module-level controls, whose values the plugin shell persists
        in its own state chunk alongside the kernel blob. */
    bool kernelManaged = true;
};

//==============================================================================
struct UiGroup
{
    std::string name;
    UiImportance importance = UiImportance::Tertiary;   // strongest member
    std::vector<int> controlIndices;                    // into UiPlan::controls
    int columnSpan = 1;                                 // hint, 1..2
};

//==============================================================================
struct UiPlan
{
    UiTemplate uiTemplate = UiTemplate::Standard;

    // ---- plugin identity / branding, straight from the spec -----------------
    std::string title;
    std::string vendor;
    std::string pluginVersion;
    std::string category;
    std::string shortDescription;
    std::string accentColor;
    std::vector<std::string> tags;

    ChannelMode channels = ChannelMode::Stereo;
    EffectType effectType = EffectType::Utility;

    /** Display order. Macros first, then groups in order of first appearance. */
    std::vector<UiControl> controls;
    std::vector<UiGroup> groups;

    /** Counts that make the three templates' densities comparable. */
    int totalControlCount = 0;         // controls declared by the spec (macros + parameters)
    int moduleLevelControlCount = 0;   // extra controls derived from module manifests
    int drawnControlCount = 0;         // == controls.size()
    int hiddenByTemplateCount = 0;     // available - drawn
    int macroCount = 0;
    int groupCount = 0;

    /** Every control this spec could ever surface under the full-access
        template: the spec's own controls plus the module-level ones. */
    int availableControlCount() const noexcept
    {
        return totalControlCount + moduleLevelControlCount;
    }

    /** Fraction of the available control surface this template draws, 0..1.
        Strictly ordered Minimal < Standard <= Advanced. */
    double density() const noexcept
    {
        const auto available = availableControlCount();
        return available > 0
                   ? static_cast<double> (drawnControlCount) / static_cast<double> (available)
                   : 0.0;
    }

    //--------------------------------------------------------------------------
    /** Compile a plan. Fails with the same registry errors GraphEngine would
        produce, so a product that cannot be drawn also cannot be built. */
    static Result<UiPlan> compile (const PluginSpec& spec,
                                   const ModuleRegistry& registry,
                                   UiTemplate uiTemplate);

    /** Deterministic fingerprint of the drawn interface: identity, template and
        every drawn control's identity, order, importance and domain. */
    uint64_t fingerprint() const;

    /** Machine-readable plan, for provenance records and CI evidence. */
    juce::var toJson() const;
    std::string toJsonText() const;

    /** Human readable layout dump (CLI / build logs). */
    std::string describe() const;
};

//==============================================================================
/** Stable 32-bit hash of a control id. Deterministic across platforms and
    builds; used as the host parameter's stable identity. */
uint32_t stableIdHash32 (const std::string& text) noexcept;

} // namespace zfactory
