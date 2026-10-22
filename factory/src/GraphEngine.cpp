#include "zfactory/GraphEngine.h"
#include "zfactory/JsonUtil.h"
#include "zfactory/Version.h"

#include <algorithm>
#include <cstring>

namespace zfactory
{

namespace
{
std::string modulePath (int index, const char* leaf)
{
    return "$/graph/modules[" + std::to_string (index) + "]/" + leaf;
}

std::string parameterPath (int index, const char* leaf)
{
    return "$/parameters[" + std::to_string (index) + "]/" + leaf;
}

FactoryError withPath (FactoryError error, std::string path)
{
    error.path = std::move (path);
    return error;
}

/** Replace NaN and +/-Inf with 0.0f in place.
    Modules sanitise their own input, but the dry path of the dry/wet mixer is
    copied from the block *before* any module runs, so hostile input would
    otherwise reach the output through the dry side of the mix. Sanitising once
    here covers both paths, and leaves every finite sample bit-identical. */
void sanitiseInput (float* const* channels, int numChannels, int offset, int numSamples) noexcept
{
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = channels[ch];

        if (data == nullptr)
            continue;

        float* block = data + offset;

        for (int i = 0; i < numSamples; ++i)
        {
            const float value = block[i];

            if (! (value > -1.0e30f && value < 1.0e30f))
                block[i] = 0.0f;
        }
    }
}
} // namespace

//==============================================================================
Result<std::unique_ptr<GraphEngine>> GraphEngine::create (const PluginSpec& spec,
                                                          const ModuleRegistry& registry,
                                                          const GraphOptions& options)
{
    // Re-run the schema pass: a caller may have built a PluginSpec in code
    // rather than by parsing a document, and it must be held to the same rules.
    if (auto error = validatePluginSpecSchema (spec); error.failed())
        return Result<std::unique_ptr<GraphEngine>>::failure (std::move (error));

    std::unique_ptr<GraphEngine> engine (new GraphEngine());
    engine->m_spec = spec;
    engine->m_specFingerprint = spec.fingerprint();
    engine->m_channelCount = spec.channelCount();

    //--------------------------------------------------------------------------
    // 1. Resolve and instantiate the module chain.
    //--------------------------------------------------------------------------
    for (size_t i = 0; i < spec.modules.size(); ++i)
    {
        const auto& requested = spec.modules[i];
        const int index = static_cast<int> (i);

        auto resolved = registry.resolve (requested.moduleId, requested.moduleVersion,
                                          options.requiredCertification);

        if (! resolved.ok())
            return Result<std::unique_ptr<GraphEngine>>::failure (
                withPath (resolved.error(), modulePath (index, "moduleId")));

        const auto* entry = resolved.value();
        const ModuleManifest& manifest = entry->manifest;

        if (! manifest.supportsChannels (spec.channels))
            return Result<std::unique_ptr<GraphEngine>>::failure (
                makeError (ErrorCode::ModuleChannelUnsupported, modulePath (index, "moduleId"),
                           "module '" + manifest.id + "' (version " + manifest.version.to_string()
                               + ") does not support channel mode '"
                               + channelModeName (spec.channels) + "'"));

        // Module-instance parameter values are validated against the manifest
        // before the module is constructed.
        for (const auto& value : requested.parameterValues)
        {
            const auto* parameter = manifest.findParameter (value.first);

            if (parameter == nullptr)
                return Result<std::unique_ptr<GraphEngine>>::failure (
                    makeError (ErrorCode::ModuleParameterUnknown,
                               modulePath (index, "moduleId"),
                               "module '" + manifest.id + "' has no parameter '" + value.first + "'"));

            if (value.second < parameter->minValue || value.second > parameter->maxValue)
                return Result<std::unique_ptr<GraphEngine>>::failure (
                    makeError (ErrorCode::ValueOutOfRange,
                               "$/graph/modules[" + std::to_string (index) + "]/parameters/" + value.first,
                               "value " + json::formatNumber (value.second) + " is outside the domain ["
                                   + json::formatNumber (parameter->minValue) + ", "
                                   + json::formatNumber (parameter->maxValue) + "] of module parameter '"
                                   + value.first + "'"));
        }

        ModuleInstance instance;
        instance.instanceId = requested.instanceId;
        instance.manifest = manifest;
        instance.module.reset (entry->create());

        if (instance.module == nullptr)
            return Result<std::unique_ptr<GraphEngine>>::failure (
                makeError (ErrorCode::UnsupportedValue, modulePath (index, "moduleId"),
                           "module '" + manifest.id + "' failed to instantiate"));

        // Seed the module with the spec's initial values (manifest defaults for
        // everything the spec leaves out). setParameter only stores values, so
        // this is valid before prepare().
        for (size_t p = 0; p < manifest.parameters.size(); ++p)
        {
            const auto& parameter = manifest.parameters[p];
            const float value = requested.parameterValueOrDefault (parameter.id, parameter.defaultValue);
            instance.module->setParameter (static_cast<int> (p), value);
        }

        engine->m_modules.push_back (std::move (instance));
    }

    //--------------------------------------------------------------------------
    // 2. Bind the exposed parameters.
    //--------------------------------------------------------------------------
    std::vector<int> bindingIndexBySpecParameter (spec.parameters.size(), -1);

    for (size_t i = 0; i < spec.parameters.size(); ++i)
    {
        const auto& parameter = spec.parameters[i];
        const int index = static_cast<int> (i);

        ParameterSystem::Binding binding;
        binding.id = parameter.id;
        binding.displayName = parameter.displayName;
        binding.unit = parameter.unit;
        binding.group = parameter.group;
        binding.range = parameter.range;
        binding.logarithmic = parameter.logarithmic;
        binding.exposeToHost = parameter.exposeToHost;

        if (parameter.isFactoryTarget())
        {
            // Factory controls owned by the kernel, not by a module.
            if (parameter.targetParameterId == "dryWet")
            {
                if (engine->m_dryWetBindingIndex >= 0)
                    return Result<std::unique_ptr<GraphEngine>>::failure (
                        makeError (ErrorCode::DuplicateIdentifier, parameterPath (index, "target"),
                                   "more than one parameter targets '$factory.dryWet'"));

                engine->m_dryWetBindingIndex = engine->m_parameters.numBindings();
            }
            else
            {
                if (engine->m_bypassBindingIndex >= 0)
                    return Result<std::unique_ptr<GraphEngine>>::failure (
                        makeError (ErrorCode::DuplicateIdentifier, parameterPath (index, "target"),
                                   "more than one parameter targets '$factory.bypass'"));

                engine->m_bypassBindingIndex = engine->m_parameters.numBindings();
            }
        }
        else
        {
            int moduleIndex = -1;

            for (size_t m = 0; m < engine->m_modules.size(); ++m)
                if (engine->m_modules[m].instanceId == parameter.targetInstanceId)
                    moduleIndex = static_cast<int> (m);

            const ModuleManifest& manifest = engine->m_modules[static_cast<size_t> (moduleIndex)].manifest;
            const int moduleParameterIndex = manifest.indexOfParameter (parameter.targetParameterId);

            if (moduleParameterIndex < 0)
                return Result<std::unique_ptr<GraphEngine>>::failure (
                    makeError (ErrorCode::ModuleParameterUnknown, parameterPath (index, "target"),
                               "module '" + manifest.id + "' has no parameter '"
                                   + parameter.targetParameterId + "'"));

            const auto& domain = manifest.parameters[static_cast<size_t> (moduleParameterIndex)];

            if (parameter.range.minValue < domain.minValue || parameter.range.maxValue > domain.maxValue)
                return Result<std::unique_ptr<GraphEngine>>::failure (
                    makeError (ErrorCode::ValueOutOfRange, parameterPath (index, "range"),
                               "range [" + json::formatNumber (parameter.range.minValue) + ", "
                                   + json::formatNumber (parameter.range.maxValue)
                                   + "] is not contained in the domain ["
                                   + json::formatNumber (domain.minValue) + ", "
                                   + json::formatNumber (domain.maxValue) + "] of '"
                                   + parameter.targetRef() + "'"));

            if (parameter.range.defaultValue < parameter.range.minValue
                || parameter.range.defaultValue > parameter.range.maxValue)
                return Result<std::unique_ptr<GraphEngine>>::failure (
                    makeError (ErrorCode::ValueOutOfRange, parameterPath (index, "range/default"),
                               "default is outside the declared range"));

            if (! parameter.unit.empty() && parameter.unit != domain.unit)
                return Result<std::unique_ptr<GraphEngine>>::failure (
                    makeError (ErrorCode::TypeMismatch, parameterPath (index, "unit"),
                               "unit '" + parameter.unit + "' does not match the module unit '"
                                   + domain.unit + "' of '" + parameter.targetRef() + "'"));

            // Two exposed parameters pointing at the same module parameter would
            // make the effective value order dependent - ambiguous, so rejected.
            for (int previous : bindingIndexBySpecParameter)
            {
                if (previous < 0)
                    continue;

                const auto& existing = engine->m_parameters.bindings()[static_cast<size_t> (previous)];

                if (existing.moduleIndex == moduleIndex
                    && existing.moduleParameterIndex == moduleParameterIndex)
                    return Result<std::unique_ptr<GraphEngine>>::failure (
                        makeError (ErrorCode::DuplicateIdentifier, parameterPath (index, "target"),
                                   "parameter '" + existing.id + "' already targets '"
                                       + parameter.targetRef() + "'"));
            }

            binding.moduleIndex = moduleIndex;
            binding.moduleParameterIndex = moduleParameterIndex;
        }

        binding.baseNormalised = binding.normalise (parameter.range.defaultValue);
        binding.effectiveNormalised = binding.baseNormalised;
        binding.effectiveValue = parameter.range.defaultValue;

        bindingIndexBySpecParameter[i] = engine->m_parameters.numBindings();
        engine->m_parameters.addBinding (std::move (binding));
    }

    //--------------------------------------------------------------------------
    // 3. Bind the macros and their mappings.
    //--------------------------------------------------------------------------
    for (const auto& macro : spec.macros)
    {
        ParameterSystem::MacroBinding macroBinding;
        macroBinding.id = macro.id;
        macroBinding.displayName = macro.displayName;
        macroBinding.value = macro.defaultValue;

        for (const auto& mapping : macro.mappings)
        {
            int parameterIndex = -1;

            for (size_t i = 0; i < spec.parameters.size(); ++i)
                if (spec.parameters[i].id == mapping.parameterId)
                    parameterIndex = bindingIndexBySpecParameter[i];

            if (parameterIndex < 0)
                return Result<std::unique_ptr<GraphEngine>>::failure (
                    makeError (ErrorCode::DanglingReference, "$/macros/" + macro.id + "/mappings",
                               "macro '" + macro.id + "' maps undeclared parameter '"
                                   + mapping.parameterId + "'"));

            ParameterSystem::Mapping resolved;
            resolved.parameterIndex = parameterIndex;
            resolved.rangeStart = mapping.rangeStart;
            resolved.rangeEnd = mapping.rangeEnd;
            resolved.curve = mapping.curve;
            resolved.scale = mapping.scale;
            resolved.invert = mapping.invert;

            const int mappingIndex = engine->m_parameters.numMappings();
            engine->m_parameters.addMapping (resolved);
            macroBinding.mappingIndices.push_back (mappingIndex);
        }

        engine->m_parameters.addMacro (std::move (macroBinding));
    }

    //--------------------------------------------------------------------------
    // 4. Dry/wet and bypass defaults.
    //
    // A parameter targeting "$factory.dryWet" / "$factory.bypass" was already
    // wired to m_dryWetBindingIndex / m_bypassBindingIndex above; without one the
    // control stays fixed at its declared default.
    //--------------------------------------------------------------------------
    engine->m_fixedDryWet = spec.dryWet.enabled ? spec.dryWet.defaultMix : 1.0f;
    engine->m_bypassed = spec.bypass.defaultBypassed;
    engine->m_parameters.recompute();
    engine->m_graphFingerprint = engine->m_specFingerprint
                                     ^ engine->m_parameters.fingerprint()
                                     ^ (registry.catalogFingerprint() * 0x9e3779b97f4a7c15ull);

    return Result<std::unique_ptr<GraphEngine>>::success (std::move (engine));
}

//==============================================================================
FactoryError GraphEngine::prepare (double sampleRate, int maxBlockSize)
{
    if (! (sampleRate >= kMinSampleRate && sampleRate <= kMaxSampleRate))
        return makeError (ErrorCode::ValueOutOfRange, "prepare/sampleRate",
                          "sample rate " + json::formatNumber (sampleRate) + " is outside "
                              + json::formatNumber (kMinSampleRate) + ".."
                              + json::formatNumber (kMaxSampleRate));

    if (maxBlockSize < 1 || maxBlockSize > kMaxBlockSize)
        return makeError (ErrorCode::ValueOutOfRange, "prepare/maxBlockSize",
                          "block size " + std::to_string (maxBlockSize) + " is outside 1.."
                              + std::to_string (kMaxBlockSize));

    m_prepared = false;
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;

    PrepareContext context;
    context.sampleRate = sampleRate;
    context.maxBlockSize = maxBlockSize;
    context.numChannels = m_channelCount;

    for (size_t i = 0; i < m_modules.size(); ++i)
    {
        auto& instance = m_modules[i];

        if (! instance.manifest.supportsSampleRate (sampleRate))
            return makeError (ErrorCode::ModuleSampleRateUnsupported,
                              modulePath (static_cast<int> (i), "moduleId"),
                              "module '" + instance.manifest.id + "' does not support sample rate "
                                  + json::formatNumber (sampleRate));

        instance.module->prepare (context);
        instance.latencySamples = std::max (0, instance.module->getLatencySamples());
    }

    m_latencySamples = 0;

    for (const auto& instance : m_modules)
        m_latencySamples += instance.latencySamples;

    const size_t scratchSize = static_cast<size_t> (m_channelCount)
                                   * static_cast<size_t> (maxBlockSize);

    m_dryScratch.assign (m_spec.dryWet.enabled ? scratchSize : 0, 0.0f);

    if (m_spec.dryWet.enabled && m_latencySamples > 0)
    {
        m_dryDelayLength = m_latencySamples + maxBlockSize + 1;
        m_dryDelay.assign (static_cast<size_t> (m_channelCount)
                               * static_cast<size_t> (m_dryDelayLength),
                           0.0f);
    }
    else
    {
        m_dryDelayLength = 0;
        m_dryDelay.clear();
    }

    m_dryDelayWriteIndex = 0;
    m_prepared = true;

    applyParameters();
    return FactoryError();
}

void GraphEngine::reset() noexcept
{
    for (auto& instance : m_modules)
        instance.module->reset();

    std::fill (m_dryDelay.begin(), m_dryDelay.end(), 0.0f);
    m_dryDelayWriteIndex = 0;
}

//==============================================================================
void GraphEngine::setBypassed (bool bypassed) noexcept
{
    m_bypassed = bypassed;
}

bool GraphEngine::isBypassed() const noexcept
{
    if (m_bypassed)
        return true;

    // A spec-declared bypass parameter is a 0..1 parameter; anything at or above
    // the midpoint means "bypassed".
    if (m_bypassBindingIndex >= 0)
        return m_parameters.getEffectiveValue (m_bypassBindingIndex) >= 0.5f;

    return false;
}

void GraphEngine::setDryWetNormalised (float mix) noexcept
{
    const float clamped = std::min (1.0f, std::max (0.0f, mix));

    if (m_dryWetBindingIndex >= 0)
        m_parameters.setBaseNormalised (m_dryWetBindingIndex, clamped);
    else
        m_fixedDryWet = clamped;
}

float GraphEngine::getDryWetNormalised() const noexcept
{
    if (! m_spec.dryWet.enabled)
        return 1.0f;

    if (m_dryWetBindingIndex >= 0)
        return std::min (1.0f, std::max (0.0f, m_parameters.getEffectiveValue (m_dryWetBindingIndex)));

    return m_fixedDryWet;
}

//==============================================================================
void GraphEngine::applyParameters() noexcept
{
    m_parameters.recompute();

    const auto& bindings = m_parameters.bindings();

    for (size_t i = 0; i < bindings.size(); ++i)
    {
        const auto& binding = bindings[i];

        if (binding.moduleIndex < 0
            || binding.moduleIndex >= static_cast<int> (m_modules.size()))
            continue;

        m_modules[static_cast<size_t> (binding.moduleIndex)]
            .module->setParameter (binding.moduleParameterIndex, binding.effectiveValue);
    }
}

//==============================================================================
void GraphEngine::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (! m_prepared || channels == nullptr || numSamples <= 0)
        return;

    // The engine is built for one channel configuration; a narrower buffer is a
    // host contract violation and is left untouched rather than half-processed.
    if (numChannels < m_channelCount)
        return;

    applyParameters();

    if (isBypassed())
        return;

    int offset = 0;
    int remaining = numSamples;

    while (remaining > 0)
    {
        const int blockSize = std::min (remaining, m_maxBlockSize);
        sanitiseInput (channels, m_channelCount, offset, blockSize);
        processBlockInternal (channels, offset, blockSize);
        offset += blockSize;
        remaining -= blockSize;
    }
}

void GraphEngine::processBlockInternal (float* const* channels, int offset, int numSamples) noexcept
{
    float* blockChannels[2] = { nullptr, nullptr };

    for (int ch = 0; ch < m_channelCount; ++ch)
        blockChannels[ch] = channels[ch] + offset;

    const bool useDryWet = m_spec.dryWet.enabled;
    const float mix = useDryWet ? getDryWetNormalised() : 1.0f;
    const bool needsDry = useDryWet && mix < 1.0f;

    if (needsDry)
    {
        if (m_dryDelayLength > 0)
        {
            const int latency = m_latencySamples;

            for (int i = 0; i < numSamples; ++i)
            {
                for (int ch = 0; ch < m_channelCount; ++ch)
                {
                    float* line = m_dryDelay.data()
                                      + static_cast<size_t> (ch) * static_cast<size_t> (m_dryDelayLength);
                    float* dry = m_dryScratch.data()
                                     + static_cast<size_t> (ch) * static_cast<size_t> (m_maxBlockSize);

                    line[m_dryDelayWriteIndex] = blockChannels[ch][i];

                    int readIndex = m_dryDelayWriteIndex - latency;

                    if (readIndex < 0)
                        readIndex += m_dryDelayLength;

                    dry[i] = line[readIndex];
                }

                if (++m_dryDelayWriteIndex >= m_dryDelayLength)
                    m_dryDelayWriteIndex = 0;
            }
        }
        else
        {
            for (int ch = 0; ch < m_channelCount; ++ch)
                std::memcpy (m_dryScratch.data() + static_cast<size_t> (ch) * static_cast<size_t> (m_maxBlockSize),
                             blockChannels[ch],
                             sizeof (float) * static_cast<size_t> (numSamples));
        }
    }

    for (auto& instance : m_modules)
        instance.module->process (blockChannels, m_channelCount, numSamples);

    if (needsDry)
    {
        for (int ch = 0; ch < m_channelCount; ++ch)
        {
            float* out = blockChannels[ch];
            const float* dry = m_dryScratch.data()
                                   + static_cast<size_t> (ch) * static_cast<size_t> (m_maxBlockSize);

            for (int i = 0; i < numSamples; ++i)
                out[i] = dry[i] * (1.0f - mix) + out[i] * mix;
        }
    }
}

//==============================================================================
std::string GraphEngine::describeGraph() const
{
    std::string text;

    text += "plugin=" + m_spec.pluginId + " " + m_spec.pluginVersion;
    text += " channels=" + std::string (channelModeName (m_spec.channels));
    text += " latency=" + std::to_string (m_latencySamples);
    text += " dryWet=" + std::string (m_spec.dryWet.enabled ? "on" : "off");
    text += " bypass=" + std::string (isBypassed() ? "on" : "off");
    text += "\n";

    for (size_t i = 0; i < m_modules.size(); ++i)
    {
        const auto& instance = m_modules[i];
        text += "  " + std::to_string (i + 1) + ". " + instance.instanceId + " -> "
                + instance.manifest.id + " " + instance.manifest.version.to_string()
                + " [" + certificationStateName (instance.manifest.certification) + "]"
                + " latency=" + std::to_string (instance.latencySamples) + "\n";
    }

    text += "parameters=" + std::to_string (m_parameters.numBindings());
    text += " macros=" + std::to_string (m_parameters.numMacros());
    text += " mappings=" + std::to_string (m_parameters.numMappings());
    text += "\n";

    for (const auto& macro : m_parameters.macros())
    {
        text += "  macro " + macro.id + " = " + json::formatNumber (macro.value)
                + " (" + std::to_string (macro.mappingIndices.size()) + " mappings)\n";
    }

    return text;
}

//==============================================================================
// State serialization
//
// The blob stores the whole control surface: macro positions, parameter base
// values, dry/wet position and bypass. It carries the factory schema version and
// the fingerprint of the spec the graph was built from, so a blob can never be
// restored into a differently shaped graph.
//
// DSP working state (filter memories, delay lines, envelopes) is deliberately
// not serialized: hosts rebuild the timeline and our plugins follow the same
// convention as the shipped ZTame / ZScorch processors, which persist their
// parameter tree only.
//==============================================================================
namespace
{
std::string toHex16 (uint64_t value)
{
    static const char* digits = "0123456789abcdef";
    std::string text (16, '0');

    for (int i = 15; i >= 0; --i)
    {
        text[static_cast<size_t> (i)] = digits[value & 0xfull];
        value >>= 4;
    }

    return text;
}

bool fromHex64 (const std::string& text, uint64_t& out)
{
    if (text.size() != 16)
        return false;

    uint64_t value = 0;

    for (char c : text)
    {
        int digit = -1;

        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return false;

        value = (value << 4) | static_cast<uint64_t> (digit);
    }

    out = value;
    return true;
}
} // namespace

juce::MemoryBlock GraphEngine::saveState() const
{
    juce::DynamicObject::Ptr root (new juce::DynamicObject());
    root->setProperty ("factorySchemaVersion", kSchemaVersion);
    root->setProperty ("kernelVersion", juce::String (kKernelVersionString));
    root->setProperty ("specFingerprint", juce::String (toHex16 (m_specFingerprint)));
    root->setProperty ("bypassed", m_bypassed);
    root->setProperty ("dryWet", static_cast<double> (m_fixedDryWet));

    juce::Array<juce::var> macros;

    for (const auto& macro : m_parameters.macros())
    {
        juce::DynamicObject::Ptr entry (new juce::DynamicObject());
        entry->setProperty ("id", juce::String (macro.id));
        entry->setProperty ("value", static_cast<double> (macro.value));
        macros.add (juce::var (entry.get()));
    }

    root->setProperty ("macros", juce::var (macros));

    juce::Array<juce::var> parameters;

    for (const auto& binding : m_parameters.bindings())
    {
        juce::DynamicObject::Ptr entry (new juce::DynamicObject());
        entry->setProperty ("id", juce::String (binding.id));
        entry->setProperty ("normalised", static_cast<double> (binding.baseNormalised));
        parameters.add (juce::var (entry.get()));
    }

    root->setProperty ("parameters", juce::var (parameters));

    const auto options = juce::JSON::FormatOptions()
                             .withSpacing (juce::JSON::Spacing::multiLine)
                             .withEncoding (juce::JSON::Encoding::ascii);

    const juce::String text = juce::JSON::toString (juce::var (root.get()), options);

    juce::MemoryBlock block;
    block.append (text.toRawUTF8(), text.getNumBytesAsUTF8());
    return block;
}

FactoryError GraphEngine::restoreState (const void* data, size_t numBytes)
{
    if (data == nullptr || numBytes == 0)
        return makeError (ErrorCode::InvalidStateBlob, "state",
                          "state blob is empty");

    juce::var root;

    const juce::Result parseResult = juce::JSON::parse (
        juce::String::fromUTF8 (static_cast<const char*> (data), static_cast<int> (numBytes)), root);

    if (parseResult.failed())
        return makeError (ErrorCode::InvalidStateBlob, "state",
                          "state blob is not valid JSON: "
                              + parseResult.getErrorMessage().toStdString());

    // getDynamicObject(), not isObject(): an array var also reports isObject()
    // as true in JUCE while yielding a null DynamicObject.
    auto* object = root.getDynamicObject();

    if (object == nullptr)
        return makeError (ErrorCode::InvalidStateBlob, "state",
                          "state blob root is not an object");

    {
        const auto value = object->getProperty ("factorySchemaVersion");

        if (! value.isInt() && ! value.isInt64() && ! value.isDouble())
            return makeError (ErrorCode::InvalidStateBlob, "state/factorySchemaVersion",
                              "state schema version is missing or not a number");

        const int schemaVersion = static_cast<int> (value);

        if (schemaVersion != kSchemaVersion)
            return makeError (ErrorCode::StateSchemaUnsupported, "state/factorySchemaVersion",
                              "state schema version " + std::to_string (schemaVersion)
                                  + " is not supported (kernel supports "
                                  + std::to_string (kSchemaVersion) + ")");
    }

    {
        const auto value = object->getProperty ("specFingerprint");

        uint64_t fingerprint = 0;

        if (! value.isString() || ! fromHex64 (value.toString().toStdString(), fingerprint))
            return makeError (ErrorCode::InvalidStateBlob, "state/specFingerprint",
                              "state spec fingerprint is missing or malformed");

        if (fingerprint != m_specFingerprint)
            return makeError (ErrorCode::StateSpecMismatch, "state/specFingerprint",
                              "state was saved from a different PluginSpec (blob "
                                  + toHex16 (fingerprint) + ", graph " + toHex16 (m_specFingerprint) + ")");
    }

    if (object->hasProperty ("bypassed"))
        m_bypassed = static_cast<bool> (object->getProperty ("bypassed"));

    if (object->hasProperty ("dryWet"))
    {
        const auto value = object->getProperty ("dryWet");

        if (value.isInt() || value.isInt64() || value.isDouble())
            setDryWetNormalised (static_cast<float> (static_cast<double> (value)));
    }

    // Macros: every entry must name a macro this graph actually has.
    if (object->hasProperty ("macros"))
    {
        const auto value = object->getProperty ("macros");

        if (! value.isArray())
            return makeError (ErrorCode::InvalidStateBlob, "state/macros",
                              "state macros node is not an array");

        for (const auto& element : *value.getArray())
        {
            const auto* entry = element.getDynamicObject();

            if (entry == nullptr)
                return makeError (ErrorCode::InvalidStateBlob, "state/macros",
                                  "state macro entry is not an object");

            const auto id = entry->getProperty ("id").toString().toStdString();
            const int index = m_parameters.indexOfMacro (id);

            if (index < 0)
                return makeError (ErrorCode::InvalidStateBlob, "state/macros",
                                  "state references unknown macro '" + id + "'");

            const auto macroValue = entry->getProperty ("value");

            if (! (macroValue.isInt() || macroValue.isInt64() || macroValue.isDouble()))
                return makeError (ErrorCode::InvalidStateBlob, "state/macros",
                                  "state macro '" + id + "' has no numeric value");

            m_parameters.setMacroValue (index, static_cast<float> (static_cast<double> (macroValue)));
        }
    }

    // Parameters: same contract.
    if (object->hasProperty ("parameters"))
    {
        const auto value = object->getProperty ("parameters");

        if (! value.isArray())
            return makeError (ErrorCode::InvalidStateBlob, "state/parameters",
                              "state parameters node is not an array");

        for (const auto& element : *value.getArray())
        {
            const auto* entry = element.getDynamicObject();

            if (entry == nullptr)
                return makeError (ErrorCode::InvalidStateBlob, "state/parameters",
                                  "state parameter entry is not an object");

            const auto id = entry->getProperty ("id").toString().toStdString();
            const int index = m_parameters.indexOfBinding (id);

            if (index < 0)
                return makeError (ErrorCode::InvalidStateBlob, "state/parameters",
                                  "state references unknown parameter '" + id + "'");

            const auto normalised = entry->getProperty ("normalised");

            if (! (normalised.isInt() || normalised.isInt64() || normalised.isDouble()))
                return makeError (ErrorCode::InvalidStateBlob, "state/parameters",
                                  "state parameter '" + id + "' has no numeric value");

            m_parameters.setBaseNormalised (index,
                                            static_cast<float> (static_cast<double> (normalised)));
        }
    }

    applyParameters();
    return FactoryError();
}

} // namespace zfactory
