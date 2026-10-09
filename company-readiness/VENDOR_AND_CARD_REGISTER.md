# Vendor & Card Register (v1, 2026-10-03)

First cost/renewal register the company has had. Revenue currently = **checkout opened 2026-10-02**
(was off since launch). Update this file monthly; it is the input to the runway model.

| Vendor | What for | Cost | Renewal / trigger | Card on file | Risk if unpaid/lost | Notes |
|---|---|---|---|---|---|---|
| Cloudflare | Workers, KV, R2, DNS, crons | Free tier today (DO free-tier row quota has throttled the OS before; paid Workers ≈ $5/mo removes that) | Monthly (if upgraded) | Founder card | Storefront + Executive OS + licence issuing all stop | Consider $5/mo upgrade in Phase 1; alerts every 15 min now cover outages |
| Domain `zarlinoaudio.com` | The address of everything | ~$10–15/yr | Annual — **auto-renew to confirm** | Founder card | Brand dead; catastrophic | Registrar unknown — confirm (FOUNDER_ACTIONS §8) |
| DeepSeek (platform) | Heavy AI tier (agent runs) | Pay-as-you-go; soft caps $5/day, $80/mo | Balance-based | **Company card: GCB Virtual Visa (tested working 2026-10-03 — $2.12 charge succeeded)** | Cloud agent stalls on 402 | New company account (2026-10-03); balance alert at $5; card needs GHS funding before larger top-ups |
| Azure | Artifact signing CI creds; VM deallocated | VM ≈ $0 while stopped; signing pay-per-use | Subscription | Founder card? | Release signing pipeline stops | VM decision: keep deallocated (Phase 2) |
| GitHub | Code hosting | Free (private repos on personal account) | — | — | n/a | Phase 1: move to an org |
| Paystack | Payments (GHS only) | Per-transaction fees | — | n/a (settles to founder bank) | Revenue/refunds freeze if account restricted | Business bank account = Phase 1 |
| ntfy.sh | Second alert channel | Free | — | — | Lose only the backup alert channel | Topic is a shared secret |
| WhatsApp (Baileys bridge) | Founder channel | Free (unofficial client — ban risk acknowledged) | — | — | Escalations queue silently (monitored every 15 min now) | Session zip = credential |
| JUCE / VST3 SDK | Framework for all plugins | **Licence status unresolved** (AGPL vs commercial) — legal risk, not a bill | — | — | Legal exposure on sales | Audit item LGL-04 |
| NSIS / MSVC / CMake / FL Studio 21 | Build + host testing | One-time (machine) | — | — | Build pipeline stops if machine dies | Replaceable |

## Card & billing rules (adopt now)

1. **Two cards minimum.** Primary + backup on Cloudflare, Azure, DeepSeek, registrar.
2. **Calendar:** note domain renewal + any annual renewals in the OS calendar (reminders).
3. **Alerts:** Cloudflare billing alerts + DeepSeek balance below $20 → top up (the OS Doctor also
   flags `provider.balance`).
4. **After the entity exists (Phase 1):** move all of the above to the business card/account and
   remove personal-card dependency.

## Current monthly burn estimate (rough, to confirm in Phase 1)

| Item | Monthly |
|---|---|
| DeepSeek (typical, within caps) | $20–80 |
| Domain | ~$1 |
| Azure VM | $0 (deallocated) |
| Cloudflare | $0–5 |
| Everything else | $0 |
| **Total** | **≈ $21–86/mo** |

Runway logic: income starts at the first paid sale (checkout open since 2026-10-02). Until then,
runway = founder's cash ÷ the burn above — which is why the DeepSeek top-up is the single most
important continuity purchase right now.
