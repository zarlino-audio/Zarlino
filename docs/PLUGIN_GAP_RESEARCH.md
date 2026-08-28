# Zarlino Plugin Gap Research — Feature & Performance Analysis

**Date:** 2026-08-28
**Method:** Reverse-engineered specs (`specs/*.md`) + competitor research (Soothe2/Soothe3, Gullfoss, FabFilter Saturn 2, iZotope Ozone Imager, Polyverse Wider, Waves S1/Vocal Rider, SSL Vocalstrip 2, Nectar 4, PluginGuru Unify, Acon Digital, Accentize).
**Scope:** ZScorch · ZTame · ZVocals · PresenceField · RACK_AI (+ shared platform).
**Result:** **24 feature/performance gaps** and **36 populated bug/feature requests** (also created as tasks in the Executive OS).

---

## Part A — 24 Feature / Performance Gaps

### A1. ZScorch — Multi-Band Adaptive Saturation/Exciter (vs FabFilter Saturn 2, Soundtoys Decapitator, iZotope Trash)

| # | Gap | Evidence / Competitor | Type |
|---|-----|----------------------|------|
| 1 | **Crossover slope not selectable** — fixed LR4 (24 dB/oct); Saturn 2 offers 6/12/24/48 dB/oct + linear-phase band processing for mastering. | FabFilter Saturn 2 | Feature |
| 2 | **No user-facing modulation system** — the adaptive Control Layer is internal; Saturn 2 ships a 50-slot mod matrix (16-step XLFO, envelope generators/followers, XY, MIDI). | Saturn 2 | Feature |
| 3 | **No per-band solo/mute** in the UI (Saturn 2 has per-band solo+mute on the interactive band display). | Saturn 2 | UX |
| 4 | **Oversampling capped at 4×** — Saturn 2 tiers are Good (8×) and Superb (32×); ZScorch's Wavefold auto-4x is a minimum, not a ceiling. | Saturn 2 | Performance/Quality |
| 5 | **No A/B compare, undo/redo, or MIDI learn** (industry standard; both Saturn 2 and Soothe2 have A/B + undo/redo). | Saturn 2 / Soothe2 | Workflow |
| 6 | **No Mid/Side mode** (explicitly out of scope; Saturn 2 processes M/S). | Saturn 2 | Feature |

### A2. ZTame — FFT Resonance Suppressor (vs oeksound Soothe2/Soothe3, Gullfoss)

| # | Gap | Evidence / Competitor | Type |
|---|-----|----------------------|------|
| 7 | **No Delta output** — "audition what was removed" (Soothe2 Delta with right-click lock) is the category-defining QC tool. | Soothe2 | Feature |
| 8 | **No per-band nodes / per-band listen+solo** (Soothe2 has 5 editable band nodes with per-band listen/on-off; ZTame is a single spectrum). | Soothe2 | Feature/UX |
| 9 | **No external sidechain input** (Soothe2 supports key input with per-DAW delay-comp guidance). | Soothe2 | Feature |
| 10 | **No resolution/quality tiers** (Soothe2: eco/high/ultra — eco ≈ half CPU — plus a separate offline-render quality). | Soothe2 | Performance |
| 11 | **No 0-sample low-latency tracking mode** (Soothe3 low-latency = 0 samples @ base SR, ~1 ms @ high SR; Gullfoss Live <2 ms). | Soothe3 / Gullfoss Live | Performance/Latency |
| 12 | **Frequency range / low-end reach** — Soothe reaches down to 20 Hz; verify ZTame's FFT (2048 @ 44.1 = ~21.5 Hz/bin) has adequate low-bin resolution & CPU profile at 4× OS. | Soothe2 / Smooth Operator (44.1 kHz analysis, "reasonable CPU") | Performance |

### A3. ZVocals — Vocal Channel Strip (vs SSL Vocalstrip 2, Waves Vocal Rider, Nectar 4)

| # | Gap | Evidence / Competitor | Type |
|---|-----|----------------------|------|
| 13 | **Module chain not re-orderable** (SSL Vocalstrip 2 lets you drag modules to reorder — a headline differentiator). | SSL Vocalstrip 2 | Feature/Workflow |
| 14 | **No de-plosser** (SSL has de-esser + de-plosser; ZVocals has de-esser only). | SSL Vocalstrip 2 | Feature |
| 15 | **No real-time spectrum overlay / GR history visualization** (SSL shows EQ curve + real-time FFT + de-esser GR history). | SSL Vocalstrip 2 | UX |
| 16 | **No automatic gain riding / auto-level** (Waves Vocal Rider ±12 dB, zero latency; Nectar ALM as transparent alternative). | Waves Vocal Rider / Nectar 4 | Feature |
| 17 | **No per-stage bypass/mix or expert parameter access** (explicitly out of scope; competitors expose stage-level control). | SSL / category | Feature |
| 18 | **No "vocal assistant"/AI starting-point** (Nectar 4 Vocal Assistant; Smooth Operator spectral strip shows the adjacent bar). | Nectar 4 | Feature |

### A4. PresenceField — Stereo Widener (vs Ozone Imager, Polyverse Wider, Waves S1, MicroShift)

| # | Gap | Evidence / Competitor | Type |
|---|-----|----------------------|------|
| 19 | **No one-click mono check in the UI** (Wider v2 added a mono-check button; PresenceField's Mono Listen is a hidden toggle). | Polyverse Wider v2 | UX |
| 20 | **No low-frequency bypass / bass-shuffler control** (Wider Low Bypass; Waves S1 Shuffler widens bass while retaining mono compatibility). | Wider / S1 Shuffler | Feature |
| 21 | **No vectorscope / correlation meter** (Ozone Imager ships three vectorscope meters + correlation). | iZotope Ozone Imager | Metering/UX |
| 22 | **No delay-based mono-safe stereoize for mono→stereo** (Ozone Stereoize II: phase-inverted delayed copies cancel in mono; Wider's all-pass comb is fully mono-safe). | Ozone Imager 2 / Wider | Feature |
| 23 | **Width modulation is static** (MicroShift's time-varying delay + saturation flavor; PresenceField's LFO is only on the Air peak frequency). | Soundtoys MicroShift | Feature |
| 24 | **No A/B / width automation visual** — verify the correlation safety factor reacts cleanly under automation at width >150%. | category | Performance/QC |

> The 24th gap is a **performance/QC** item; the correlation-safety engine is unique to PresenceField, but it must be benchmarked (CPU at 200% width, mono-sum artifact test).

---

## Part B — 36 Populated Bug / Feature Requests

Created as tasks in the Executive OS (`auto_run` = ON so the factory works them; founder-decision items stay manual).

### ZScorch (6)
1. `[FEATURE]` ZScorch: selectable crossover slopes (6/12/24/48 dB/oct) + linear-phase band option.
2. `[FEATURE]` ZScorch: modulation matrix — LFO / envelope → drive, tilt, exciter, band gain, crossover.
3. `[FEATURE]` ZScorch: per-band solo + mute, A/B compare, undo/redo in the band display.
4. `[PERF]` ZScorch: add Good (8×) / Superb (32×) oversampling tiers for mastering.
5. `[FEATURE]` ZScorch: Mid/Side processing mode.
6. `[BUG]` ZScorch: verify Wavefold aliasing at 2× OS and surface an in-UI oversampling warning when a mode needs 4×.

### ZTame (6)
7. `[FEATURE]` ZTame: Delta/listen output (audition only the suppressed material; right-click lock).
8. `[FEATURE]` ZTame: external sidechain input with per-DAW delay-compensation guidance.
9. `[PERF]` ZTame: resolution tiers (eco/high/ultra) + offline-render quality override.
10. `[PERF]` ZTame: 0-sample low-latency tracking mode (Soothe3 bar) for live/tracking use.
11. `[BUG]` ZTame P8: pumping/warbling on transients → detection-path smoothing / peak-hold.
12. `[BUG]` ZTame: Selectivity knob inaudible (threshold moves only ±5.25 dB vs Sensitivity 12 dB) → widen range / remap.

### ZVocals (6)
13. `[FEATURE]` ZVocals: re-orderable module chain (drag SSL-style).
14. `[FEATURE]` ZVocals: de-plosser stage alongside the de-esser.
15. `[UX]` ZVocals: real-time spectrum/FFT overlay + GR history per stage.
16. `[FEATURE]` ZVocals: automatic gain riding / auto-level mode.
17. `[FEATURE]` ZVocals: per-stage bypass/mix + advanced parameter access.
18. `[BUG]` ZVocals: verify de-esser stays content-adaptive at extreme BODY/PUNCH macro positions (no forced sibilance cut).

### PresenceField (6)
19. `[UX]` PresenceField: one-click mono-check button.
20. `[FEATURE]` PresenceField: low-frequency bypass / bass-shuffler control.
21. `[FEATURE]` PresenceField: vectorscope + correlation meter.
22. `[FEATURE]` PresenceField: delay-based mono-safe stereoize mode for mono→stereo.
23. `[BUG]` PresenceField: mono-compatibility safety at width >150% (correlation safety factor benchmark + mono-sum artifact test).
24. `[BUG]` PresenceField: Air LFO modulation click / zipper at high width — verify cubic-interpolated fractional-delay reads.

### RACK_AI (6)
25. `[FEATURE]` RACK_AI: per-layer output routing / multi-output (stems).
26. `[PERF]` RACK_AI: publish real-time latency budget + disclosure for AI processing.
27. `[FEATURE]` RACK_AI: macro knobs with built-in LFOs (Unify-style).
28. `[FEATURE]` Platform: NKS / Quick-Control integration (macros, tags, previews) across the catalog.
29. `[FEATURE]` RACK_AI: plugin-in-plugin hosting (host any VST3 effect/instrument).
30. `[BUG]` RACK_AI: VST3 state/preset round-trip across DAWs (IStateHandler) — no cross-DAW preset corruption.

### Platform / Shared (6)
31. `[FEATURE]` Platform: AAX Native + AudioSuite support across the catalog.
32. `[FEATURE]` Platform: CLAP format support (emerging standard; Saturn 2 ships it).
33. `[FEATURE]` Platform: NKS preset ecosystem + `.preset`-style shareable tagged presets.
34. `[PERF]` Platform: per-plugin CPU benchmark matrix (eco vs high settings) + FFT-path optimization pass.
35. `[FEATURE]` Platform: undo/redo + A/B compare + per-band solo in all flagship UIs.
36. `[BUG]` QA: golden-reference audio files missing for Gate 5 — capture null-test goldens for ZScorch & ZTame.

---

## References (selected)
- oeksound Soothe2 / Soothe3 product pages & manuals
- Soundtheory Gullfoss (standard / Live / Master latency split)
- FabFilter Saturn 2 product page
- iZotope Ozone Imager / Ozone help docs
- Polyverse Wider v2 (mono check, Low Bypass)
- Waves S1 Stereo Imager / Vocal Rider
- SSL Native Vocalstrip 2
- iZotope Nectar 4
- PluginGuru Unify 2
- Acon Digital Remix / Accentize (latency & local-ML facts)
