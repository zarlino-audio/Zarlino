# Marketing & Affiliate Outreach Playbook (v1 — 2026-10-03)

The company must raise its own money. Sales are open; this playbook is how revenue
starts flowing **without the founder**: a capped, auditable cold-outreach pipeline
that recruits affiliates (25% commission) among producers and audio educators.

## 1. The machine (what is already built)

| Piece | Where |
|---|---|
| Prospect queue (NEW → CONTACTED → REPLIED → APPLIED → APPROVED / REJECTED / UNSUBSCRIBED) | OS store, migration v35 (`prospects`) |
| Every email we send (subject/status/error/time) | `outreach_touches` (audited) |
| Email sending (Brevo or Mailjet over HTTPS) | `src/integrations/email.ts` (EMAIL_* config) |
| The daily cycle (bounded, kill-switchable) | `runOutreachCycle` — runs on the OS maintenance sweep **only** when enabled |
| Founder API | `POST /api/outreach/prospects` · `PATCH /api/outreach/prospects/:id` · `POST /api/outreach/send` · `POST /api/outreach/cycle` · `POST /api/outreach/config` · `GET /api/outreach/report` |
| Agent tools | `outreach.list_prospects` · `outreach.add_prospect` · `outreach.send` · `outreach.report` |

Hard rules enforced in code, not vibes:
- **Kill switch**: `outreach_enabled` setting (default OFF). Nothing sends until flipped.
- **Daily cap**: `OUTREACH_DAILY_CAP` (default 20) across manual AND automatic sends.
- **No fabrication**: no email → prospect is skipped and flagged, never guessed.
- **Audit**: every send/failure/config change lands in the audit log.

## 2. Who we contact (ICP — in order of fit)

1. **Afrobeats / Amapiano / highlife producers & channels** — our home scene; ZTame's
   vocal-sibilance and room-resonance use cases map directly. Strongest warm intro story.
2. **FL Studio-focused creators** — ZTame/ZScorch are Windows VST3; FL Studio is the
   dominant Windows DAW in the community, and A/B content performs well.
3. **Mixing/mastering educators** (YouTube/TikTok/IG) whose content is "fix harshness,
   control resonances" — ZTame is a direct demo topic.
4. **Beat-selling / template creators** with audiences of paying producers — they convert
   tool recommendations into affiliate clicks.
5. **Mixing engineers with newsletters/blogs** — small but high-intent audiences.

Avoid: generalist music channels with no production workflow, non-Windows-primary creators
for v1, and anyone whose contact email isn't a public business inquiry address.

## 3. How we source prospects (honest sourcing only)

- Public business emails only: channel "About/Contact" pages, linked websites, public
  press kits. Never from private sources or scraped databases.
- Record per target: name, platform, handle, public email, URL, why-they-fit (notes).
- Quality bar: **one line of notes per prospect stating why they fit** — if you can't write
  it, don't add them.
- First batches: 20–40 prospects per week, added via the API (bulk list prepared by the OS
  team from public pages, reviewed by the founder before sending).
- Tooling: `youtube.search_channels` / `youtube.channel` / `youtube.prospect_candidates`
  (needs the `YOUTUBE_API_KEY` secret; mines **only** public channel-description contacts and
  returns a review list — nothing is auto-added or auto-sent).

## 4. The message (already written, in code)

Two templates live in `src/core/outreach.ts` (`affiliate_intro`, `follow_up`):
- Short, personal, one product (ZTame), one ask (free NFR license + 25% affiliate).
- Explicit opt-out: `reply "no"` + support address. No hype, no AI-trace wording.
- Follow-up only via the same thread, not before ~5 days (manual for now).

Rules: no attachments, no tracking pixels, no fake personalisation, no more than
one follow-up without a reply. Any "no" → status `UNSUBSCRIBED` same day.
Follow-ups are now automated (see §6b): once per prospect, ≥5 days after the last touch.

## 5. Email provider — DONE via Cloudflare Email Service (2026-10-03)

Outreach sends through the **Cloudflare Email Sending binding** (`[[send_email]]`
`SEND_EMAIL` in `wrangler.toml`) as `support@zarlinoaudio.com`. The zone already
carries the Email Service MX/DKIM/SPF records, and DMARC is `p=reject`.
First live send verified (self-test prospect, 2026-10-03: `{"ok":true}`, audited,
counted against the daily cap).

Notes:
- Provider switch: `EMAIL_PROVIDER = "cloudflare"` — no API key required.
- Fallbacks remain wired in code (`brevo` / `mailjet` via `EMAIL_API_KEY` secret)
  if Cloudflare Email Sending is ever unavailable on the plan.
- Deploys no longer need interactive login: set `CLOUDFLARE_API_TOKEN` from
  `.dev.vars` (the scoped token) before `npx wrangler deploy`.

## 6. Enable sequence — DONE through step 3 (2026-10-03)
1. **[x] Smoke test** — delivered to the founder inbox (Cloudflare Email Sending proven end-to-end).
2. **[ ] Load batch 1** — 20–40 reviewed prospects via `POST /api/outreach/prospects`.
3. **[x] Switch on** — `{"enabled": true}` live; the 6-hour maintenance sweep sends ≤20/day, stops at the cap.
4. **[ ] Watch the funnel** — `GET /api/outreach/report` daily (`recentReplies`, `autoRepliesToday`).
   Replies are answered by the growth agent; the founder only reads **escalations**.

### 6b. Reply handling **and nurturing** are autonomous (2026-10-03)
Inbound mail for support@ is relayed by the `zarlino-inbound-mail` worker to
`POST /api/outreach/reply`: every reply is stored, intent-classified
(OPT_OUT / INTERESTED / QUESTION / OTHER), opt-outs flip the prospect to
UNSUBSCRIBED instantly (they can never be contacted again — enforced in code),
and the founder is notified through the usual channel.

**The growth agent answers them** (`src/integrations/growth-ai.ts` +
`maybeAutoReply` in `src/core/outreach.ts`):
- **Draft**: DeepSeek (`deepseek-chat`, temp 0.4) grounded in `GROWTH_FACTS` — the price
  (free NFR license + 25% affiliate), payout route, product facts. No tools, no web.
- **Policy gate before send** (`checkReplyPolicy`): length 20–1600 chars, no placeholders,
  no guarantees, no AI/vendor branding, must mention "Zarlino" — a failing draft is
  **not sent**; it escalates to the founder instead.
- **Skips by design** (loop/abuse guards): outreach disabled, OPT_OUT, unknown sender,
  our own address, auto-responder headers, prospect already REPLIED, daily cap reached.
- **Follow-ups**: silent CONTACTED prospects get one follow-up ≥5 days after the last touch,
  using the same template + optional opt-out, sharing the *same* daily cap as cold sends.
- **Caps**: `GROWTH_REPLY_DAILY_CAP` (default 10, cluster var) · `FOLLOWUP_AFTER_DAYS` (default 5).
- **Kill switch**: setting `growth_auto_reply_enabled = "0"` disables auto-replies
  (follow-ups stop with the main `outreach_enabled` switch).
- **Audit**: every draft/auto-reply/follow-up lands in `outreach_touches`
  (templates `auto_reply`, `follow_up`) and auto-replies show in the daily report.

Live proof (2026-10-03): simulated reply from the founder Gmail
(`"Yes, please send me the license details!"`) → `{"intent":"INTERESTED","status":"REPLIED",
"autoReply":{"sent":true}}` — the LLM-drafted answer landed in that inbox, sent by the
company itself. `autoRepliesToday=1`.

## 7. KPIs (weekly review)

- Prospects added · emails sent · reply rate (target ≥ 8–10% for niche cold outreach)
- Affiliate applications (site `/affiliates` + `/affiliate-report`) · approved affiliates
- License sales (Paystack) · revenue vs DeepSeek/burn (the "raise its own money" line)
- Unsubscribes (keep < 2%) — if higher, tighten the ICP and slow the ramp.

## 8. Standing safety rails

- Cap starts at 20/day (outreach) and 10/day (auto-replies); raise only after two weeks
  with reply rate ≥ 5% and zero complaints.
- Kill switches are one API call each (`{"enabled": false}` for all outreach;
  `growth_auto_reply_enabled = "0"` for LLM replies); both survive restarts.
- The system never invents an address, never emails a "no", and logs everything;
  any unverified claim from the LLM is blocked by the policy gate, not by review.
- Deliverability: if bounces > 5%, pause and clean the list before continuing.

## 9. What "the marketing team" is (reality check)

Today it is: this pipeline + the growth agent (sourcing, sending, replying, following up)
+ the founder auditing escalations and exceptions. Phase 3 adds a human community/support
contractor — this playbook is their onboarding.
Until then, autonomy means: **the machine sources, sends, answers, and follows up;
the founder only audits.**
