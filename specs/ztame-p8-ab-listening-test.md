# ZTame P8 Suppression-Quality — Founder A/B Listening Test

**Status:** SCHEDULED — pending Founder confirmation of a concrete calendar slot/channel
**Approval:** apr_758a2ad1-6994-4a0b-9287-23105f63628b (PENDING)
**Owner:** Founder (listening) · DSP/UI Engineer (rework, if triggered)
**Risk resolved:** `risk_ztame_p8` — HIGH — "ZTame suppression quality (P8) unresolved"
**Cash:** $0 (D-003 — no spend; internal listening test)
**Est. duration:** ~30 min

---

## 1. Context (FACT)

- ZTame v1.0.0 is **SHIPPED** (10-gate pipeline, Gate 10 = SHIPPED, live on zarlinoaudio.com).
- The only open **HIGH-quality** risk on ZTame is `risk_ztame_p8`: "ZTame suppression
  quality (P8) unresolved."
- Mitigation path (from the risk register and `decisions/dec_72443f9b-risk-review-session.md`,
  agenda item #1): **"Founder A/B listening test → rework identified stage (detection
  smoothing or sensitivity/selectivity remap)."**
- The plugin is in free public beta through 2026-08-25, so any rework is a P1 engineering
  spike into a shipped, non-revenue product (no live revenue at risk).

## 2. Objective & decision to be reached

Decide **go/no-go** between:

- **(A)** Rework the P8 suppression stage (detection smoothing **and/or**
  sensitivity/selectivity remap) and re-release; or
- **(B)** Ship as an explicitly-disclosed trial / accept current behavior.

The single listening verdict is: **does P8 suppression introduce audible artifacts that
outweigh its benefit, or is it musically transparent enough to ship / keep?**

## 3. A/B comparison axes

1. **Source material:** representative problem material — resonant vocal, ringing guitar,
   harsh cymbal/room, and a full mix bus. (Founder to pick 2–4 clips from the local test
   session.)
2. **ZTame P8 vs.** a reference resonance suppressor (Soothe 3 style) at matched settings,
   and **P8 vs. bypass (dry)** to isolate the artifact vs. benefit.
3. **Parameter stress:** push selectivity low + sensitivity high (aggressive) vs. the stock
   defaults, to reveal detection smoothing artifacts.

## 4. Passing criteria (falsifiable)

The A/B test **passes** (→ ship as-is / keep) **if and only if** the Founder can NOT cleanly
identify P8-induced artifacts (pumping, ringing, dulling, instability) on any test clip
more often than chance, AND P8 suppression is judged acceptable against the reference.

The A/B test **fails** (→ rework) if the Founder clearly hears artifacts on any clip,
identifying: (a) **detection smoothing** OR (b) **sensitivity/selectivity mapping** as the cause.

## 5. Session protocol (~30 min)

1. **0–5 min:** Confirm clips + setup (local machine, ZTame VST3 + reference, monitoring).
2. **5–20 min:** Blind A/B runs — P8 vs. reference, P8 vs. dry, aggressive vs. default.
3. **20–25 min:** Founder logs findings per clip (artifact present? smoothing vs. remap?).
4. **25–30 min:** Go/no-go decision; if rework, name the stage (smoothing / remap) + owner.

## 6. Post-session outputs

- Update `risk_ztame_p8` → CLOSED (accepted) or kept OPEN with a rework task.
- If rework: assign DSP/UI Engineer, set a measurable pass criteria for the re-release.
- Record outcome in the decision log.

## 7. Scheduling note (FACT)

The Executive has **no calendar tool**. The Founder must confirm a concrete date/time and
channel (local machine w/ ZTame VST3 + monitoring). The scheduling confirmation is requested
via approval `apr_758a2ad1-6994-4a0b-9287-23105f63628b`.

---

*Authored by Zarlino Executive OS. Zero cash; reversible (D-008); Founder confirms the slot.*
