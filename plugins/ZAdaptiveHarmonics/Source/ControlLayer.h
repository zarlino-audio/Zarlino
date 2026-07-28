#pragma once

#include "AnalysisCore.h"
#include <JuceHeader.h>

//==============================================================================
/**
    Per-module target values produced by the ControlLayer.
    These drive each DSP module's processing parameters.
*/
struct ModuleTargets
{
    float satEven          = 0.0f;  // 2nd harmonic emphasis target
    float satOdd           = 0.0f;  // 3rd harmonic emphasis target
    float satHarmReduction = 0.0f;  // higher harmonic reduction amount
    float tiltAmount       = 0.0f;  // spectral tilt amount
    float exciterAmount    = 0.0f;  // exciter drive level
    float exciterEnabled   = 0.0f;  // exciter gate
};

//==============================================================================
/**
    Control Layer — maps the ControlVector and macro parameters (Lift, Character)
    into smoothed per-module targets for each DSP module.

    Smoothing uses juce::SmoothedValue with ~50ms time constant for click-free
    parameter modulation.
*/
class ControlLayer
{
public:
    ControlLayer() = default;

    /** Initialize smoothing time constants. */
    void prepare (double sampleRate);

    /** Reset all smoothed values to zero. */
    void reset();

    /**
        Update module targets from the control vector and macro parameters.
        Should be called at control rate (every ~256-512 samples) alongside
        the AnalysisCore, or always in processBlock (smoothed internally).
    */
    void update (const ControlVector& cv,
                 float liftParam,
                 float characterParam,
                 float satEvenParam,
                 float satOddParam,
                 float satHarmReductionParam,
                 float tiltParam,
                 float exciterAmountParam,
                 float exciterEnabledParam,
                 double sampleRate);

    /** Get the current smoothed targets. Safe to call from audio thread. */
    ModuleTargets getCurrentTargets() const noexcept;

    /** Advance all smoothed values by one step and return current targets. */
    ModuleTargets getNextTarget();

    /** Advance all smoothed values by `numSamples` steps without computing targets. */
    void skipSamples (int numSamples);

private:
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> satEvenTarget;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> satOddTarget;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> satHarmReductionTarget;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> tiltTarget;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> exciterAmountTarget;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> exciterEnabledTarget;
};
