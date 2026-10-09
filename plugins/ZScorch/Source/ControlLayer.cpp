#include "ControlLayer.h"

//==============================================================================
void ControlLayer::prepare (double sampleRate)
{
    constexpr float smoothingTimeMs = 50.0f;
    const auto rampLengthSeconds = smoothingTimeMs / 1000.0;

    satDriveTarget.reset           (sampleRate, rampLengthSeconds);
    satHarmReductionTarget.reset   (sampleRate, rampLengthSeconds);
    tiltTarget.reset               (sampleRate, rampLengthSeconds);
    exciterAmountTarget.reset      (sampleRate, rampLengthSeconds);
    exciterEnabledTarget.reset     (sampleRate, rampLengthSeconds);

    reset();
}

void ControlLayer::reset()
{
    satDriveTarget.setCurrentAndTargetValue (0.0f);
    satHarmReductionTarget.setCurrentAndTargetValue (0.0f);
    tiltTarget.setCurrentAndTargetValue (0.0f);
    exciterAmountTarget.setCurrentAndTargetValue (0.0f);
    exciterEnabledTarget.setCurrentAndTargetValue (0.0f);
    currentSatMode = SaturationMode::Tube;
}

//==============================================================================
void ControlLayer::update (const ControlVector& cv,
                           float liftParam,
                           float characterParam,
                           float satDriveParam,
                           float satModeParam,
                           float satHarmRedParam,
                           float tiltParam,
                           float exciterAmtParam,
                           float exciterEnParam,
                           float bandGainDbParam,
                           double /*sampleRate*/)
{
    // Clamp inputs
    liftParam      = juce::jlimit (0.0f, 1.0f, liftParam);
    characterParam = juce::jlimit (-1.0f, 1.0f, characterParam);
    satModeParam   = juce::jlimit (0.0f, 1.0f, satModeParam);

    // --- Map satModeParam to SaturationMode enum ---
    // 0.0–0.166: Tube, 0.166–0.333: Tape, 0.333–0.5: Transistor,
    // 0.5–0.666: Germanium, 0.666–0.833: Diode, 0.833–1.0: Wavefold
    if (satModeParam < 0.166f)           currentSatMode = SaturationMode::Tube;
    else if (satModeParam < 0.333f)      currentSatMode = SaturationMode::Tape;
    else if (satModeParam < 0.5f)        currentSatMode = SaturationMode::Transistor;
    else if (satModeParam < 0.666f)      currentSatMode = SaturationMode::Germanium;
    else if (satModeParam < 0.833f)      currentSatMode = SaturationMode::Diode;
    else                                 currentSatMode = SaturationMode::Wavefold;

    const float c = characterParam;

    // --- Adaptive modulation from analysis ---
    // harmonicDensity: 1.0 = rich harmonic content → reduce drive
    // spectralFlatness: 1.0 = noise-like → reduce drive
    const float adaptiveGain = 1.0f - cv.harmonicDensity * 0.5f - cv.spectralFlatness * 0.3f;
    const float adaptedSatScale = juce::jlimit (0.3f, 1.0f, adaptiveGain);

    // --- Lift mapping ---
    const float lift = liftParam;

    // Saturator drive: lift * user drive * adaptive scale
    satDriveTarget.setTargetValue (lift * satDriveParam * adaptedSatScale);
    satHarmReductionTarget.setCurrentAndTargetValue (satHarmRedParam);

    // --- Tilt: character-driven spectral tilt ---
    constexpr float maxTiltDb = 6.0f;
    const float crestMod = 1.0f + cv.crestFactor * 0.5f;
    const float tiltDb = c * maxTiltDb * crestMod;
    tiltTarget.setTargetValue (lift * tiltParam * tiltDb);

    // --- Exciter: gated by character and brightness ---
    const float exciterGate = juce::jmax (0.0f, c) * (1.0f - cv.brightness * 0.5f);
    exciterAmountTarget.setTargetValue (lift * exciterAmtParam * exciterGate);
    exciterEnabledTarget.setCurrentAndTargetValue (exciterEnParam);

    // --- Band gain (passed through directly) ---
    bandGainDb = bandGainDbParam;
}

//==============================================================================
ModuleTargets ControlLayer::getCurrentTargets() const noexcept
{
    ModuleTargets t;
    t.satDrive         = satDriveTarget.getCurrentValue();
    t.satHarmReduction = satHarmReductionTarget.getCurrentValue();
    t.satMode          = currentSatMode;
    t.tiltAmount       = tiltTarget.getCurrentValue();
    t.exciterAmount    = exciterAmountTarget.getCurrentValue();
    t.exciterEnabled   = exciterEnabledTarget.getCurrentValue();
    t.bandGainDb       = bandGainDb;
    return t;
}

ModuleTargets ControlLayer::getNextTarget()
{
    ModuleTargets t;
    t.satDrive         = satDriveTarget.getNextValue();
    t.satHarmReduction = satHarmReductionTarget.getNextValue();
    t.satMode          = currentSatMode;
    t.tiltAmount       = tiltTarget.getNextValue();
    t.exciterAmount    = exciterAmountTarget.getNextValue();
    t.exciterEnabled   = exciterEnabledTarget.getNextValue();
    t.bandGainDb       = bandGainDb;
    return t;
}

void ControlLayer::skipSamples (int numSamples)
{
    satDriveTarget.skip (numSamples);
    satHarmReductionTarget.skip (numSamples);
    tiltTarget.skip (numSamples);
    exciterAmountTarget.skip (numSamples);
    exciterEnabledTarget.skip (numSamples);
}
