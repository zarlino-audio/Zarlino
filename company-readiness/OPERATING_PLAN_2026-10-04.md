# Operating Plan & Handover — week of 2026-10-04

**Operator handover.** The founder issued a directive on 2026-10-04. This document records it,
what was executed immediately, what the organization executes this week, and what only the
founder can decide. After this, the operating layer is: the OS (cloud brain), the VM (always-on
host + build host), the daemon (executor), and the founder (decisions + ears).

---

## 1. The directive (verbatim intent)

1. **ZScorch improvement** — the curves must actually be nice to hear; expose only the advanced
   session; remove 3 knobs; make ZScorch **completely free**.
2. **Finish zarlino-source** (stem separation) and **publish the songwriting platform**.
3. **ZTame is the only product sold** — ZTame Standard **$49**, ZTame Pro **$99** (Pro must be
   engineered and built).
4. **All sales, marketing, branding and growth activity: HOLD.**

---

## 2. Executed immediately (verified)

| Action | Evidence |
|---|---|
| **Go-to-market held** | outreach kill switch OFF: `POST /api/outreach/config {enabled:false}` → `{"ok":true,"enabled":false}`, `sentToday=0`. Nurture + follow-ups stop with the same switch; opt-outs stay enforced |
| **4 decision records** created (product focus, ZScorch free/reduced, GTM hold, week's deliverables) | OS decisions via `executive.create_decision` |
| **7 work orders** created with acceptance criteria + verifier (QA) | `tsk_muu2fejq…`, `tsk_muu2fevl…`, `tsk_muu2ffdi…`, `tsk_muu2ffrr…`, `tsk_muu2fg92…`, `tsk_muu2fgl3…`, `tsk_muu2fh1a…` |
| **Host** — laptop retired, VM `Zarlino1` runs the founder channel + executor | bridge drain test passed; `local_agent.health` OK after the DeepSeek key fix |
| **Build host moved** — MSVC/CMake/Ninja/NSIS + repo (JUCE submodule) + verified ZTame build + nightly backup on the VM | `C:\Zarlino\buildhost.log`, `release-gate` |

## 3. The week's board (org executes; each task carries a contract)

| Task | Owner role | Acceptance (short) |
|---|---|---|
| Website: only ZTame purchasable ($49 Standard / $99 Pro), ZScorch free | product/eng | one product, two tiers; ZScorch free, no licence gate; server price map matches UI |
| ZTame Pro: define feature set, engineer, build | DSP + product | written Pro feature set (≥3 visible capabilities), builds green, licence tier distinct, only then on sale |
| ZScorch: rework filter curves for musical voicing | DSP | founder ear gate on rendered A/B; no ringing/honk/zipper |
| ZScorch: advanced-panel-only UI, remove 3 knobs | DSP/UI | single advanced panel; exactly 3 named knobs removed; host load still passes |
| ZScorch: ship free | eng/release | no licence prompt, free download, no Paystack mapping |
| zarlino-source: finish engine, worker + UI, package | eng | end-to-end separation on a real track; deployed; 3-track ear review |
| Songwriting platform: publish | eng/product | publicly reachable; pricing decision recorded; separate from audio commerce |

## 4. Commerce change — exact file map (for whoever executes it)

Working repo: `C:\Users\khrit\Desktop\plugin\zarlino-website`.

| Step | File | Change |
|---|---|---|
| 1 | `src/config/products.ts` (~L126) | ZScorch: `priceUsd: 0`, `priceGhs: 0`, labels `Free`; catalogue copy drops price/licence language |
| 2 | `src/config/products.ts` (~L160) | `CART_PRICES` keeps **only** `ztame` (remove `zscorch`) |
| 3 | `src/config/products.ts` (interface ~L35) | add a `purchasable: boolean` (or `status: 'released' \| 'coming_soon'`) so Pro can exist at $99 without being sellable |
| 4 | `src/config/products.ts` (~L167) | add `ztamePro` ($99 / ₵1,500, `coming_soon`); retire `BUNDLE` (it prices ztame+zscorch and no longer matches the lineup) |
| 5 | `src/config/sales.ts` | `SALES_DISABLED_NOTE` + comments: "Only ZTame Standard is on sale; ZScorch is free" |
| 6 | `src/index.ts` (worker, price/cart handler near `/api/paystack/initialize`) | **server-side guard**: refuse any cart item whose key is not `ztame` (authoritative, regardless of UI) |
| 7 | product pages / `PluginLineup` / `BundleSection` | ZScorch: Download CTA instead of buy; hide the bundle card; ZTame shows $49 |
| 8 | verify | build + deploy; cold browser: ZScorch page shows Free (no buy), ZTame buy $49, no other purchasable product, sitemap/lineup consistent |

**Do not half-apply this**: `BundleSection` and the cart read `CART_PRICES`, so steps 2–4 and 7
land in the same change, or the store renders a $0/absent product.

## 5. Only the founder can decide

1. **ZTame Pro feature set** — what makes Pro worth $99 (needs ≥3 user-visible capabilities before
   engineering starts). Pro stays `coming_soon` until it builds and its licence tier works.
2. **ZScorch "advanced session"** — which panel is *the* advanced one, and **which 3 knobs** go.
3. **Songwriting platform pricing** — free, paid, or invite-only (must be recorded before launch).
4. **Ear gates** — ZScorch curves and zarlino-source separation quality: the perceptual review is
   the founder's (or an external evaluator's), never the AI's.

## 6. Operating model after handover

| Layer | Runs where | Notes |
|---|---|---|
| Brain, queue, approvals, finance, outreach switches | Cloudflare DO + cron (`os.zarlinoaudio.com`) | the org's command layer; console = Ops view |
| Founder channel | VM `Zarlino1` WhatsApp bridge | one linked device; the console is the alternative |
| Executor | VM daemon (`C:\Zarlino\exec-os`) | claims local tasks; needs `DEEPSEEK_API_KEY` in `.dev.vars` |
| Builds / releases | VM build host (`C:\Zarlino\workspace`, `build-plugin.ps1`) | `release-gate` is the acceptance test |
| Backups | OS → R2 (daily, 30 deep) + VM nightly (14 days) | laptop no longer load-bearing |

**Health in one look:** `POST /api/doctor/run` → expect `0 FAIL`. A clean system is
`29 OK / 1 WARN` (the WARN is the human perceptual gate) with `0` open contradictions.

## 7. Operator retirement statement

The session operator (this assistant) is retired as the bottleneck: the directive is recorded in
the org, the work is assigned with contracts, the machines that execute it are verified, and the
go-to-market surface is frozen. What remains is engineering execution by the org and four
decisions plus two ear gates from the founder. Nothing in this plan depends on a chat session.
