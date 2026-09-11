# ZTame v1.1 — Rework Design (DSP architecture → implementation contract)

Status: implementation contract. Written before code, per `dsp-architect`.
Scope: fixes the v1.0.0 engine defects, adds sidechain + draggable focus band + live
visualisation, and re-lays out the editor.

---

## 0. Why this rework (v1.0.0 defect audit)

Audit of `plugins/ZTame/Source/PluginProcessor.cpp` (v1.0.0) against the JUCE FFT contract
*and the shipped tests*:

| # | Defect | Evidence | Effect |
|---|--------|----------|--------|
| B1 | Real-FFT scratch buffer sized **N floats**, JUCE requires **2·N**. `performRealOnlyForwardTransform` writes 2·N floats. | `juce_FFT.h:86` "must be 2 * getSize()"; `state.fftBuffer.assign(fftSize)` | 8 KB heap overflow **every frame**. Arbitrary corruption; explains "sometimes silence, sometimes −20 dB". |
| B2 | Packed spectrum indexing off by one (`spectrum[2i−1]`, `spectrum[2i]`); Nyquist read from `spectrum[N/2]`. | Correct layout is `re[k]=d[2k]`, `im[k]=d[2k+1]`. | Detection read imaginary/real parts of *neighbouring* bins → detection was meaningless noise. |
| B3 | Hann analysis + Hann synthesis + `1/hopSize` normalisation. COLA sum for Hann² at 50 % overlap is `0.5·(1+cos²)` (ripple, mean 0.75), not 1.0. | analytic | ~−2..−6 dB level error with frame-rate amplitude ripple. |
| B4 | Frame gain smoothed with a **per-sample** time constant but applied **once per hop (1024 samples)**. | `timeConstantToCoeff(attackMs, sampleRate)` called per frame | every stated attack/release is 1024× too slow (3 ms → 3 s). |
| B5 | Latency never declared (`setLatencySamples` absent) while the wet path is delayed by ~N samples. | impulse test shows wet onset ≈ N samples late | host/PDC misalignment; dry/wet mixing comb-filters (−46 ms offset between dry and wet). |
| B6 | Split-band "crossover" is a no-op (`(void) binFreq`). | code | the control did nothing. |
| B7 | `dryBuffer.makeCopyOf(buffer)` inside `processBlock` (allocation on audio thread, re-allocates on every block-size change). | code | RT-unsafe. |

Net effect: the plugin's own wet path was broken, so **the user could not perceive what it
did**. That is defect #1 this rework fixes; everything else builds on it.

---

## 1. One-sentence purpose (unchanged)

> ZTame removes narrow-band resonances / ringing harshness while leaving the surrounding
> tonal mass intact — "Tame the ring. Keep the tone."

Every stage below traces back to that sentence. Nothing was added that does not.

## 2. Signal chain (fixed order)

```
                       ┌─ analysis (sqrt-Hann, FFT N=1024/2048, 50 % overlap)
in (main) ─ ring ──────┤
                       └─ resynthesis (gains) ─ OLA ─ wet
                                                        │
sidechain (aux bus) ─ analysis ─ detection source ──────┘   (detection only)
                                      │
                                 focus band ─ suppression mask
                                      │
   out = (1−mix)·dry[N] + mix·wet   |   listen: out = Δ·2  (Δ = (1−g)·X, the removed material)
```

Stage boundaries (what each stage is *not* responsible for):
- **Analysis**: spectrum only. Does not gate, judge or suppress.
- **Detector**: per-bin resonance ratio → reduction in dB. Does not touch time domain.
- **Focus band / mask**: frequency-domain weighting of the reduction. Does not change detection.
- **Gain smoother**: attack/release trajectory. Does not re-detect.
- **Resynthesis/OLA**: perfect reconstruction when gains = 1. Does not normalise anything else.

## 3. Numerical contract (what the engine must satisfy)

1. **Perfect reconstruction**: with `range = 0` (gain ≡ 1) the output must null against the
   input delayed by exactly `N` samples, residual ≤ −80 dBFS.
2. **Latency**: wet path delay = `N` (frame length) exactly; reported via `setLatencySamples`
   and applied to *every* path (dry mix, bypass, host bypass) so PDC stays coherent.
   `N = 1024` (Fast) or `2048` (Precise) → 23.2 ms / 46.4 ms at 44.1 kHz.
3. **Analysis/synthesis windows**: `w_a = w_s = sqrt(Hann)`; then
   `Σ_frames w_a·w_s = Hann[j] + Hann[j+N/2] = 1` exactly → no normalisation divisor, no ripple.
4. **Spectrum packing** (JUCE real-only, `onlyCalculateNonNegativeFrequencies = true`):
   buffer 2·N floats, input samples in `[0,N)`, bin `k` at `(d[2k], d[2k+1])`, `k = 0..N/2`.
   Inverse uses bins `0..N/2` only; the upper half is reconstructed by conjugation.
5. **Denormal / NaN policy**: `ScopedNoDenormals` + per-sample input sanitisation
   (non-finite → 0). No NaN can enter the recursive gain state.

## 4. Detector (the musical core)

- Local baseline: mean of `db[k]` over an annulus `|j−k| ∈ [2, r(k)]`, `r(k) = 2 + int(2.5·√(f_k/1 kHz))`
  (frequency-adaptive, kept from v1.0 as differentiator D7) — computed with a prefix sum, O(N).
- `excess[k] = db[k] − baseline[k]`.
- `reduction[k] = clamp(excess[k] − threshold, 0, range) · band(f_k)`, then 3-bin triangular
  smoothing along frequency (prevents isolated single-bin notches → no metallic chirp).
- `threshold = 24 − 18·sens + (sel − 0.5)·12` dB, clamped to `[3, 30]`.
  Selectivity is now an **independent** ±6 dB axis at fixed sensitivity (fixes differentiator D5,
  which required ≥ 6 dB of independent travel).
- Detector peak-hold per bin (~80 ms decay) to stop on/off chatter on modulated material.
- Bin gate: bins below −90 dBFS are never suppressed.
- **Focus band** `band(f)`: 1 inside `[low, high]`, raised-cosine skirt (~1/3 octave) outside to 0.
  Replaces the inert v1.0 "split crossover"; it is the draggable rectangle in the UI.

## 5. Gain trajectory

Per-bin one-pole with attack/release **in frames**: `coeff = 1 − exp(−hop / (t·fs))`
(hop = N/2). Attack default 8 ms (frame period at 44.1 kHz is 23 ms, so sub-frame attacks are
"instant" by construction), release default 180 ms. `range` clamps depth at every sample of the
trajectory (never below −range dB, never above unity, no polarity flip).

## 6. Sidechain

- Auxiliary stereo input bus `Sidechain`, declared **inactive by default** (VST3 kAux).
- `sidechain` parameter arms it. Detection falls back to the main input when the bus is
  inactive or carries no signal (> −80 dBFS in the last ~0.5 s), so the control can never
  silently kill the effect. UI shows the live state.
- Sidechain replaces only the **detection source**; gains are always applied to the main input.
- Bus count ≠ 2 (mono main, mono/stereo SC) supported.

## 7. Gain staging / levels

- Idle (gains = 1): unity, bit-transparent track, no trim, no makeup.
- Wet path unity ⇒ dry/wet blend is phase-coherent by construction (both delayed by N).
- Δ (listen) monitor: +6 dB fixed, documented in the tooltip ("hear what is removed").
- No auto-gain, no limiter: the plugin only ever cuts.

## 8. Oversampling

Not applicable — the engine is linear time-varying (per-bin gains), no nonlinearity in the
signal path, so there is nothing to alias. Explicitly not added (CPU for no benefit).

## 9. UI contract (perceivability)

- Live display: input spectrum (fill), output spectrum (line), **removed energy shaded in
  amber**, detected-peak markers with dB labels, focus-band rectangle with drag handles,
  stereo GR meter (average bar + peak marker + numeric).
- Interaction: drag band body = move band; drag edge = resize; double-click = reset to full range.
- Layout: header (identity + Listen/ByPass) → spectrum+GR (hero) → three labelled panels
  DETECT / SHAPE / OUTPUT → status strip (latency, sample rate, sidechain, version).
- Every control has a plain-language tooltip; threshold readout exposes the dB the detector
  is currently requiring.
- Telemetry is a lock-free double-buffered snapshot published by the audio thread (≤ 1 frame
  behind), consumed by a 30 Hz UI timer. No locks, no allocation on the audio thread.

## 10. Verification (see `Tests/`)

- Correctness battery: impulse, frequency response, **latency/null (≤ −80 dBFS)**, boundary
  params, channel independence, sample-rate independence, bypass (delayed passthrough),
  determinism, focus-band selectivity, sidechain routing, resolution modes, Δ null
  (`listen == dry − wet`), GR metering monotonicity.
- Fuzz battery: random params, NaN, Inf, denormals, rapid automation, SR change, buffer-size
  change, soak.
- Perf budget: analysis+resynthesis hot path measured (existing `ZTAME-PERF-001`).
