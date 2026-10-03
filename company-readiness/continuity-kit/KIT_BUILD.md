# Building the sealed Continuity Kit (custodian-ready in ~30 minutes)

Goal: if the founder is unreachable for a month, a trusted person can keep the company running:
storefront up, licences issuing, money moving, backups restoring, support answered.

## What the kit is

One **encrypted** archive (or vault folder) containing:

1. **Credentials + recovery codes** for: Google (root), Cloudflare, GitHub, Paystack, DeepSeek,
   Azure, domain registrar, Windows login. (From your password vault — see FOUNDER_ACTIONS step 7.)
2. **This repo's kit docs**: `ACCOUNTS_JOURNAL.md` + `FOUNDER_ACTIONS_PHASE0.md` + the main audit
   (`../FOUNDER_GAP_AND_RUNWAY_AUDIT.md`).
3. **The latest local backup day** (`Desktop\_zarlino_backups\<date>\`): git bundles, worktree
   snapshots, `.dev.vars` files, WhatsApp bridge session. Contains SECRETS.
4. **Bridge session export** (`scripts/export-bridge-session.ps1` → zip) so the WhatsApp channel can
   move to another machine without a QR scan.
5. **One-page custodian instructions** (write from the template below).

## How to seal it

- Create the archive, then encrypt with a strong passphrase (7-Zip AES-256, or KeePassXC database,
  or Bitwarden attachment in a vault with emergency access enabled).
- Store TWO copies in different places: e.g. a safe / lawyer / trusted person + an encrypted USB or
  encrypted cloud folder. Never a plain folder on a synced drive.
- Write the passphrase somewhere that survives you being unreachable — that is the whole point
  (e.g. a sealed envelope with the same trusted person as the physical archive).

## Custodian quick-start (the one-pager to include)

1. **Storefront**: Cloudflare dashboard → Workers → `zarlino-website`. If down: re-deploy from
   GitHub `zarlino-audio/zarlino-website` (Actions) or ask a developer with the kit.
2. **Support + refunds**: read `support@zarlinoaudio.com`; refund requests → Paystack dashboard →
   Transactions → Refund (14-day policy: see zarlinoaudio.com/legal/refunds).
3. **Licences**: if a buyer loses a key, re-issue from the admin dashboard (`zarlinoaudio.com/admin`)
   or email support.
4. **Alerts**: subscribe to ntfy topic `zarlino-alerts-h7k2mq4x` on your phone; the OS monitors the
   site every 15 minutes and alerts there.
5. **If the laptop dies**: any Windows machine can become the host in ~5 minutes —
   `zarlinoaudio.com/instance/` (ZarlinoInstanceSetup.exe); use the kit's bridge-session zip to
   re-link WhatsApp without a QR scan.
6. **Backups**: nightlies at `Desktop\_zarlino_backups\`; restore = `git clone <bundle>` + copy the
   worktree snapshot over it + restore `.dev.vars` into `zarlino-executive-os/`.
7. **Money**: top up DeepSeek when the Doctor says `provider.balance`; pay domain/Cloudflare from the
   card on file. Keep receipts.
8. **When the founder returns** (or permanently): change every password, rotate every token, and
   re-verify the accounts journal.

## Test before you need it (quarterly)

- [ ] Custodian logs into Cloudflare + GitHub (read-only) using the kit.
- [ ] A laptop other than the founder's can run `ZarlinoInstanceSetup.exe` and show fresh heartbeats.
- [ ] Restore drill: clone one git bundle and open the project.
- [ ] ntfy test alert arrives on the custodian's phone.

## Status 2026-10-03

- Kit docs: ✅ created (`ACCOUNTS_JOURNAL.md`, `FOUNDER_ACTIONS_PHASE0.md`, this file).
- Vault + recovery codes: ⬜ founder action (FOUNDER_ACTIONS §7).
- Custodian named: ⬜ deferred by founder — the structure above is ready; naming takes minutes.
- Local backup automation: ✅ nightly task "Zarlino Nightly Backup" (02:30, 14-day retention).
- Cloud backup: ✅ daily OS-database snapshot → R2, 30 deep.
