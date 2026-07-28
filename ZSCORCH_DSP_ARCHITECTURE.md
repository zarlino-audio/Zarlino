# ZScorch — DSP Architecture Design Document

**Plugin:** ZScorch (Flagship Series #8)
**Version:** 1.0 Draft
**Owner:** Zarlino Audio
**Date:** 2026-07-21

---

## 0. One-Sentence Purpose

> **ZScorch is a multi-mode, multi-band harmonic saturator that gives producers surgical control over exactly which frequencies burn and how they burn, from subtle analog warmth to full-spectrum destruction.**

Every stage in this chain exists to serve that sentence. A stage that doesn't give the user control over *where* or *how* harmonics are generated is scope creep.

---

## 1. Signal Chain Topology

### Fixed order (not reorderable)

```
[Input Gain] → [Pre-Emphasis EQ] → [Crossover Split] → [SATURATION ENGINE × 4 bands]
    → [Crossover Recombine] → [Post-Emphasis EQ] → [Output Gain + Ceiling Limiter] → [Dry/Wet Mix]
```

### Stage-by-stage rationale

| # | Stage | Purpose | NOT responsible for |
|---|---|---|---|
| 1 | **Input Gain** | ±24 dB trim to feed the saturator at the right level. Unity at 0 dB. | Gain staging downstream stages — each band has its own drive. |
| 2 | **Pre-Emphasis EQ** | Sculpt what frequencies hit the saturator harder. A low-shelf boost here makes the low end saturate earlier; a high-shelf boost pushes the top end into breakup. Two shelves + one parametric bell. | Tone-shaping the output — that's Post-Emphasis. |
| 3 | **Crossover Split** | Divide the signal into 4 bands (Low, Lo-Mid, Hi-Mid, High) using Linkwitz-Riley 24 dB/oct filters. Three user-adjustable crossover frequencies. | Any per-band processing other than split/recombine. |
| 4 | **Saturation Engine × 4** | Four parallel saturation modules, one per band, each independently configurable for mode (Tube/Tape/Transistor/Germanium/Diode/Wavefold), drive amount, asymmetry, and wet/dry mix. This is the core. | Clean gain or EQ — the saturation engine only clips/shapes, it doesn't boost or cut linearly. |
| 5 | **Crossover Recombine** | Sum the 4 processed bands back into a single stereo signal. LR24 ensures phase-coherent recombination (no comb filtering at crossover points when all bands are at unity). | Any tone-shaping beyond band recombination. |
| 6 | **Post-Emphasis EQ** | Two shelves + one parametric bell for final tone-shaping after saturation. Compensates for the low-end loss and high-end buildup typical of heavy saturation. | Corrective EQ for the dry signal — that's Pre-Emphasis. |
| 7 | **Output Gain + Ceiling** | ±24 dB output trim, followed by a soft-clip limiter with a user-set ceiling (default -1 dBTP). Protects downstream plugins from overs. | True brickwall limiting — this is a soft safety net, not a mastering limiter. |
| 8 | **Dry/Wet Mix** | Crossfade between dry (post-input-gain, pre-everything else) and wet (fully processed). Parallel saturation without external routing. | Phase alignment of dry/wet — the dry path has zero latency; crossover and EQ stages introduce latency. Delay-compensated with a matching delay line on the dry branch. |

### Why this order

- **Gain before EQ before saturation:** This is the classic "drive the input" signal flow. Pre-EQ controls *what* distorts; Input Gain controls *how much* it distorts. Together they give the producer a two-dimensional control surface over saturation character.
- **Crossover after Pre-EQ:** The pre-emphasis EQ shapes the spectrum *before* the bands are split. This means a high-shelf boost on Pre-EQ pushes the Hi-Mid and High bands into saturation harder *without* touching their per-band drive knobs — a different sound than boosting drive on those bands.
- **Post-EQ after saturation:** Saturation always changes tonal balance (adds harmonics, can thin out low end). Post-EQ compensates for this across the entire spectrum — it's a global tone control on the saturated result, not a per-band correction.
- **Ceiling at the very end:** Everything before the ceiling can generate overs. The ceiling catches them all without coloring the saturation character.
- **Dry/Wet last:** This lets the producer blend the fully-processed signal with the original. Putting it before the ceiling would mean the blended signal could still clip downstream.

### Reorderability decision: **Fixed.**

A reorderable chain (e.g., swapping Pre-EQ and Post-EQ position, or moving the crossover before Pre-EQ) would multiply the test surface area by 4–8× without a clear product need. The producer can already achieve equivalent results by adjusting the Pre-EQ vs. Post-EQ curves — the order is an implementation detail, not a creative parameter. Fixed chain = simpler testing, fewer null-test edge cases, lower maintenance burden.

---

## 2. Algorithm / Model Selection

### 2.1 Saturation Engine — Multi-Mode Architecture

ZScorch's core identity is the saturation engine. Rather than a single transfer curve, it supports six modes, each with a distinct harmonic fingerprint.

| Mode | Approach | Harmonic signature | Rejected alternatives |
|---|---|---|---|
| **Tube** | Asymmetric soft-clip (arctan with DC offset). Models a 12AX7 triode's asymmetric transfer characteristic: the positive half-cycle clips later than the negative half-cycle. Generates strong even (2nd, 4th) harmonics + moderate odd harmonics. | Warm, "round," thickening. Even-order dominant. | Full SPICE circuit model — rejected because the CPU cost of a per-sample triode model × 4 bands would blow the CPU budget. The asymmetric arctan captures 90% of the character at <5% of the CPU cost. |
| **Tape** | Symmetric soft-clip with frequency-dependent saturation knee (higher frequencies hit saturation at a lower threshold, modeling tape's HF headroom limits) + gentle 3rd-harmonic emphasis. Dynamic: the knee softens further as RMS level increases (hysteresis-like behavior without the full physics model). | "Glue," compression-like saturation, HF softening. | Jiles-Atherton hysteresis model — rejected for CPU cost and because the audible benefit over the frequency-dependent-knee approach is marginal for a production tool (vs. a mastering tape emulation). |
| **Transistor** | Hard-clip with a soft transition zone (piecewise: linear below threshold, polynomial blend zone, hard clip above). Symmetric. Odd-harmonic dominant (3rd, 5th, 7th). Fast transition = aggressive. | "Crunchy," "aggressive," transistor-pedal character. | Schottky diode model — rejected because transistor mode should be the "clean aggressive" option; diode mode (below) covers the more extreme clipping character. |
| **Germanium** | Soft-clip with very early, very gradual onset — starts saturating at -18 dBFS and gently curves all the way to hard clip. Symmetric with a subtle bias offset that shifts with input level (emulating germanium's thermal drift at the circuit level, in a controlled way). | "Fuzzy," "lo-fi," "vintage console overdrive." | Physical germanium diode model — rejected; the thermal-drift emulation via level-dependent bias captures the character without the instability risk of a full physical model. |
| **Diode** | Two-stage: soft-knee arctan followed by a hard-clip brickwall. Models a diode clipper circuit where the first diode pair starts conducting softly, then the second pair hard-clamps. Asymmetric option (positive and negative thresholds independently adjustable). | "Clipped," "aggressive," "distortion pedal." | Full diode ring model — rejected because the two-stage approach delivers the same dual-threshold effect at a fraction of the cost. |
| **Wavefold** | Sine-based wavefolder: `sin(input × π/2 × foldAmount)` mapped through multiple fold stages. Up to 4x folding. Produces extreme harmonics far beyond the original bandwidth — requires the most aggressive oversampling (see §4). | "Metallic," "digital destruction," "complex harmonics." | Chebyshev polynomial wavefolder — rejected because the sine-based approach gives more intuitive control over fold amount and is easier to antialias-filter. |

**Per-mode, not per-plugin:** Each of the 4 bands can independently select its saturation mode. Producer can have Tube on lows, Tape on lo-mids, Transistor on hi-mids, and Wavefold on highs — or four identical modes with different drive levels.

### 2.2 Crossover — Linkwitz-Riley 24 dB/oct

**Choice:** 4th-order Linkwitz-Riley (LR4, 24 dB/oct) crossover filters.

**Rationale:** LR4 is the standard for multiband processing because it guarantees phase-coherent recombination: when all bands are summed at unity gain, the output is exactly the input (no comb filtering, no amplitude ripple). 24 dB/oct is the sweet spot — steep enough for good band isolation (adjacent bands won't bleed into each other's saturation), shallow enough to keep filter ringing minimal and latency low.

**Rejected:** 
- Simple 2nd-order parallel filters (12 dB/oct) — rejected because the band isolation is insufficient for independent saturation. A heavily-driven hi-mid band would leak into the high band and create unexpected intermodulation.
- LR8 (48 dB/oct) — rejected because the steeper slope adds latency and filter ringing without meaningful isolation improvement for saturation (unlike a multiband compressor, where 48 dB/oct can be justified for tighter band control).

**Crossover frequency defaults:** 250 Hz, 1.5 kHz, 6 kHz. All three are user-adjustable (50 Hz–20 kHz range, with guard that lower < middle < upper always).

### 2.3 EQ — Minimum-Phase IIR

**Choice:** Biquad cascade, minimum-phase IIR filters (RBJ cookbook). Pre-Emphasis: Low Shelf, Parametric Bell, High Shelf. Post-Emphasis: same topology.

**Rationale:** Minimum-phase IIR is the correct choice for a creative coloring EQ → saturation chain. The phase shift from minimum-phase filters is part of the character — it's what makes the EQ feel "musical" and analog, especially before a saturator where the phase shift changes *how* the harmonics alias and interact. Linear-phase would be the wrong choice here — it would sound sterile and "separated" in a context where blending and interaction are the goal. CPU cost: ~6 biquads × 2 channels = negligible at any sample rate.

**Rejected:** Linear-phase FIR — adds latency the product doesn't need, costs more CPU, and actively works against the "analog warmth" character the plugin is selling.

### 2.4 Ceiling Limiter — Soft-Clip Lookahead

**Choice:** 1 ms lookahead peak limiter with a soft knee (arctan transfer curve above ceiling). Not a mastering limiter — it's a safety net.

**Rationale:** The ceiling exists to catch stray overs from the saturation stages and Post-EQ boost, not to do loudness maximization. 1 ms lookahead is enough to catch true-peak transients without audible distortion. The arctan soft knee prevents hard-clip artifacts at the ceiling threshold.

**Rejected:** Full brickwall lookahead limiter (e.g. 5ms+) — too much latency for what's essentially a safety net, and the "aggressive limiting" job belongs to ZMaster, not ZScorch.

---

## 3. Gain Staging

All values are in dBFS unless otherwise noted. These are **targets**, not hard rules — the plugin operates correctly at any input level, but these targets guide the default parameters and preset design.

| Stage boundary | Target average level | Target peak headroom | Notes |
|---|---|---|---|
| Plugin input → Input Gain | -18 dBFS (nominal) | User-controlled via Input Gain | Accepts anything from -60 to +24 dBFS without crashing. Input Gain trims to taste. |
| Input Gain → Pre-EQ | User-set (default: -12 dBFS) | 12 dB above nominal | Default -12 dBFS leaves room for Pre-EQ boosts without overdriving the saturation unintentionally. |
| Pre-EQ → Crossover | -12 to -6 dBFS typical | EQ-dependent | Pre-EQ boosts raise the level into the crossover. The crossover itself is linear — it doesn't clip, so there's no headroom concern at this boundary. |
| Crossover → Saturation (per band) | Band-dependent, user-set via per-band Drive | Drive control calibrated so 0 dB = unity through the saturation stage at nominal input | Saturation engines use soft-clip transfer functions — they inherently handle any input level. But Drive sets the operating point on the transfer curve. |
| Saturation → Crossover Recombine | Band-dependent | Post-saturation level is lower than pre-saturation (clipping always reduces level). No risk of overs. | Each band's output is summed. If all four bands are at -6 dBFS, the summed output could peak at +6 dBFS. This is normal and expected — the Ceiling handles it. |
| Crossover Recombine → Post-EQ | Typically -3 to 0 dBFS peaks | Post-EQ can boost, creating overs | The Ceiling stage exists specifically to catch these overs. |
| Post-EQ → Ceiling | Typically slightly above the ceiling threshold | Ceiling catches everything | User-set ceiling default: -1 dBTP. |
| Ceiling → Dry/Wet → Plugin Output | ≤ Ceiling threshold | Safe | Zero risk of overs at plugin output boundary. |

**Gain compensation: Manual.** Each band has its own Drive control; there's a global Output Gain after the Ceiling. No automatic gain compensation — the producer hears exactly what the saturation does, and compensates manually if they want level-matched A/B comparison. Auto-gain would add correctness-testing surface (the auto-gain algorithm itself would need validation) for a feature the target user (producer, not mastering engineer) isn't asking for.

---

## 4. Oversampling Strategy

ZScorch is a nonlinear plugin — every saturation mode generates harmonics above Nyquist that alias back down. The oversampling strategy is per-stage, not per-plugin.

| Stage | Oversampling | Factor | Forced or optional? | Latency added | Justification |
|---|---|---|---|---|---|
| **Saturation Engine** (all 4 bands, all modes) | **Yes** | 4× | **Forced** | ~1.5 ms at 48 kHz (from the 4× up/down FIR filter pair) | All six saturation modes are nonlinear transfer functions. Tube, Tape, and Germanium are moderate-generating (2× might suffice), but Transistor (hard-clip transition) and especially Wavefold (4× folding generates harmonics up to 4× the fundamental — at 10 kHz input, that's 40 kHz of harmonics folded into the audible band) need 4× to keep aliasing below audibility. Since the user can switch any band to Wavefold at any time, forced 4× on all bands is the only correct choice. An "optional" toggle that measurably changes Wavefold's character isn't optional — it's two different products. |
| **Ceiling Limiter** | **Yes** | 2× | **Forced** | ~0.3 ms at 48 kHz (from the shared 4× up/down stream — the Ceiling operates at 4× because the saturation engine already upsampled) | The soft-clip kneed limiter is a nonlinear transfer function. At 2× (or the inherited 4× from the saturation engine), aliasing on transient peaks is pushed below audibility. The Ceiling operates at the same oversampled rate as the saturation engine to avoid an extra up/down filter pair. |
| **Pre-EQ** | No | — | — | 0 | Linear filters (biquads) don't alias. No benefit to oversampling. |
| **Post-EQ** | No | — | — | 0 | Same reasoning as Pre-EQ. |
| **Crossover** | No | — | — | 0 | LR4 crossover filters are linear. No aliasing risk. (They do introduce their own latency — see §5.) |

**Oversampling implementation:** The saturation engine + ceiling operate in a single 4×-upsampled intermediate processing block. The Pre-EQ → crossover split runs at 1×, the 4 bands are each upsampled via a polyphase FIR (half-band, 65-tap, -120 dB stopband) before the saturation engine, processed at 4×, then downsampled back. The crossover recombine → Post-EQ runs at 1×. This means the FIR filter pair latency is paid once, not 4× per band.

---

## 5. Latency Budget

ZScorch targets **zero-latency usability for tracking**, with an acceptable maximum of **≤3 ms total**. Here's the budget:

| Latency source | Samples at 48 kHz | Milliseconds | Priority |
|---|---|---|---|
| LR4 Crossover (3 × 4th-order filters) | ~24 samples | ~0.5 ms | Core architecture — can't reduce without switching to LR2 (rejected for band isolation reasons) |
| 4× Oversampling FIR filter pair | ~60 samples | ~1.25 ms | Required for Wavefold and Transistor modes — non-negotiable |
| Ceiling lookahead (1 ms) | 48 samples | ~1.0 ms | Required for true-peak catching — non-negotiable |
| Dry/Wet delay compensation | 0 (dry path delayed to match wet) | 0 (uses same delay buffer) | No additional latency — the dry branch reads from a ring buffer at the same offset |
| **Total reported via setLatencySamples** | **~132 samples** | **~2.75 ms** | **Within budget (≤3 ms)** |

This is a **latency-accepted** design with a hard cap. The 2.75 ms total is well within the ≤3 ms budget, leaving a small margin for implementation-rounding differences (e.g., the FIR filter pair might be 64 or 72 samples depending on the exact design, not exactly 60). At 96 kHz, all sample counts double but the millisecond total stays the same (~2.75 ms).

**Why not zero-latency?**
- Wavefold mode *must* be oversampled to avoid un-musical aliasing. A "zero-latency variant" that disables oversampling would produce a measurably different (and worse) product in Wavefold mode.
- LR4 crossovers can't be made zero-latency without switching to LR2, which weakens band isolation.
- 2.75 ms is imperceptible for tracking (equivalent to standing ~3 feet further from a monitor speaker).

---

## 6. Parameter Summary

### Global parameters

| Parameter | Range | Default | Host automation |
|---|---|---|---|
| Input Gain | -24 to +24 dB | 0 dB | Yes |
| Output Gain | -24 to +24 dB | 0 dB | Yes |
| Ceiling | -12 to 0 dBTP | -1 dBTP | Yes |
| Dry/Wet Mix | 0% to 100% | 100% | Yes |
| Bypass | On/Off | Off | Yes |

### Pre-Emphasis EQ

| Parameter | Range | Default |
|---|---|---|
| Low Shelf Freq | 50–500 Hz | 200 Hz |
| Low Shelf Gain | -12 to +12 dB | 0 dB |
| Bell Freq | 200–8000 Hz | 1500 Hz |
| Bell Gain | -12 to +12 dB | 0 dB |
| Bell Q | 0.1–4.0 | 0.71 |
| High Shelf Freq | 2–16 kHz | 8 kHz |
| High Shelf Gain | -12 to +12 dB | 0 dB |

### Crossover

| Parameter | Range | Default |
|---|---|---|
| Crossover 1 (Low ↔ Lo-Mid) | 50–500 Hz | 250 Hz |
| Crossover 2 (Lo-Mid ↔ Hi-Mid) | 200–4000 Hz | 1500 Hz |
| Crossover 3 (Hi-Mid ↔ High) | 1–16 kHz | 6 kHz |

### Per-Band Saturation (×4 bands, identical parameter set per band)

| Parameter | Range | Default | Notes |
|---|---|---|---|
| Mode | Tube / Tape / Transistor / Germanium / Diode / Wavefold | Tube | Per-band, not global |
| Drive | 0–100% (maps to transfer curve intensity) | 25% | 0% = linear passthrough within the band |
| Asymmetry | -100% to +100% | 0% (symmetric) | Shifts the transfer curve DC offset. Negative = more even-order on the negative half-cycle. Only meaningful on Tube, Germanium, Diode. Grayed out (fixed at 0%) on Transistor and Wavefold. |
| Fold Amount | 1×–4× | 2× | Only active in Wavefold mode; grayed out otherwise. |
| HF Rolloff | Off, -3 dB, -6 dB at 10 kHz | Off | Simulates tape-like HF headroom reduction. Only active in Tape mode. |
| Mix | 0–100% wet | 100% | Per-band parallel saturation blend |
| Solo | On/Off | Off | Solos this band's output (mutes other 3 bands). Monitoring tool, not an automation target. |
| Mute | On/Off | Off | Mutes this band. |

### Post-Emphasis EQ

Identical parameter set to Pre-Emphasis EQ.

**Total automatable parameters: 7 global + 7 pre-EQ + 3 crossover + (7 per-band × 4 bands) + 7 post-EQ = 52 parameters.**

---

## 7. CPU Budget

Per the Zarlino QA Platform spec, the reference machine is a **4-core CPU / 4GB RAM** running at 48 kHz / 128-sample buffer.

| Operation | Budget | Justification |
|---|---|---|
| Saturation engine (4× bands at 4× oversampling) | ≤0.5% CPU total | Six modes × 4 bands @ 192 kHz internal rate. The transfer functions are cheap (arctan, polynomial, sine), so per-sample cost is low even at 4× rate. |
| FIR up/down filter pair (65-tap × 2 passes × 2 channels) | ≤0.15% CPU | 65-tap polyphase FIR is 33 multiplies per output sample per channel per pass. At 4×, that's (33 × 192 kHz × 2 passes × 2 channels) = ~25 MMAC/s — well within budget. |
| LR4 crossover (6 filters × 2 channels) | ≤0.05% CPU | Biquads are ~5 operations per sample. Negligible. |
| EQ (6 biquads × 2 channels) | ≤0.05% CPU | Same order of magnitude as crossover. |
| Ceiling limiter | ≤0.05% CPU | Simple arctan soft-clip + peak detector. |
| **Total target** | **≤0.8% CPU** | Leaves 0.2% headroom below a 1.0% budget for UI thread, parameter smoothing, and OS jitter. |

The ZScorch performance test unit will be: `ZSCORCH-PERF-CPU-0001` — CPU ≤ 0.8% on reference machine at 48kHz/128 with all 4 bands active in Tube mode at 50% drive.

---

## 8. Architecture → QA Pillar Traceability

Every architecture decision maps to a testable unit in the Zarlino QA Platform spec.

| Architecture decision | QA pillar | Test unit example |
|---|---|---|
| LR4 crossover recombination is phase-coherent | Reliability (DSP Correctness) | `ZSCORCH-DSP-CROSSOVER-NULL-001` — all bands at unity gain and Drive=0% must produce bit-identical output to input (null test) |
| 4× oversampling on saturation engine | Reliability (DSP Correctness) | `ZSCORCH-DSP-ALIAS-001` — 10 kHz sine at 48 kHz through Wavefold mode must have aliasing products ≤ -96 dBFS |
| Ceiling at -1 dBTP default | Reliability (DSP Correctness) | `ZSCORCH-DSP-CEILING-001` — 10k random input peaks through full chain must never exceed -1 dBTP |
| No heap allocation in processBlock | Reliability (Real-Time Safety) | `ZSCORCH-RTSAFE-ALLOC-001` — allocation hook fires zero times during 10-min stress run |
| CPU ≤ 0.8% on reference machine | Efficiency (Performance) | `ZSCORCH-PERF-CPU-0001` |
| setLatencySamples reports 132 (± tolerance) | Reliability (Host Compatibility) | `ZSCORCH-HOST-REAPER-LATENCY-001` — REAPER reports latency within ±4 samples of 132 |
| 52 automatable parameters | Experience (UX) | `ZSCORCH-UX-AUTOMNAME-001` — all 52 params have correct host-visible names |

---

## 9. Rejected Alternatives Log

| Rejected | Why |
|---|---|
| Reorderable signal chain | Multiplies test surface by 4–8× for no clear product need. Producer gets equivalent control via EQ placement + band drive. |
| Per-band oversampling toggle | Wavefold mode *requires* 4× oversampling to avoid un-musical aliasing. A toggle that makes Wavefold sound worse isn't a feature — it's a second, worse product. Forced 4× is the only honest option. |
| Auto-gain compensation | Adds correctness-testing surface (auto-gain algorithm validation) for a feature the target user isn't asking for. Manual Output Gain is sufficient and keeps the audio path simpler to test. |
| Full SPICE circuit models for saturation | CPU cost × 4 bands would exceed the 0.8% budget. The simplified transfer curves (arctan, polynomial, sine) capture 90% of the character at <5% of the cost. |
| Linear-phase EQ | Adds latency, costs more CPU, and actively works against the "analog warmth" character. Minimum-phase IIR is the correct creative choice for a pre-saturation EQ. |
| More than 4 bands | Each additional band adds an LR4 crossover pair (more latency, more CPU) and a full saturation engine. 4 bands offers enough surgical control without bloat. 5+ bands would push CPU and latency budgets. |
| Zero-latency variant | Can't do it without removing Wavefold mode or disabling oversampling (see above). At 2.75 ms, the latency is imperceptible for tracking. |

---

## 10. Implementation Sequence (for plugin-engineer)

When `plugin-engineer` picks this up, the recommended build order is:

1. **Scaffold plugin** — CMakeLists.txt, PluginProcessor skeleton, parameter layout (all 52 params via APVTS)
2. **Saturation engine first** — implement all 6 modes as standalone functions with unit tests. This is the hardest-to-get-right module and the core identity.
3. **Oversampling wrapper** — 4× up/down FIR filter pair, verified null-test (upsample → downsample = bit-identical at 1× rate, at least within the filter's passband)
4. **Crossover** — LR4 band split/recombine, null-test at unity
5. **EQ modules** — RBJ biquad shelves + bell, tested against reference coefficients
6. **Ceiling + Dry/Wet** — soft-clip limiter + delay-compensated mix
7. **Wire the chain** — connect all stages in order, run full-chain null test (all drives at 0%, all EQs flat → output = input)
8. **UI** — build the editor after the DSP is verified
