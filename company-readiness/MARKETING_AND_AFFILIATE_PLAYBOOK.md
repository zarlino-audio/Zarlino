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

## 4. The message (already written, in code)

Two templates live in `src/core/outreach.ts` (`affiliate_intro`, `follow_up`):
- Short, personal, one product (ZTame), one ask (free NFR license + 25% affiliate).
- Explicit opt-out: `reply "no"` + support address. No hype, no AI-trace wording.
- Follow-up only via the same thread, not before ~5 days (manual for now).

Rules: no attachments, no tracking pixels, no fake personalisation, no more than
one follow-up without a reply. Any "no" → status `UNSUBSCRIBED` same day.

## 5. Email provider setup (the one missing piece — 3 minutes)

Recommended: **Brevo free tier** (300 sends/day) — no domain DNS needed for v1:
1. Sign up at brevo.com with `zarlino001@gmail.com`.
2. Senders → add + verify sender `support@zarlinoaudio.com` **if that inbox actually
   receives mail** (confirm its delivery target first!). Otherwise verify
   `zarlino001@gmail.com` and we'll start from there.
3. SMTP & API → API Keys → create key → copy it.
4. Tell the OS side (me): run the clipboard wiring — `wrangler secret put EMAIL_API_KEY`
   for the exec-os Worker (+ the same key value updated locally in `.dev.vars`).
5. Verify: `GET /api/outreach/report` → `providerConfigured: true`.

(Alternative: Mailjet — same flow, key format `APIKEY:SECRET`.)

## 6. Enable sequence (the moment the key is wired)

1. **Smoke test**: `POST /api/outreach/send {"prospectId": <self-test id>}` → the
   self-test prospect (`support@zarlinoaudio.com`) receives the intro email. Check inbox.
2. **Load batch 1**: 20–40 reviewed prospects via `POST /api/outreach/prospects`.
3. **Switch on**: `POST /api/outreach/config {"enabled": true}` — the maintenance sweep
   now sends ≤20/day automatically, and stops at the cap.
4. **Watch the funnel**: `GET /api/outreach/report` daily; founder reviews REPLIED.
   Replies go to `support@zarlinoaudio.com` — they must be answered within 24h.

## 7. KPIs (weekly review)

- Prospects added · emails sent · reply rate (target ≥ 8–10% for niche cold outreach)
- Affiliate applications (site `/affiliates` + `/affiliate-report`) · approved affiliates
- License sales (Paystack) · revenue vs DeepSeek/burn (the "raise its own money" line)
- Unsubscribes (keep < 2%) — if higher, tighten the ICP and slow the ramp.

## 8. Standing safety rails

- Cap starts at 20/day; raise only after two weeks with reply rate ≥ 5% and zero complaints.
- Kill switch is one API call (`{"enabled": false}`); it also survives restarts.
- The system never invents an address, never emails a "no", and logs everything.
- Deliverability: if Brevo shows bounces > 5%, pause and clean the list before continuing.

## 9. What "the marketing team" is (reality check)

Today it is: this pipeline + the OS agents (tools above) + the founder's judgment.
Phase 3 adds a human community/support contractor — this playbook is their onboarding.
Until then, autonomy means: **the machine sends; the founder only reads replies.**
