#pragma once

#include "zfactory/ModuleManifest.h"

namespace zfactory
{

//==============================================================================
// Everything a module needs to know about the host audio environment before it
// can process. prepare() may allocate; it is never called from the audio
// thread.
//==============================================================================
struct PrepareContext
{
    double sampleRate = 48000.0;
    int maxBlockSize = 512;
    int numChannels = 2;
};

//==============================================================================
// The one stable interface through which every factory-compatible DSP module
// operates.
//
// REAL-TIME SAFETY CONTRACT
//   * prepare() and reset()      - may allocate, never called on the audio thread.
//   * process()                  - MUST NOT allocate, lock (unbounded), perform
//                                  I/O, call into the registry, or throw.
//   * setParameter()             - MUST NOT allocate or lock. It stores a value
//                                  and marks coefficients dirty; cheap work is
//                                  done at the top of the next process() call.
//   * getParameter()             - MUST be cheap and noexcept.
//
//   ModuleManifest::realtimeSafe must be true for a module to be registered, and
//   the factory test suite enforces the contract by running the whole graph with
//   a global allocation trap armed.
//
// PARAMETER INDEXING
//   Parameters are addressed by integer index resolved once at graph build time
//   from the module's manifest. No string lookup happens while audio is running.
//
// BUFFERS
//   process() receives non-interleaved channel pointers whose channel count
//   matches the graph's ChannelMode. Buffers are processed in place.
//==============================================================================
class DspModule
{
public:
    virtual ~DspModule() = default;

    DspModule (const DspModule&) = delete;
    DspModule& operator= (const DspModule&) = delete;

    /** Identity + parameter metadata + certification. Stateless, noexcept. */
    virtual const ModuleManifest& getManifest() const noexcept = 0;

    /** Allocate everything process() will need. Called off the audio thread. */
    virtual void prepare (const PrepareContext& context) = 0;

    /** Clear all internal state to silence. Must be noexcept and allocation
        free so a host may call it between blocks. */
    virtual void reset() noexcept = 0;

    /** Process numSamples in place for every channel. Realtime-safe. */
    virtual void process (float* const* channels, int numChannels, int numSamples) noexcept = 0;

    /** Push an engineering-unit parameter value. Realtime-safe. */
    virtual void setParameter (int parameterIndex, float value) noexcept = 0;

    /** Read back the last engineering-unit value pushed. Realtime-safe. */
    virtual float getParameter (int parameterIndex) const noexcept = 0;

    /** Actual latency in samples after the most recent prepare(). */
    virtual int getLatencySamples() const noexcept = 0;

    //--------------------------------------------------------------------------
    // Note events. Only modules whose manifest declares `isSource`, or that
    // shape a note-gated signal, need these; the defaults make every other
    // module ignore MIDI, so hosts that send notes to a processor are harmless.
    // All three are realtime safe: no allocation, no locks, no I/O.
    //--------------------------------------------------------------------------
    virtual void handleNoteOn (int /*midiNote*/, float /*velocity*/) noexcept {}
    virtual void handleNoteOff (int /*midiNote*/) noexcept {}
    virtual void handleAllNotesOff() noexcept {}

protected:
    DspModule() = default;
};

//==============================================================================
// A module factory: the manifest plus a constructor for the processor.
// Module source lives in factory/modules and is registered by
// registerBuiltinModules().
//==============================================================================
struct ModuleDescriptor
{
    ModuleManifest manifest;
    DspModule* (*create)() = nullptr;

    bool valid() const noexcept { return create != nullptr && ! manifest.id.empty(); }
};

} // namespace zfactory
