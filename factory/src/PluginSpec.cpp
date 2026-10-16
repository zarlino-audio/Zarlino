#include "zfactory/PluginSpec.h"
#include "zfactory/JsonUtil.h"
#include "zfactory/ModuleManifest.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace zfactory
{

namespace
{
using json::childPath;
using json::indexPath;

//==============================================================================
// The factory's two virtual controls. They are addressed exactly like a module
// parameter ("$factory.dryWet", "$factory.bypass") so the rest of the parameter
// and macro machinery needs no special cases.
const char* const kFactoryInstanceId = "$factory";
const char* const kFactoryDryWet = "dryWet";
const char* const kFactoryBypass = "bypass";

bool isFactoryTargetValid (const std::string& instanceId, const std::string& parameterId)
{
    if (instanceId != kFactoryInstanceId)
        return false;

    return parameterId == kFactoryDryWet || parameterId == kFactoryBypass;
}

bool isHexDigit (char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool isValidColour (const std::string& text)
{
    if (text.size() != 7 || text[0] != '#')
        return false;

    for (size_t i = 1; i < text.size(); ++i)
        if (! isHexDigit (text[i]))
            return false;

    return true;
}

bool isTagValid (const std::string& tag)
{
    if (tag.empty() || tag.size() > 32)
        return false;

    for (char c : tag)
        if (! ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return false;

    return true;
}

bool splitTarget (const std::string& target, std::string& instanceId, std::string& parameterId)
{
    const size_t dot = target.find ('.');

    if (dot == std::string::npos || dot == 0 || dot + 1 >= target.size())
        return false;

    instanceId = target.substr (0, dot);
    parameterId = target.substr (dot + 1);
    return true;
}

//==============================================================================
const std::vector<std::string>& rootKeys()
{
    static const std::vector<std::string> keys {
        "factorySchemaVersion", "plugin", "graph", "parameters",
        "macros", "dryWet", "bypass", "ui"
    };
    return keys;
}

const std::vector<std::string>& pluginKeys()
{
    static const std::vector<std::string> keys {
        "id", "name", "vendor", "version", "effectType", "channels"
    };
    return keys;
}

const std::vector<std::string>& graphKeys()
{
    static const std::vector<std::string> keys { "modules" };
    return keys;
}

const std::vector<std::string>& moduleKeys()
{
    static const std::vector<std::string> keys {
        "instanceId", "moduleId", "moduleVersion", "parameters"
    };
    return keys;
}

const std::vector<std::string>& parameterKeys()
{
    static const std::vector<std::string> keys {
        "id", "name", "target", "range", "unit", "logarithmic", "group", "exposeToHost"
    };
    return keys;
}

const std::vector<std::string>& rangeKeys()
{
    static const std::vector<std::string> keys { "min", "max", "default" };
    return keys;
}

const std::vector<std::string>& macroKeys()
{
    static const std::vector<std::string> keys { "id", "name", "defaultValue", "mappings" };
    return keys;
}

const std::vector<std::string>& mappingKeys()
{
    static const std::vector<std::string> keys {
        "parameter", "rangeStart", "rangeEnd", "curve", "scale", "invert"
    };
    return keys;
}

const std::vector<std::string>& dryWetKeys()
{
    static const std::vector<std::string> keys { "enabled", "defaultMix" };
    return keys;
}

const std::vector<std::string>& bypassKeys()
{
    static const std::vector<std::string> keys { "mode", "defaultBypassed" };
    return keys;
}

const std::vector<std::string>& uiKeys()
{
    static const std::vector<std::string> keys {
        "accentColor", "category", "shortDescription", "tags", "layout", "parameterOrder"
    };
    return keys;
}

//==============================================================================
float asFloat (double value)
{
    return static_cast<float> (value);
}
} // namespace

//==============================================================================
const SpecModuleInstance* PluginSpec::findModule (const std::string& instanceId) const
{
    for (const auto& module : modules)
        if (module.instanceId == instanceId)
            return &module;

    return nullptr;
}

const SpecParameter* PluginSpec::findParameter (const std::string& parameterId) const
{
    for (const auto& parameter : parameters)
        if (parameter.id == parameterId)
            return &parameter;

    return nullptr;
}

int PluginSpec::exposedParameterCount() const
{
    int count = 0;

    for (const auto& parameter : parameters)
        if (parameter.exposeToHost)
            ++count;

    return count;
}

float SpecModuleInstance::parameterValueOrDefault (const std::string& parameterId, float fallback) const
{
    for (const auto& entry : parameterValues)
        if (entry.first == parameterId)
            return entry.second;

    return fallback;
}

//==============================================================================
// Parsing
//==============================================================================
namespace
{

class SpecParser
{
public:
    explicit SpecParser (const juce::DynamicObject& rootObject) : root (rootObject) {}

    PluginSpec spec;
    FactoryError error;

    bool run()
    {
        // The schema version is checked first: a document written against a
        // different schema must be rejected as such, before we interpret any of
        // its fields.
        if (! parseSchemaVersion())  return false;

        if (! json::checkKeys (root, "$", rootKeys(), error))
            return false;

        if (! parsePlugin())         return false;
        if (! parseGraph())          return false;
        if (! parseParameters())     return false;
        if (! parseMacros())         return false;
        if (! parseDryWet())         return false;
        if (! parseBypass())         return false;
        if (! parseUi())             return false;

        error = validatePluginSpecSchema (spec);
        return error.ok();
    }

private:
    bool fail (ErrorCode code, const std::string& path, const std::string& message)
    {
        error = makeError (code, path, message);
        return false;
    }

    bool parseSchemaVersion()
    {
        int version = 0;

        if (! json::requireInt (root, "$", "factorySchemaVersion", version, error))
            return false;

        if (version != kSchemaVersion)
            return fail (ErrorCode::UnsupportedSchemaVersion, "$/factorySchemaVersion",
                         "unsupported schema version " + std::to_string (version)
                             + " (kernel supports " + std::to_string (kSchemaVersion) + ")");

        spec.schemaVersion = version;
        return true;
    }

    bool parsePlugin()
    {
        const juce::DynamicObject* plugin = nullptr;

        if (! json::requireObject (root, "$", "plugin", plugin, error))
            return false;

        const std::string path = "$/plugin";

        if (! json::checkKeys (*plugin, path, pluginKeys(), error))
            return false;

        if (! json::requireString (*plugin, path, "id", spec.pluginId, error))            return false;
        if (! json::requireString (*plugin, path, "name", spec.pluginName, error))        return false;
        if (! json::requireString (*plugin, path, "vendor", spec.vendor, error))          return false;
        if (! json::requireString (*plugin, path, "version", spec.pluginVersion, error))  return false;

        std::string effectTypeText;

        if (! json::requireString (*plugin, path, "effectType", effectTypeText, error))
            return false;

        if (! parseEffectType (effectTypeText, spec.effectType))
            return fail (ErrorCode::UnsupportedValue, childPath (path, "effectType"),
                         "unsupported effectType '" + effectTypeText + "'");

        std::string channelsText;

        if (! json::requireString (*plugin, path, "channels", channelsText, error))
            return false;

        if (! parseChannelMode (channelsText, spec.channels))
            return fail (ErrorCode::UnsupportedValue, childPath (path, "channels"),
                         "unsupported channel mode '" + channelsText + "' (expected 'mono' or 'stereo')");

        return true;
    }

    bool parseGraph()
    {
        const juce::DynamicObject* graph = nullptr;

        if (! json::requireObject (root, "$", "graph", graph, error))
            return false;

        const std::string path = "$/graph";

        if (! json::checkKeys (*graph, path, graphKeys(), error))
            return false;

        const juce::Array<juce::var>* modules = nullptr;

        if (! json::requireArray (*graph, path, "modules", modules, error))
            return false;

        if (modules->isEmpty())
            return fail (ErrorCode::EmptyCollection, childPath (path, "modules"),
                         "graph must contain at least one module");

        if (modules->size() > kMaxGraphModules)
            return fail (ErrorCode::CollectionTooLarge, childPath (path, "modules"),
                         "graph contains " + std::to_string (modules->size())
                             + " modules (limit " + std::to_string (kMaxGraphModules) + ")");

        for (int i = 0; i < modules->size(); ++i)
        {
            if (! parseModule ((*modules)[i], indexPath (childPath (path, "modules"), i)))
                return false;
        }

        return true;
    }

    bool parseModule (const juce::var& element, const std::string& path)
    {
        const juce::DynamicObject* module = nullptr;

        if (! json::elementAsObject (element, path, module, error))
            return false;

        if (! json::checkKeys (*module, path, moduleKeys(), error))
            return false;

        SpecModuleInstance instance;

        if (! json::requireString (*module, path, "instanceId", instance.instanceId, error))    return false;
        if (! json::requireString (*module, path, "moduleId", instance.moduleId, error))        return false;

        std::string versionText;

        if (! json::requireString (*module, path, "moduleVersion", versionText, error))
            return false;

        if (! parseSemVer (versionText, instance.moduleVersion))
            return fail (ErrorCode::TypeMismatch, childPath (path, "moduleVersion"),
                         "moduleVersion '" + versionText + "' is not MAJOR.MINOR.PATCH");

        const juce::DynamicObject* values = nullptr;

        if (! json::requireObject (*module, path, "parameters", values, error))
            return false;

        const auto& properties = values->getProperties();

        for (int i = 0; i < properties.size(); ++i)
        {
            const auto key = properties.getName (i).toString().toStdString();

            if (! json::isValidIdentifier (key))
                return fail (ErrorCode::InvalidIdentifier, childPath (path, "parameters/" + key),
                             "parameter id is not a valid identifier");

            double value = 0.0;

            if (! json::elementAsNumber (properties.getValueAt (i), childPath (path, "parameters/" + key), value, error))
                return false;

            instance.parameterValues.emplace_back (key, asFloat (value));
        }

        spec.modules.push_back (std::move (instance));
        return true;
    }

    bool parseParameters()
    {
        const juce::Array<juce::var>* parameters = nullptr;

        if (! json::requireArray (root, "$", "parameters", parameters, error))
            return false;

        if (parameters->size() > kMaxExposedParameters)
            return fail (ErrorCode::CollectionTooLarge, "$/parameters",
                         "too many parameters (" + std::to_string (parameters->size())
                             + ", limit " + std::to_string (kMaxExposedParameters) + ")");

        for (int i = 0; i < parameters->size(); ++i)
        {
            if (! parseParameter ((*parameters)[i], indexPath ("$/parameters", i)))
                return false;
        }

        return true;
    }

    bool parseParameter (const juce::var& element, const std::string& path)
    {
        const juce::DynamicObject* parameter = nullptr;

        if (! json::elementAsObject (element, path, parameter, error))
            return false;

        if (! json::checkKeys (*parameter, path, parameterKeys(), error))
            return false;

        SpecParameter parsed;

        if (! json::requireString (*parameter, path, "id", parsed.id, error))     return false;
        if (! json::requireString (*parameter, path, "name", parsed.displayName, error)) return false;

        std::string target;

        if (! json::requireString (*parameter, path, "target", target, error))
            return false;

        if (! splitTarget (target, parsed.targetInstanceId, parsed.targetParameterId))
            return fail (ErrorCode::UnsupportedValue, childPath (path, "target"),
                         "target '" + target + "' must be '<moduleInstanceId>.<parameterId>'");

        const juce::DynamicObject* range = nullptr;

        if (! json::requireObject (*parameter, path, "range", range, error))
            return false;

        const std::string rangePath = childPath (path, "range");

        if (! json::checkKeys (*range, rangePath, rangeKeys(), error))
            return false;

        double minValue = 0.0, maxValue = 0.0, defaultValue = 0.0;

        if (! json::requireNumber (*range, rangePath, "min", minValue, error))     return false;
        if (! json::requireNumber (*range, rangePath, "max", maxValue, error))     return false;
        if (! json::requireNumber (*range, rangePath, "default", defaultValue, error)) return false;

        parsed.range.minValue = asFloat (minValue);
        parsed.range.maxValue = asFloat (maxValue);
        parsed.range.defaultValue = asFloat (defaultValue);

        if (! json::optionalString (*parameter, path, "unit", parsed.unit, error))        return false;
        if (! json::optionalBool (*parameter, path, "logarithmic", parsed.logarithmic, error)) return false;
        if (! json::optionalBool (*parameter, path, "exposeToHost", parsed.exposeToHost, error)) return false;
        if (! json::optionalString (*parameter, path, "group", parsed.group, error))      return false;

        spec.parameters.push_back (std::move (parsed));
        return true;
    }

    bool parseMacros()
    {
        const juce::Array<juce::var>* macros = nullptr;

        if (! json::requireArray (root, "$", "macros", macros, error))
            return false;

        if (macros->size() > kMaxMacros)
            return fail (ErrorCode::CollectionTooLarge, "$/macros",
                         "too many macros (" + std::to_string (macros->size())
                             + ", limit " + std::to_string (kMaxMacros) + ")");

        int totalMappings = 0;

        for (int i = 0; i < macros->size(); ++i)
        {
            if (! parseMacro ((*macros)[i], indexPath ("$/macros", i), totalMappings))
                return false;
        }

        return true;
    }

    bool parseMacro (const juce::var& element, const std::string& path, int& totalMappings)
    {
        const juce::DynamicObject* macro = nullptr;

        if (! json::elementAsObject (element, path, macro, error))
            return false;

        if (! json::checkKeys (*macro, path, macroKeys(), error))
            return false;

        SpecMacro parsed;

        if (! json::requireString (*macro, path, "id", parsed.id, error))       return false;
        if (! json::requireString (*macro, path, "name", parsed.displayName, error)) return false;

        double defaultValue = 0.0;

        if (! json::requireNumber (*macro, path, "defaultValue", defaultValue, error))
            return false;

        parsed.defaultValue = asFloat (defaultValue);

        const juce::Array<juce::var>* mappings = nullptr;

        if (! json::requireArray (*macro, path, "mappings", mappings, error))
            return false;

        if (mappings->isEmpty())
            return fail (ErrorCode::EmptyCollection, childPath (path, "mappings"),
                         "macro '" + parsed.id + "' has no mappings");

        totalMappings += mappings->size();

        if (totalMappings > kMaxMacroMappings)
            return fail (ErrorCode::CollectionTooLarge, childPath (path, "mappings"),
                         "too many macro mappings in total (limit " + std::to_string (kMaxMacroMappings) + ")");

        for (int i = 0; i < mappings->size(); ++i)
        {
            if (! parseMapping ((*mappings)[i], indexPath (childPath (path, "mappings"), i), parsed))
                return false;
        }

        spec.macros.push_back (std::move (parsed));
        return true;
    }

    bool parseMapping (const juce::var& element, const std::string& path, SpecMacro& macro)
    {
        const juce::DynamicObject* mapping = nullptr;

        if (! json::elementAsObject (element, path, mapping, error))
            return false;

        if (! json::checkKeys (*mapping, path, mappingKeys(), error))
            return false;

        SpecMacroMapping parsed;

        if (! json::requireString (*mapping, path, "parameter", parsed.parameterId, error))
            return false;

        double rangeStart = 0.0, rangeEnd = 1.0, curve = 1.0, scale = 1.0;

        if (! json::optionalNumber (*mapping, path, "rangeStart", rangeStart, error)) return false;
        if (! json::optionalNumber (*mapping, path, "rangeEnd", rangeEnd, error))     return false;
        if (! json::optionalNumber (*mapping, path, "curve", curve, error))           return false;
        if (! json::optionalNumber (*mapping, path, "scale", scale, error))           return false;
        if (! json::optionalBool (*mapping, path, "invert", parsed.invert, error))    return false;

        parsed.rangeStart = asFloat (rangeStart);
        parsed.rangeEnd = asFloat (rangeEnd);
        parsed.curve = asFloat (curve);
        parsed.scale = asFloat (scale);

        macro.mappings.push_back (parsed);
        return true;
    }

    bool parseDryWet()
    {
        const juce::DynamicObject* dryWet = nullptr;

        if (! json::requireObject (root, "$", "dryWet", dryWet, error))
            return false;

        const std::string path = "$/dryWet";

        if (! json::checkKeys (*dryWet, path, dryWetKeys(), error))
            return false;

        if (! json::requireBool (*dryWet, path, "enabled", spec.dryWet.enabled, error))
            return false;

        double mix = 1.0;

        if (! json::requireNumber (*dryWet, path, "defaultMix", mix, error))
            return false;

        spec.dryWet.defaultMix = asFloat (mix);
        return true;
    }

    bool parseBypass()
    {
        const juce::DynamicObject* bypass = nullptr;

        if (! json::requireObject (root, "$", "bypass", bypass, error))
            return false;

        const std::string path = "$/bypass";

        if (! json::checkKeys (*bypass, path, bypassKeys(), error))
            return false;

        if (! json::requireString (*bypass, path, "mode", spec.bypass.mode, error))
            return false;

        return json::optionalBool (*bypass, path, "defaultBypassed", spec.bypass.defaultBypassed, error);
    }

    bool parseUi()
    {
        const juce::DynamicObject* ui = nullptr;

        if (! json::requireObject (root, "$", "ui", ui, error))
            return false;

        const std::string path = "$/ui";

        if (! json::checkKeys (*ui, path, uiKeys(), error))
            return false;

        if (! json::requireString (*ui, path, "accentColor", spec.ui.accentColor, error)) return false;
        if (! json::requireString (*ui, path, "category", spec.ui.category, error))       return false;
        if (! json::requireString (*ui, path, "layout", spec.ui.layout, error))           return false;

        if (! json::optionalString (*ui, path, "shortDescription", spec.ui.shortDescription, error))
            return false;

        if (ui->hasProperty ("tags"))
        {
            const juce::Array<juce::var>* tags = nullptr;

            if (! json::requireArray (*ui, path, "tags", tags, error))
                return false;

            std::vector<std::string> parsedTags;

            for (int i = 0; i < tags->size(); ++i)
            {
                std::string tag;

                if (! json::elementAsString ((*tags)[i], indexPath (childPath (path, "tags"), i), tag, error))
                    return false;

                parsedTags.push_back (std::move (tag));
            }

            spec.ui.tags = std::move (parsedTags);
        }

        if (ui->hasProperty ("parameterOrder"))
        {
            const juce::Array<juce::var>* order = nullptr;

            if (! json::requireArray (*ui, path, "parameterOrder", order, error))
                return false;

            std::vector<std::string> parsedOrder;

            for (int i = 0; i < order->size(); ++i)
            {
                std::string id;

                if (! json::elementAsString ((*order)[i], indexPath (childPath (path, "parameterOrder"), i), id, error))
                    return false;

                parsedOrder.push_back (std::move (id));
            }

            spec.ui.parameterOrder = std::move (parsedOrder);
        }

        return true;
    }

    const juce::DynamicObject& root;
};

} // namespace

//==============================================================================
FactoryError validatePluginSpecSchema (const PluginSpec& spec)
{
    // ---- Plugin identity ----------------------------------------------------
    if (! json::isValidIdentifier (spec.pluginId))
        return makeError (ErrorCode::InvalidIdentifier, "$/plugin/id",
                          "plugin id '" + spec.pluginId + "' is not a valid identifier");

    if (spec.pluginName.empty() || spec.pluginName.size() > 96)
        return makeError (ErrorCode::ValueOutOfRange, "$/plugin/name",
                          "plugin name length " + std::to_string (spec.pluginName.size()) + " is outside 1..96");

    if (spec.vendor.empty() || spec.vendor.size() > 96)
        return makeError (ErrorCode::ValueOutOfRange, "$/plugin/vendor",
                          "vendor length " + std::to_string (spec.vendor.size()) + " is outside 1..96");

    SemVer pluginVersion;

    if (! parseSemVer (spec.pluginVersion, pluginVersion))
        return makeError (ErrorCode::TypeMismatch, "$/plugin/version",
                          "plugin version '" + spec.pluginVersion + "' is not MAJOR.MINOR.PATCH");

    // ---- Graph --------------------------------------------------------------
    if (spec.modules.empty())
        return makeError (ErrorCode::GraphEmpty, "$/graph/modules",
                          "graph must contain at least one module");

    if (static_cast<int> (spec.modules.size()) > kMaxGraphModules)
        return makeError (ErrorCode::GraphTooLong, "$/graph/modules",
                          "graph contains " + std::to_string (spec.modules.size())
                              + " modules (limit " + std::to_string (kMaxGraphModules) + ")");

    std::set<std::string> instanceIds;

    for (size_t i = 0; i < spec.modules.size(); ++i)
    {
        const auto& module = spec.modules[i];
        const std::string path = indexPath ("$/graph/modules", static_cast<int> (i));

        if (! json::isValidIdentifier (module.instanceId))
            return makeError (ErrorCode::InvalidIdentifier, childPath (path, "instanceId"),
                              "instance id '" + module.instanceId + "' is not a valid identifier");

        if (! instanceIds.insert (module.instanceId).second)
            return makeError (ErrorCode::DuplicateIdentifier, childPath (path, "instanceId"),
                              "duplicate module instance id '" + module.instanceId + "'");

        if (! json::isValidIdentifier (module.moduleId))
            return makeError (ErrorCode::InvalidIdentifier, childPath (path, "moduleId"),
                              "module id '" + module.moduleId + "' is not a valid identifier");

        SemVer requested;

        if (! parseSemVer (module.moduleVersion.to_string(), requested))
            return makeError (ErrorCode::TypeMismatch, childPath (path, "moduleVersion"),
                              "module version is not MAJOR.MINOR.PATCH");
    }

    // ---- Parameters ---------------------------------------------------------
    if (static_cast<int> (spec.parameters.size()) > kMaxExposedParameters)
        return makeError (ErrorCode::CollectionTooLarge, "$/parameters",
                          "too many parameters (limit " + std::to_string (kMaxExposedParameters) + ")");

    std::set<std::string> parameterIds;

    for (size_t i = 0; i < spec.parameters.size(); ++i)
    {
        const auto& parameter = spec.parameters[i];
        const std::string path = indexPath ("$/parameters", static_cast<int> (i));

        if (! json::isValidIdentifier (parameter.id))
            return makeError (ErrorCode::InvalidIdentifier, childPath (path, "id"),
                              "parameter id '" + parameter.id + "' is not a valid identifier");

        if (! parameterIds.insert (parameter.id).second)
            return makeError (ErrorCode::DuplicateIdentifier, childPath (path, "id"),
                              "duplicate parameter id '" + parameter.id + "'");

        if (parameter.displayName.empty() || parameter.displayName.size() > 96)
            return makeError (ErrorCode::ValueOutOfRange, childPath (path, "name"),
                              "parameter name length is outside 1..96");

        if (! (parameter.range.minValue < parameter.range.maxValue))
            return makeError (ErrorCode::ValueOutOfRange, childPath (path, "range/min"),
                              "range min must be strictly less than max");

        if (! std::isfinite (parameter.range.minValue) || ! std::isfinite (parameter.range.maxValue)
            || ! std::isfinite (parameter.range.defaultValue))
            return makeError (ErrorCode::ValueOutOfRange, childPath (path, "range"),
                              "range values must be finite");

        if (parameter.range.defaultValue < parameter.range.minValue
            || parameter.range.defaultValue > parameter.range.maxValue)
            return makeError (ErrorCode::ValueOutOfRange, childPath (path, "range/default"),
                              "default must lie inside [min, max]");

        if (parameter.logarithmic && parameter.range.minValue <= 0.0f)
            return makeError (ErrorCode::ValueOutOfRange, childPath (path, "logarithmic"),
                              "logarithmic parameters require a strictly positive range min");

        if (parameter.targetInstanceId.empty() || parameter.targetParameterId.empty())
            return makeError (ErrorCode::UnsupportedValue, childPath (path, "target"),
                              "target must be '<moduleInstanceId>.<parameterId>' or '$factory.dryWet' / '$factory.bypass'");

        if (parameter.isFactoryTarget())
        {
            if (! isFactoryTargetValid (parameter.targetInstanceId, parameter.targetParameterId))
                return makeError (ErrorCode::UnsupportedValue, childPath (path, "target"),
                                  "unknown factory control '" + parameter.targetRef()
                                      + "' (supported: $factory.dryWet, $factory.bypass)");

            if (parameter.targetParameterId == kFactoryDryWet && ! spec.dryWet.enabled)
                return makeError (ErrorCode::UnsupportedValue, childPath (path, "target"),
                                  "target '$factory.dryWet' requires dryWet.enabled = true");

            if (parameter.range.minValue < 0.0f || parameter.range.maxValue > 1.0f)
                return makeError (ErrorCode::ValueOutOfRange, childPath (path, "range"),
                                  "a factory control parameter must be bounded to 0..1");
        }
        else
        {
            if (! json::isValidIdentifier (parameter.targetInstanceId))
                return makeError (ErrorCode::InvalidIdentifier, childPath (path, "target"),
                                  "target instance id '" + parameter.targetInstanceId
                                      + "' is not a valid identifier");

            if (spec.findModule (parameter.targetInstanceId) == nullptr)
                return makeError (ErrorCode::DanglingReference, childPath (path, "target"),
                                  "target instance '" + parameter.targetInstanceId + "' is not in the graph");
        }
    }

    // ---- Macros -------------------------------------------------------------
    if (static_cast<int> (spec.macros.size()) > kMaxMacros)
        return makeError (ErrorCode::CollectionTooLarge, "$/macros",
                          "too many macros (limit " + std::to_string (kMaxMacros) + ")");

    std::set<std::string> macroIds;
    int totalMappings = 0;

    for (size_t i = 0; i < spec.macros.size(); ++i)
    {
        const auto& macro = spec.macros[i];
        const std::string path = indexPath ("$/macros", static_cast<int> (i));

        if (! json::isValidIdentifier (macro.id))
            return makeError (ErrorCode::InvalidIdentifier, childPath (path, "id"),
                              "macro id '" + macro.id + "' is not a valid identifier");

        if (! macroIds.insert (macro.id).second)
            return makeError (ErrorCode::DuplicateIdentifier, childPath (path, "id"),
                              "duplicate macro id '" + macro.id + "'");

        if (macro.displayName.empty() || macro.displayName.size() > 96)
            return makeError (ErrorCode::ValueOutOfRange, childPath (path, "name"),
                              "macro name length is outside 1..96");

        if (! (macro.defaultValue >= 0.0f && macro.defaultValue <= 1.0f))
            return makeError (ErrorCode::ValueOutOfRange, childPath (path, "defaultValue"),
                              "macro default must lie in 0..1");

        if (macro.mappings.empty())
            return makeError (ErrorCode::EmptyCollection, childPath (path, "mappings"),
                              "macro '" + macro.id + "' has no mappings");

        totalMappings += static_cast<int> (macro.mappings.size());

        if (totalMappings > kMaxMacroMappings)
            return makeError (ErrorCode::CollectionTooLarge, childPath (path, "mappings"),
                              "too many macro mappings in total (limit "
                                  + std::to_string (kMaxMacroMappings) + ")");

        std::set<std::string> mappedParameters;

        for (size_t m = 0; m < macro.mappings.size(); ++m)
        {
            const auto& mapping = macro.mappings[m];
            const std::string mappingPath = indexPath (childPath (path, "mappings"), static_cast<int> (m));

            if (! json::isValidIdentifier (mapping.parameterId))
                return makeError (ErrorCode::InvalidIdentifier, childPath (mappingPath, "parameter"),
                                  "mapped parameter id '" + mapping.parameterId + "' is not a valid identifier");

            if (spec.findParameter (mapping.parameterId) == nullptr)
                return makeError (ErrorCode::DanglingReference, childPath (mappingPath, "parameter"),
                                  "mapped parameter '" + mapping.parameterId + "' is not declared");

            if (! mappedParameters.insert (mapping.parameterId).second)
                return makeError (ErrorCode::DuplicateIdentifier, childPath (mappingPath, "parameter"),
                                  "macro '" + macro.id + "' maps parameter '" + mapping.parameterId
                                      + "' more than once");

            if (! (mapping.rangeStart >= 0.0f && mapping.rangeStart <= 1.0f)
                || ! (mapping.rangeEnd >= 0.0f && mapping.rangeEnd <= 1.0f))
                return makeError (ErrorCode::ValueOutOfRange, childPath (mappingPath, "rangeStart"),
                                  "mapping range bounds must lie in 0..1");

            if (! (mapping.curve >= 0.05f && mapping.curve <= 8.0f))
                return makeError (ErrorCode::ValueOutOfRange, childPath (mappingPath, "curve"),
                                  "mapping curve must lie in 0.05..8");

            if (! (mapping.scale >= 0.0f && mapping.scale <= 4.0f))
                return makeError (ErrorCode::ValueOutOfRange, childPath (mappingPath, "scale"),
                                  "mapping scale must lie in 0..4");
        }
    }

    // ---- Dry/wet ------------------------------------------------------------
    if (! (spec.dryWet.defaultMix >= 0.0f && spec.dryWet.defaultMix <= 1.0f))
        return makeError (ErrorCode::ValueOutOfRange, "$/dryWet/defaultMix",
                          "dry/wet default mix must lie in 0..1");

    // ---- Bypass -------------------------------------------------------------
    if (spec.bypass.mode != "hard")
        return makeError (ErrorCode::UnsupportedValue, "$/bypass/mode",
                          "unsupported bypass mode '" + spec.bypass.mode + "' (v1 supports 'hard')");

    // ---- UI ----------------------------------------------------------------
    if (! isValidColour (spec.ui.accentColor))
        return makeError (ErrorCode::UnsupportedValue, "$/ui/accentColor",
                          "accentColor '" + spec.ui.accentColor + "' must be '#RRGGBB'");

    if (spec.ui.category.empty() || spec.ui.category.size() > 48)
        return makeError (ErrorCode::ValueOutOfRange, "$/ui/category",
                          "ui category length is outside 1..48");

    if (spec.ui.shortDescription.size() > 256)
        return makeError (ErrorCode::ValueOutOfRange, "$/ui/shortDescription",
                          "shortDescription length exceeds 256");

    if (spec.ui.layout != "vertical" && spec.ui.layout != "horizontal")
        return makeError (ErrorCode::UnsupportedValue, "$/ui/layout",
                          "unsupported layout '" + spec.ui.layout + "'");

    if (spec.ui.tags.size() > 16)
        return makeError (ErrorCode::CollectionTooLarge, "$/ui/tags",
                          "too many tags (limit 16)");

    for (size_t i = 0; i < spec.ui.tags.size(); ++i)
        if (! isTagValid (spec.ui.tags[i]))
            return makeError (ErrorCode::InvalidIdentifier, indexPath ("$/ui/tags", static_cast<int> (i)),
                              "tag '" + spec.ui.tags[i] + "' is not a valid tag");

    std::set<std::string> ordered;

    for (size_t i = 0; i < spec.ui.parameterOrder.size(); ++i)
    {
        const auto& id = spec.ui.parameterOrder[i];

        if (spec.findParameter (id) == nullptr)
            return makeError (ErrorCode::DanglingReference,
                              indexPath ("$/ui/parameterOrder", static_cast<int> (i)),
                              "parameterOrder entry '" + id + "' is not declared");

        if (! ordered.insert (id).second)
            return makeError (ErrorCode::DuplicateIdentifier,
                              indexPath ("$/ui/parameterOrder", static_cast<int> (i)),
                              "parameterOrder entry '" + id + "' appears more than once");
    }

    return FactoryError();
}

namespace
{
juce::var makeObject()
{
    return juce::var (new juce::DynamicObject());
}

void set (juce::var& object, const char* key, const juce::var& value)
{
    object.getDynamicObject()->setProperty (key, value);
}

juce::var number (float value)
{
    return juce::var (static_cast<double> (value));
}

juce::var text (const std::string& value)
{
    return juce::var (juce::String (value));
}
} // namespace

juce::var PluginSpec::toJson() const
{
    juce::var root = makeObject();

    set (root, "factorySchemaVersion", juce::var (schemaVersion));

    {
        juce::var plugin = makeObject();
        set (plugin, "id", text (pluginId));
        set (plugin, "name", text (pluginName));
        set (plugin, "vendor", text (vendor));
        set (plugin, "version", text (pluginVersion));
        set (plugin, "effectType", text (effectTypeName (effectType)));
        set (plugin, "channels", text (channelModeName (channels)));
        set (root, "plugin", plugin);
    }

    {
        juce::Array<juce::var> moduleArray;

        for (const auto& module : modules)
        {
            juce::var entry = makeObject();
            set (entry, "instanceId", text (module.instanceId));
            set (entry, "moduleId", text (module.moduleId));
            set (entry, "moduleVersion", text (module.moduleVersion.to_string()));

            juce::var values = makeObject();

            for (const auto& value : module.parameterValues)
                set (values, value.first.c_str(), number (value.second));

            set (entry, "parameters", values);
            moduleArray.add (entry);
        }

        juce::var graph = makeObject();
        set (graph, "modules", juce::var (moduleArray));
        set (root, "graph", graph);
    }

    {
        juce::Array<juce::var> parameterArray;

        for (const auto& parameter : parameters)
        {
            juce::var entry = makeObject();
            set (entry, "id", text (parameter.id));
            set (entry, "name", text (parameter.displayName));
            set (entry, "target", text (parameter.targetRef()));

            juce::var range = makeObject();
            set (range, "min", number (parameter.range.minValue));
            set (range, "max", number (parameter.range.maxValue));
            set (range, "default", number (parameter.range.defaultValue));
            set (entry, "range", range);

            set (entry, "unit", text (parameter.unit));
            set (entry, "logarithmic", juce::var (parameter.logarithmic));
            set (entry, "group", text (parameter.group));
            set (entry, "exposeToHost", juce::var (parameter.exposeToHost));
            parameterArray.add (entry);
        }

        set (root, "parameters", juce::var (parameterArray));
    }

    {
        juce::Array<juce::var> macroArray;

        for (const auto& macro : macros)
        {
            juce::var entry = makeObject();
            set (entry, "id", text (macro.id));
            set (entry, "name", text (macro.displayName));
            set (entry, "defaultValue", number (macro.defaultValue));

            juce::Array<juce::var> mappings;

            for (const auto& mapping : macro.mappings)
            {
                juce::var m = makeObject();
                set (m, "parameter", text (mapping.parameterId));
                set (m, "rangeStart", number (mapping.rangeStart));
                set (m, "rangeEnd", number (mapping.rangeEnd));
                set (m, "curve", number (mapping.curve));
                set (m, "scale", number (mapping.scale));
                set (m, "invert", juce::var (mapping.invert));
                mappings.add (m);
            }

            set (entry, "mappings", juce::var (mappings));
            macroArray.add (entry);
        }

        set (root, "macros", juce::var (macroArray));
    }

    {
        juce::var dryWetObject = makeObject();
        set (dryWetObject, "enabled", juce::var (this->dryWet.enabled));
        set (dryWetObject, "defaultMix", number (this->dryWet.defaultMix));
        set (root, "dryWet", dryWetObject);
    }

    {
        juce::var bypassObject = makeObject();
        set (bypassObject, "mode", text (this->bypass.mode));
        set (bypassObject, "defaultBypassed", juce::var (this->bypass.defaultBypassed));
        set (root, "bypass", bypassObject);
    }

    {
        juce::var uiObject = makeObject();
        set (uiObject, "accentColor", text (this->ui.accentColor));
        set (uiObject, "category", text (this->ui.category));
        set (uiObject, "shortDescription", text (this->ui.shortDescription));

        juce::Array<juce::var> tags;

        for (const auto& tag : this->ui.tags)
            tags.add (text (tag));

        set (uiObject, "tags", juce::var (tags));
        set (uiObject, "layout", text (this->ui.layout));

        juce::Array<juce::var> order;

        for (const auto& id : this->ui.parameterOrder)
            order.add (text (id));

        set (uiObject, "parameterOrder", juce::var (order));
        set (root, "ui", uiObject);
    }

    return root;
}

std::string PluginSpec::toJsonText() const
{
    const auto options = juce::JSON::FormatOptions()
                             .withSpacing (juce::JSON::Spacing::multiLine)
                             .withEncoding (juce::JSON::Encoding::ascii);

    return juce::JSON::toString (toJson(), options).toStdString();
}

uint64_t PluginSpec::fingerprint() const
{
    constexpr uint64_t kSeed = 1469598103934665603ull;

    uint64_t h = kSeed;
    h = hashInt (h, schemaVersion);
    h = hashString (h, pluginId);
    h = hashString (h, pluginName);
    h = hashString (h, vendor);
    h = hashString (h, pluginVersion);
    h = hashInt (h, static_cast<int64_t> (effectType));
    h = hashInt (h, static_cast<int64_t> (channels));

    h = hashInt (h, modules.size());

    for (const auto& module : modules)
    {
        h = hashString (h, module.instanceId);
        h = hashString (h, module.moduleId);
        h = hashInt (h, module.moduleVersion.major);
        h = hashInt (h, module.moduleVersion.minor);
        h = hashInt (h, module.moduleVersion.patch);
        h = hashInt (h, module.parameterValues.size());

        for (const auto& value : module.parameterValues)
        {
            h = hashString (h, value.first);
            h = hashFloat (h, value.second);
        }
    }

    h = hashInt (h, parameters.size());

    for (const auto& parameter : parameters)
    {
        h = hashString (h, parameter.id);
        h = hashString (h, parameter.displayName);
        h = hashString (h, parameter.unit);
        h = hashString (h, parameter.group);
        h = hashString (h, parameter.targetRef());
        h = hashFloat (h, parameter.range.minValue);
        h = hashFloat (h, parameter.range.maxValue);
        h = hashFloat (h, parameter.range.defaultValue);
        h = hashInt (h, parameter.logarithmic ? 1 : 0);
        h = hashInt (h, parameter.exposeToHost ? 1 : 0);
    }

    h = hashInt (h, macros.size());

    for (const auto& macro : macros)
    {
        h = hashString (h, macro.id);
        h = hashString (h, macro.displayName);
        h = hashFloat (h, macro.defaultValue);
        h = hashInt (h, macro.mappings.size());

        for (const auto& mapping : macro.mappings)
        {
            h = hashString (h, mapping.parameterId);
            h = hashFloat (h, mapping.rangeStart);
            h = hashFloat (h, mapping.rangeEnd);
            h = hashFloat (h, mapping.curve);
            h = hashFloat (h, mapping.scale);
            h = hashInt (h, mapping.invert ? 1 : 0);
        }
    }

    h = hashInt (h, dryWet.enabled ? 1 : 0);
    h = hashFloat (h, dryWet.defaultMix);

    h = hashString (h, bypass.mode);
    h = hashInt (h, bypass.defaultBypassed ? 1 : 0);

    h = hashString (h, ui.accentColor);
    h = hashString (h, ui.category);
    h = hashString (h, ui.shortDescription);
    h = hashString (h, ui.layout);

    for (const auto& tag : ui.tags)
        h = hashString (h, tag);

    for (const auto& id : ui.parameterOrder)
        h = hashString (h, id);

    return h;
}

//==============================================================================
Result<PluginSpec> PluginSpec::fromJson (const juce::var& root)
{
    const juce::DynamicObject* object = nullptr;
    FactoryError error;

    if (! json::readRootObject (root, object, error))
        return Result<PluginSpec>::failure (std::move (error));

    SpecParser parser (*object);

    if (! parser.run())
        return Result<PluginSpec>::failure (std::move (parser.error));

    return Result<PluginSpec>::success (std::move (parser.spec));
}

Result<PluginSpec> PluginSpec::fromJsonText (const std::string& jsonText)
{
    juce::var parsed;

    const juce::String text = juce::String::fromUTF8 (jsonText.c_str(),
                                                      static_cast<int> (jsonText.size()));
    const juce::Result parseResult = juce::JSON::parse (text, parsed);

    if (parseResult.failed())
        return Result<PluginSpec>::failure (
            makeError (ErrorCode::JsonParseFailed, "$",
                       "document is not valid JSON: " + parseResult.getErrorMessage().toStdString()));

    if (json::hasTrailingContent (jsonText))
        return Result<PluginSpec>::failure (
            makeError (ErrorCode::JsonParseFailed, "$",
                       "document carries trailing content after the JSON value"));

    return fromJson (parsed);
}

Result<PluginSpec> PluginSpec::fromJsonFile (const std::string& filePath)
{
    const juce::String path = juce::String::fromUTF8 (filePath.c_str(),
                                                      static_cast<int> (filePath.size()));
    const juce::File file (path);

    if (! file.existsAsFile())
        return Result<PluginSpec>::failure (
            makeError (ErrorCode::JsonParseFailed, "$",
                       "spec file does not exist: " + filePath));

    juce::var parsed;

    const juce::Result parseResult = juce::JSON::parse (file.loadFileAsString(), parsed);

    if (parseResult.failed())
        return Result<PluginSpec>::failure (
            makeError (ErrorCode::JsonParseFailed, "$",
                       "document is not valid JSON: " + parseResult.getErrorMessage().toStdString()));

    return fromJson (parsed);
}

} // namespace zfactory
