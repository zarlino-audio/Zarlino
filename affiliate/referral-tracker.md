# Affiliate Referral Tracker (Manual, Shared Spreadsheet)

**Parent decision:** dec_420964fb (Launch affiliate enrolling dashboard) — **Step 5/7**
**Status:** ACTIVE (manual tracker engaged; automated/KV dashboard BLOCKED on Founder approvals)
**Cash:** $0 spent — cash unchanged at $1.60 [FACT] (D-007/D-012)
**Created:** 2026-08-29

## Purpose
Track referrals manually via a shared spreadsheet (this `referral-tracker.csv`, viewable/editable
in GitHub, or exported to Google Sheets / Excel). Companion to the automated dashboard which
remains BLOCKED (Founder approvals: KV binding `apr_62408eeb`, commission policy, source-of-truth
restore `apr_5d27f41a`, exposure decision `apr_28dde06a`, /plugins fix `apr_0398bfcf`, signing
`apr_429f042c`). The manual tracker is the instrument that works TODAY at $0 cost and is fully
reversible.

## Falsifiable hypothesis being measured (from step 4/7)
> Within 60 days of launching the affiliate enrolling dashboard, the affiliate cohort of 5-10
> generates **Y = 3 signups** at **CAC < Z = $15** — and cash stays ≥ $1.60.

**Success condition (this task):** ZScorch + ZTame shipped (MET — both SHIPPED v1.0.0), affiliate
test cohort 5-10 produces ≥3 signups at CAC<$15 within 60 days, cash remains ≥$1.60 (no spend).

## How to use (SOP)
1. **Row per affiliate/ref-code.** Each candidate/referrer gets one row (AFF_CAND_01..10 seeded from
   the confirmed candidate list + Zed Audio Reviewer if enrolled).
2. **Transition status** CANDIDATE → OUTREACH_SENT → APPLIED → APPROVED → ACTIVE, and on to
   PAUSED/REVOKED if needed. Log outreach_date / apply_date / approved_date.
3. **Issue a ref_code** on approval (format `REF-<NAME>-<NNN>`; matches the live `?ref=` param the
   worker already supports and the `cart_ref` stored in Paystack order metadata).
4. **Record funnel numbers** as they arrive: clicks, add_to_cart, cart_value (GHEs), checkouts,
   paid_orders, revenue (USD), signups. Pull `paid_orders`/`signups`/`revenue` from Paystack
   (`finance` sync / paystack tools) and attribute via `cart_ref` = ref_code.
5. **Log time/cost** in `cost_time_min` (manual outreach + admin) and `cost_cash` per D-003 so CAC
   is computed as (cash spend + time equivalent) / signups. Time is logged against the same book:
   record to the nearest 15 min.
6. **60-day window:** reset clock at dashboard launch (or at cohort go-live). Target dashboard
   launch + 60 ≤ 2026-12-16. Reopen/measure at day 30 and day 60.
7. **Escalate** if CAC approaches $15 without ≥3 signups (falsify → stop/pivot, D-003/D-008).

## Measurement notes
- `signups` = new account/license signups attributable to a ref_code (per the Y=3 definition).
- `revenue_usd` = completed paid orders attributable to the ref_code (source: Paystack).
- **CAC = (cost_cash + cost_time_min converted to $) / signups** — target < $15.
- This tracker is the source-of-truth UNTIL the automated dashboard + KV persistence is approved
  and live, then it becomes the manual fallback/audit trail.

## Where this lives
- Spreadsheet: `affiliate/referral-tracker.csv` (this repo, private, no deploy trigger).
- This SOP: `affiliate/referral-tracker.md`.
- Companion measurement spec: pinned note `nt_d84eac30` (Step 4/7) + `nt_a77f4569`.
- Recruitment package (SEND on reset gate): pinned note `nt_5037ac79` (Step 3/7).

## Files
- `affiliate/referral-tracker.csv` — the shared spreadsheet (one row per referrer).
- `affiliate/referral-tracker.md` — this SOP.

Zero cash spent. Reversible (delete the files/tag). No fabricated funnel data — blanks are UNKNOWN.
