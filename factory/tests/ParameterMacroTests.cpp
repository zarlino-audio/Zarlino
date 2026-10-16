//==============================================================================
// Parameter and macro algebra: propagation into the DSP, inversion, scaling,
// bounding, clamping and deterministic composition.
//==============================================================================

#include "TestSupport.h"

using namespace zfactory;
using namespace zfactory::test;

namespace
{

class ParameterMacroTests : public juce::UnitTest
{
public:
    ParameterMacroTests() : juce::UnitTest ("Factory Parameters and Macros") {}

    void runTest() override
    {
        testParameterPropagation();
        testMacroPropagation();
        testMappingAlgebra();
        testClampingAndExtremes();
        testFactoryControls();
        testInternalParameters();
    }

private:
    /** gain -> tilt EQ, with macros on both parameters. */
    PluginSpec makeSpecWithMacros()
    {
        auto spec = makeBaseSpec();

        addModule (spec, "gain", "zarlino.gain");
        addModule (spec, "tone", "zarlino.eq.tilt");

        addParameter (spec, "trim", "gain.gainDb", -18.0f, 18.0f, 0.0f, "dB");
        addParameter (spec, "tone", "tone.tiltDb", -6.0f, 6.0f, 0.0f, "dB");

        addMacro (spec, "level", 0.5f, { mapping ("trim", 0.0f, 1.0f) });
        addMacro (spec, "inverted", 0.0f, { mapping ("trim", 0.0f, 1.0f, true) });
        addMacro (spec, "shape", 0.5f, { mapping ("tone", 0.0f, 1.0f) });

        return spec;
    }

    /** The same chain with no macros at all, so the host layer owns every
        parameter and direct writes are observable. */
    PluginSpec makeSpecWithoutMacros()
    {
        auto spec = makeBaseSpec();

        addModule (spec, "gain", "zarlino.gain");
        addModule (spec, "tone", "zarlino.eq.tilt");

        addParameter (spec, "trim", "gain.gainDb", -18.0f, 18.0f, 0.0f, "dB");
        addParameter (spec, "tone", "tone.tiltDb", -6.0f, 6.0f, 0.0f, "dB");

        return spec;
    }

    void testParameterPropagation()
    {
        beginTest ("Exposed parameters reach the module they target");

        auto spec = makeSpecWithoutMacros();
        auto engine = GraphEngine::create (spec, builtinRegistry());

        if (! expectSuccess (*this, engine, "graph"))
            return;

        auto& built = *engine.value();

        if (auto error = built.prepare (48000.0, 128); error.failed())
        {
            expect (false, "prepare failed: " + jstr (error.describe()));
            return;
        }

        expectEquals (built.parameters().indexOfBinding ("trim"), 0);
        expectEquals (built.parameters().indexOfBinding ("tone"), 1);
        expectEquals (built.parameters().indexOfBinding ("absent"), -1);

        // Normalised write, engineering value read back.
        built.parameters().setBaseNormalised (0, 1.0f);
        built.applyParameters();
        expectWithinAbsoluteError (built.modules()[0].module->getParameter (0), 18.0f, 1.0e-3f,
                                   "normalised 1.0 must be the top of the trim range");

        built.parameters().setBaseNormalised (0, 0.0f);
        built.applyParameters();
        expectWithinAbsoluteError (built.modules()[0].module->getParameter (0), -18.0f, 1.0e-3f,
                                   "normalised 0.0 must be the bottom of the trim range");

        // Engineering value write.
        built.parameters().setBaseValue (1, 3.0f);
        built.applyParameters();
        expectWithinAbsoluteError (built.modules()[1].module->getParameter (0), 3.0f, 1.0e-3f,
                                   "tilt must receive the engineering value");

        expectWithinAbsoluteError (built.parameters().getEffectiveValue (1), 3.0f, 1.0e-3f);
        expectWithinAbsoluteError (built.parameters().getEffectiveValue (0), -18.0f, 1.0e-3f);
    }

    void testMacroPropagation()
    {
        beginTest ("Macros drive every parameter they map, in spec order");

        auto spec = makeSpecWithMacros();
        auto engine = GraphEngine::create (spec, builtinRegistry());

        if (! expectSuccess (*this, engine, "graph"))
            return;

        auto& built = *engine.value();
        built.prepare (48000.0, 128);

        expectEquals (built.parameters().numMacros(), 3);
        expectEquals (built.parameters().numMappings(), 3);
        expectEquals (built.parameters().indexOfMacro ("level"), 0);

        // "level" and "inverted" both target trim. "inverted" is later in spec
        // order, so it owns the parameter and "level" can no longer move it -
        // the documented "last mapping in spec order wins" rule.
        built.parameters().setMacroValue (1, 0.0f);
        built.parameters().setMacroValue (0, 0.0f);
        built.applyParameters();

        const auto levelLow = built.parameters().getEffectiveValue (0);
        expectWithinAbsoluteError (levelLow, 18.0f, 1.0e-3f,
                                   "inverted 0 -> top of the trim range");

        built.parameters().setMacroValue (0, 1.0f);
        built.applyParameters();

        expectWithinAbsoluteError (built.parameters().getEffectiveValue (0), levelLow, 1.0e-3f,
                                   "the later macro in spec order owns the parameter");
        expectWithinAbsoluteError (built.parameters().getEffectiveValue (0), 18.0f, 1.0e-3f);

        // Move the inverted macro to 1: it should land on the bottom.
        built.parameters().setMacroValue (1, 1.0f);
        built.applyParameters();
        expectWithinAbsoluteError (built.parameters().getEffectiveValue (0), -18.0f, 1.0e-3f,
                                   "inverted 1 -> bottom of the trim range");

        // "shape" only touches the tone parameter and leaves trim alone.
        built.parameters().setMacroValue (2, 0.0f);
        built.applyParameters();
        expectWithinAbsoluteError (built.parameters().getEffectiveValue (1), -6.0f, 1.0e-3f);

        built.parameters().setMacroValue (2, 1.0f);
        built.applyParameters();
        expectWithinAbsoluteError (built.parameters().getEffectiveValue (1), 6.0f, 1.0e-3f);
        expectWithinAbsoluteError (built.modules()[1].module->getParameter (0), 6.0f, 1.0e-3f);

        // A macro-driven parameter ignores direct host writes (documented rule).
        built.parameters().setBaseValue (1, -6.0f);
        built.parameters().setMacroValue (2, 0.0f);
        built.applyParameters();
        expectWithinAbsoluteError (built.parameters().getEffectiveValue (1), -6.0f, 1.0e-3f,
                                   "the macro still owns the parameter");
        expect (built.parameters().bindings()[1].macroDriven, "binding must be marked macro driven");
        expect (built.parameters().bindings()[0].macroDriven, "trim is driven by two macros");
    }

    void testMappingAlgebra()
    {
        beginTest ("Inversion, curve, scaling and sub-ranges behave exactly as documented");

        // Every macro drives a distinct parameter, so the "last mapping wins"
        // rule cannot interfere with the per-macro expectations below.
        auto spec = makeBaseSpec();

        addModule (spec, "gain", "zarlino.gain");
        addModule (spec, "gain2", "zarlino.gain");
        addModule (spec, "level", "zarlino.gain");
        addModule (spec, "tone", "zarlino.eq.tilt");

        addParameter (spec, "trim", "gain.gainDb", -18.0f, 18.0f, 0.0f, "dB");
        addParameter (spec, "invertedTrim", "gain2.gainDb", -18.0f, 18.0f, 0.0f, "dB");
        addParameter (spec, "scaledTrim", "level.gainDb", -18.0f, 18.0f, 0.0f, "dB");
        addParameter (spec, "tone", "tone.tiltDb", -6.0f, 6.0f, 0.0f, "dB");

        // 0.25 .. 0.75 of the trim normalised space is -9 .. +9 dB.
        addMacro (spec, "sub", 0.0f, { mapping ("trim", 0.25f, 0.75f) });
        addMacro (spec, "curved", 0.0f, { mapping ("tone", 0.0f, 1.0f, false, 2.0f) });
        addMacro (spec, "scaled", 0.0f, { mapping ("scaledTrim", 0.0f, 1.0f, false, 1.0f, 0.25f) });
        addMacro (spec, "inverted", 0.0f, { mapping ("invertedTrim", 0.0f, 1.0f, true) });

        auto engine = GraphEngine::create (spec, builtinRegistry());

        if (! expectSuccess (*this, engine, "graph"))
            return;

        auto& built = *engine.value();
        built.prepare (48000.0, 64);

        expectEquals (built.parameters().indexOfBinding ("trim"), 0);
        expectEquals (built.parameters().indexOfBinding ("invertedTrim"), 1);
        expectEquals (built.parameters().indexOfBinding ("scaledTrim"), 2);
        expectEquals (built.parameters().indexOfBinding ("tone"), 3);

        // Sub-range: macro 0 -> 0.25 of the trim normalised space -> -9 dB.
        built.parameters().setMacroValue (0, 0.0f);
        built.applyParameters();
        expectWithinAbsoluteError (built.parameters().getEffectiveValue (0), -9.0f, 1.0e-3f);
        expectWithinAbsoluteError (built.modules()[0].module->getParameter (0), -9.0f, 1.0e-3f);

        built.parameters().setMacroValue (0, 1.0f);
        built.applyParameters();
        expectWithinAbsoluteError (built.parameters().getEffectiveValue (0), 9.0f, 1.0e-3f);

        // Curve 2.0: macro 0.5 -> normalised 0.25 -> -6 .. 6 -> -3 dB.
        built.parameters().setMacroValue (1, 0.5f);
        built.applyParameters();
        expectWithinAbsoluteError (built.parameters().getEffectiveValue (3), -3.0f, 1.0e-3f);

        built.parameters().setMacroValue (1, 1.0f);
        built.applyParameters();
        expectWithinAbsoluteError (built.parameters().getEffectiveValue (3), 6.0f, 1.0e-3f);

        // Scaling by 0.25: macro 1.0 -> normalised 0.25 -> -18 .. 18 -> -9 dB.
        built.parameters().setMacroValue (2, 0.0f);
        built.applyParameters();
        expectWithinAbsoluteError (built.parameters().getEffectiveValue (2), -18.0f, 1.0e-3f,
                                   "scale 0.25 pulls the top of the range down");

        built.parameters().setMacroValue (2, 1.0f);
        built.applyParameters();
        expectWithinAbsoluteError (built.parameters().getEffectiveValue (2), -9.0f, 1.0e-3f);

        // Inversion.
        built.parameters().setMacroValue (3, 0.0f);
        built.applyParameters();
        expectWithinAbsoluteError (built.parameters().getEffectiveValue (1), 18.0f, 1.0e-3f);

        built.parameters().setMacroValue (3, 1.0f);
        built.applyParameters();
        expectWithinAbsoluteError (built.parameters().getEffectiveValue (1), -18.0f, 1.0e-3f);
    }

    void testClampingAndExtremes()
    {
        beginTest ("Out-of-range macro inputs and extreme values are clamped, never extrapolated");

        auto spec = makeSpecWithMacros();
        auto engine = GraphEngine::create (spec, builtinRegistry());

        if (! expectSuccess (*this, engine, "graph"))
            return;

        auto& built = *engine.value();

        built.parameters().setMacroValue (2, 5.0f);
        built.applyParameters();
        expectWithinAbsoluteError (built.parameters().getMacroValue (2), 1.0f, 1.0e-6f,
                                   "macro values above 1 are clamped");
        expectWithinAbsoluteError (built.parameters().getEffectiveValue (1), 6.0f, 1.0e-3f);

        built.parameters().setMacroValue (2, -3.0f);
        built.applyParameters();
        expectWithinAbsoluteError (built.parameters().getMacroValue (2), 0.0f, 1.0e-6f);
        expectWithinAbsoluteError (built.parameters().getEffectiveValue (1), -6.0f, 1.0e-3f);

        built.parameters().setMacroValue (2, std::numeric_limits<float>::quiet_NaN());
        built.applyParameters();
        expectWithinAbsoluteError (built.parameters().getMacroValue (2), 0.0f, 1.0e-6f,
                                   "NaN macro values clamp to zero");
        expect (std::isfinite (built.parameters().getEffectiveValue (1)), "effective value must stay finite");

        // Direct parameter writes outside the range clamp to the ends.
        built.parameters().setBaseValue (1, 500.0f);
        expectWithinAbsoluteError (built.parameters().bindings()[1].baseNormalised, 1.0f, 1.0e-6f);
        built.parameters().setBaseValue (1, -500.0f);
        expectWithinAbsoluteError (built.parameters().bindings()[1].baseNormalised, 0.0f, 1.0e-6f);

        // resetToDefaults restores the declared defaults.
        built.parameters().resetToDefaults();
        expectWithinAbsoluteError (built.parameters().getMacroValue (2), 0.0f, 1.0e-6f);

        auto defaults = makeSpecWithMacros();
        auto fresh = GraphEngine::create (defaults, builtinRegistry());
        expectSuccess (*this, fresh, "fresh graph");

        if (fresh.ok())
        {
            for (int i = 0; i < fresh.value()->parameters().numMacros(); ++i)
            {
                expectWithinAbsoluteError (fresh.value()->parameters().getMacroValue (i),
                                           defaults.macros[static_cast<size_t> (i)].defaultValue, 1.0e-6f,
                                           "macros start at their declared default");
            }
        }
    }

    void testFactoryControls()
    {
        beginTest ("$factory.dryWet and $factory.bypass drive the engine");

        auto spec = makeBaseSpec();
        addModule (spec, "gain", "zarlino.gain");
        addParameter (spec, "mix", "$factory.dryWet", 0.0f, 1.0f, 1.0f);
        addParameter (spec, "bypassed", "$factory.bypass", 0.0f, 1.0f, 0.0f);
        spec.dryWet.enabled = true;

        auto engine = GraphEngine::create (spec, builtinRegistry());

        if (! expectSuccess (*this, engine, "graph"))
            return;

        auto& built = *engine.value();
        built.prepare (48000.0, 128);

        expectEquals (built.parameters().numBindings(), 2);
        expect (built.parameters().bindings()[0].exposeToHost, "factory controls are exposable");

        expectWithinAbsoluteError (built.getDryWetNormalised(), 1.0f, 1.0e-6f, "declared default mix");
        expect (! built.isBypassed());

        built.parameters().setBaseNormalised (0, 0.25f);
        built.applyParameters();
        expectWithinAbsoluteError (built.getDryWetNormalised(), 0.25f, 1.0e-6f);

        built.setDryWetNormalised (0.75f);
        built.applyParameters();
        expectWithinAbsoluteError (built.parameters().getEffectiveValue (0), 0.75f, 1.0e-6f,
                                   "setDryWetNormalised routes through the spec parameter");
        expectWithinAbsoluteError (built.getDryWetNormalised(), 0.75f, 1.0e-6f);

        // Bypass: the parameter moves the switch at and above the midpoint.
        built.parameters().setBaseNormalised (1, 0.49f);
        built.applyParameters();
        expect (! built.isBypassed(), "below the midpoint is not bypassed");

        built.parameters().setBaseNormalised (1, 0.51f);
        built.applyParameters();
        expect (built.isBypassed(), "at or above the midpoint bypasses");

        built.parameters().setBaseNormalised (1, 0.0f);
        built.applyParameters();
        expect (! built.isBypassed());

        // The explicit switch and the parameter are independent.
        built.setBypassed (true);
        expect (built.isBypassed());
        built.setBypassed (false);
        expect (! built.isBypassed());
    }

    void testInternalParameters()
    {
        beginTest ("Internal parameters are macro-only and excluded from the host surface");

        auto spec = makeBaseSpec();
        addModule (spec, "gain", "zarlino.gain");
        addParameter (spec, "trim", "gain.gainDb", -18.0f, 18.0f, 0.0f, "dB", true);
        addParameter (spec, "internalTrim", "gain.gainDb", -6.0f, 6.0f, 0.0f, "dB", false);

        // Two parameters on one module parameter would be ambiguous, so this must
        // be rejected - proving that exposeToHost does not create a loophole.
        auto ambiguous = GraphEngine::create (spec, builtinRegistry());
        expectFailure (*this, ambiguous, ErrorCode::DuplicateIdentifier,
                       "internal parameters are still subject to the one-target rule");

        auto distinct = makeBaseSpec();
        addModule (distinct, "gain", "zarlino.gain");
        addModule (distinct, "level", "zarlino.gain");
        addParameter (distinct, "trim", "gain.gainDb", -18.0f, 18.0f, 0.0f, "dB", true);
        addParameter (distinct, "internalTrim", "level.gainDb", -6.0f, 6.0f, 0.0f, "dB", false);

        expectEquals (distinct.exposedParameterCount(), 1);

        addMacro (distinct, "trim", 0.0f, { mapping ("internalTrim", 0.0f, 1.0f) });

        auto engine = GraphEngine::create (distinct, builtinRegistry());

        if (! expectSuccess (*this, engine, "graph with an internal parameter"))
            return;

        auto& built = *engine.value();
        built.prepare (48000.0, 64);

        expectEquals (built.parameters().numBindings(), 2);
        expect (built.parameters().bindings()[1].exposeToHost == false);

        built.parameters().setMacroValue (0, 1.0f);
        built.applyParameters();
        expectWithinAbsoluteError (built.modules()[1].module->getParameter (0), 6.0f, 1.0e-3f,
                                   "a macro can drive an internal parameter");
    }
};

ParameterMacroTests parameterMacroTests;

} // namespace

void registerParameterMacroTests() {}
