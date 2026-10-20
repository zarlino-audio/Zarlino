#include "zfactory/UiPlan.h"

#include "zfactory/ModuleManifest.h"
#include "zfactory/ParameterSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace zfactory
{

//==============================================================================
// Template design constants.
//
// A template must produce a usable interface even for a deliberately tiny spec,
// and must not become an unbounded wall of controls for a large one. These two
// bounds are the whole of the "how much" policy, and they are what guarantees
// the three templates stay strictly nested:
//
//     keepCount (Minimal)  <= 8  <=  keepCount (Standard)  <=  all  == Advanced
//
// so Minimal is always a subset of Standard and Standard a subset of Advanced.
//==============================================================================
namespace
{
constexpr int kMinimalFloor   = 3;
constexpr int kMinimalCeiling = 8;
constexpr int kStandardFloor   = 8;
constexpr int kStandardCeiling = 16;
}

//==============================================================================
const char* uiTemplateName (UiTemplate type) noexcept
{
    switch (type)
    {
        case UiTemplate::Minimal:  return "minimal";
        case UiTemplate::Standard: return "standard";
        case UiTemplate::Advanced: return "advanced";
    }

    return "unknown";
}

bool parseUiTemplate (const std::string& text, UiTemplate& out) noexcept
{
    std::string lower;
    lower.reserve (text.size());

    for (const auto c : text)
        lower.push_back (static_cast<char> (std::tolower (static_cast<unsigned char> (c))));

    if (lower == "minimal")  { out = UiTemplate::Minimal;  return true; }
    if (lower == "standard") { out = UiTemplate::Standard; return true; }
    if (lower == "advanced") { out = UiTemplate::Advanced; return true; }

    return false;
}

const char* uiImportanceName (UiImportance importance) noexcept
{
    switch (importance)
    {
        case UiImportance::Primary:   return "primary";
        case UiImportance::Secondary: return "secondary";
        case UiImportance::Tertiary:  return "tertiary";
    }

    return "unknown";
}

const char* uiControlKindName (UiControlKind kind) noexcept
{
    switch (kind)
    {
        case UiControlKind::Knob:   return "knob";
        case UiControlKind::Fader:  return "fader";
        case UiControlKind::Switch: return "switch";
        case UiControlKind::Select: return "select";
    }

    return "unknown";
}

//==============================================================================
uint32_t stableIdHash32 (const std::string& text) noexcept
{
    // FNV-1a 32. Stable across platforms, builds and compilers.
    uint32_t hash = 2166136261u;

    for (const auto c : text)
    {
        hash ^= static_cast<uint32_t> (static_cast<unsigned char> (c));
        hash *= 16777619u;
    }

    return hash;
}

//==============================================================================
namespace
{

/** Deterministic, locale-independent rendering of a control value. */
juce::String formatValue (float value)
{
    if (value == 0.0f)
        return "0";

    const auto magnitude = std::abs (value);

    if (magnitude >= 1000.0f)
        return juce::String (static_cast<int> (std::lround (value)));

    if (magnitude >= 1.0f)
        return juce::String (value, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");

    return juce::String (value, 3).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
}

/** One tooltip format for every control in every template, so a control reads
    the same wherever it is drawn. Built with operator<< to keep the arithmetic
    unambiguous between std::string, juce::String and string literals. */
juce::String makeTooltip (const std::string& label,
                          const std::string& unit,
                          float minValue,
                          float maxValue,
                          const juce::String& suffix = juce::String())
{
    juce::String text;
    text << juce::String (label);

    if (! unit.empty())
        text << " (" << juce::String (unit) << ")";

    text << " - " << formatValue (minValue) << " to " << formatValue (maxValue);

    if (! unit.empty())
        text << " " << juce::String (unit);

    return text + suffix;
}

UiControlKind kindFromDomain (const ParameterManifest* manifest, bool isFactoryControl)
{
    if (manifest != nullptr)
    {
        if (manifest->stepped && manifest->stepCount == 2)
            return UiControlKind::Switch;

        if (manifest->stepped && manifest->stepCount > 2)
            return UiControlKind::Select;

        return manifest->logarithmic ? UiControlKind::Fader : UiControlKind::Knob;
    }

    // Factory controls ($factory.dryWet, $factory.bypass) are continuous 0..1.
    juce::ignoreUnused (isFactoryControl);
    return UiControlKind::Knob;
}

/** Importance rank used for ordering; lower sorts first. */
int importanceRank (UiImportance importance) noexcept
{
    return static_cast<int> (importance);
}

} // namespace

//==============================================================================
Result<UiPlan> UiPlan::compile (const PluginSpec& spec,
                                const ModuleRegistry& registry,
                                UiTemplate uiTemplate)
{
    UiPlan plan;
    plan.uiTemplate = uiTemplate;

    plan.title = spec.pluginName;
    plan.vendor = spec.vendor;
    plan.pluginVersion = spec.pluginVersion;
    plan.category = spec.ui.category;
    plan.shortDescription = spec.ui.shortDescription;
    plan.accentColor = spec.ui.accentColor;
    plan.tags = spec.ui.tags;
    plan.channels = spec.channels;
    plan.effectType = spec.effectType;

    //--------------------------------------------------------------------------
    // 1. Resolve every module instance so a control can inherit the module's own
    //    parameter metadata (unit domain, stepping, UI grouping hint). The
    //    resolution policy matches GraphEngine's, so a product that cannot be
    //    built cannot be drawn either.
    //--------------------------------------------------------------------------
    struct ResolvedModule
    {
        const ModuleManifest* manifest = nullptr;
    };

    std::vector<ResolvedModule> resolved (spec.modules.size());

    for (size_t i = 0; i < spec.modules.size(); ++i)
    {
        const auto& instance = spec.modules[i];
        const auto entry = registry.resolve (instance.moduleId,
                                             instance.moduleVersion,
                                             CertificationState::Certified);

        if (! entry.ok())
            return Result<UiPlan>::failure (entry.error());

        resolved[i].manifest = &entry.value()->manifest;
    }

    const auto moduleIndexOf = [&spec] (const std::string& instanceId) -> int
    {
        for (size_t i = 0; i < spec.modules.size(); ++i)
            if (spec.modules[i].instanceId == instanceId)
                return static_cast<int> (i);

        return -1;
    };

    // Which module parameters does the spec already expose? The Advanced template
    // adds a control for every module parameter that is NOT in here.
    std::vector<std::vector<bool>> coveredParameters (spec.modules.size());

    for (size_t i = 0; i < spec.modules.size(); ++i)
        coveredParameters[i].assign (resolved[i].manifest != nullptr
                                         ? resolved[i].manifest->parameters.size()
                                         : 0,
                                     false);

    //--------------------------------------------------------------------------
    // 2. Which parameters are macro driven? A macro-driven parameter is the
    //    spec author's explicit statement that this is a performance control.
    //--------------------------------------------------------------------------
    std::vector<bool> macroDriven (spec.parameters.size(), false);

    for (const auto& macro : spec.macros)
        for (const auto& mapping : macro.mappings)
            for (size_t p = 0; p < spec.parameters.size(); ++p)
                if (spec.parameters[p].id == mapping.parameterId)
                    macroDriven[p] = true;

    //--------------------------------------------------------------------------
    // 3. Display order for parameters: ui.parameterOrder first, in the order the
    //    spec lists it, then everything else in declaration order.
    //--------------------------------------------------------------------------
    std::vector<int> displayOrder;
    displayOrder.reserve (spec.parameters.size());

    for (const auto& orderedId : spec.ui.parameterOrder)
        for (size_t p = 0; p < spec.parameters.size(); ++p)
            if (spec.parameters[p].id == orderedId
                && std::find (displayOrder.begin(), displayOrder.end(), static_cast<int> (p)) == displayOrder.end())
                displayOrder.push_back (static_cast<int> (p));

    for (size_t p = 0; p < spec.parameters.size(); ++p)
        if (std::find (displayOrder.begin(), displayOrder.end(), static_cast<int> (p)) == displayOrder.end())
            displayOrder.push_back (static_cast<int> (p));

    //--------------------------------------------------------------------------
    // 4. Build the parameter controls in display order.
    //--------------------------------------------------------------------------
    std::vector<UiControl> parameterControls;
    parameterControls.reserve (spec.parameters.size());

    for (const auto parameterIndex : displayOrder)
    {
        const auto& parameter = spec.parameters[static_cast<size_t> (parameterIndex)];
        const auto moduleIndex = parameter.isFactoryTarget() ? -1 : moduleIndexOf (parameter.targetInstanceId);
        const auto* manifest = moduleIndex >= 0 ? resolved[static_cast<size_t> (moduleIndex)].manifest : nullptr;

        const ParameterManifest* parameterManifest = nullptr;
        if (manifest != nullptr)
        {
            const auto moduleParameterIndex = manifest->indexOfParameter (parameter.targetParameterId);
            if (moduleParameterIndex >= 0)
            {
                parameterManifest = &manifest->parameters[static_cast<size_t> (moduleParameterIndex)];
                coveredParameters[static_cast<size_t> (moduleIndex)][static_cast<size_t> (moduleParameterIndex)] = true;
            }
        }

        UiControl control;
        control.id = parameter.id;
        control.label = parameter.displayName;
        control.unit = parameter.unit;
        control.isFactoryControl = parameter.isFactoryTarget();
        control.exposedToHost = parameter.exposeToHost;
        control.macroDriven = macroDriven[static_cast<size_t> (parameterIndex)];
        control.specParameterIndex = parameterIndex;
        control.targetInstanceId = parameter.targetInstanceId;
        control.targetRef = parameter.targetRef();
        control.stableIdHash = stableIdHash32 (parameter.id);
        control.moduleIndex = moduleIndex;

        if (manifest != nullptr && ! parameter.targetParameterId.empty())
            control.moduleParameterIndex = manifest->indexOfParameter (parameter.targetParameterId);

        control.minValue = parameter.range.minValue;
        control.maxValue = parameter.range.maxValue;
        control.defaultValue = parameter.range.defaultValue;
        control.logarithmic = parameter.logarithmic;
        control.kind = kindFromDomain (parameterManifest, control.isFactoryControl);

        if (parameterManifest != nullptr)
        {
            control.stepCount = parameterManifest->stepped ? parameterManifest->stepCount : 0;
        }

        // Normalised default, computed with the same map the parameter layer
        // uses, so the editor's initial position and the audio engine agree.
        {
            ParameterSystem::Binding binding;
            binding.range = parameter.range;
            binding.logarithmic = parameter.logarithmic;
            control.defaultValueNormalised = binding.normalise (parameter.range.defaultValue);
        }

        // Group: the spec's own group wins; otherwise the module's manifest hint;
        // otherwise the module's display name; factory controls get their own.
        if (! parameter.group.empty())
            control.group = parameter.group;
        else if (control.isFactoryControl)
            control.group = "Mix";
        else if (parameterManifest != nullptr && ! parameterManifest->displayGroup.empty())
            control.group = parameterManifest->displayGroup;
        else if (manifest != nullptr && ! manifest->displayName.empty())
            control.group = manifest->displayName;
        else
            control.group = "Parameters";

        // Importance.
        if (control.macroDriven)
            control.importance = UiImportance::Primary;
        else if (control.isFactoryControl
                 || std::find (spec.ui.parameterOrder.begin(), spec.ui.parameterOrder.end(), parameter.id)
                        != spec.ui.parameterOrder.end())
            control.importance = UiImportance::Secondary;
        else
            control.importance = UiImportance::Tertiary;

        control.tooltip = makeTooltip (control.label, control.unit,
                                       control.minValue, control.maxValue).toStdString();

        parameterControls.push_back (std::move (control));
    }

    //--------------------------------------------------------------------------
    // 5. Host parameter numbering. Macros come first because they are the
    //    performance controls, then the host-visible parameters in SPEC order so
    //    that the audio engine's parameter indices -- which the kernel resolves
    //    at graph build time -- line up exactly.
    //--------------------------------------------------------------------------
    {
        int hostIndex = static_cast<int> (spec.macros.size());
        for (size_t p = 0; p < spec.parameters.size(); ++p)
        {
            if (! spec.parameters[p].exposeToHost)
                continue;

            for (auto& control : parameterControls)
            {
                if (control.specParameterIndex == static_cast<int> (p))
                {
                    control.hostParameterIndex = hostIndex;
                    break;
                }
            }

            ++hostIndex;
        }
    }

    //--------------------------------------------------------------------------
    // 6. Macro controls. They are always Primary and always drawn.
    //--------------------------------------------------------------------------
    std::vector<UiControl> macroControls;
    macroControls.reserve (spec.macros.size());

    for (const auto& macro : spec.macros)
    {
        UiControl control;
        control.id = "macro:" + macro.id;
        control.label = macro.displayName;
        control.group = "Macros";
        control.importance = UiImportance::Primary;
        control.kind = UiControlKind::Knob;
        control.isMacro = true;
        control.exposedToHost = true;
        control.minValue = 0.0f;
        control.maxValue = 1.0f;
        control.defaultValue = macro.defaultValue;
        control.defaultValueNormalised = macro.defaultValue;
        control.stableIdHash = stableIdHash32 (control.id);
        control.specParameterIndex = -1;
        control.hostParameterIndex = static_cast<int> (macroControls.size());
        juce::String macroSuffix;
        macroSuffix << " - drives " << static_cast<int> (macro.mappings.size())
                    << (macro.mappings.size() == 1 ? " parameter" : " parameters");

        control.tooltip = makeTooltip (control.label, control.unit,
                                       control.minValue, control.maxValue, macroSuffix).toStdString();

        macroControls.push_back (std::move (control));
    }

    //--------------------------------------------------------------------------
    // 7. The full, ordered control list: macros first, then parameters in
    //    display order. Groups therefore emerge Macros -> first parameter group
    //    -> ... -> Mix, which is the Zarlino design system's reading order.
    //--------------------------------------------------------------------------
    std::vector<UiControl> ordered;
    ordered.reserve (macroControls.size() + parameterControls.size());
    ordered.insert (ordered.end(), macroControls.begin(), macroControls.end());
    ordered.insert (ordered.end(), parameterControls.begin(), parameterControls.end());

    plan.totalControlCount = static_cast<int> (ordered.size());
    plan.macroCount = static_cast<int> (macroControls.size());

    //--------------------------------------------------------------------------
    // 8. Template policy. `priorityOrder` is "most important first, then display
    //    order" over the non-internal controls; each template keeps a prefix of
    //    it, which is what makes the three templates strictly nested.
    //--------------------------------------------------------------------------
    std::vector<int> priorityOrder;
    priorityOrder.reserve (ordered.size());

    for (size_t i = 0; i < ordered.size(); ++i)
        if (ordered[i].exposedToHost)
            priorityOrder.push_back (static_cast<int> (i));

    std::stable_sort (priorityOrder.begin(), priorityOrder.end(),
                      [&ordered] (int a, int b)
                      {
                          return importanceRank (ordered[static_cast<size_t> (a)].importance)
                                 < importanceRank (ordered[static_cast<size_t> (b)].importance);
                      });

    int primaryCount = 0;
    int secondaryCount = 0;

    for (size_t i = 0; i < ordered.size(); ++i)
    {
        if (! ordered[i].exposedToHost)
            continue;

        if (ordered[i].importance == UiImportance::Primary)
            ++primaryCount;
        else if (ordered[i].importance == UiImportance::Secondary)
            ++secondaryCount;
    }

    int keepCount = static_cast<int> (ordered.size());

    switch (uiTemplate)
    {
        case UiTemplate::Minimal:
            keepCount = std::max (kMinimalFloor, primaryCount);
            keepCount = std::min (keepCount, kMinimalCeiling);
            break;

        case UiTemplate::Standard:
            keepCount = std::max (kStandardFloor, primaryCount + secondaryCount);
            keepCount = std::min (keepCount, kStandardCeiling);
            break;

        case UiTemplate::Advanced:
            keepCount = static_cast<int> (ordered.size());
            break;
    }

    std::vector<bool> keep (ordered.size(), false);

    if (uiTemplate == UiTemplate::Advanced)
    {
        std::fill (keep.begin(), keep.end(), true);
    }
    else
    {
        const auto limit = std::min (keepCount, static_cast<int> (priorityOrder.size()));
        for (int i = 0; i < limit; ++i)
            keep[static_cast<size_t> (priorityOrder[static_cast<size_t> (i)])] = true;
    }

    //--------------------------------------------------------------------------
    // 9. Materialise the drawn controls and their groups, preserving order.
    //--------------------------------------------------------------------------
    for (size_t i = 0; i < ordered.size(); ++i)
    {
        if (! keep[i])
            continue;

        auto control = ordered[i];

        int groupIndex = -1;
        for (size_t g = 0; g < plan.groups.size(); ++g)
        {
            if (plan.groups[g].name == control.group)
            {
                groupIndex = static_cast<int> (g);
                break;
            }
        }

        if (groupIndex < 0)
        {
            UiGroup group;
            group.name = control.group;
            plan.groups.push_back (std::move (group));
            groupIndex = static_cast<int> (plan.groups.size()) - 1;
        }

        auto& group = plan.groups[static_cast<size_t> (groupIndex)];
        group.controlIndices.push_back (static_cast<int> (plan.controls.size()));

        if (importanceRank (control.importance) < importanceRank (group.importance)
            || group.controlIndices.size() == 1)
            group.importance = control.importance;

        plan.controls.push_back (std::move (control));
    }

    //--------------------------------------------------------------------------
    // 10. Module-level controls.
    //
    // The specs above expose a curated subset of each module: a compressor
    // module declares attack, release, knee and detection, but a channel strip
    // usually curates that down to threshold, ratio and makeup. Standard shows
    // exactly what the spec declares. Advanced additionally surfaces the rest of
    // every certified module's own parameter set, which is the only honest way
    // for a template to be genuinely denser rather than merely rearranged.
    //
    // The candidate set is computed for every template - it is a property of the
    // spec and the certified catalog, not of the chosen layout - so that the
    // three templates are measured against the same available surface.
    //
    // These controls are NOT host parameters: the spec never declared them, so
    // the host has no automatable ID for them. The plugin shell draws them and
    // persists their values itself (kernelManaged == false), next to the kernel's
    // own state blob.
    //--------------------------------------------------------------------------
    std::vector<UiControl> moduleControls;

    for (size_t m = 0; m < spec.modules.size(); ++m)
    {
        const auto* manifest = resolved[m].manifest;

        if (manifest == nullptr)
            continue;

        for (size_t p = 0; p < manifest->parameters.size(); ++p)
        {
            if (coveredParameters[m][p])
                continue;

            const auto& parameterManifest = manifest->parameters[p];

            UiControl control;
            control.id = spec.modules[m].instanceId + "." + parameterManifest.id;
            control.label = parameterManifest.displayName.empty() ? parameterManifest.id
                                                                  : parameterManifest.displayName;
            control.unit = parameterManifest.unit;
            control.group = manifest->displayName.empty() ? spec.modules[m].instanceId
                                                          : manifest->displayName;
            control.importance = UiImportance::Tertiary;
            control.kind = kindFromDomain (&parameterManifest, false);
            control.exposedToHost = false;
            control.moduleLevel = true;
            control.kernelManaged = false;
            control.moduleIndex = static_cast<int> (m);
            control.moduleParameterIndex = static_cast<int> (p);
            control.stepCount = parameterManifest.stepped ? parameterManifest.stepCount : 0;
            control.targetInstanceId = spec.modules[m].instanceId;
            control.targetRef = control.id;
            control.stableIdHash = stableIdHash32 (control.id);

            control.minValue = parameterManifest.minValue;
            control.maxValue = parameterManifest.maxValue;
            control.defaultValue = parameterManifest.defaultValue;
            control.logarithmic = parameterManifest.logarithmic;
            control.defaultValueNormalised = parameterManifest.normalise (parameterManifest.defaultValue);

            juce::String moduleSuffix;
            moduleSuffix << " [" << juce::String (spec.modules[m].instanceId) << "]";

            control.tooltip = makeTooltip (control.label, control.unit,
                                           control.minValue, control.maxValue,
                                           moduleSuffix).toStdString();

            moduleControls.push_back (std::move (control));
        }
    }

    plan.moduleLevelControlCount = static_cast<int> (moduleControls.size());

    if (uiTemplate == UiTemplate::Advanced)
    {
        for (auto& control : moduleControls)
        {
            int groupIndex = -1;
            for (size_t g = 0; g < plan.groups.size(); ++g)
            {
                if (plan.groups[g].name == control.group)
                {
                    groupIndex = static_cast<int> (g);
                    break;
                }
            }

            if (groupIndex < 0)
            {
                UiGroup group;
                group.name = control.group;
                group.importance = UiImportance::Tertiary;
                plan.groups.push_back (std::move (group));
                groupIndex = static_cast<int> (plan.groups.size()) - 1;
            }

            auto& group = plan.groups[static_cast<size_t> (groupIndex)];
            group.controlIndices.push_back (static_cast<int> (plan.controls.size()));
            plan.controls.push_back (std::move (control));
        }
    }

    plan.drawnControlCount = static_cast<int> (plan.controls.size());
    plan.hiddenByTemplateCount = plan.availableControlCount() - plan.drawnControlCount;
    plan.groupCount = static_cast<int> (plan.groups.size());

    // Column span hint: a group holding a single control reads better full width.
    for (auto& group : plan.groups)
        group.columnSpan = group.controlIndices.size() <= 1 ? 2 : 1;

    return Result<UiPlan>::success (std::move (plan));
}

//==============================================================================
uint64_t UiPlan::fingerprint() const
{
    uint64_t hash = 0xcbf29ce484222325ull;

    hash = hashString (hash, uiTemplateName (uiTemplate));
    hash = hashString (hash, title);
    hash = hashString (hash, vendor);
    hash = hashString (hash, pluginVersion);
    hash = hashString (hash, accentColor);
    hash = hashInt (hash, static_cast<int64_t> (numChannels (channels)));
    hash = hashInt (hash, static_cast<int64_t> (static_cast<int> (effectType)));

    for (const auto& control : controls)
    {
        hash = hashString (hash, control.id);
        hash = hashString (hash, control.label);
        hash = hashString (hash, control.unit);
        hash = hashString (hash, control.group);
        hash = hashInt (hash, static_cast<int64_t> (static_cast<int> (control.importance)));
        hash = hashInt (hash, static_cast<int64_t> (static_cast<int> (control.kind)));
        hash = hashInt (hash, static_cast<int64_t> (control.hostParameterIndex));
        hash = hashInt (hash, static_cast<int64_t> (control.specParameterIndex));
        hash = hashInt (hash, static_cast<int64_t> (control.stableIdHash));
        hash = hashInt (hash, control.isMacro ? 1 : 0);
        hash = hashInt (hash, control.exposedToHost ? 1 : 0);
        hash = hashInt (hash, control.moduleLevel ? 1 : 0);
        hash = hashInt (hash, control.kernelManaged ? 1 : 0);
        hash = hashInt (hash, static_cast<int64_t> (control.moduleIndex));
        hash = hashInt (hash, static_cast<int64_t> (control.moduleParameterIndex));
        hash = hashFloat (hash, control.minValue);
        hash = hashFloat (hash, control.maxValue);
        hash = hashFloat (hash, control.defaultValue);
    }

    for (const auto& group : groups)
    {
        hash = hashString (hash, group.name);
        hash = hashInt (hash, static_cast<int64_t> (group.controlIndices.size()));

        for (const auto index : group.controlIndices)
            hash = hashInt (hash, static_cast<int64_t> (index));
    }

    return hash;
}

//==============================================================================
juce::var UiPlan::toJson() const
{
    auto* root = new juce::DynamicObject();

    root->setProperty ("uiTemplate", uiTemplateName (uiTemplate));
    root->setProperty ("title", juce::String (title));
    root->setProperty ("vendor", juce::String (vendor));
    root->setProperty ("pluginVersion", juce::String (pluginVersion));
    root->setProperty ("category", juce::String (category));
    root->setProperty ("accentColor", juce::String (accentColor));
    root->setProperty ("channels", juce::String (channelModeName (channels)));
    root->setProperty ("effectType", juce::String (effectTypeName (effectType)));
    root->setProperty ("totalControlCount", totalControlCount);
    root->setProperty ("moduleLevelControlCount", moduleLevelControlCount);
    root->setProperty ("availableControlCount", availableControlCount());
    root->setProperty ("drawnControlCount", drawnControlCount);
    root->setProperty ("hiddenByTemplateCount", hiddenByTemplateCount);
    root->setProperty ("macroCount", macroCount);
    root->setProperty ("groupCount", groupCount);
    root->setProperty ("density", density());
    root->setProperty ("fingerprint", juce::String::toHexString (static_cast<juce::int64> (fingerprint())));

    juce::Array<juce::var> tagArray;
    for (const auto& tag : tags)
        tagArray.add (juce::String (tag));
    root->setProperty ("tags", tagArray);

    juce::Array<juce::var> groupArray;
    for (const auto& group : groups)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("name", juce::String (group.name));
        object->setProperty ("importance", uiImportanceName (group.importance));
        object->setProperty ("columnSpan", group.columnSpan);

        juce::Array<juce::var> members;
        for (const auto index : group.controlIndices)
            members.add (juce::String (controls[static_cast<size_t> (index)].id));
        object->setProperty ("controls", members);

        groupArray.add (juce::var (object));
    }
    root->setProperty ("groups", groupArray);

    juce::Array<juce::var> controlArray;
    for (const auto& control : controls)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("id", juce::String (control.id));
        object->setProperty ("label", juce::String (control.label));
        object->setProperty ("unit", juce::String (control.unit));
        object->setProperty ("group", juce::String (control.group));
        object->setProperty ("importance", uiImportanceName (control.importance));
        object->setProperty ("kind", uiControlKindName (control.kind));
        object->setProperty ("isMacro", control.isMacro);
        object->setProperty ("isFactoryControl", control.isFactoryControl);
        object->setProperty ("exposedToHost", control.exposedToHost);
        object->setProperty ("macroDriven", control.macroDriven);
        object->setProperty ("moduleLevel", control.moduleLevel);
        object->setProperty ("kernelManaged", control.kernelManaged);
        object->setProperty ("moduleIndex", control.moduleIndex);
        object->setProperty ("moduleParameterIndex", control.moduleParameterIndex);
        object->setProperty ("minValue", static_cast<double> (control.minValue));
        object->setProperty ("maxValue", static_cast<double> (control.maxValue));
        object->setProperty ("defaultValue", static_cast<double> (control.defaultValue));
        object->setProperty ("logarithmic", control.logarithmic);
        object->setProperty ("stepCount", control.stepCount);
        object->setProperty ("specParameterIndex", control.specParameterIndex);
        object->setProperty ("hostParameterIndex", control.hostParameterIndex);
        object->setProperty ("stableId", juce::String::toHexString (static_cast<juce::int64> (control.stableIdHash)));
        object->setProperty ("targetRef", juce::String (control.targetRef));

        controlArray.add (juce::var (object));
    }
    root->setProperty ("controls", controlArray);

    return juce::var (root);
}

std::string UiPlan::toJsonText() const
{
    // Single line on purpose: this text is embedded verbatim into generated C++
    // and written into build provenance records, where a stable, line-oriented
    // representation is what makes the plan diffable and hashable.
    return juce::JSON::toString (toJson(), true).toStdString();
}

//==============================================================================
std::string UiPlan::describe() const
{
    juce::String text;

    text << "UiPlan " << uiTemplateName (uiTemplate)
         << " template=\"" << juce::String (title) << "\""
         << " controls=" << drawnControlCount
         << "/" << availableControlCount()
         << " (spec=" << totalControlCount
         << " module-level=" << moduleLevelControlCount << ")"
         << " groups=" << groupCount
         << " macros=" << macroCount
         << " density=" << juce::String (density(), 3) << "\n";

    for (const auto& group : groups)
    {
        text << "  [" << juce::String (group.name) << "] " << uiImportanceName (group.importance) << "\n";

        for (const auto index : group.controlIndices)
        {
            const auto& control = controls[static_cast<size_t> (index)];
            text << "    - " << juce::String (control.id)
                 << " (\"" << juce::String (control.label) << "\""
                 << " " << uiControlKindName (control.kind);

            if (! control.unit.empty())
                text << " " << juce::String (control.unit);

            text << " " << uiImportanceName (control.importance);

            if (control.isMacro)
                text << " macro";

            if (control.moduleLevel)
                text << " module-level";

            if (! control.exposedToHost)
                text << " internal";

            text << ")\n";
        }
    }

    return text.toStdString();
}

} // namespace zfactory
