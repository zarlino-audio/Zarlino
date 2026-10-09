---
name: zarlino-sales-growth-ops
description: Run Zarlino Audio's sales and growth machine — sourcing prospects, cold outreach under caps, affiliate recruiting, reply handling and nurture. Use this whenever the user asks who to contact, whether to send, how the funnel looks, why outreach stopped, how a reply should be answered, or how to grow revenue through affiliates/partners. Trigger for "source prospects", "send outreach", "who replied", "affiliate", "funnel", "outreach report", "why is outreach off", or any question about turning attention into a paying customer, even if the words "sales" or "growth" never appear.
---

# Zarlino Sales & Growth Operations

The machine already runs on its own: it finds prospects, emails them, answers replies with an
LLM, and follows up. **Your job is to keep it honest and fed, not to send email by hand.**

## 1. The pipeline (schema v35/36)

`prospects` → `outreach_touches` → `prospect_replies`.

- Statuses: `NEW` → `CONTACTED` → `REPLIED` → `APPLIED` → `APPROVED` / `REJECTED` / `UNSUBSCRIBED`.
- Every send/failure is audited in `outreach_touches` — a claim about a send is only real if a
  touch row says so.
- `UNSUBSCRIBED` and `REJECTED` are **hard blocks**: `sendOutreachNow` refuses them forever.
  Never suggest "just one more follow-up" to an opted-out prospect.

## 2. Sourcing rules (non-negotiable)

- Public **business** contacts only: channel About/Contact pages, press kits, public sites.
  Never scraped databases, never private data.
- One line of `notes` stating why this person fits. If you cannot write it, do not add them.
- `youtube.prospect_candidates` mines public channel descriptions and returns a **review list** —
  nothing is auto-added. Dedupe by email is enforced (409 on duplicates).
- Self-test rows (`source = self-test*` / `smoke-test`) are excluded from the funnel; the report
  shows them separately as `testProspects`.

## 3. Sending rules

- `outreach_enabled` is the master kill switch (setting, default OFF in code — live it is ON).
- Daily cap `OUTREACH_DAILY_CAP` (20) covers manual **and** automatic sends.
- Templates live in `src/core/outreach.ts`; they carry a real opt-out line. Never strip it.
- `outreach.send` … when in doubt read `GET /api/outreach/report` first, then send.

## 4. Replies and nurture (autonomous)

- Inbound mail → `zarlino-inbound-mail` worker → `POST /api/outreach/reply` → intent
  (`OPT_OUT` / `INTERESTED` / `QUESTION` / `OTHER`).
- `OPT_OUT` wins over everything: instant `UNSUBSCRIBED`.
- INTERESTED/QUESTION replies are answered by the **growth agent** (DeepSeek draft + policy gate:
  20–1600 chars, no guarantees, no AI branding, must mention Zarlino). A failed draft escalates to
  the founder instead of sending.
- Follow-ups: silent CONTACTED prospects get **one** follow-up ≥5 days after the last touch
  (`FOLLOWUP_AFTER_DAYS`), sharing the same daily cap.
- Caps/kill switches: `GROWTH_REPLY_DAILY_CAP` (10), setting `growth_auto_reply_enabled = "0"`.

## 5. Affiliates

25% commission, free NFR licence, agreement on the site. An affiliate becomes real only when the
application is recorded and the licence is issued — talk is not a pipeline.

## 6. What "good" looks like

| Metric | Healthy | Action if not |
|---|---|---|
| Reply rate (cold, niche) | ≥ 8–10% | tighten ICP, re-read templates |
| Unsubscribes | < 2% | slow the ramp, check targeting |
| Funnel stalls | every CONTACTED touched ≤ 5 days ago | check the sweep actually ran |
| `autoRepliesToday` | > 0 when replies exist | check the policy gate / escalation log |

Report facts with numbers from the OS, never from memory.
