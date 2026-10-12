# Decision Record — Reject Affiliate Platform Build (dec_420964fb-affiliate-dashboard)

**Decision reference:** dec_420964fb ("Launch affiliate enrolling dashboard") — superseded by the **Reject affiliate platform build** decision (dec_29d3135e-4330-4b20-b6b0-ac1e29c498d9).
**Record step:** Step 2/5 of the "Reject affiliate platform build" decision flow.
**Status:** DECIDED — affiliate PLATFORM build REJECTED (do not build/expose).
**Date recorded:** 2026-08-29 (verified live).
**Author:** Zarlino Executive OS.

## Context
Zarlino previously defined a 7-step plan (dec_420964fb) to **launch an affiliate enrolling dashboard**. Execution on 2026-08-29 verified (all FACT):
- ZScorch and ZTame are **SHIPPED** (live, zarlinoaudio.com, /plugins 200).
- **cash = $1.60**; revenue = **$0** (Paystack: 5 transactions, all abandoned/failed).
- The falsifiable hypothesis (Y = 3 affiliate signups, CAC < $15 within 60 days) was **NEVER TESTED**: t0 was never set; the enrolling dashboard was not instrumented/launched; the 5–10 affiliate test cohort was never enrolled (recruitment SEND gated on KV binding + ratified commission policy + >=1 paid conversion — none cleared); effective cohort = 0; measurement = $0 revenue.
- A live `/affiliates` page/edit had previously been exposed without authorization and required a retro/rollback decision (apr_28dde06a); automated affiliate persistence also depended on a KV binding (apr_62408eeb) and a ratified commission policy — all still blocked.

## Decision
**Reject the affiliate platform build.** Do NOT build, deploy, or expose the affiliate enrolling dashboard / platform at this stage. No cash is spent and no engineering hours are allocated to building the affiliate platform. The affiliate channel remains effectively UNINVESTED until a clearly positive, data-backed go/no-go is established against a **paid, revenue-generating** product.

Consistent with:
- **D-012** — prioritise generating cash over expansion at this early stage; an unproven $0-revenue platform is a cost center, not a revenue path.
- **D-003** — no spend without a falsifiable expected outcome; no hypothesis ever passed, and none can be supported with zero test data.
- **D-008** — prefer reversible experiments; a manual referral tracker is the reversible, $0 instrument that exists today (affiliate/referral-tracker.csv + .md) and requires no platform build.
- **D-007 / D-002** — protect cash and optimize only against measured bottlenecks; the measured bottleneck is paid conversion (0/3940 GHS cart intent), not a missing affiliate dashboard.

## Rationale
1. **Zero test data.** The affiliate enrollment dashboard's premise was to validate an affiliate channel; that validation never ran. There is no evidence the channel (or the platform) would generate adequate ROI, so building the platform now would be spending engineering hours on an unproven, $0-revenue cost center (D-012, D-003).
2. **Cash constraint.** cash = $1.60 [FACT]. Any non-zero spend is protected by D-007/D-003. Accelerating a full platform build without a positive signal would trade scarce runway for unverified upside.
3. **Preconditions for a meaningful test were never met** (KV bound, commission policy ratified, >=1 paid conversion) — and the paid-conversion precondition depends on the signing/live-release path (Gate 7 + 10), which is the real revenue bottleneck.
4. **Reversibility (D-008).** Rejecting the build is fully reversible: no infrastructure is destroyed (nothing was built), the manual referral tracker is preserved, and the channel can be revisited under the revisit triggers below. Building the platform would be the larger, harder-to-reverse commitment.
5. **The affiliate "test cohort" concept is retained, not killed.** The record does not permanently abandon affiliate marketing; it rejects *building the platform now* while unproven.

## What is NOT included
- No affiliate dashboard, affiliate enrolling flow, or `/affiliates` automation is to be built/exposed.
- No cash commitment and no engineering-hours budget for the affiliate platform.
- Does NOT delete the manual referral tracker (affiliate/referral-tracker.csv, affiliate/referral-tracker.md).
- Does NOT permanently abandon the affiliate acquisition channel as an idea — it defers it (see triggers).

## Revisit triggers (when this rejection should be reconsidered)
Reopen the affiliate-platform question **only** when at least one of these is met, and then re-run the falsifiable-hypothesis gate (Y = 3 signups, CAC < $15 within 60 days) with a real enrolled cohort:
1. **A paid, revenue-generating product exists** — a completed paid conversion (revenue > 0) and a live, gate-passed release path (Gate 7 Security/Licensing + Gate 10 Human Release) — so affiliate commissions can actually be paid and the channel can demonstrably monetize.
2. **The preconditions clear** — Cloudflare KV bound (ZARLINO_KV), a ratified commission policy, and >=1 real paid sale — enabling a true 60-day instrumented test.
3. **A Founder-level positive go/no-go on running the 60-day test** is given explicitly (the decision to arm a real, measured cohort), with the enrolled cohort instrumented from a set t0.
4. **Measured inbound demand justifies it** — e.g. organic affiliate/referral or partnership interest signals a real channel before any further build.

Until a trigger is met, the default remains: **no affiliate platform build, no spend, no engineering hours** (D-007/D-012/D-003).

## Archive
This record is archived alongside companion artifacts in the private `Zarlino` repo (`affiliate/` manual referral tracker + 60-day protocol nt_d84eac30) and the executive decision/risk register (risk_1da468cb closed; dec_29d3135e record). No affiliate build artifact beyond documentation exists.

---
*Recorded by Zarlino Executive OS under delegated authority. A Founder decision on the future of the affiliate channel is separately pending (apr_60e75ef6) for the broader go/no-go; this record documents the platform-build rejection specifically.*