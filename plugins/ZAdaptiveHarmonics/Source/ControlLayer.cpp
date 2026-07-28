#include "ControlLayer.h"

//==============================================================================
void ControlLayer::prepare (double sampleRate)
{
    constexpr float smoothingTimeMs = 50.0f;
    const auto rampLength = sampleRate * (smoothingTimeMs / 1000.0);

    satEvenTarget.reset           (sampleRate, rampLength);
    satOddTarget.reset            (sampleRate, rampLength);
    satHarmReductionTarget.reset  (sampleRate, rampLength);
    tiltTarget.reset              (sampleRate, rampLength);
    exciterAmountTarget.reset     (sampleRate, rampLength);
    exciterEnabledTarget.reset    (sampleRate, rampLength);

    reset();
}

void ControlLayer::reset()
{
    satEvenTarget.setCurrentAndTargetValue (0.0f);
    satOddTarget.setCurrentAndTargetValue (0.0f);
    satHarmReductionTarget.setCurrentAndTargetValue (0.0f);
    tiltTarget.setCurrentAndTargetValue (0.0f);
    exciterAmountTarget.setCurrentAndTargetValue (0.0f);
    exciterEnabledTarget.setCurrentAndTargetValue (0.0f);
}

//==============================================================================
void ControlLayer::update (const ControlVector& cv,
                           float liftParam,
                           float characterParam,
                           float satEvenParam,
                           float satOddParam,
                           float satHarmReductionParam,
                           float tiltParam,
                           float exciterAmountParam,
                           float exciterEnabledParam,
                           double /*sampleRate*/)
{
    // Clamp inputs
    liftParam      = juce::jlimit (0.0f, 1.0f, liftParam);
    characterParam = juce::jlimit (-1.0f, 1.0f, characterParam);

    // --- Character mapping for Saturator ---
    // c = -1 (warm): emphasize 2nd (+8dB full, a2 ≈ 1.5), de-emphasize 3rd (+2dB, a3 ≈ 0.26)
    // c = +1 (bright): reverse — 2nd +2dB, 3rd +5dB
    // Use linear interpolation between the two extremes over the bipolar Character range.
    const float c = characterParam;

    // Map dB gains to polynomial coefficients (approximate mapping)
    // a2: 0 dB → 0.0, +8 dB → ~1.5  (dB ≈ 20*log10(1 + a2) for small signals)
    // a3: 0 dB → 0.0, +5 dB → ~0.78
    constexpr float a2Warm = 1.5f;   // +8 dB emphasis
    constexpr float a2Bright = 0.26f;  // +2 dB
    constexpr float a3Warm = 0.26f;    // +2 dB
    constexpr float a3Bright = 0.78f;  // +5 dB

    // Blended character-adjusted coefficients
    const float charBlendA2 = a2Warm * (1.0f - c) * 0.5f + a2Bright * (1.0f + c) * 0.5f;
    const float charBlendA3 = a3Warm * (1.0f - c) * 0.5f + a3Bright * (1.0f + c) * 0.5f;

    // --- Adaptive modulation from analysis ---
    // harmonicDensity: 1.0 = rich harmonic content → reduce saturation (avoid over-saturation)
    // spectralFlatness: 1.0 = noise-like → reduce harmonics (not useful on noise)
    const float adaptiveGain = 1.0f - cv.harmonicDensity * 0.5f - cv.spectralFlatness * 0.3f;
    const float adaptedSatScale = juce::jlimit (0.3f, 1.0f, adaptiveGain);

    // --- Lift mapping ---
    // Lift 0.0 = minimal processing, Lift 1.0 = full designed effect
    const float lift = liftParam;

    // Saturator targets: lift * user param * character blend * adaptive scale
    satEvenTarget.setTargetValue (lift * satEvenParam * charBlendA2 * adaptedSatScale);
    satOddTarget.setTargetValue  (lift * satOddParam  * charBlendA3 * adaptedSatScale);
    satHarmReductionTarget.setTargetValue (satHarmReductionParam);

    // --- Tilt: base tilt scales with character, crest factor modulates depth ---
    // c = -1 → bass-heavy (-maxTilt), c = +1 → treble-heavy (+maxTilt)
    // Peakier signals (high crestFactor) get more bass boost when warm
    constexpr float maxTiltDb = 6.0f;
    const float crestMod = 1.0f + cv.crestFactor * 0.5f; // 1.0x–1.5x tilt depth
    const float tiltDb = c * maxTiltDb * crestMod;
    tiltTarget.setTargetValue (lift * tiltParam * tiltDb);

    // --- Exciter: gated by character and brightness ---
    // Warm (c <= 0): exciter is near-zero. Bright (c > 0): exciter scales with c.
    // Less exciter on already-bright material, more on dark material
    const float exciterGate = juce::jmax (0.0f, c) * (1.0f - cv.brightness * 0.5f);
    exciterAmountTarget.setTargetValue (lift * exciterAmountParam * exciterGate);
    exciterEnabledTarget.setTargetValue (exciterEnabledParam);
}

//==============================================================================
ModuleTargets ControlLayer::getCurrentTargets() const noexcept
{
    ModuleTargets t;
    t.satEven          = satEvenTarget.getCurrentValue();
    t.satOdd           = satOddTarget.getCurrentValue();
    t.satHarmReduction = satHarmReductionTarget.getCurrentValue();
    t.tiltAmount       = tiltTarget.getCurrentValue();
    t.exciterAmount    = exciterAmountTarget.getCurrentValue();
    t.exciterEnabled   = exciterEnabledTarget.getCurrentValue();
    return t;
}

ModuleTargets ControlLayer::getNextTarget()
{
    ModuleTargets t;
    t.satEven          = satEvenTarget.getNextValue();
    t.satOdd           = satOddTarget.getNextValue();
    t.satHarmReduction = satHarmReductionTarget.getNextValue();
    t.tiltAmount       = tiltTarget.getNextValue();
    t.exciterAmount    = exciterAmountTarget.getNextValue();
    t.exciterEnabled   = exciterEnabledTarget.getNextValue();
    return t;
}

void ControlLayer::skipSamples (int numSamples)
{
    satEvenTarget.skip (numSamples);
    satOddTarget.skip (numSamples);
    satHarmReductionTarget.skip (numSamples);
    tiltTarget.skip (numSamples);
    exciterAmountTarget.skip (numSamples);
    exciterEnabledTarget.skip (numSamples);
}
