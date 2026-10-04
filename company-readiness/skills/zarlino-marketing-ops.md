---
name: zarlino-marketing-ops
description: Plan and run Zarlino Audio's marketing execution — site copy, waitlist and consented email campaigns, content calendar, brand voice, channel analytics. Use this whenever the user asks to promote something, write or change site copy, email a list, plan content, run a campaign, or question whether a marketing action is allowed. Trigger for "campaign", "waitlist", "newsletter", "site copy", "launch announcement", "brand voice", or any request that would say something to an audience on the company's behalf.
---

# Zarlino Marketing Operations

Two jobs: **decide what to say** (growth intelligence) and **say it without breaking trust**
(marketing execution). Never let the second job be an afterthought.

## 1. Hard rules

- **Consent before contact.** A waitlist signup is permission for a specific promise, not a
  licence to mail forever. Without recorded consent, mailing a list is an escalation, not a task.
- **No unverifiable claims.** No guarantees, no "best", no invented benchmarks, no fake
  testimonials, no countdowns that reset. The product's real facts live in `GROWTH_FACTS` and the
  site config — use them.
- **Every campaign carries an opt-out.** One click, honoured immediately, no "are you sure".
- **No fabricated numbers.** If you cannot measure it, you do not report it.
- The waitlist is currently **off by config** (`waitlistEnabled: false`) while the sending path is
  wired. Do not enable it as a side-effect of another change.

## 2. What exists today

| Surface | Where |
|---|---|
| Prospect email (cold + nurture) | `outreach.*` tools, growth agent (`zarlino-sales-growth-ops`) |
| Waitlist capture (KV `waitlist:<topic>:<email>`) | website → OS; count it with `marketing.waitlist_stats` |
| Campaign registry | `marketing.list_campaigns` / `marketing.get_campaign` / `marketing.create_experiment` |
| Site + sitemap + status | `website.status`, `website.sitemap_check` |
| Brand voice | the product copy itself — short, technical, no hype |

## 3. The honest gap (state it, don't paper over it)

There is **no bulk-campaign sender yet**. Before one exists, a campaign to the waitlist needs:
consent recorded at capture time, an unsubscribe path, per-send rate limits, and an audit row per
recipient. Until those exist, the answer to "email the whole waitlist" is: record the plan as a
campaign, prepare the copy, and escalate for the missing capability — do not improvise a loop of
`outreach.send` calls.

## 4. Campaign checklist

1. **Objective** written as a metric (signups, downloads, replies) — not "awareness".
2. **Audience** named exactly (list, size from `marketing.waitlist_stats`, consent basis).
3. **Copy** in Zarlino voice: one product, one ask, real numbers.
4. **Falsifiable claims checked** against current product facts.
5. **Opt-out + escalation path** stated before sending.
6. **After**: record actual outcome in the campaign record; if it under-performed, say so.

## 5. Things that always escalate

Pricing changes · promises about dates · anything touching money or refunds · sending to a list
whose consent basis you cannot name · public statements about competitors.
