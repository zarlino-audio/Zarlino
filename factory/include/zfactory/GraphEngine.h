#pragma once

#include "zfactory/DspModule.h"
#include "zfactory/ModuleRegistry.h"
#include "zfactory/ParameterSystem.h"
#include "zfactory/PluginSpec.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <string>
#include <vector>

namespace zfactory
{

//==============================================================================
// Certification policy. V1 user-generated plugins may reference CERTIFIED
// modules only; that is the default.
//==============================================================================
struct GraphOptions
{
    CertificationState requiredCertification = CertificationState::Certified;
};

//==============================================================================
// GraphEngine - deterministic construction and execution of the processor
// described by a PluginSpec.
//
// Topology (v1): a strictly serial chain of module instances, wrapped in one
// global dry/wet mixer and one global bypass switch. There is no parallel
// routing, no feedback, and no dynamic graph editing: the spec fully determines
// the graph, and the same spec always produces the same graph.
//
//   in --> [ bypass ] --> [ dry copy ] --> m0 -> m1 -> ... -> mN --> [ mix ] --> out
//
// Latency: the sum of the module latencies reported after prepare(). When
// latency is non-zero the dry path is delayed by the same amount so that the
// dry/wet mix stays phase coherent, and the total latency is reported to the
// host through getLatencySamples().
//
// THREADING
//   * create() / prepare() / restoreState() - control thread, may allocate.
//   * process() / applyParameters() / reset() / set* - audio thread. None of
//     them allocate, lock or perform I/O. Enforced by the factory test suite,
//     which runs the whole graph with a global allocation trap armed.
//==============================================================================
class GraphEngine
{
public:
    struct ModuleInstance
    {
        std::string instanceId;
        ModuleManifest manifest;
        std::unique_ptr<DspModule> module;
        int latencySamples = 0;
    };

    //--------------------------------------------------------------------------
    static Result<std::unique_ptr<GraphEngine>> create (const PluginSpec& spec,
                                                       const ModuleRegistry& registry,
                                                       const GraphOptions& options = GraphOptions());

    ~GraphEngine() = default;

    GraphEngine (const GraphEngine&) = delete;
    GraphEngine& operator= (const GraphEngine&) = delete;

    //--------------------------------------------------------------------------
    // Lifecycle
    //--------------------------------------------------------------------------
    /** Prepare every module and allocate all scratch memory. Returns a
        deterministic error for an unsupported sample rate, an unsupported block
        size, or a module whose declared sample-rate domain excludes the rate. */
    FactoryError prepare (double sampleRate, int maxBlockSize);

    /** Clear every module's state. Allocation free, realtime safe. */
    void reset() noexcept;

    //--------------------------------------------------------------------------
    // Audio. `channels` must carry getChannelCount() distinct channel pointers;
    // blocks larger than the prepared maximum are processed in chunks.
    //--------------------------------------------------------------------------
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    //--------------------------------------------------------------------------
    // Control. All of these are realtime safe; changes take effect at the next
    // process() call (which recomputes the parameter layer itself).
    //--------------------------------------------------------------------------
    void setBypassed (bool bypassed) noexcept;
    bool isBypassed() const noexcept;

    /** Normalised 0..1 dry/wet position. Routes through the spec's dry/wet
        parameter when one is declared, otherwise sets the fixed mix. */
    void setDryWetNormalised (float mix) noexcept;
    float getDryWetNormalised() const noexcept;

    ParameterSystem& parameters() noexcept { return m_parameters; }
    const ParameterSystem& parameters() const noexcept { return m_parameters; }

    /** Push the current effective parameter values into the modules. Called
        automatically at the top of process(); exposed for tests and for hosts
        that want to flush automation explicitly. */
    void applyParameters() noexcept;

    //--------------------------------------------------------------------------
    // Introspection
    //--------------------------------------------------------------------------
    bool isPrepared() const noexcept { return m_prepared; }
    double getSampleRate() const noexcept { return m_sampleRate; }
    int getMaxBlockSize() const noexcept { return m_maxBlockSize; }
    int getChannelCount() const noexcept { return m_channelCount; }
    int getLatencySamples() const noexcept { return m_latencySamples; }

    const std::vector<ModuleInstance>& modules() const noexcept { return m_modules; }
    const PluginSpec& spec() const noexcept { return m_spec; }

    /** Fingerprint of the constructed graph: spec + resolved catalog identity. */
    uint64_t graphFingerprint() const noexcept { return m_graphFingerprint; }

    /** Stable, human readable description of the built chain (CI evidence). */
    std::string describeGraph() const;

    //--------------------------------------------------------------------------
    // State serialization. The blob carries the factory schema version and the
    // spec fingerprint; restoring a blob into a graph built from a different
    // spec fails deterministically instead of silently mis-mapping values.
    //--------------------------------------------------------------------------
    juce::MemoryBlock saveState() const;
    FactoryError restoreState (const void* data, size_t numBytes);

private:
    GraphEngine() = default;

    void processBlockInternal (float* const* channels, int offset, int numSamples) noexcept;

    PluginSpec m_spec;
    uint64_t m_specFingerprint = 0;
    uint64_t m_graphFingerprint = 0;

    std::vector<ModuleInstance> m_modules;
    ParameterSystem m_parameters;

    int m_dryWetBindingIndex = -1;
    int m_bypassBindingIndex = -1;
    float m_fixedDryWet = 1.0f;

    bool m_prepared = false;
    bool m_bypassed = false;
    double m_sampleRate = 48000.0;
    int m_maxBlockSize = 0;
    int m_channelCount = 2;
    int m_latencySamples = 0;

    std::vector<float> m_dryScratch;      // channelCount * maxBlockSize
    std::vector<float> m_dryDelay;        // channelCount * delayLength
    int m_dryDelayLength = 0;
    int m_dryDelayWriteIndex = 0;
};

} // namespace zfactory
