# Risk Review Session — Schedule Plan

**Decision:** dec_72443f9b ("Review risks and mitigations") — **Step 1/6**
**Status:** SCHEDULED (awaiting Founder calendar-slot confirmation)
**Approval basis:** apr_a47d66d0 — "Conduct a time-boxed internal risk review … zero cash spend" (APPROVED)
**Cash:** $0 (D-003 — no spend without a falsifiable hypothesis; this is an internal review)

## Session objective (1 hour)
Walk all 14 open risks, assign likelihood/impact on the 10-gate pipeline, and agree a
time-boxed, reversible mitigation for the top blockers. Primary focus: risks that could
block or gate the ZScorch / ZTame commercial ship path.

## Proposed window (within 24 hours)
- Target: next 24h window from this record.
- Duration: **60 minutes**.
- Format: async-first (executive pre-distributes this plan) + optional live sync for decisions.
- Founder confirms the single concrete calendar slot/channel (executive has no calendar tool).

## Attendees (role-based — "core team")
| Role | Why in session |
|------|----------------|
| Founder | Gate-10 human release authority; final mitigation decisions |
| Product Manager | Ship-gate scope (ZScorch/ZTame/PresenceField) |
| DSP / UI Engineer | ZTame suppression-quality (risk_ztame_p8) |
| Security & Licensing Engineer | code-signing risks (risk_ztame_signing, risk_zscorch_signing) |
| QA Engineer | golden-reference audio risk (risk_golden_refs) |
| Executive (Chief of Staff) | facilitation, decisions, task updates after session |

## 1-hour agenda (mapped to open risks)
1. **0–15 min — Top blockers (HIGH):** risk_ztame_p8 (suppression quality) and
   risk_ztame_signing (unsigned + dev license key). Decide: A/B listening test plan vs.
   ship-as-disclosed-trial. Owner: DSP/UI + Security + Founder.
2. **15–25 min — MEDIUM:** risk_zscorch_signing (unsigned release). Decide signing
   certificate + CI signing config. Owner: Security & Licensing.
3. **25–40 min — Remaining open risks:** risk_golden_refs (QA, LOW), plus the 10
   LOW operationally-generated risks. Triage each: accept / mitigate / watch.
4. **40–50 min — Mitigation owners & measurable triggers** for the top 3; time-boxed
   and reversible (D-008), each with a falsifiable measure (D-003).
5. **50–60 min — Next steps:** update risk register + task list (tasks 2–6 of the decision).

## Success criterion
Every top-risk (2 HIGH + 1 MEDIUM) has a named owner, a measurable trigger, and a
reversible, time-boxed mitigation written down — confirmed by Founder by end of session.

## Post-session
- Update risk register (executive).
- Re-prioritize tasks (Step 3/6).
- Review after each ship gate; escalate only if cost > $0 or risk exceeds authority (D-009).

---
*Recorded by Zarlino Executive OS. Scheduling is coordinated within delegated authority;
the calendar-slot hold requires the Founder's calendar (outside executive tooling) — see
pending approval.*
