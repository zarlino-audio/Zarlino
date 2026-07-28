# ZAdaptiveHarmonics

Adaptive harmonic processor by Zarlino Audio.

Continuous real-time spectral analysis drives four DSP modules — Saturator,
Dynamic Spectral Tilt, Exciter, and harmonic management — behind a single
Lift macro control. Steers program material toward warmth, clarity, and energy
without obvious EQ or distortion artifacts.

## Controls

- **Lift** — Master macro. 0 = bypass, 1 = full processing.
- **Character** — Warm (−1) to Bright (+1). Shifts harmonic emphasis and tilt.
- **Mix** — Dry/wet blend.

### Advanced Page
- **Saturator Even/Odd** — 2nd and 3rd harmonic drive levels.
- **Harm Redux** — Reduces 5th+ harmonics post-saturation.
- **Tilt Amount** — Spectral balance shift.
- **Exciter Amount + Enable** — High-frequency harmonic generation (disabled by default).

## Signal Chain

Analysis Core → Saturator (2x OS) → Dynamic Spectral Tilt → Exciter (2x OS, gated)

## Technical

- Formats: VST3, Standalone
- Platform: Windows
- Latency: ~4 samples (oversampling filters)
- True bypass: Lift = 0 passes audio unchanged

## Build

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target ZAdaptiveHarmonics
```

Output: `build/ZAdaptiveHarmonics_artefacts/Release/VST3/ZAdaptiveHarmonics.vst3/`
