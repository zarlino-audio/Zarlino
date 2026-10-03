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

## 3. Rotate the Cloudflare API token (the old one has been invalid since September)
- [ ] dash.cloudflare.com → My Profile → API Tokens → Create Token → custom token with:
      Workers Scripts: Edit · Workers KV: Edit · R2: Edit · Account Settings: Read · Zone (zarlinoaudio.com): Read + DNS: Edit
- [ ] In a terminal (you type the value — it never passes through the AI):
      `cd C:\Users\khrit\Desktop\zarlino-executive-os`
      `npx wrangler secret put CF_API_TOKEN` ← paste when prompted
- [ ] Update the local copy too: edit `C:\Users\khrit\Desktop\zarlino-executive-os\.dev.vars`
      (CF_API_TOKEN=...) — the nightly backup will pick it up automatically.
- [ ] Verify: ask me to run the Doctor; `secrets.required` should stay green and the exec-os Cloudflare
      tools should stop returning 401.

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

## Done by the AI on 2026-10-02 (no action needed)
- Sales opened (SALES_ENABLED=true), stale "free until Aug 25" promo removed from every page.
- Legal v1 pages live: /legal/privacy, /legal/terms, /legal/license, /legal/refunds; footer wired.
- EULA added to the ZTame installer (applies to the next installer build).
- 15-minute external monitor (website, OS, downloads, WhatsApp bridge) + ntfy channel (live).
- Daily cloud backup of the OS database to R2 (verified), kept 30 deep.
- Nightly local backup task (git bundles + uncommitted work + secrets), 14-day retention (verified).
- `INTERNAL_SECRET` hardcode removed (now a rotated Worker secret).
