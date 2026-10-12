# Zarlino Affiliate Program - Dashboard & Enrolling Flow Specification (ARCHIVED copy)

> **ARCHIVED COPY** - 2026-08-29, Step 3/5 of dec_29d3135e (Reject affiliate platform build).
> Original source: `specs/affiliate-program.md` (branch `feat/affiliate-dashboard-spec`, PR #5).
> Preserved verbatim for future reference. The affiliate platform build is **REJECTED** at this stage.

Status: **DRAFT / DESIGN** (not yet implemented on production)
Product owner: Zarlino Executive OS (Marketing)
Source: Founder task "Create affiliate dashboard and enroling flow"
Created: 2026-08-25
Confidence: SPEC (authoritative design, not yet live)

> **Implementation note.** The production website (zarlinoaudio.com) is served by a
> Cloudflare Worker (`zarlino-website`) whose source is not present in this repository and
> which has no write/deploy path available in the executive toolset. This document is the
> authoritative design for the affiliate feature. It is explicitly **NOT** exposed to partners
> until (a) the payout/commission policy is approved by the Founder and (b) a deploy path to the
> live worker is available. Evergreen principle: build the design now; go live only on Founder
> approval (D-009) and only after an approved commission policy (D-003).

---

## 1. Purpose

Give creators/influencers (YouTube, Instagram, blog/Patreon, Discord, audio educators) a
self-serve way to:

1. **Enroll** as an affiliate (apply, agree to terms, get an affiliate code + links).
2. **Track** clicks and qualified conversions from their links.
3. **See** earned commission and payout status in a private dashboard.

## 2. Non-goals (v1)

- No automatic payouts / no payout-provider wiring in v1 (manual or batched payout run).
- No public leaderboard or partner portal branding in v1.
- No multi-tier / sub-affiliate model in v1.
- No AAX or license-key distribution via affiliates in v1 (license delivery stays in checkout).

## 3. Core concepts / data model

| Entity | Fields | Notes |
|--------|--------|-------|
| `Affiliate` | affiliateId (uuid), name, email, website/social URL, country, applyReason, status (`applied`, `approved`, `rejected`, `suspended`), commissionRatePct, joinedAt, approvedAt | One row per partner. |
| `AffiliateLink` | linkId, affiliateId, productId (`ZTame`/`ZScorch`), slug, url, createdAt, active | Unique generated tracking link. |
| `Click` | clickId, linkId, affiliateId, ts, referrer, userAgentHash, campaign | Privacy-minimised: no raw email/IP; store hash only. |
| `Conversion` | convId, linkId, affiliateId, orderId, productId, amountUsd, commissionUsd, status (`pending`, `approved`, `paid`, `reversed`), occurredAt | Tied to a real checkout via `orderId`. |
| `PayoutRun` | payoutId, period, status, totalUsd, paidAt | Manual/approved batched run. |

Attribution window: **90-day attribution cookie** and **30-day conversion window**, both
configurable in one setting and ratified in the commission policy.

## 4. Commission policy (REQUIRES FOUNDER APPROVAL)

Proposed defaults - **to be ratified, not yet policy**:

- ZTame and ZScorch are currently **free/in beta**; commissionable only after a paid price exists.
- Proposed base commission: **25% of net sale** (excluding taxes and refunds).
- First-purchase only in v1 (no recurring).
- Refunded/chargebacked orders reverse commission.
- Minimum payout: **$50**; payout cadence: **monthly** (below-$50 balances roll).
- Partners must post an FTC-compliant disclosure on content using affiliate links.

## 5. Enrolling flow (public)

1. **Land** - affiliate landing (add a nav entry). Explains who it's for, commission, how
   tracking works, and links to Terms.
2. **Apply** - form: Name, Email, website/brand URL, primary channel (select), audience size
   (select), how they'd promote Zarlino. Explicit consent checkbox for Affiliate Terms + FTC
   disclosure obligation.
3. **Submit** - record `Affiliate{status=applied}`. Send confirmation (approval pending). No
   payout obligation is created at this step.
4. **Review** - Owner approves/rejects in Admin dashboard.
5. **Activate** - on approval, generate the affiliate code + a default `AffiliateLink` per live
   product, and email the links + a disclosure snippet.
6. **Go live** - partner places links; tracking begins.

## 6. Affiliate dashboard (private)

Route-less view inside the existing SPA, gated by the same session model as `/admin` (tokened),
scoped to one affiliate.

Affiliate views:
- **Overview**: lifetime clicks, conversions, approved commission, pending payout, status badge.
- **Links**: table of `AffiliateLink`s, copy button, active toggle.
- **Performance**: clicks + conversions + commission by product and date range.
- **Payouts**: payout history and outstanding balance vs $50 threshold.

Owner views:
- **Affiliates**: list with status filter; approve/reject/suspend.
- **Conversions**: match to orders; approve or reverse commission.
- **Settings**: commission rate, window, payout threshold/cadence.

## 7. Compliance & privacy

- Every affiliate link resolves through a `/<slug>` redirect page that (a) sets the attribution
  cookie, (b) renders `rel="sponsored nofollow"` on the outbound anchor, and (c) shows an
  FTC-compliant disclosure on the page introducing the link (visible, not JS-only).
- No raw customer PII (email/IP) stored on `Click`; use hash
