#include "zfactory/shell/ShellProcessor.h"

#include "zfactory/ParameterSystem.h"
#include "zfactory/shell/ShellEditor.h"

#include <algorithm>
#include <cmath>

namespace zfactory
{
namespace shell
{

namespace
{

//==============================================================================
/** The embedded product, parsed exactly once per process.
 *
 *  A plugin binary *is* one product, so this is genuinely process-global: every
 *  component in the process must see the same immutable definition, and parsing
 *  it more than once would be both wasteful and a determinism risk.
 */
struct LoadedProduct
{
    ProductRuntime runtime;
    FactoryError error;
    bool ok = false;
};

const LoadedProduct& loadedProduct()
{
    static const LoadedProduct instance = []
    {
        LoadedProduct loaded;

        auto result = ProductRuntime::create();

        if (result.ok())
        {
            loaded.runtime = result.takeValue();
            loaded.ok = true;
        }
        else
        {
            loaded.error = result.error();
        }

        return loaded;
    }();

    return instance;
}

/** The channel set the specification asks for: exactly its channel count, no
    side chains. A product cannot negotiate a layout its compiled graph was not
    built for, so this is the only configuration it will ever offer. */
juce::AudioChannelSet channelsFor (const ProductRuntime& runtime)
{
    return runtime.isValid() && runtime.channelCount() == 1
               ? juce::AudioChannelSet::mono()
               : juce::AudioChannelSet::stereo();
}

juce::String formatControlValue (const UiControl& control, float value)
{
    juce::String text;

    if (std::abs (value) >= 1000.0f)
        text = juce::String (static_cast<int> (std::lround (value)));
    else if (std::abs (value) >= 100.0f)
        text = juce::String (value, 1);
    else if (std::abs (value) >= 1.0f)
        text = juce::String (value, 2);
    else
        text = juce::String (value, 3);

    if (! control.unit.empty())
        text << " " << juce::String (control.unit);

    return text;
}

/** Mirrors the kernel's own normalise/denormalise pair exactly, so the value the
    host automates and the value the DSP runs are the same number. */
ParameterSystem::Binding makeDomain (const UiControl& control)
{
    ParameterSystem::Binding domain;
    domain.range.minValue = control.minValue;
    domain.range.maxValue = control.maxValue;
    domain.range.defaultValue = control.defaultValue;
    domain.logarithmic = control.logarithmic;
    return domain;
}

} // namespace

//==============================================================================
ShellParameter::ShellParameter (const UiControl& control, Kind kind, int engineIndex)
    : juce::RangedAudioParameter (juce::String::fromUTF8 (control.id.c_str()),
                                  juce::String::fromUTF8 (control.label.c_str()),
                                  juce::String (control.unit)),
      m_control (control),
      m_kind (kind),
      m_engineIndex (engineIndex)
{
    if (control.stepCount > 1)
        m_numSteps = control.stepCount;

    const auto domain = makeDomain (control);

    // The reported range delegates to the kernel's own conversion functions, so
    // the host's normalised view and the engine's engineering view are the same
    // mapping by construction rather than by coincidence.
    m_range = juce::NormalisableRange<float> (
        control.minValue,
        control.maxValue,
        [domain] (float start, float end, float normalised)
        {
            juce::ignoreUnused (start, end);
            return domain.denormalise (normalised);
        },
        [domain] (float start, float end, float value)
        {
            juce::ignoreUnused (start, end);
            return domain.normalise (value);
        },
        [] (float start, float end, float value)
        {
            juce::ignoreUnused (start, end);
            return value;
        });

    m_range.interval = 0.0f;
}

//==============================================================================
float ShellParameter::effectiveValue() const
{
    if (m_engine == nullptr)
        return m_control.defaultValue;

    if (m_kind == Kind::Macro)
        return m_engine->parameters().getMacroValue (m_engineIndex);

    return m_engine->parameters().getEffectiveValue (m_engineIndex);
}

float ShellParameter::effectiveNormalised() const
{
    if (m_engine == nullptr)
        return m_control.defaultValueNormalised;

    if (m_kind == Kind::Macro)
        return m_engine->parameters().getMacroValue (m_engineIndex);

    return m_engine->parameters().getEffectiveNormalised (m_engineIndex);
}

float ShellParameter::getValue() const
{
    if (m_engine == nullptr)
        return m_control.defaultValueNormalised;

    if (m_kind == Kind::Macro)
        return m_engine->parameters().getMacroValue (m_engineIndex);

    if (const auto* binding = m_engine->parameters().bindingAt (m_engineIndex))
        return binding->baseNormalised;

    return m_control.defaultValueNormalised;
}

void ShellParameter::setValue (float newValue)
{
    if (m_engine == nullptr)
        return;

    if (m_kind == Kind::Macro)
        m_engine->parameters().setMacroValue (m_engineIndex, newValue);
    else
        m_engine->parameters().setBaseNormalised (m_engineIndex, newValue);
}

juce::String ShellParameter::getText (float normalisedValue, int maximumStringLength) const
{
    const auto domain = makeDomain (m_control);

    juce::String text;

    if (m_kind == Kind::Macro)
        text = juce::String (juce::jlimit (0.0f, 1.0f, normalisedValue), 3);
    else
        text = formatControlValue (m_control, domain.denormalise (normalisedValue));

    // A macro-driven parameter reports the value the macro is currently holding
    // it at, which is what the user hears, not the stale base value.
    if (m_kind == Kind::SpecParameter && m_control.macroDriven && m_engine != nullptr)
        text = formatControlValue (m_control, effectiveValue());

    return text.substring (0, maximumStringLength);
}

float ShellParameter::getValueForText (const juce::String& text) const
{
    const auto domain = makeDomain (m_control);
    return domain.normalise (text.getFloatValue());
}

//==============================================================================
// Automation gestures are handled by the base class: JUCE's
// beginChangeGesture()/endChangeGesture() are not virtual, and they already do
// the only thing a parameter needs to do - tell the host a change is starting or
// finishing. Nothing here needs to override them.

void ShellParameter::refreshFromEngine()
{
    sendValueChangedMessageToListeners (getValue());
}

//==============================================================================
ShellProcessor::ShellProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", channelsFor (loadedProduct().runtime), true)
                                .withOutput ("Output", channelsFor (loadedProduct().runtime), true)),
      m_runtime (loadedProduct().runtime)
{
    m_engineError = loadedProduct().error;

    if (! m_runtime.isValid())
        return;

    auto engineResult = GraphEngine::create (m_runtime.spec(), builtinRegistry(), GraphOptions());

    if (! engineResult.ok())
    {
        m_engineError = engineResult.error();
        return;
    }

    m_engine = engineResult.takeValue();
    m_engineOk = true;

    buildParameters();

    m_moduleLevelValues.assign (m_runtime.plan().controls.size(), 0.0f);

    for (size_t i = 0; i < m_runtime.plan().controls.size(); ++i)
        m_moduleLevelValues[i] = m_runtime.plan().controls[i].defaultValueNormalised;
}

ShellProcessor::~ShellProcessor() = default;

//==============================================================================
void ShellProcessor::buildParameters()
{
    const auto& controls = m_runtime.plan().controls;

    m_controlToParameterIndex.assign (controls.size(), -1);

    struct Pending
    {
        int hostIndex = -1;
        std::unique_ptr<ShellParameter> parameter;
    };

    std::vector<Pending> pending;

    for (size_t i = 0; i < controls.size(); ++i)
    {
        const auto& control = controls[i];

        if (control.hostParameterIndex < 0)
            continue;

        ShellParameter::Kind kind = ShellParameter::Kind::SpecParameter;
        int engineIndex = -1;

        if (control.isMacro)
        {
            kind = ShellParameter::Kind::Macro;

            // "macro:<id>" -> <id>
            const auto macroId = control.id.substr (control.id.find (':') + 1);
            engineIndex = m_engine->parameters().indexOfMacro (macroId);
        }
        else
        {
            engineIndex = m_engine->parameters().indexOfBinding (control.id);
        }

        if (engineIndex < 0)
            continue;   // the plan and the engine disagree; QA catches this

        Pending entry;
        entry.hostIndex = control.hostParameterIndex;
        entry.parameter = std::make_unique<ShellParameter> (control, kind, engineIndex);
        entry.parameter->attachEngine (*m_engine);

        pending.push_back (std::move (entry));
    }

    // JUCE defines a parameter's index by the order it is added, so the list is
    // added in host-index order - which is the plan's order, which is the spec's.
    std::sort (pending.begin(), pending.end(),
               [] (const Pending& a, const Pending& b) { return a.hostIndex < b.hostIndex; });

    m_parameters.reserve (pending.size());

    for (auto& entry : pending)
    {
        for (size_t i = 0; i < controls.size(); ++i)
            if (controls[i].hostParameterIndex == entry.hostIndex)
                m_controlToParameterIndex[i] = static_cast<int> (m_parameters.size());

        addParameter (entry.parameter.get());
        m_parameters.push_back (std::move (entry.parameter));
    }
}

//==============================================================================
const UiControl* ShellProcessor::getControl (int controlIndex) const noexcept
{
    const auto& controls = m_runtime.plan().controls;

    if (controlIndex < 0 || controlIndex >= static_cast<int> (controls.size()))
        return nullptr;

    return &controls[static_cast<size_t> (controlIndex)];
}

float ShellProcessor::getControlNormalised (int controlIndex) const
{
    const auto* control = getControl (controlIndex);

    if (control == nullptr || m_engine == nullptr)
        return 0.0f;

    if (control->moduleLevel)
    {
        if (controlIndex < static_cast<int> (m_moduleLevelValues.size()))
            return m_moduleLevelValues[static_cast<size_t> (controlIndex)];

        return control->defaultValueNormalised;
    }

    const auto hostIndex = m_controlToParameterIndex[static_cast<size_t> (controlIndex)];

    if (hostIndex < 0 || hostIndex >= static_cast<int> (m_parameters.size()))
        return control->defaultValueNormalised;

    return m_parameters[static_cast<size_t> (hostIndex)]->getValue();
}

void ShellProcessor::setControlNormalised (int controlIndex, float normalised)
{
    const auto* control = getControl (controlIndex);

    if (control == nullptr || m_engine == nullptr)
        return;

    const auto clamped = juce::jlimit (0.0f, 1.0f, normalised);

    if (control->moduleLevel)
    {
        writeModuleLevelValue (controlIndex, clamped);
        return;
    }

    const auto hostIndex = m_controlToParameterIndex[static_cast<size_t> (controlIndex)];

    if (hostIndex < 0 || hostIndex >= static_cast<int> (m_parameters.size()))
        return;

    // Deliberately silent: this is the plain value write, with no host
    // notification. The quality gate is not a host, and a short-lived process
    // that notifies a host that does not exist leaves notification machinery
    // pending at teardown. The editor uses setControlFromUser(), which does
    // notify, because there a real host is recording automation.
    m_parameters[static_cast<size_t> (hostIndex)]->setValue (clamped);
}

void ShellProcessor::setControlFromUser (int controlIndex, float normalised)
{
    const auto* control = getControl (controlIndex);

    if (control == nullptr || m_engine == nullptr)
        return;

    const auto clamped = juce::jlimit (0.0f, 1.0f, normalised);

    if (control->moduleLevel)
    {
        writeModuleLevelValue (controlIndex, clamped);
        return;
    }

    const auto hostIndex = m_controlToParameterIndex[static_cast<size_t> (controlIndex)];

    if (hostIndex < 0 || hostIndex >= static_cast<int> (m_parameters.size()))
        return;

    // Route through the host parameter so automation, undo and the host's own
    // value display all see the change; writing to the engine behind the host's
    // back would make an automated plugin behave differently from a hand-edited
    // one.
    m_parameters[static_cast<size_t> (hostIndex)]->setValueNotifyingHost (clamped);
}

juce::String ShellProcessor::getControlText (int controlIndex) const
{
    const auto* control = getControl (controlIndex);

    if (control == nullptr || m_engine == nullptr)
        return {};

    const auto domain = makeDomain (*control);
    const auto normalised = getControlNormalised (controlIndex);

    if (control->isMacro)
        return juce::String (normalised, 3);

    return formatControlValue (*control, domain.denormalise (normalised));
}

void ShellProcessor::beginControlGesture (int controlIndex)
{
    const auto hostIndex = controlIndex >= 0 && controlIndex < static_cast<int> (m_controlToParameterIndex.size())
                               ? m_controlToParameterIndex[static_cast<size_t> (controlIndex)]
                               : -1;

    if (hostIndex >= 0 && hostIndex < static_cast<int> (m_parameters.size()))
        m_parameters[static_cast<size_t> (hostIndex)]->beginChangeGesture();
}

void ShellProcessor::endControlGesture (int controlIndex)
{
    const auto hostIndex = controlIndex >= 0 && controlIndex < static_cast<int> (m_controlToParameterIndex.size())
                               ? m_controlToParameterIndex[static_cast<size_t> (controlIndex)]
                               : -1;

    if (hostIndex >= 0 && hostIndex < static_cast<int> (m_parameters.size()))
        m_parameters[static_cast<size_t> (hostIndex)]->endChangeGesture();
}

ShellParameter* ShellProcessor::getParameterForControl (int controlIndex) const noexcept
{
    if (controlIndex < 0 || controlIndex >= static_cast<int> (m_controlToParameterIndex.size()))
        return nullptr;

    const auto hostIndex = m_controlToParameterIndex[static_cast<size_t> (controlIndex)];

    if (hostIndex < 0 || hostIndex >= static_cast<int> (m_parameters.size()))
        return nullptr;

    return m_parameters[static_cast<size_t> (hostIndex)].get();
}

void ShellProcessor::writeModuleLevelValue (int controlIndex, float normalised)
{
    const auto* control = getControl (controlIndex);

    if (control == nullptr || ! control->moduleLevel || m_engine == nullptr)
        return;

    const auto& modules = m_engine->modules();

    if (control->moduleIndex < 0 || control->moduleIndex >= static_cast<int> (modules.size()))
        return;

    if (controlIndex < static_cast<int> (m_moduleLevelValues.size()))
        m_moduleLevelValues[static_cast<size_t> (controlIndex)] = normalised;

    const auto& manifest = modules[static_cast<size_t> (control->moduleIndex)].manifest;

    if (control->moduleParameterIndex < 0
        || control->moduleParameterIndex >= static_cast<int> (manifest.parameters.size()))
        return;

    const auto& domain = manifest.parameters[static_cast<size_t> (control->moduleParameterIndex)];
    auto* module = modules[static_cast<size_t> (control->moduleIndex)].module.get();

    if (module != nullptr)
        module->setParameter (control->moduleParameterIndex, domain.denormalise (normalised));
}

void ShellProcessor::applyModuleLevelValues (const juce::var& values)
{
    if (! values.isObject())
        return;

    for (size_t i = 0; i < m_runtime.plan().controls.size(); ++i)
    {
        const auto& control = m_runtime.plan().controls[i];

        if (! control.moduleLevel)
            continue;

        auto* dynamic = values.getDynamicObject();

        if (dynamic == nullptr || ! dynamic->hasProperty (juce::Identifier (control.id)))
            continue;

        const auto value = static_cast<float> (dynamic->getProperty (juce::Identifier (control.id)));
        writeModuleLevelValue (static_cast<int> (i), juce::jlimit (0.0f, 1.0f, value));
    }
}

juce::var ShellProcessor::saveModuleLevelValues() const
{
    auto* root = new juce::DynamicObject();

    for (size_t i = 0; i < m_runtime.plan().controls.size(); ++i)
    {
        const auto& control = m_runtime.plan().controls[i];

        if (! control.moduleLevel)
            continue;

        const auto value = i < m_moduleLevelValues.size() ? m_moduleLevelValues[i]
                                                          : control.defaultValueNormalised;

        root->setProperty (juce::Identifier (control.id), static_cast<double> (value));
    }

    return juce::var (root);
}

//==============================================================================
const juce::String ShellProcessor::getName() const
{
    return juce::String::fromUTF8 (m_runtime.name().c_str());
}

bool ShellProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (! m_runtime.isValid())
        return false;

    const auto required = m_runtime.channelCount();

    if (layouts.getMainInputChannelSet().size() != required)
        return false;

    if (layouts.getMainOutputChannelSet().size() != required)
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return ! layouts.getMainInputChannelSet().isDiscreteLayout();
}

juce::AudioProcessorParameter* ShellProcessor::getBypassParameter() const
{
    // The specification owns bypass. When it declares a $factory.bypass control,
    // that control IS the host's bypass parameter, so the host's bypass button
    // and the plugin's own switch cannot disagree.
    for (size_t i = 0; i < m_runtime.plan().controls.size(); ++i)
    {
        const auto& control = m_runtime.plan().controls[i];

        if (! control.isFactoryControl)
            continue;

        if (control.targetRef != "$factory.bypass")
            continue;

        if (auto* parameter = getParameterForControl (static_cast<int> (i)))
            return parameter;
    }

    return nullptr;
}

//==============================================================================
void ShellProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    m_preparedSampleRate = sampleRate;
    m_preparedBlockSize = samplesPerBlock;

    if (m_engine == nullptr)
        return;

    const auto error = m_engine->prepare (sampleRate, samplesPerBlock);

    if (error.failed())
    {
        m_engineOk = false;
        m_engineError = error;
        return;
    }

    setLatencySamples (m_engine->getLatencySamples());

    // Module-level values live outside the kernel's state blob, so they are
    // re-pushed after every prepare.
    for (size_t i = 0; i < m_runtime.plan().controls.size(); ++i)
        if (m_runtime.plan().controls[i].moduleLevel)
            writeModuleLevelValue (static_cast<int> (i),
                                   i < m_moduleLevelValues.size()
                                       ? m_moduleLevelValues[i]
                                       : m_runtime.plan().controls[i].defaultValueNormalised);
}

void ShellProcessor::releaseResources()
{
    if (m_engine != nullptr)
        m_engine->reset();
}

void ShellProcessor::reset()
{
    if (m_engine != nullptr)
        m_engine->reset();
}

void ShellProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();

    if (m_engine == nullptr || ! m_engine->isPrepared() || numSamples <= 0)
    {
        buffer.clear();
        return;
    }

    const auto required = m_engine->getChannelCount();

    if (buffer.getNumChannels() < required)
    {
        buffer.clear();
        return;
    }

    // Channels the graph does not drive are silenced rather than left carrying
    // whatever the host had in them.
    for (int channel = required; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, 0, numSamples);

    float* channels[2] = { nullptr, nullptr };

    for (int channel = 0; channel < required && channel < 2; ++channel)
        channels[channel] = buffer.getWritePointer (channel);

    // An instrument renders note by note: the block is split at every MIDI event
    // so a note starts on the sample the host scheduled it, not on the block
    // boundary. A processor chain takes the fast path and ignores MIDI entirely.
    if (m_engine->isInstrument() && ! midi.isEmpty())
    {
        int position = 0;

        for (const auto metadata : midi)
        {
            const int eventPosition = juce::jlimit (0, numSamples, metadata.samplePosition);

            if (eventPosition > position)
            {
                m_engine->process (channels, required, eventPosition - position);

                for (int channel = 0; channel < required && channel < 2; ++channel)
                    channels[channel] += eventPosition - position;

                position = eventPosition;
            }

            deliverMidi (metadata.getMessage());
        }

        if (position < numSamples)
            m_engine->process (channels, required, numSamples - position);

        return;
    }

    m_engine->process (channels, required, numSamples);
}

void ShellProcessor::deliverMidi (const juce::MidiMessage& message) noexcept
{
    if (message.isNoteOn())
        m_engine->handleNoteOn (message.getNoteNumber(), message.getFloatVelocity());
    else if (message.isNoteOff())
        m_engine->handleNoteOff (message.getNoteNumber());
    else if (message.isAllNotesOff() || message.isAllSoundOff())
        m_engine->handleAllNotesOff();
}

bool ShellProcessor::acceptsMidi() const
{
    // Only an instrument source can use notes; a processor that claimed them
    // would make a DAW route its MIDI away from the instrument before it.
    return m_engine != nullptr && m_engine->isInstrument();
}

void ShellProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Hard bypass: the input passes through untouched, and the reported latency
    // is deliberately NOT cleared, so bypassing never shifts the plugin's
    // position in the host's delay compensation.
    juce::ignoreUnused (midi, buffer);
    juce::ScopedNoDenormals noDenormals;
}

//==============================================================================
juce::AudioProcessorEditor* ShellProcessor::createEditor()
{
    return new ShellEditor (*this);
}

//==============================================================================
void ShellProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    auto* root = new juce::DynamicObject();

    root->setProperty ("stateSchemaVersion", 1);
    root->setProperty ("productId", juce::String (m_runtime.pluginId()));
    root->setProperty ("productVersion", juce::String (m_runtime.version()));
    root->setProperty ("buildId", juce::String (m_runtime.provenance().buildId));

    // The kernel blob carries macros, base parameter values, dry/wet and bypass,
    // and it refuses to restore into a graph built from a different spec.
    if (m_engine != nullptr)
    {
        const auto blob = m_engine->saveState();

        root->setProperty ("factoryStateHash",
                           juce::String::toHexString (static_cast<juce::int64> (m_runtime.spec().fingerprint())));
        root->setProperty ("factoryState",
                           juce::Base64::toBase64 (blob.getData(), blob.getSize()));
    }

    // Module-level controls are not kernel bindings, so the shell persists them.
    root->setProperty ("moduleLevel", saveModuleLevelValues());

    const auto text = juce::JSON::toString (juce::var (root), false);
    destination.replaceAll (text.toRawUTF8(), text.getNumBytesAsUTF8());
}

void ShellProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    juce::var root;
    const auto parseResult = juce::JSON::parse (juce::String::fromUTF8 (static_cast<const char*> (data),
                                                                      sizeInBytes),
                                               root);

    auto* dynamic = root.getDynamicObject();

    if (parseResult.failed() || dynamic == nullptr)
        return;

    // A blob from a different product (or a different factory schema) is refused
    // rather than mis-mapped, exactly as the kernel refuses a mismatched spec.
    if (static_cast<int> (dynamic->getProperty ("stateSchemaVersion")) != 1)
        return;

    if (dynamic->getProperty ("productId").toString().toStdString() != m_runtime.pluginId())
        return;

    if (m_engine == nullptr)
        return;

    const auto encoded = dynamic->getProperty ("factoryState").toString();

    if (encoded.isNotEmpty())
    {
        juce::MemoryBlock blob;

        if (blob.fromBase64Encoding (encoded))
            if (m_engine->restoreState (blob.getData(), blob.getSize()).failed())
                return;   // the kernel rejected it; keep the current state intact
    }

    applyModuleLevelValues (dynamic->getProperty ("moduleLevel"));

    for (auto& parameter : m_parameters)
        parameter->refreshFromEngine();
}

//==============================================================================
juce::var ShellProcessor::describeBuild() const
{
    auto* root = new juce::DynamicObject();

    root->setProperty ("valid", isProductValid());
    root->setProperty ("pluginId", juce::String (m_runtime.pluginId()));

    if (! isProductValid())
    {
        root->setProperty ("error", juce::String (initError().describe()));
        return juce::var (root);
    }

    root->setProperty ("pluginName", juce::String (m_runtime.name()));
    root->setProperty ("vendor", juce::String (m_runtime.vendor()));
    root->setProperty ("version", juce::String (m_runtime.version()));
    root->setProperty ("uiTemplate", juce::String (uiTemplateName (m_runtime.uiTemplate())));
    root->setProperty ("specFingerprint",
                       juce::String::toHexString (static_cast<juce::int64> (m_runtime.spec().fingerprint())));
    root->setProperty ("uiPlanFingerprint",
                       juce::String::toHexString (static_cast<juce::int64> (m_runtime.plan().fingerprint())));
    root->setProperty ("graphFingerprint",
                       juce::String::toHexString (static_cast<juce::int64> (m_engine->graphFingerprint())));
    root->setProperty ("catalogFingerprint",
                       juce::String::toHexString (static_cast<juce::int64> (builtinRegistry().catalogFingerprint())));
    root->setProperty ("hostParameterCount", static_cast<int> (m_parameters.size()));
    root->setProperty ("controlCount", getNumControls());
    root->setProperty ("stereo", m_runtime.channelCount() == 2);
    root->setProperty ("buildStamp", juce::String (m_runtime.buildStamp()));
    root->setProperty ("chain", juce::String (m_runtime.chainDescription()));

    return juce::var (root);
}

} // namespace shell
} // namespace zfactory
