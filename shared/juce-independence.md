# JUCE Independence — Shared Library Audit

**Purpose**: Track which shared modules each plugin contributes to and consumes. This prevents accidental duplication and identifies extraction opportunities.

**Last Updated**: 2025-07-12

---

## Shared Modules Inventory

The `shared/` directory contains modules used across multiple Zarlino plugins. Each module is a self-contained set of `.h`/`.cpp` files.

### Core Infrastructure

| Module | Files | Purpose | Contributors |
|--------|-------|---------|-------------|
| **ZOversampling** | `ZOversampling.h/.cpp` | Configurable multi-rate oversampling (2x, 4x, 8x) with half-band FIR filtering; latency compensation | ZScorch, ZTame |
| **ZLicenseManager** | `ZLicenseManager.h/.cpp` | Commercial license validation (key-based), non-blocking audio thread check, grace-period fallback | ZScorch, ZTame |
| **ZarlinoLookAndFeel** | `ZarlinoLookAndFeel.h/.cpp` | Unified dark-theme Look & Feel: custom rotary sliders, buttons, meters, typography, color palette | ZScorch, ZTame, ZVocal, PresenceField |
| **ZGUIMacro** | `ZGUIMacro.h` | Preprocessor macros for parameter layout definitions, GUI attachment wiring, and preset boilerplate | ZScorch, ZTame |

### DSP Building Blocks

| Module | Files | Purpose | Contributors |
|--------|-------|---------|-------------|
| **ZAnalysisCore** | `ZAnalysisCore.h/.cpp` | FFT-based spectrum analysis (configurable window, overlap, bin-to-frequency mapping); used by ZScorch (multi-band routing) and ZTame (spectral analysis) | ZScorch, ZTame |
| **ZAdaptiveHarmonics** | `ZAdaptiveHarmonics.h/.cpp` | Adaptive harmonic synthesis with dynamic threshold tracking; used by ZScorch exciters | ZScorch |
| **ZPitchAnalyzer** | `ZPitchAnalyzer.h/.cpp` | Real-time monophonic pitch detection (autocorrelation-based, octave-aware); extracted from VocalToneShaper 1.0, targeted for ZTune | VocalToneShaper 1.0 (historical) |

### UI Helpers

| Module | Files | Purpose | Contributors |
|--------|-------|---------|-------------|
| **ZFrequencySpectrum** | `ZFrequencySpectrum.h/.cpp` | Real-time frequency analyzer UI component; used by ZScorch, ZTame | ZScorch, ZTame |
| **ZVectorScope** | `ZVectorScope.h/.cpp` | L/R vector scope visualization; used by PresenceField | PresenceField |

### Third-Party / Bundled

| Module | Files | License | Purpose |
|--------|-------|---------|---------|
| **FoleysFinest** | `foleys_fine_storm/` | MIT | GUI magic GUI builder; used by MyFirstEQ (and VocalToneShaper 1.0, pre-rebrand) |

---

## Plugin Dependency Matrix

| Plugin | ZOversampling | ZLicenseManager | ZarlinoL&F | ZGUIMacro | ZAnalysisCore | ZAdaptiveHarmonics | ZPitchAnalyzer | ZFrequencySpectrum | ZVectorScope | FoleysFinest |
|--------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **ZScorch** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | ✅ | — | — |
| **ZTame** | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | — | — |
| **ZVocal** | — | ✅ | ✅ | — | — | — | — | — | — | — |
| **PresenceField** | — | — | ✅ | ✅ | — | — | — | — | ✅ | — |
| **MyFirstEQ** | — | — | — | — | — | — | — | — | — | ✅ |
| **RACK_AI** | — | — | — | — | — | — | — | — | — | — |

---

## Contributor Tally

### ZScorch
**Contributed to shared/**: ZOversampling (co-developed), ZLicenseManager (co-developed), ZAnalysisCore (co-developed), ZAdaptiveHarmonics, ZFrequencySpectrum
**Extracted from plugin code**: ZAnalysisCore was originally ZScorch's internal analyzer; generalized for ZTame's spectral analysis needs. ZAdaptiveHarmonics is ZScorch-specific but in shared/ for potential reuse by ZForge and ZMojo.

### ZTame
**Contributed to shared/**: ZOversampling (co-developed), ZLicenseManager (co-developed), ZAnalysisCore (co-developed)
**Extracted from plugin code**: ZTame's FFT preprocessing was merged with ZScorch's to create ZAnalysisCore.

### ZVocal
**Contributed to shared/**: ZPitchAnalyzer (extracted during the pre-rebrand VocalToneShaper era; now targeted for ZTune)
**Consumed from shared/**: ZLicenseManager, ZarlinoLookAndFeel (via custom JUCE editor)

### PresenceField
**Contributed to shared/**: ZVectorScope
**Consumed from shared/**: ZarlinoLookAndFeel, ZGUIMacro

### MyFirstEQ
**Contributed to shared/**: None
**Consumed from shared/**: FoleysFinest only

---

## Extraction Candidates

Modules that exist inside a single plugin but SHOULD be extracted to `shared/`:

| Module | Currently In | Reason to Extract | Blockers |
|--------|-------------|-------------------|----------|
| **Resonance detector** | ZTame Source/ | ZCarve (Dynamic EQ) and ZClear (De-Noise) will need similar peak detection | Needs generalization (configurable Q, threshold curve) |
| **Tilt EQ** | ZScorch Source/ | ZLift needs a tilt EQ; ZScorch uses it as a pre/post stage | Already general-purpose in ZScorch; just needs moving |
| **Vocal chain utilities** | ZVocal Source/ | ZTune will need similar preprocessing (HPF, gate, level detect) | Duplicates some ZAnalysisCore functionality; needs consolidation |

---

## Duplication Watchlist

*Check these before starting any new implementation:*

1. **FFT/spectral analysis** → Use `ZAnalysisCore`. Do not write custom FFT wrappers.
2. **Oversampling** → Use `ZOversampling`. Do not inline half-band filters.
3. **License checks** → Use `ZLicenseManager`. Do not write custom key validators.
4. **Rotary knobs** → Use `ZarlinoLookAndFeel::drawRotarySlider`. Do not write custom knob renderers.
5. **Parameter smoothing** → Use JUCE's `SmoothedValue`. Do not write custom smoothing.
6. **Pitch detection** → Use `ZPitchAnalyzer` for monophonic. Do not write custom autocorrelation.

---

## Policy

1. **Before creating a new module**, check this file and the `shared/` directory for existing solutions.
2. **If a module could serve 2+ plugins**, extract it to `shared/` and update this file.
3. **When consuming a shared module**, add your plugin to the dependency matrix.
4. **When contributing a shared module**, document it in the inventory and add yourself to the contributor tally.

---

## Change Log

| Date | Change | Author |
|------|--------|--------|
| 2025-07-12 | Initial inventory from code audit | Orchestrator |
