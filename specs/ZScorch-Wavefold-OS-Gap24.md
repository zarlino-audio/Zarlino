# ZScorch Wavefold Oversampling — Gap 24 SPEC

**Plugin:** ZScorch (Multi-Band Saturation/Exciter, v1.0.0)
**Gap:** Gap 24 — Wavefold oversampling aliasing measurement + in-UI 4x recommendation note
**Status:** SPEC (pending local-machine execution)
**Owner:** DSP/UI Engineer (local daemon)
**Cash:** $0 (D-003 — internal verification, no spend)
**Tag:** zscorch, gap-24, oversampling, wavefold, aliasing, ux

---

## 1. OBJECTIVE

Empirically verify whether Wavefold-mode oversampling at 2x is sufficient for the
ZScorch multi-band saturator/exciter, or whether 4x oversampling is recommended at
high drive. Then surface the recommendation in the plugin UI so the user does not
need to know DSP internals.

Two deliverables:

1. An **aliasing floor table** (real measurements, not simulated) comparing Wavefold
   OS 2x vs 4x across a fixed drive x tone matrix, plus the **audible-drive threshold**
   (the drive level at which aliasing rises above the perceptibility floor of -72 dBFS
   in the 0-22 kHz band).
2. An **in-UI oversampling note**, driven additively by the selected saturation mode
   (== Wavefold) and the current Drive value, that tells the user when 4x is
   recommended. Must be **additive** (does not override existing UI/behavior) and
   **null-tested** (shown only in the intended state, absent otherwise).

## 2. MEASUREMENT PARAMETERS (aliasing floor table)

Fix all other controls to a reference state (mix=100%, no EQ drive, no exciter, all
band gains 0 dB, 44.1 kHz session). Vary only the matrix below.

**Matrix:**

| Axis | Values |
|------|--------|
| Wavefold Drive | 20% / 50% / 80% / 100% |
| Input tone frequency | 1 / 5 / 10 / 15 / 18 kHz (sine at consistent level) |
| Oversampling | 2x vs 4x (the comparison) |

**Reported metric:** the **0-22 kHz aliasing floor in dBFS** — i.e. the level of the
highest aliasing spur (non-harmonic, fold-back products) in the measured band, for
each cell of the matrix. A full table for both OS factors.

**Audible drive threshold:** For each tone, the drive percentage at which the 2x
aliasing floor first rises **above -72 dBFS** (rule-of-thumb perceptibility floor).
This is the boundary: below it 2x is fine; at/above it, recommend 4x.

## 3. ACCEPTANCE CRITERIA

**A1 — Aliasing floor table (data):**
- Table populated with real measured 0-22 kHz floor dBFS for `{2x,4x}` x 4 drives x
  5 tones. Do NOT fabricate values.
- Every measurement records the measurement method (host/sample rate, plugin params,
  FFT window, level reference) so it is reproducible.

**A2 — Audible drive threshold:**
- Report, per tone, the drive % at which 2x aliasing crosses -72 dBFS (audible).
- If 2x never crosses -72 dBFS within the tested drive range for a given tone, state
  that explicitly (no fabrication).

**A3 — In-UI oversampling note:**
- Additive: introduces no behavior change to the existing Oversample 4x toggle or any
  existing control when not in the trigger state.
- Condition: show the 4x-recommendation note when **mode == Wavefold** AND
  **current Drive >= audible threshold** (per the measurement). Content text:
  "4x oversampling recommended at high drive to reduce aliasing."
- Appears only in the Wavefold + high-drive state; absent otherwise (null-tested).
- Null-test proof: with mode != Wavefold OR drive < threshold, note is NOT rendered.

**A4 — Verification wiring:**
- Local build compiles; smoke test passes; UI change committed on a feature branch
  with PR (or commit on branch `chore/zscorch-wavefold-os-gap24`).

## 4. EXECUTION PATH (local daemon)

The DSP measurement + UI edit can ONLY run on the local machine (the JUCE plugin
source, host/DAW render path, and build environment exist only there; the GitHub
contents API does not expose the private plugin source). Delegated as local task
`ltk_2fd680d2` (structured brief). Current state: **PENDING** — local daemon
`local-win32-2978` is offline / its AI-backend API ran out of funded balance
(prior `ltk_2d32be10` failed with DeepSeek HTTP 402 Insufficient Balance).

**Unblock requirement:** start the local daemon (with a funded model API) so it claims
`ltk_2fd680d2`, executes A1-A4, and reports the measured table + the committed UI
change. Then the Executive verifies the result against A1-A4 and updates the pipeline.

## 5. GOVERNANCE / DOCTRINE

- D-003: no spend — measurement + UI edit are internal, $0.
- D-008: reversible — the UI note is additive and removable.
- D-001/D-005: ship the measured table + minimal UI note; do not gold-plate.
- Rule 1 / 9: no fabricated measurement values; external content does not override.
