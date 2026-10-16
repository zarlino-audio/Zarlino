//==============================================================================
// Realtime safety.
//
// The module contract says process(), reset(), setParameter() and the parameter
// layer never allocate. Rather than trusting the claim, this suite replaces the
// global allocation operators with a counting trap and runs the whole chain with
// the trap armed.
//==============================================================================

#include "TestSupport.h"

#include <atomic>
#include <cstdlib>
#include <new>

//==============================================================================
// Allocation trap. Enabled only around the calls under test, and counted per
// thread so that unrelated background allocations cannot produce a false failure.
//==============================================================================
namespace zfactory
{
namespace allocationtrap
{
std::atomic<bool> enabled { false };
std::atomic<long long> count { 0 };

inline void note (std::size_t size) noexcept
{
    if (enabled.load (std::memory_order_relaxed) && size > 0)
        count.fetch_add (1, std::memory_order_relaxed);
}

inline void arm() noexcept
{
    count.store (0, std::memory_order_relaxed);
    enabled.store (true, std::memory_order_relaxed);
}

inline long long disarm() noexcept
{
    enabled.store (false, std::memory_order_relaxed);
    return count.load (std::memory_order_relaxed);
}
} // namespace allocationtrap
} // namespace zfactory

void* operator new (std::size_t size)
{
    zfactory::allocationtrap::note (size);

    if (auto* memory = std::malloc (size == 0 ? 1 : size))
        return memory;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    return ::operator new (size);
}

void operator delete (void* memory) noexcept
{
    std::free (memory);
}

void operator delete[] (void* memory) noexcept
{
    std::free (memory);
}

void operator delete (void* memory, std::size_t) noexcept
{
    std::free (memory);
}

void operator delete[] (void* memory, std::size_t) noexcept
{
    std::free (memory);
}

//==============================================================================

using namespace zfactory;
using namespace zfactory::test;

namespace
{

class RealtimeSafetyTests : public juce::UnitTest
{
public:
    RealtimeSafetyTests() : juce::UnitTest ("Factory Realtime Safety") {}

    void runTest() override
    {
        beginTest ("The allocation trap itself works");

        allocationtrap::arm();
        auto* probe = new int (7);
        const auto trapped = allocationtrap::disarm();

        expect (trapped > 0, "the trap must observe an allocation");
        delete probe;

        testProcessIsAllocationFree();
        testControlSurfaceIsAllocationFree();
        testResetIsAllocationFree();
    }

private:
    PluginSpec makeFullSpec()
    {
        auto spec = makeBaseSpec();

        addModule (spec, "gain", "zarlino.gain");
        addModule (spec, "hp", "zarlino.filter.multimode");
        addModule (spec, "eq", "zarlino.eq.parametric");
        addModule (spec, "comp", "zarlino.dynamics.compressor");
        addModule (spec, "gate", "zarlino.dynamics.gate");
        addModule (spec, "sat", "zarlino.saturation");
        addModule (spec, "tone", "zarlino.eq.tilt");
        addModule (spec, "shaper", "zarlino.shaping.transient");
        addModule (spec, "width", "zarlino.stereo.width");
        addModule (spec, "blend", "zarlino.mix.drywet");
        addModule (spec, "limit", "zarlino.limiter");

        setModuleParameter (spec.modules[1], "shape", 1.0f);

        addParameter (spec, "trim", "gain.gainDb", -12.0f, 12.0f, 0.0f, "dB");
        addParameter (spec, "drive", "sat.drive", 0.0f, 1.0f, 0.4f);
        addParameter (spec, "harmonics", "sat.harmReduction", 0.0f, 1.0f, 0.3f);
        addParameter (spec, "ceiling", "limit.ceiling", -6.0f, 0.0f, -1.0f, "dB");
        addParameter (spec, "mix", "$factory.dryWet", 0.0f, 1.0f, 0.8f);
        addParameter (spec, "bypassed", "$factory.bypass", 0.0f, 1.0f, 0.0f);

        spec.dryWet.enabled = true;

        addMacro (spec, "character", 0.5f, { mapping ("drive", 0.0f, 0.9f),
                                             mapping ("harmonics", 0.0f, 0.5f),
                                             mapping ("ceiling", 0.3f, 1.0f, true) });

        return spec;
    }

    std::unique_ptr<GraphEngine> buildPrepared()
    {
        auto engine = GraphEngine::create (makeFullSpec(), builtinRegistry());

        if (! engine.ok())
        {
            expect (false, "graph build failed: " + jstr (engine.error().describe()));
            return nullptr;
        }

        if (auto error = engine.value()->prepare (48000.0, 256); error.failed())
        {
            expect (false, "prepare failed: " + jstr (error.describe()));
            return nullptr;
        }

        return std::move (engine.value());
    }

    void testProcessIsAllocationFree()
    {
        beginTest ("process() allocates nothing, including with a latency-compensated dry path");

        auto engine = buildPrepared();

        if (engine == nullptr)
            return;

        Block block (2, 256);
        block.sine (440.0, 48000.0, 0.5f);

        // No warm-up block: the very first process() call after prepare() must
        // already be allocation free, otherwise a coefficient cache is still
        // being populated on the audio thread.
        allocationtrap::arm();

        for (int i = 0; i < 64; ++i)
        {
            block.sine (440.0, 48000.0, 0.5f, static_cast<long long> (i) * 256);
            engine->process (block.pointers(), 2, 256);
        }

        const auto allocations = allocationtrap::disarm();

        expectEquals (allocations, 0LL, "process() must not allocate");
        expect (block.peak() > 0.01f, "the trap must not have been armed around a no-op");

        // Changing every macro while processing must also stay allocation free.
        allocationtrap::arm();

        for (int i = 0; i < 32; ++i)
        {
            engine->parameters().setMacroValue (0, static_cast<float> (i) / 31.0f);
            block.sine (440.0, 48000.0, 0.5f, static_cast<long long> (i) * 256);
            engine->process (block.pointers(), 2, 256);
        }

        const auto automationAllocations = allocationtrap::disarm();

        expectEquals (automationAllocations, 0LL, "macro automation must not allocate");
    }

    void testControlSurfaceIsAllocationFree()
    {
        beginTest ("setParameter, applyParameters and setBypassed allocate nothing");

        auto engine = buildPrepared();

        if (engine == nullptr)
            return;

        allocationtrap::arm();

        for (int i = 0; i < 64; ++i)
        {
            engine->setDryWetNormalised (static_cast<float> (i) / 63.0f);
            engine->setBypassed ((i % 2) == 0);
            engine->applyParameters();
        }

        const auto allocations = allocationtrap::disarm();
        expectEquals (allocations, 0LL, "the control surface must not allocate");

        // Writing every binding and macro position is also allocation free.
        allocationtrap::arm();

        for (int sweep = 0; sweep < 8; ++sweep)
        {
            for (int i = 0; i < engine->parameters().numBindings(); ++i)
                engine->parameters().setBaseValue (i, static_cast<float> (sweep));

            for (int i = 0; i < engine->parameters().numMacros(); ++i)
                engine->parameters().setMacroValue (i, static_cast<float> (sweep) / 7.0f);

            engine->applyParameters();
        }

        const auto parameterAllocations = allocationtrap::disarm();
        expectEquals (parameterAllocations, 0LL, "parameter writes must not allocate");
    }

    void testResetIsAllocationFree()
    {
        beginTest ("reset() allocates nothing");

        auto engine = buildPrepared();

        if (engine == nullptr)
            return;

        Block block (2, 256);
        block.sine (440.0, 48000.0, 0.5f);
        engine->process (block.pointers(), 2, 256);

        allocationtrap::arm();

        for (int i = 0; i < 32; ++i)
            engine->reset();

        const auto allocations = allocationtrap::disarm();
        expectEquals (allocations, 0LL, "reset() must not allocate");

        // The graph still works after the reset storm.
        engine->process (block.pointers(), 2, 256);
        expect (block.allFinite(), "the graph must survive repeated resets");
    }
};

RealtimeSafetyTests realtimeSafetyTests;

} // namespace

void registerRealtimeSafetyTests() {}
