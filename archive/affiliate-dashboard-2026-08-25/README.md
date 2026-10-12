# Archive — Affiliate Dashboard Notes & Plans (2026-08-25)

**Decision:** Reject affiliate platform build — dec_29d3135e-4330-4b20-b6b0-ac1e29c498d9
**Archive step:** Step 3/5 of the "Reject affiliate platform build" decision flow.
**Created:** 2026-08-29 (archive date 2026-08-25 refers to the original spec date).
**Author:** Zarlino Executive OS.
**Status:** ARCHIVED — context preserved for future reference; NO action taken. The affiliate
platform (enrolling dashboard + `/affiliates` automation) build remains **REJECTED** (see
`decisions/dec_420964fb-affiliate-dashboard.md`).

## Purpose

Preserve the affiliate dashboard notes/plans (design spec, manual referral tracker SOP and
spreadsheet) so the context is retained **without building or exposing** any affiliate platform.
Per the decision record dec_29d3135e: zero cash spent, zero engineering hours allocated to the
affiliate platform, and the affiliate channel remains uninvested until a positive, data-backed
go/no-go exists against a paid, revenue-generating product (D-012 / D-003 / D-007 / D-002 / D-008).

This archive records *why* the affiliate dashboard was considered, what was designed, and the
state at the time of the rejection — so it can be revisited under the decision's revisit triggers
without losing the design work.

## Contents

| File | Source | Description |
|------|--------|-------------|
| `affiliate-program-spec.md` | original `specs/affiliate-program.md` | Authoritative design spec for the affiliate dashboard + enrolling flow (status DRAFT/DESIGN; never built to production). |
| `referral-tracker.md` | original `affiliate/referral-tracker.md` | Manual referral-tracker SOP (Step 5/7 of the superseded dec_420964fb) — the $0, reversible instrument kept active. |
| `referral-tracker.csv` | original `affiliate/referral-tracker.csv` | Shared spreadsheet: one row per affiliate candidate (10 seeded, outreach HOLD). |

## Why archived (not deleted)

- The manual referral tracker is **retained** (not killed) per the decision record — it is the $0
  reversible instrument that works today and costs no engineering hours.
- The design spec is preserved so the channel can be revisited under the decision's revisit
  triggers without re-deriving the data model / flow / commission policy.
- Reversible: no build artifact exists; nothing was created beyond documentation.

## State at archive time (all FACT, verified 2026-08-29)

- cash = $1.60; revenue = $0 (Paystack: 5 transactions, all abandoned/failed).
- Falsifiable affiliate hypothesis (Y = 3 signups, CAC < $15 in 60 days) NEVER TESTED (t0 unset,
  cohort 0, $0 revenue).
- Affiliate platform build REJECTED; no cash spent; no engineering hours allocated.
- Associated notes retained in the Executive register: rejected-platform decision record,
  paused/closed measurement ledger `nt_d84eac30`, hypothesis note `nt_a77f4569`, 60-day HOLD
  `nt_1a5e2867`, recruitment package `nt_5037ac79` (SEND held on reset gate).

## Related decisions / records

- `../decisions/dec_420964fb-affiliate-dashboard.md` — the Step 2/5 decision record (reject build).
- dec_420964fb (superseded "Launch affiliate enrolling dashboard") — the original 7-step plan.

*Archived by Zarlino Executive OS under delegated authority. No action taken; documentation only.
Reversible: delete the archive directory to undo (D-008).*
