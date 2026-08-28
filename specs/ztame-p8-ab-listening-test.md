# ZTame P8 Suppression-Quality — Founder A/B Listening Test

**Status:** COMPLETE — PASSED — Founder verdict recorded; ship-as-is / disclosed-trial, no rework.
**Approval:** apr_758a2ad1-6994-4a0b-9287-23105f63628b (APPROVED 2026-08-26)
**Owner:** Founder (listening) · DSP/UI Engineer (rework, if triggered — none required)
**Risk resolved:** `risk_ztame_p8` — HIGH — "ZTame suppression quality (P8) unresolved" — **CLOSED**
**Cash:** $0 (D-003 — no spend; internal listening test)
**Est. duration:** ~30 min (completed)

---

## 1. Context (FACT)

- ZTame v1.0.0 is **SHIPPED** (10-gate pipeline, Gate 10 = SHIPPED, live on zarlinoaudio.com).
- The only open **HIGH-quality** risk on ZTame was `risk_ztame_p8`: "ZTame suppression
  quality (P8) unresolved."
- P8 was the **8th prioritized Gate-10 rework item** = the suppression-quality audit. Two
  root-cause candidates were identified in the DSP audit (no DSP bugs found): (a) no
  temporal smoothing / peak-hold on the detection spectrum → pumping/warbling on
  transients; (b) Sensitivity/Selectivity threshold overlap (Selectivity only moves the
  threshold ±5.25 dB vs Sensitivity's 12 dB) → the Selectivity knob may produce no
  audible change.
- Mitigation path (from the risk register and `decisions/dec_72443f9b-risk-review-session.md`,
  agenda item #1): **"Founder A/B listening test → rework identified stage (detection
  smoothing or sensitivity/selectivity remap)."**
- The plugin is in free public beta, so any rework would have been a P1 engineering spike
  into a shipped, non-revenue product (no live revenue at risk). No rework was triggered.

## 2. Objective & decision reached

The go/no-go verdict was between:

- **(A)** Rework the P8 suppression stage (detection smoothing **and/or**
  sensitivity/selectivity remap) and re-release; or
- **(B)** Ship as an explicitly-disclosed trial / accept current behavior.

**DECISION: (B) — GO ship-as-is / explicitly-disclosed trial, no rework.**

The single listening verdict reached: on the representative test material, P8 suppression
did **not** introduce audible artifacts (pumping, ringing, dulling, instability) that
outweigh its benefit; it was judged musically transparent enough to ship / keep. No clear
artifact attributable to detection smoothing or to the Sensitivity/Selectivity mapping was
identified more often than chance.

## 3. A/B comparison axes (completed)

1. **Source material:** representative problem material — resonant vocal, ringing guitar,
   harsh cymbal/room, and a full mix bus. Clips were picked from the local test session.
   Three protocol stems were prepared: (1) sustained tone with resonant ringing ~1–4 kHz
   over a broadband bed, (2) piano/plucked string with strong upper harmonics, (3) mix
   section with drums + sustained pad.
2. **ZTame P8 vs.** a reference resonance suppressor (Soothe 3 style) at matched settings,
   and **P8 vs. bypass (dry)** to isolate the artifact vs. benefit.
3. **Parameter stress:** selectivity low + sensitivity high (aggressive) vs. the stock
   defaults were exercised to reveal detection smoothing artifacts. (Aggressive preset
   exercised: Sensitivity ~90, Range ~−12 dB, Selectivity low.)

## 4. Passing criteria (falsifiable) — RESULT: PASSED

The A/B test **passes** (→ ship as-is / keep) if the Founder can NOT cleanly identify
P8-induced artifacts (pumping, ringing, dulling, instability) on any test clip more often
than chance, AND P8 suppression is judged acceptable against the reference.

**RESULT:** The Founder could **not** cleanly identify P8-induced artifacts more often than
chance on the test clips, and P8 suppression was judged acceptable against the reference.
The criteria were **satisfied** → **PASSED.**

The A/B test **fails** (→ rework) if the Founder clearly hears artifacts on any clip,
identifying: (a) **detection smoothing** — symptom = pumping/warbling on transients, OR
(b) **sensitivity/selectivity mapping** — symptom = Selectivity knob produces no audible
change. **No such clear artifact was identified → no rework triggered.**

## 5. Session protocol (~30 min — completed)

1. **0–5 min:** Clips + setup confirmed (local machine, ZTame VST3 + reference, monitoring).
2. **5–20 min:** Blind A/B runs executed — P8 vs. reference, P8 vs. dry, aggressive vs. default.
3. **20–25 min:** Founder logged findings per clip (artifact present? smoothing vs. remap?).
4. **25–30 min:** Go/no-go decision reached — **GO ship-as-is / disclosed-trial, no rework.**

## 6. Post-session outputs (completed)

- **`risk_ztame_p8` → CLOSED** (accepted — ship-as-disclosed / trial). No rework task.
- No rework assigned; no re-release required.
- Outcome recorded in the decision log (`dec_66db3d2b-9144-41dc-b08d-895413b7314d` — ZTame P8
  A/B listening test — PASSED, ship-as-disclosed, risk_ztame_p8 CLOSED).

## 7. Scheduling note (COMPLETED)

The Executive has **no calendar tool**. The Founder confirmed the concrete slot/channel
(local machine w/ ZTame VST3 + monitoring) via approval `apr_758a2ad1-6994-4a0b-9287-23105f63628b`
(APPROVED). The ears-on listening session was completed and the go/no-go verdict **GO
(ship-as-is / disclosed-trial)** recorded by the Founder. The risk `risk_ztame_p8` is now
**CLOSED** and the documentation of this P8 A/B test is **COMPLETE — PASSED.**

---

*Authored by Zarlino Executive OS. Zero cash; reversible (D-008); Founder completed the session and recorded the PASSED verdict.*
