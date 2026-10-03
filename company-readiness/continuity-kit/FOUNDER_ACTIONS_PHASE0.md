# Founder Actions — Phase 0 (things only you can do)

Everything else in Phase 0 is already shipped (see the audit file). These are the items that need
your hands — accounts, cards, identity. Work top to bottom; nothing here needs coding.

Track: mark done with date.

## 1. Money failsafe
- [x] **DeepSeek account created (2026-10-03)** — new account on `zarlino001@gmail.com` (the old one
      was unrecoverable). Payment flow **tested live**: GCB virtual card charged $2.12 → $2.00 credit
      recorded (the earlier $100 attempt was cancelled and never charged). Balance alert enabled at
      **$5** (emails the account address automatically).
- [x] **New API key created + propagated (2026-10-03)** — `zarlino-exec-os-prod`; pushed to the OS,
      realestate-os and songwriting-studio Workers + all local `.dev.vars`. Verified live: Doctor
      healthy (0 fail) and a real chat call returned `ALIVE-OK`.
- [x] **OS low-balance sentinel (2026-10-03)** — the OS reads the live DeepSeek balance every
      15 minutes; below the $10 line it alerts through BOTH channels (WhatsApp + ntfy), at most once
      per 24h. Proven live at $1.99. (DeepSeek has no auto-recharge API; this + the saved card is the
      closest achievable "top up without me".)
- [ ] **Fund the card, then do the real top-up** (~$100–150 ≈ 2–3 months at current caps). The test
      card had ≈ GHS 65 and now sits near **GHS 40** — top the card up in the GCB app first.
      (Google Pay is NOT usable: its payment sheet loads with an empty merchant id and never renders.
      Use the card form — it works.)
- [ ] **Backup card**: add a second payment method to Cloudflare billing (and Azure if active).
      One card is currently the root failure point for domain + workers + AI.
- [ ] **Check the domain registrar + auto-renew**: Cloudflare dashboard → Domain Registration (or the
      registrar listed there). Confirm auto-renew is ON. You selected "keep laptop, revisit VM" —
      nothing to do for Azure today.

## 2. Second alert channel (2 minutes)
- [ ] Install the **ntfy** app on your second device (iOS/Android/browser at ntfy.sh).
- [ ] Subscribe to topic: `zarlino-alerts-h7k2mq4x`
      (A test ping was sent 2026-10-02; if you subscribe later and see nothing, ask me to send another.)
- [ ] Optional: bookmark `https://ntfy.sh/zarlino-alerts-h7k2mq4x` on your desktop browser as a web fallback.

## 3. Cloudflare API token — DONE 2026-10-03 (one optional re-rotation)
- [x] Token `zarlino-exec-os` created (Workers Scripts/KV/R2: Edit · Account Settings: Read ·
      Zone zarlinoaudio.com: Zone Read + DNS Edit + Cache Purge). Wired to the Worker secret and
      local `.dev.vars`; verified live — `cloudflare.list_zones` returned the zone from the OS.
- [ ] OPTIONAL (2 min, recommended if you ever export/share logs): re-rotate — create a new token
      with the same scopes named `zarlino-exec-os-2`, then run
      `powershell -ExecutionPolicy Bypass -File "C:\Users\khrit\Desktop\plugin\company-readiness\scripts\set-cf-token.ps1"`
      (paste it there; it never passes through chat), then delete the old `zarlino-exec-os` token
      in the dashboard.

## 4. DeepSeek key — create + propagate the NEW account's key
- [x] Old key with the unrecoverable account: superseded (the account was replaced entirely 2026-10-03).
- [ ] **Create the key**: platform.deepseek.com → API Keys → Create new API key → name
      `zarlino-exec-os-prod` → **copy it immediately** (it is shown once).
- [ ] **Run the propagation script** (reads the key from YOUR paste; it never passes through chat):
      `powershell -ExecutionPolicy Bypass -File "C:\Users\khrit\Desktop\plugin\company-readiness\scripts\set-deepseek-key.ps1"`
      It updates all local `.dev.vars` files and pushes the Worker secret to the OS, realestate-os,
      and songwriting-studio.
- [ ] If the collaborative-mql5-ide tool is ever used again, give it the new key via its environment
      (it is env-only now — no code change needed).

## 5. Prove the money path end-to-end (30 min)
- [ ] Buy ZTame on zarlinoaudio.com with a real card (smallest currency amount) using YOUR email.
- [ ] Confirm: redirected success page shows the licence key; the plugin accepts the key.
- [ ] Then request a refund yourself at support@zarlinoaudio.com and process it (Paystack dashboard →
      Transactions → Refund). This tests the only two money paths customers will ever use.
- [ ] Recreate the website admin token copy (the old file is gone):
      `cd C:\Users\khrit\Desktop\plugin\zarlino-website` → `npx -y wrangler@4 secret put ADMIN_TOKEN`
      (generate a fresh random value) → store it in your password vault, not only on disk.

## 6. Offsite copies (the part that survives a dead laptop)
- [ ] After tonight (task "Zarlino Nightly Backup", 02:30), copy
      `C:\Users\khrit\Desktop\_zarlino_backups\<date>\` to an **encrypted** USB stick or an encrypted
      cloud folder. **It contains secrets** (`.dev.vars`, WhatsApp session) — treat it like a password.
- [ ] Decide where the second copy of the Continuity Kit lives (see `KIT_BUILD.md`).

## 7. Vault + custodian (the 30-minute version) — you chose "structure now, name later"
- [ ] Create a password-manager vault (Bitwarden free / 1Password / KeePassXC):
      add Google recovery + 2FA backup codes, Cloudflare, GitHub, Paystack, DeepSeek, Azure, registrar,
      Windows login, and the ntfy topic.
- [ ] When you name the custodian: give them (a) vault emergency access, (b) the sealed kit copy,
      (c) the steps in `KIT_BUILD.md`. Target: they can log in and keep the store running within a day.

## 8. Confirm two unknowns (5 minutes, write the answers in ACCOUNTS_JOURNAL.md)
- [ ] Where does **support@zarlinoaudio.com** actually deliver? (Ask me to check if it's a Cloudflare
      route or a forwarded mailbox — if it's a Gmail forward only, add a second destination.)
- [ ] Who is the **domain registrar** for zarlinoaudio.com (Cloudflare Registrar vs third party)?

## 9. Outreach pipeline (built 2026-10-03 — sending LIVE via Cloudflare Email)
- [x] Email provider: **Cloudflare Email Sending** (no third party). Binding `SEND_EMAIL`
      deployed; first send to the self-test prospect succeeded 2026-10-03 (`{"ok":true}`);
      `providerConfigured: true` confirmed live.
- [x] Smoke email received at the routed inbox (founder-confirmed 2026-10-03).
- [x] Cycle switched ON 2026-10-03 (`enabled: true`, live) — the 6-hour sweep sends ≤20/day.
- [x] Load the first reviewed prospects — **Batch 1 SENT 2026-10-03**: 6 intro emails
      (PML via the automatic sweep + 5 via one bounded cycle) — 0 failures; funnel CONTACTED=7.
      Keep adding: founder-supplied names + verified public business contacts only.
- [ ] Fund the card, then the **real DeepSeek top-up** (~$100-150) — the outreach/agent
      work needs headroom beyond the $2 smoke balance.
- Deploys: if wrangler asks for login, use the scoped token —
      `$env:CLOUDFLARE_API_TOKEN = (value from .dev.vars)` before `npx wrangler deploy`.
- See `../MARKETING_AND_AFFILIATE_PLAYBOOK.md` for the full operating manual.

## 10. YouTube prospect sourcing — DONE 2026-10-03
- [x] YouTube Data API v3 enabled (Google Cloud, project `My First Project`) + API key created,
      wired via `set-youtube-key.ps1` (`YOUTUBE_API_KEY` secret — verified live).
- [x] First sourcing sweep run — 5 public-contact candidates queued and batch 1 sent (see §9).
      Tools: `youtube.search_channels`, `youtube.channel`, `youtube.prospect_candidates`
      (public description contacts only — always a review list).

## 11. WhatsApp bridge (founder channel) — restarted on Baileys 7 (2026-10-03)
- The bridge was flapping (LINKED↔408 every few minutes) on Baileys 6.7 — the classic cause of
  sends showing **"Waiting for this message"**. Upgraded to 7.0.0-rc14 and restarted via
  `tools\whatsapp-bridge\start-bridge.vbs` (auth preserved — no re-pair needed).
- [ ] Sanity check: send any message to the business line — the auto-reply must be readable
      (no "Waiting for this message"). If it still appears: delete `auth\`, restart the bridge,
      scan the QR from http://localhost:3037/ .

## 12. Autonomous reply handling — built 2026-10-03 (one dashboard click left)
- When a prospect replies, it now routes itself: inbound-mail worker → OS
  `/api/outreach/reply` → intent classified → **opt-outs enforced** (UNSUBSCRIBED, never
  contacted again) → founder notified. Verified live with a simulated reply (`matched:true`).
- [ ] **One-time dashboard setup:** Cloudflare → zarlinoaudio.com → **Email** → **Email Routing**
      → **Routes** → the `support@zarlinoaudio.com` rule → **Edit** → action **Send to a Worker** →
      select **zarlino-inbound-mail** → **Save**. (Mail still reaches your inbox — the worker
      relays it to the OS first, then forwards.)
- [ ] Tell me where support@ forwarded BEFORE this change — if it was not `zarlino001@gmail.com`,
      I'll update the worker's `FORWARD_TO` and redeploy in ~1 minute.
- Verify afterwards: reply to the old smoke email — it must reach your inbox AND be classified
  in the OS (REPLIED; a plain "no" → UNSUBSCRIBED).
- Note: `INTERNAL_SECRET` was **rotated** 2026-10-03 (fresh 48-char value set on both workers +
  local `.dev.vars`). Nothing else referenced the old value.

## 13. Autonomous LLM nurturing — built 2026-10-03 (nothing to do; it is already on)
- **Nurturing is the LLM's job now, not yours.** When a prospect replies INTERESTED or asks a
  QUESTION, the growth agent drafts the answer with DeepSeek (grounded in the real price/terms
  facts), runs it through a **policy gate** (length, no guarantees, no AI branding, must mention
  Zarlino), and sends it as `support@zarlinoaudio.com`. A failing draft is **never sent** —
  it escalates to you with a notification instead.
- **Follow-ups**: prospects who went quiet ≥5 days after the last touch get one follow-up,
  once per person, sharing the same daily cap as cold sends.
- **Live proof**: a simulated reply from your Gmail (`"Yes, please send me the license
  details!"`) produced `{\"intent\":\"INTERESTED\",\"status\":\"REPLIED\",\"autoReply\":{\"sent\":true}}`
  — check that Gmail inbox: the company answered by itself.
- **Loop/abuse guards**: never replies to itself, to unknown senders, to auto-responders,
  to opted-out prospects, or twice to the same prospect; hard daily cap (10 replies).
- **Controls** (founder API, no deploy needed): `growth_auto_reply_enabled = \"0\"` disables
  LLM replies · `GROWTH_REPLY_DAILY_CAP` (10) · `FOLLOWUP_AFTER_DAYS` (5).
- **You only see exceptions**: escalations (draft blocked/failed) and the daily report's
  `recentReplies` / `autoRepliesToday`. Zero inbox work required.

## Done by the AI on 2026-10-02 (no action needed)
- Sales opened (SALES_ENABLED=true), stale "free until Aug 25" promo removed from every page.
- Legal v1 pages live: /legal/privacy, /legal/terms, /legal/license, /legal/refunds; footer wired.
- EULA added to the ZTame installer (applies to the next installer build).
- 15-minute external monitor (website, OS, downloads, WhatsApp bridge) + ntfy channel (live).
- Daily cloud backup of the OS database to R2 (verified), kept 30 deep.
- Nightly local backup task (git bundles + uncommitted work + secrets), 14-day retention (verified).
- `INTERNAL_SECRET` hardcode removed (now a rotated Worker secret).
- 2026-10-03: **DeepSeek migrated** to the new company account (key propagated to all workers,
  Doctor healthy, live chat proven); **low-balance sentinel** live (alerts < $10 on both channels).
- 2026-10-03: **affiliate outreach pipeline shipped** (prospects v35, email integration, capped daily
  cycle, tools + founder API, playbook) — kill switch OFF until the email provider is wired.
- 2026-10-03: all work committed + pushed to GitHub in all three repos (offsite).
