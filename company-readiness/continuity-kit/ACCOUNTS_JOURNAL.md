# Continuity Kit — Accounts Journal (v1, 2026-10-03)

Every account the company depends on: who owns it, what breaks without it, and how to recover it.
**This file contains NO secret values** — the values live in the password vault / sealed kit (see `KIT_BUILD.md`).
Status column uses: VERIFIED (checked this audit) · TO CONFIRM (founder must confirm in the dashboard).

Legend for "2nd access": does anyone other than the founder hold admin/recovery access today?
(None do — that is the single biggest founder-gap. See KIT_BUILD.md to fix in ~30 minutes.)

## The root of everything

| Account | What it powers | Owner | 2FA | 2nd access | If lost | Recovery path |
|---|---|---|---|---|---|---|
| **Google `zarlino001@gmail.com`** | Root login for Cloudflare, GitHub, Paystack, Azure, DeepSeek sign-ups — everything | Founder personal | UNKNOWN | None | Nearly the whole company | Google account recovery (phone/backup codes) + vault copy |
| **support@zarlinoaudio.com** | Customer support + licence re-issue + refund requests | Founder (forwarding target: TO CONFIRM) | — | None | Customers can't reach support | Verify the forward; add a second mailbox |

## Infrastructure

| Account | What it powers | Where it lives | 2FA | 2nd access | If lost | Recovery |
|---|---|---|---|---|---|---|
| **Cloudflare** — account `ae604b60…` (Zarlino001@gmail.com's Account) | Workers (Executive OS, website), KV (`ZARLINO_KV`), R2 (`zarlino-artifacts`), DNS for zarlinoaudio.com, crons, billing | Personal Gmail | UNKNOWN | None | Storefront + OS + licence issuing go dark together; DNS lost | Email access + 2FA; add a member today |
| **Domain `zarlinoaudio.com`** | The whole brand's address | Registrar: TO CONFIRM (Cloudflare Registrar or third party) | — | None | Everything dies | Confirm registrar, **turn auto-renew ON**, put backup card |
| **GitHub `zarlino-audio`** (personal USER account, not an org) | All code: `Zarlino` (private monorepo), `zarlino-website` (public), `zarlino-feedback` (public), `zarlino-executive-os` (private), `songwriting-studio`, `realestate-os` | Founder personal | UNKNOWN | None | Code frozen — nothing can ship or be fixed | Account email + 2FA recovery codes; Phase 1: move repos into an ORG with 2 owners |
| **Azure subscription** | Signing service creds in CI (`AZURE_*`), VM `102.37.153.164` (deallocated — kept for later) | Founder personal | UNKNOWN | None | Release signing stops; VM unavailable | Portal access via email; `az` CLI not installed locally |
| **DeepSeek platform** — NEW account on `zarlino001@gmail.com` (created 2026-10-03; the old Gmail-bound account is retired) | Heavy AI tier for the OS (pay-as-you-go) | Company (root Gmail) | UNKNOWN | None | Cloud agent stalls (HTTP 402) until topped up | Top up with the company card form (Google Pay is unusable — empty merchant id); balance alert at $5 enabled; key steps in FOUNDER_ACTIONS §4 |

## Money

| Account | What it powers | Notes | If lost | Recovery |
|---|---|---|---|---|
| **Paystack** (GHS-only live account) | Checkout, licence purchase records, payouts to founder's bank | `sk_live` is a Worker secret; `pk_live` is public | No revenue, no refunds, payouts freeze | Support + KYC docs; keep settlement bank details in vault |
| Business bank account | — | **None found — revenue currently settles to a personal account** | Legal/tax exposure | Phase 1: register entity + business account |

## Founder channel + alerts

| Account/system | What it powers | Notes | If lost |
|---|---|---|---|
| **WhatsApp business line +233 59 798 7855** (linked device bridge) | Founder escalations + replies | Session lives in `zarlino-executive-os/tools/whatsapp-bridge/auth` (a live credential — now also in the nightly local backup + should be in the vault) | Escalations queue silently; export/import via `scripts/export-bridge-session.ps1` |
| **ntfy topic `zarlino-alerts-h7k2mq4x`** | Independent second alert channel | Public topic — treat as a shared secret; anyone who learns it can read/post | Lose the second alert channel (WhatsApp still works) — swap topic in `wrangler.toml` |

## Machines

| Machine | What it runs | If it dies |
|---|---|---|
| **Founder laptop** (Windows, this machine) | Local daemon (builds/renders/FL), WhatsApp bridge (auto-start links verified), local backups | No local builds/renders; WhatsApp silent until bridge moves to another host (~5 min with the instance kit); backup folder frozen — that's why the offsite copy matters |
| **Azure VM** `102.37.153.164` | Intended always-on host (deallocated; decision 2026-10-02: keep laptop for now) | Nothing today |

## Dev / build-only (non-critical but documented)

| Item | Notes |
|---|---|
| VS Code + Copilot (DeepSeek V4 plugin), Ollama | Tooling; replaceable |
| FL Studio 21 | Host-compat testing |
| MSVC/VS 18, CMake 4.2, Ninja, NSIS, Python 3.14 | Build toolchain |
| JUCE 8 + VST3 SDK | Licence decisions tracked in the audit (LGL-04) |

## Known security debt to close (see FOUNDER_ACTIONS)

1. `CF_API_TOKEN` — **RESOLVED 2026-10-03**: fresh scoped token `zarlino-exec-os` created (Account: Workers Scripts/KV/R2 Edit + Account Settings Read; Zone zarlinoaudio.com: Zone Read + DNS Edit + Cache Purge). Wired to the Worker secret + local `.dev.vars`; verified `active` via API and end-to-end via `cloudflare.list_zones` (zone id 2effba86…). Note: the token value was visible once on the dashboard reveal page during setup — if you ever export/share the local session logs, rotate first (see FOUNDER_ACTIONS §3).
2. DeepSeek key was hard-coded in `C:\Users\khrit\collaborative-mql5-ide\backend\server.py` (removed 2026-10-02 — env-only now). The old account it belonged to is retired as of 2026-10-03; the new account + key are handled in FOUNDER_ACTIONS §4.
3. `INTERNAL_SECRET` — was a hardcoded constant; as of 2026-10-02 it is a Worker secret (rotated).
4. Website `ADMIN_TOKEN` — previously committed to the public repo; rotated 2026-09-29; founder copy file is missing (re-create).
