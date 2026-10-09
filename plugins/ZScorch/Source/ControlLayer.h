#pragma once

#include "AnalysisCore.h"
#include "Saturator.h"
#include <JuceHeader.h>
#include "ZSmoother.h"

//==============================================================================
/**
    Per-module target values produced by the ControlLayer.
    These drive each DSP module's processing parameters.
*/
struct ModuleTargets
{
    // Saturator
    float satDrive         = 0.0f;  // Pre-gain drive amount (0.0–2.0)
    float satHarmReduction = 0.0f;  // Higher harmonic reduction (0.0–1.0)
    SaturationMode satMode = SaturationMode::Tube; // Saturation mode

    // Tilt
    float tiltAmount       = 0.0f;  // Spectral tilt amount (dB)

    // Exciter
    float exciterAmount    = 0.0f;  // Exciter drive level
    float exciterEnabled   = 0.0f;  // Exciter gate

    // Band gain (for multi-band processing, ±6 dB)
    float bandGainDb       = 0.0f;
};

//==============================================================================
/**
    Control Layer — maps the ControlVector and macro parameters (Lift, Character)
    into smoothed per-module targets for each DSP module.

    Smoothing uses ZLinearSmoother with ~50ms time constant for click-free
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
        the AnalysisCore.

        @param cv               AnalysisCore control vector (may be band-specific).
        @param liftParam        Lift macro (0.0–1.0).
        @param characterParam   Character macro (-1.0–1.0).
        @param satDriveParam    Saturator drive user param (0.0–1.0).
        @param satModeParam     Saturation mode (0.0–1.0 mapped to enum).
        @param satHarmRedParam  Harmonic reduction amount (0.0–1.0).
        @param tiltParam        Tilt amount user param (0.0–1.0).
        @param exciterAmtParam  Exciter amount user param (0.0–1.0).
        @param exciterEnParam   Exciter enabled user param (0.0–1.0).
        @param bandGainDbParam  Band gain in dB (-6.0–+6.0).
        @param sampleRate       Current sample rate.
    */
    void update (const ControlVector& cv,
                 float liftParam,
                 float characterParam,
                 float satDriveParam,
                 float satModeParam,
                 float satHarmRedParam,
                 float tiltParam,
                 float exciterAmtParam,
                 float exciterEnParam,
                 float bandGainDbParam,
                 double sampleRate);

    /** Get the current smoothed targets. Safe to call from audio thread. */
    ModuleTargets getCurrentTargets() const noexcept;

    /** Advance all smoothed values by one step and return current targets. */
    ModuleTargets getNextTarget();

    /** Advance all smoothed values by `numSamples` steps without computing targets. */
    void skipSamples (int numSamples);

private:
    ZLinearSmoother satDriveTarget;
    ZLinearSmoother satHarmReductionTarget;
    ZLinearSmoother tiltTarget;
    ZLinearSmoother exciterAmountTarget;
    ZLinearSmoother exciterEnabledTarget;

    // Non-smoothed mode (switch immediately)
    SaturationMode currentSatMode = SaturationMode::Tube;

    // Band gain from ControlLayer update (passed through to ModuleTargets)
    float bandGainDb = 0.0f;
};
