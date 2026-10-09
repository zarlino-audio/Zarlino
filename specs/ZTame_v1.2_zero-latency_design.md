# ZTame v1.2 — Zero-Latency Engine (design contract + delivered results)

Status: **shipped design record** (v1.2.0, 2026-09-20). Written per `dsp-architect`; results
validated per `dsp-correctness` + `rt-safety-performance`.

Scope: replace the v1.1 STFT engine (1024/2048-sample latency, FFT + IFFT + overlap-add
round trip) with an engine that is **0 samples latency**, **cheaper**, **more transparent**,
and **at least as aggressive on real resonances**.

---

## 0. Goals → measured outcomes

| Goal | v1.1 (measured, same machine, same process) | v1.2 (delivered) |
|---|---|---|
| Latency | 1024 / 2048 samples (23 / 46 ms @ 44.1 k) | **0 samples, every path** (incl. host bypass) |
| Idle transparency | −80 dBFS class reconstruction residual | **bit-exact** (residual exactly 0 with depth 0) |
| CPU, active material (best-of-4, interleaved) | 650 ns/sample | **560 ns/sample** (−14 %; 48-band vs v1.1 Precise: 639 vs 825, −23 %) |
| CPU, nothing detected | 751 ns/sample | **220 ns/sample** (−71 %) |
| CPU, silence | 666 ns/sample | **37.5 ns/sample** (−94 %) |
| Suppression, +18 dB Q=8 ring @ 2.4 kHz | 13.5 dB @ engaged | **14.9 dB** engaged / 7.5 dB at defaults |
| Noise-only material | (detector could chatter) | **0.0 dB** reduction (no false triggers) |

Numbers: `artifacts/ztame-v1.2.0/cpu-report.txt` (both engines measured interleaved in one
process; this dev machine is shared/thermally noisy, so same-run deltas are quoted).

---

## 1. One-sentence purpose (unchanged)

> ZTame removes narrow-band resonances / ringing harshness while leaving the surrounding
> tonal mass intact — "Tame the ring. Keep the tone."

---

## 2. Architecture (shipped)

```
in ─┬─ correction : cascade of N peaking bells (cuts only, Q = 2)
    │                  ▲ per-band gain dB (attack/release smoothed, per hop)
    │        ┌─────────┴───────────┐
    │        │ band aggregation    │  max reduction per band + 3-band smoothing
    │        └─────────▲───────────┘
    │                  │
    └─ analysis : PAST-window FFT (N = 1024, hop = N, sqrt(Hann))
                  ─► per-bin magnitude + 80 ms peak hold
                  ─► excess over local neighbourhood (frequency-adaptive radius)
                  ─► threshold / selectivity / focus mask
                  ─► per-bin reduction  → band targets

out = x + (wet − x) · mix          (listen: out = (x − wet) · 2)
```

**Correction** — a serial cascade of 32 (`Standard`) or 48 (`Precise`) RBJ peaking biquads at
log-spaced centres, gains clamped to `[−range, 0]` dB. Three properties carry the design:

1. **Unity = identity.** At 0.000 dB the RBJ peaking coefficients degenerate to
   `b == a`, and with aligned states the direct-form-I stage computes `y = x` exactly.
   Cuts below 0.2 dB are snapped to 0 so the audio path skips the filter entirely.
2. **Gain-invariant poles.** The denominator does not depend on the gain term, so the
   trajectory can move arbitrarily fast without instability.
3. **Click-free switching.** A band that starts cutting seeds its state with the signal at
   that exact sample (`x1=x2=y1=y2=x`); the first output is then exactly the input for any
   cut depth, so no step ever reaches the output.

**Analysis** — a strictly causal FFT of the last 1024 samples (sqrt-Hann, updated every 1024
samples ≈ 21.7 ms; v1.1-Precise-class detection cadence). It **never touches the audio
path**; the only thing the signal passes through is the bell cascade.

### Why this topology (and what it replaced)

The first v1.2 draft used a band-domain ladder (N+1 one-pole lowpasses, band signal
`LP[k+1] − LP[k]`). Measured on the bench it produced `peakGR = 0` on every test case: a
tone centred in a 1/3-octave band is carried by *several* difference bands, so the
"excess over neighbourhood" contrast collapses to ≤ 1 dB — below any usable threshold.
A causal FFT has none of that weakness (fine bins, sidelobes ≥ 40 dB down) and, being
analysis-only, costs a fraction of the v1.1 round trip (no IFFT, no synthesis window, no
overlap-add, no per-bin gain trajectory over 1025 bins).

Rejected alternatives, kept on the record:

| Rejected | Reason |
|---|---|
| Band ladder of difference-of-lowpass signals | Insufficient contrast (measured above); unusable detector. |
| Dynamic notch tracking (adaptive biquads) | Movement chirps, unbounded notch count for clusters, non-deterministic for tests. |
| Allpass-split perfect-reconstruction tree | A full leaf cut removes only ≈ 40 % of a centred tone → few dB; "aggressive" unreachable without blurring cuts. |
| Causal sliding-DFT / one-sided window STFT | Leakage/coloration plus per-bin update cost; worse than the FFT-only analysis. |
| Oversampling | The signal path is linear (time-varying IIR): nothing to alias. |

---

## 3. Band geometry

- N bands log-spaced 20 Hz … `min(20000, 0.45·fs)`; `resolution` selects N = 32 (default)
  or 48.
- Bell Q = 2 (≈ 0.5-octave width): a resonance's neighbours overlap it by design, so
  clusters tame smoothly; 48-band mode tightens the centres.
- FFT bin → band lookup precomputed in the band plan (monotonic edge walk).

## 4. Detector (v1.1 semantics, moved to the band domain)

- Threshold (unchanged formula → existing presets keep meaning):
  `clamp(24 − 18·sens + (sel − 0.5)·12, 3, 30)` dB.
- `excess[k] = binLevelDb[k] − mean(binLevelDb[j])` over `|j−k| ∈ [2, r(k)]`,
  `r(k) = 2 + 2.5·√(f/1 kHz)` (frequency-adaptive, O(1) via prefix sums).
- `reduce[k] = clamp(excess − threshold, 0, range) · focusProfile(f_k)`; bins ≤ −90 dBFS
  are never touched; 3-bin smoothing along frequency (no isolated notches).
- 80 ms peak hold; stereo link = max across channels; sidechain replaces the **detection
  source only** (never the audio path; falls back to main input when dead for ≈ 0.25 s).
- Aggregation: `bandTarget[b] = max(reduce[k] for bins in b)`, then `[0.25 0.5 0.25]`
  smoothing across bands so bell skirts and detector widths agree.

## 5. Gain trajectory

- Per-band one-pole in dB at hop rate; attack when deepening, release when recovering
  (same ids, ranges, defaults as v1.1: 8 ms / 180 ms).
- Snap ladder: `< 0.0005 dB` → exactly 0 (identity, bit-transparent);
  `< 0.2 dB` → skip the filter (inaudible, cheaper; measured −30 % typical CPU).
- Coefficient refresh only when the smoothed cut moved > 1e−3 dB.

## 6. Gain staging / output

- Idle: unity, bit-exact, no trim, no makeup, no limiter.
- Mix is sample-aligned (0 latency) → 50 % mix cannot comb (tested bit-exact).
- Listen: `out = (x − wet) · 2` (+6 dB monitor), exact because correction is subtractive.
- Bypass (parameter or host): buffer is left untouched — bit-exact.

## 7. Oversampling

Not applicable — no nonlinearity anywhere in the audio path (linear time-varying IIR).

## 8. Latency budget

**0 samples.** No delay line, no lookahead, no linear-phase stage. Detection runs on past
frames, so attack resolution is bounded by the hop (≈ 21.7 ms worst case) — the v1.1
"Precise" mode behaved the same way, except v1.1 also had 46 ms of latency.

## 9. CPU / RT safety

- Three fast paths: **silence** (block peak ≤ −90 dBFS: analysis/cascade skipped, buffer
  sanitised and returned), **idle** (no band cutting: cascade skipped), **bypass** (engine
  parked; reopens with fresh state on the falling edge).
- `processBlock` allocates nothing, takes no locks, does no I/O; all buffers are sized in
  `prepareToPlay`; `ScopedNoDenormals` wraps the block; telemetry is a lock-free
  double-buffered snapshot published only while an editor is attached.
- Measured (stereo 48 kHz, 512-sample blocks): active 2.7 % realtime, idle 1.05 %,
  silence 0.18 %, bypass 0.05 %.

## 10. Verification (delivered)

`ZTame_Tests` — **36 suites, 0 failures** (`ctest -R ZTame` is the release gate):

- 001 impulse (no pre-ringing, impulse position preserved), 002 bit-exact unity and
  sub-gate passthrough, 003 zero latency + bit-exact null + comb-free 50 % mix,
  004 bit-exact bypass (parameter and host), 005 boundary parameters, 006 channel
  independence, 007 sample-rate independence (44.1/48/96/192 k), 008 exact delta audition,
  009 determinism (100 runs), 010 focus band, 011 sidechain routing/isolation,
  012 both band plans (null + suppression), 013 GR metering, 014 display mapping,
  015 UI telemetry, 016 M/S modes (lossless codec, protected half bit-exact),
  **017 aggression + transparency** (≥ 9 dB engaged / ≥ 5 dB default suppression,
  ≤ 1.5 dB collateral 6–9 kHz, 0 dB on resonance-free noise).
- Fuzz: random parameters, NaN/Inf injection, denormals, rapid automation, mid-stream
  sample-rate and buffer-size changes, soak.
- Golden: sine passthrough, resonance suppression, delta audition, dry/wet, impulse train,
  100-run determinism, sensitivity/range/attack/selectivity sweeps.
- CPU report: `ZTame_PerfLab` → `artifacts/ztame-v1.2.0/cpu-report.txt`.

## 11. Migration & compatibility

- All parameter ids, ranges and defaults unchanged; v1.1 presets load with identical values.
- `resolution` keeps its id; items are now `Standard (32)` / `Precise (48)` (default
  Standard). `UiSnapshot::fftSize` → `bandCount`.
- Factory programs: `Init` + 6 presets via `getNumPrograms()/getProgramName()/setCurrentProgram()`.
- State version property → 3.

## 12. Release criteria (v1.2.0) — status

- [x] Full suite green (36 suites) and registered with CTest as `ZTame.Unit`
      (`release-gate` label);
- [x] CPU report recorded (`artifacts/ztame-v1.2.0/`);
- [x] version 1.2.0 in CMake, installer, release workflow, render tools;
- [x] spec updated to the shipped architecture (this file);
- [ ] demo A/B renders regenerated with the new engine (RenderLab / RealRender) — still open;
- [x] website product facts updated (version, downloads, zero-latency copy) — re-shipped
      2026-09-21 together with the editor regression fix below.

### 12.1 Post-release fix — editor regression (2026-09-21)

The first v1.2.0 build shipped the wrong editor: the working tree had been carrying an
August-era variant (`InteractiveSpectrum`, no draggable focus band) instead of the
committed v1.1 editor, so the released UI looked rolled back and lost the focus band.

Fix applied:

- `PluginEditor.h/.cpp` restored from `5915901c` (v1.1 editor: `ZTameSpectrumDisplay`
  with the draggable focus band, `writeBand`, live removed-energy display, 1000x640 design)
  and adapted to the v1.2 processor: the processing-mode tile cycles the 4-choice
  `processingMode` parameter, the `resolution` tile shows Standard/Precise band counts,
  the status strip reports `bandCount` + "zero latency", and presets delegate to
  `processor.setCurrentProgram()` (the editor no longer duplicates preset values).
- Rejected variant preserved at `backups/ztame-editor-interactivespectrum/`.
- All targets rebuilt; 36-suite battery green again; UI shot regenerated (1000x640,
  demo band 700 Hz - 9 kHz) and the binaries + website assets republished
  (installer, VST3 zip, standalone zip; site version `18be4899`).

### 12.2 Post-release fix — UI text mojibake (2026-09-21)

The republished v1.2.0 UI shot (and the running plugin) showed mojibake glyphs
("Â·" instead of "·", "â€”" instead of "—", "Î”" instead of "Δ") in every label
that used a non-ASCII character.

Root cause (proven with a controlled render experiment): this toolchain's
`juce::String (const char*)` constructor decodes the raw bytes of a narrow string
literal byte-per-character (its charset guess is not UTF-8), so a literal U+00B7
stored as UTF-8 bytes (C2 B7) materialises as two chars (U+00C2 U+00B7) and renders
as the two-glyph composite "Â·". The compiler side is fine — `/utf-8` is already set
for all ZTame targets, so the shipped binaries contain correct UTF-8 bytes; the
corruption happens at runtime string construction.

Fix applied (`plugins/ZTame/Source/PluginEditor.cpp`):

- Added anonymous-namespace constants built from code points:
  `kMidDot` (U+00B7), `kEmDash` (U+2014), `kPlusMinus` (U+00B1), `kDelta` (U+0394)
  via `juce::String::charToString ((juce::juce_wchar) 0x..)`.
- Replaced all 15 raw non-ASCII literal sites (band readout, LISTEN tile, tooltips,
  mode tile, resolution tile, sidechain tile, status strip, header tagline) with
  concatenations of those constants; ternaries wrapped in `juce::String (...)`.
- Rule going forward: never type raw non-ASCII bytes into narrow string literals in
  this project; build them from code points (comments may keep raw glyphs).

Verification: pixel-level measurement of the rendered UI shot (column ink-top map
of the tagline strip) — the old shot showed ink 4-5 px ABOVE the text x-height in
the separator zone (the "Â" cap of the composite); the fixed shot shows a single
in-line dot, with the surrounding letters pixel-identical. All targets rebuilt,
36-suite battery green, UI shot + binaries + website assets republished.

