#pragma once

#include "zfactory/GraphEngine.h"
#include "zfactory/shell/ProductRuntime.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

namespace zfactory
{
namespace shell
{

//==============================================================================
// ShellProcessor - the single plugin implementation every Zarlino factory
// product shares.
//
// It owns the product runtime, the compiled GraphEngine and the host parameter
// list. It contains no product-specific logic: everything it does is driven by
// the embedded specification, so adding a new product never means writing or
// editing a processor.
//
// HOST PARAMETER LIST
//   index 0 .. numMacros-1                  macros, in spec order
//   index numMacros .. numHostParameters-1   exposed spec parameters, in spec order
//
// Parameter identities are the control ids from the compiled UI plan, never
// ordinals, so an automation lane recorded against one product revision still
// resolves in the next one. Module-level controls (only drawn by the Advanced
// template) are deliberately NOT host parameters: they are not declared by the
// specification, so they are persisted by this processor itself.
//
// THREADING
//   * prepareToPlay / releaseResources / getStateInformation   - control thread, may allocate
//   * processBlock                                             - audio thread, allocation free
//   * setControlNormalised / getControlNormalised              - either thread, lock free
//==============================================================================

/** A host-facing parameter. Normalised access forwards straight into the
    kernel's parameter layer, so the host and the DSP can never disagree about
    what a value means. */
class ShellParameter : public juce::RangedAudioParameter
{
public:
    enum class Kind { Macro, SpecParameter };

    ShellParameter (const UiControl& control, Kind kind, int engineIndex);

    // ---- identity -----------------------------------------------------------
    // The parameter id, name and unit are supplied to AudioProcessorParameterWithID
    // in the constructor, so getParameterID()/getName()/getLabel() all come from
    // the compiled UI plan and can never drift from it.

    int getNumSteps() const override { return m_numSteps; }
    bool isDiscrete() const override { return m_numSteps > 0; }
    bool isBoolean() const override { return m_numSteps == 2; }

    // ---- value --------------------------------------------------------------
    float getValue() const override;
    void setValue (float newValue) override;
    float getDefaultValue() const override { return m_control.defaultValueNormalised; }

    juce::String getText (float normalisedValue, int maximumStringLength) const override;
    float getValueForText (const juce::String& text) const override;

    const juce::NormalisableRange<float>& getNormalisableRange() const override { return m_range; }

    // ---- shell internals ----------------------------------------------------
    void attachEngine (GraphEngine& engine) noexcept { m_engine = &engine; }

    /** Re-read the effective value after a macro moved, without disturbing the
        host-owned base value. */
    void refreshFromEngine();

    const UiControl& control() const noexcept { return m_control; }
    Kind kind() const noexcept { return m_kind; }

private:
    float effectiveValue() const;
    float effectiveNormalised() const;

    UiControl m_control;
    Kind m_kind;
    int m_engineIndex;              // binding index, or macro index
    int m_numSteps = 0;
    juce::NormalisableRange<float> m_range;
    GraphEngine* m_engine = nullptr;
};

//==============================================================================
class ShellProcessor : public juce::AudioProcessor
{
public:
    ShellProcessor();
    ~ShellProcessor() override;

    // ---- AudioProcessor -----------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    void processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    void getStateInformation (juce::MemoryBlock& destination) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    // ---- shell API used by the editor and the QA gate ------------------------
    /** The product failed to initialise: the plugin stays silent and the editor
        shows the deterministic kernel error instead of a broken interface. */
    bool isProductValid() const noexcept { return m_runtime.isValid() && m_engineOk; }
    const ProductRuntime& product() const noexcept { return m_runtime; }
    const FactoryError& initError() const noexcept
    {
        return m_runtime.isValid() ? m_engineError : m_runtime.error();
    }

    GraphEngine* engine() noexcept { return m_engine.get(); }
    const GraphEngine* engine() const noexcept { return m_engine.get(); }

    /** Number of controls in the compiled UI plan (including ones no template
        draws, and module-level controls that are not host parameters). */
    int getNumControls() const noexcept { return static_cast<int> (m_runtime.plan().controls.size()); }
    const UiControl* getControl (int controlIndex) const noexcept;

    /** Uniform access for the editor, regardless of whether a control is a host
        parameter, a macro or a module-level value. */
    float getControlNormalised (int controlIndex) const;

    /** Writes a control value WITHOUT notifying a host. Used by the quality gate,
        which is not a host. */
    void setControlNormalised (int controlIndex, float normalised);

    /** Writes a control value on behalf of a real user gesture, notifying the
        host so the change is recorded as automation. Used by the editor. */
    void setControlFromUser (int controlIndex, float normalised);

    juce::String getControlText (int controlIndex) const;
    void beginControlGesture (int controlIndex);
    void endControlGesture (int controlIndex);

    /** Host parameter for a control, or nullptr when the control is not one. */
    ShellParameter* getParameterForControl (int controlIndex) const noexcept;

    /** Values of module-level controls, keyed by control index. Persisted next
        to the kernel's own blob because the kernel has no binding for them. */
    juce::var saveModuleLevelValues() const;

    // ---- QA support ---------------------------------------------------------
    /** Deterministic description of everything this build claims to be. */
    juce::var describeBuild() const;

private:
    void buildParameters();
    void applyModuleLevelValues (const juce::var& values);
    void writeModuleLevelValue (int controlIndex, float normalised);

    ProductRuntime m_runtime;
    std::unique_ptr<GraphEngine> m_engine;

    bool m_engineOk = false;
    FactoryError m_engineError;

    /** Route one MIDI message to the engine's note interface. Realtime safe. */
    void deliverMidi (const juce::MidiMessage& message) noexcept;

    std::vector<std::unique_ptr<ShellParameter>> m_parameters;
    std::vector<int> m_controlToParameterIndex;   // control index -> host index, or -1
    std::vector<float> m_moduleLevelValues;       // normalised, per control index

    double m_preparedSampleRate = 0.0;
    int m_preparedBlockSize = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShellProcessor)
};

} // namespace shell
} // namespace zfactory
