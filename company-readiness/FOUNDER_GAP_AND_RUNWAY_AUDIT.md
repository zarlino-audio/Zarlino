# Founder-Gap Audit & Runway Readiness — Zarlino

**Date:** 2026-10-02
**Scenario audited:** the founder disappears today and returns in 12 months.
**Core question:** what stops the organization from running — and what must change to go from a
toylike founder-dependent setup to a company that generates and manages its own runway.

**Evidence legend:** [V] verified in files/repos/live systems during this audit · [I] inferred from
verified facts · [U] unknown / must be confirmed by the founder.

---

## 0. Verdict in five lines

1. **The cloud keeps breathing; the company stops earning.** The OS (Cloudflare worker + DO + cron
   + Doctor) survives without the founder, but every *money, identity, machine and decision* layer
   is tied to one human.
2. **Bus factor = 1 everywhere.** One personal Gmail (`zarlino001@gmail.com`) owns Cloudflare, the
   domain, the workers, the KV/R2 stores and the billing. One personal GitHub user account owns all
   code. One laptop runs the only daemon + the WhatsApp bridge. One card pays for AI, domain, Azure.
3. **There is no live revenue channel.** `SALES_ENABLED = false` in the website source; the live
   site still advertises "free until August 25, 2026" (passed) and a bundle that is disabled.
4. **There is no legal surface at all.** No privacy policy, terms, EULA or refund policy exists
   (footer links are `#`); no evidence of a registered company; no licence paperwork; unsigned
   installers.
5. **Nothing is watched and nothing is backed up on a schedule.** No uptime monitor, no
   alerting, no recurring offsite backup, no second custodian for any account.

---

## 1. What the organization actually is (as of today)

### 1.1 Business lines

| Line | State | Evidence |
|---|---|---|
| **Zarlino Audio — plugins** (ZTame, ZScorch) | Storefront live, downloads live, **checkout OFF**; ZTame v1.2.0 shipped as public beta, free promo expired; ZScorch local source `2.0.0` vs site `1.0.0` | [V] `zarlino-website/src/config/sales.ts`, live site copy, `plugins/ZScorch/Source/Version.h` |
| **Zarlino Audio — pipeline** | ZVocal 0.2.0 (P1–P7 unimplemented per design doc), PresenceField 0.1.0, ZAdaptiveHarmonics, ZPunch, MyFirstEQ, RACK_AI (scaffold, never compiled), zarlino-source (stem-separation engine, early, 22/22 tests, no UI/worker) | [V] repo file states; `docs/platform/zarlino-qa-platform-spec-2.md` plans 20 plugins |
| **realestate-os (Vee Properties)** | 16-agent demo org deployed on `workers.dev`; website page live with **placeholder** contact/phone/listings | [V] `realestate-os/docs/AGENTS.md`, `zarlino-website/src/config/vee.ts` placeholders |
| **Songwriting Studio** | Own repo, own worker + container analysis engine, no pricing/monetisation found | [V] repo memory + live worker notes |
| **funds-exec-env (forex desk)** | **Demo-only** MQL5 desk on the laptop (XM MT5, Headway-Demo login 4954825). No real trades, no P&L, not connected to the cloud, git-ignored. A real-account history file (16946195) exists on disk | [V] `funds-exec-env/ENVIRONMENT.md`, `desk/observe_report.md` |
| **Real-estate + trading + songwriting** all wait on the founder | — | [I] |

### 1.2 The infrastructure that runs it

- **Executive OS** — Cloudflare Worker `zarlino-executive-os` at `os.zarlinoaudio.com` (+ workers.dev
  fallback), one Durable Object `ExecutiveAgent` (SQLite), cron `0 */6 * * *`, System Doctor
  self-healing, CEO agent cycles, WhatsApp founder channel, 71 registered tools, MCP read-only
  plane. Account: `Zarlino001@gmail.com's Account` (`ae604b60…`). [V] `wrangler.toml`, repo memory.
- **Website** — Worker `zarlino-website` at `zarlinoaudio.com`; Astro store + license API
  (RSA-signed license blobs), Paystack checkout (GHS), affiliate program (25%), admin dashboard,
  waitlist capture into KV, feedback → public GitHub repo `zarlino-audio/zarlino-feedback`. [V]
- **Machine layer** — the founder's laptop runs the only build/render daemon and the only WhatsApp
  bridge; both auto-start from the Startup folder (`Zarlino Executive Daemon.lnk`,
  `Zarlino WhatsApp Bridge.lnk`). The intended always-on Azure VM (`102.37.153.164`, `Zarlino1.rdp`)
  is **deallocated**. [V] Startup folder listing; `docs/INSTANCE_AND_RETIREMENT.md`.
- **Secrets** — live keys in `executive-os/.dev.vars` locally; production secrets on the workers
  (`DEEPSEEK_API_KEY`, `FOUNDER_AUTH_TOKEN`, `GITHUB_TOKEN`, `CF_API_TOKEN` (verified invalid on the
  local copy), `PAYSTACK_SECRET_KEY`, `ZARLINO_LICENSE_PRIVATE_KEY`). The website admin token was
  rotated 2026-09-29 after being committed to a public repo; the documented founder copy
  (`.local/website-admin-token.txt`) is **not present** today — `.local/` is empty. [V]

### 1.3 The agentic stack (prompts / skills / agents / tools)

| Asset class | What exists | What's missing |
|---|---|---|
| **Prompts** | None found. The VS Code user prompts folder does not exist; no `.prompt.md` anywhere in the workspace. Prompts live only inside chat history and spec docs. | A versioned prompt library for company ops (support replies, release notes, marketing, finance) |
| **Skills** | 5 user-level skills at `~/.copilot/skills/`: `dsp-architect`, `dsp-correctness`, `plugin-build-host-compat`, `plugin-engineer`, `rt-safety-performance` — all engineering-facing | Skills for sales, marketing, support, finance, legal-drafting, release-ops, incident response |
| **Agents** | Exec-OS AOrgI organisation (6 departments / 12 roles / CEO agent, capability-granted, audited); `realestate-os` 16-agent roster with per-agent tool allow-lists; local daemon = capability executor | Live agent *instances*: only the cloud DO + the laptop daemon exist (VM absent). No agents for sales/marketing/support run in production today. No VS Code custom agents (`.agent.md`) exist |
| **Tools** | Exec-OS: 71 tools across finance(4)/projects(5)/marketing(3)/executive(16)/pipeline/calendar/notes/reminders/contacts/website/review/productivity/perceptual + paystack(5) + github + cloudflare + web + vision + website + whatsapp + Workers AI. Local: build/test/render/FL-Studio/ship-check tools. Workspace: a self-commissioned tool register (`tools/TOOL_MANIFEST.md`) | No email-sending tool (no SMTP/Resend/Buttner), no banking/accounting tool, no social/marketing channel tool, no ticketing/helpdesk tool, no monitoring/alerting tool |

---

## 2. Failure timeline — what happens when the founder is gone

| Horizon | What still runs | What breaks |
|---|---|---|
| **Immediate** | Cloudflare cron + DO + Doctor + CEO cycles; website serves; downloads serve; license API issues keys (if the website worker runs); escalations *queue* for the founder | No WhatsApp delivery (bridge needs the laptop to stay alive and linked); no local builds/renders/FL tests; no human sees escalations; no approvals decisions |
| **Days** | Everything cloud-side continues | DeepSeek balance ($0.29–$0.60 typical) runs out → all heavy agent work fails HTTP 402; no top-up possible; "thinking" degrades to the free Workers-AI tier only |
| **Weeks** | Storefront serves; free/license flows creep along | No releases (nobody to approve/sign/ship); bug reports pile up unanswered (public GitHub issues); waitlist signups store into KV but **cannot be emailed** (no email provider wired) even though the site promises exactly that |
| **1–3 months** | Possibly still serving | Card charges fail (domain renewal, Cloudflare/DeepSeek/Azure if on the same card) → workers suspended → **storefront, OS, license issuance all down**; domain can lapse → everything dead; DO free-tier row quota historically throttled the whole OS when busy |
| **3–12 months** | — | Paystack disputes/refunds unanswered → account restriction risk; GitHub account (personal, 2FA) unrecoverable → code frozen; laptop dies → uncommitted work + WhatsApp session + build pipeline gone (only committed code survives); brand decays (stale copy, dead support email, unsigned installers) |

---

## 3. Blocker register (sorted by category)

Severity: **S1** stops the organisation · **S2** stops money or operations · **S3** degrades ·
**S4** hygiene. "Bites" = when it starts to hurt without the founder.

### A. Infrastructure gaps

| # | Severity | Bites | Blocker | Evidence |
|---|---|---|---|---|
| INF-01 | S1 | weeks | Every account (Cloudflare, domain, KV, R2, workers, billing) is under one **personal Gmail**; no second admin, no recovery document | [V] inventory §3; `wrangler.toml` |
| INF-02 | S1 | months | Billing on one card; DeepSeek is topped up by hand and is frequently near zero (402 outages across Aug–Sep) | [V] `provider.balance` incidents; doctor history |
| INF-03 | S1 | immediate | The only always-on machine is the founder's laptop (daemon + WhatsApp bridge auto-start); the Azure VM designed to replace it is deallocated | [V] Startup folder; `INSTANCE_AND_RETIREMENT.md` |
| INF-04 | S2 | immediate | WhatsApp founder channel = unofficial Baileys linked-device bridge; session file is a live credential; ban/relink risk; if the laptop dies the channel is silent (messages queue) | [V] `tools/whatsapp-bridge/`, repo memory |
| INF-05 | S2 | weeks | **No uptime monitoring or alerting.** A public `/api/health` exists "for uptime monitors" but no monitor is configured; alerts would go to the founder anyway | [V] comment in `src/index.ts:1201`; no monitoring tool/integration found |
| INF-06 | S2 | days–weeks | **No scheduled offsite backup.** A snapshot export/import endpoint exists, and R2 is bound — but no cron job takes snapshots; source safety depends on manual `git push` | [V] `src/index.ts` snapshot routes; no scheduled snapshot found |
| INF-07 | S2 | weeks | Secrets sprawl: invalid `CF_API_TOKEN` in `exec-dev.vars`; a live DeepSeek key hard-coded in `C:\Users\khrit\collaborative-mql5-ide\server.py` (outside the repo); no central vault | [V] inventory SEC findings; `desk/observe_report.md` |
| INF-08 | S3 | months | Free-tier ceilings: DO rows-read quota has previously blocked the entire OS; Workers AI 10k neurons/day; no paid tier decision recorded | [V] repo memory 2026-08-26/28; `wrangler.toml` |
| INF-09 | S3 | immediate | Console login = founder-only credentials (previously seeded `Khritical` / temp password `0000`; rotation status unknown); no role-based multi-user access | [V] auth history; single founder account in `founder_credentials` |

### B. Access & identity gaps (the "keys to the company")

| # | Severity | Bites | Blocker | Evidence |
|---|---|---|---|---|
| ACC-01 | S1 | immediate | No documented, sealed "company continuity kit": account list, recovery codes, 2FA devices, registrar, card details, KYC documents. `.local/` (where the admin token copy was recorded) is empty | [V] audit; memory notes |
| ACC-02 | S1 | weeks | GitHub is a **personal user account** (`zarlino-audio`) — no organisation, no second owner, no recovery mailbox. All product code lives here | [V] inventory §2; repo remotes |
| ACC-03 | S2 | months | Paystack (GHS-only, real money) KYC is founder-bound; payouts go to the founder's bank. A locked identity = money frozen | [V] `zarlino-website/wrangler.toml`; paystack integration |
| ACC-04 | S2 | weeks | Domain + DNS control (zarlinoaudio.com) — registrar account ownership unverified; renewal depends on the same card | [V] zone list was BLOCKED in inventory §3 |
| ACC-05 | S3 | immediate | `FOUNDER_AUTH_TOKEN` is both the console/API machine key and the credential the local daemon uses; single secret, no rotation schedule | [V] exec-os auth notes |

### C. Financial

| # | Severity | Bites | Blocker | Evidence |
|---|---|---|---|---|
| FIN-01 | S1 | now | **No revenue engine is ON.** `SALES_ENABLED=false` gates checkout, cart, buy buttons; the live site still says "free until August 25, 2026" (passed) and shows `$49 / FREE` | [V] `src/config/sales.ts`; live pages |
| FIN-02 | S1 | months | No company bank account, bookkeeping, tax registration or invoicing found anywhere; finance = a manual "cash/revenue/expenses" calculator inside the OS (`assessFinancials`) | [V] `src/core/finance/calc.ts`; no accounting artefacts found |
| FIN-03 | S2 | now | "Runway" is a manual number. Revenue attribution exists (Paystack sync tool) but there is no real ledger, no burn model, no reserve policy | [V] finance module; paystack tools |
| FIN-04 | S2 | months | Refunds/disputes have no policy or process (none found); Paystack can restrict the account when disputes go unanswered | [I] no refund content anywhere |
| FIN-05 | S2 | weeks | Recurring costs require manual top-ups and card presence: DeepSeek (AI), domain renewal, Azure (subscription + signing service `AZURE_*` CI secrets), Cloudflare (possibly free tier). No cost register exists | [V] secrets names; deployment notes |
| FIN-06 | S3 | — | The forex desk (`funds-exec-env`) is demo-only and cannot trade (cloud has no broker access; no real account wired; explicit demo-first standing decision) — it can *not* currently generate runway | [V] `ENVIRONMENT.md`, `dec-funds-001` |
| FIN-07 | S3 | — | Side ventures (realestate, songwriting) are parked demos with no pricing/payments wired; placeholder contact data on the live real-estate page | [V] `vee.ts` placeholders; no pricing found |

### D. Legal & compliance

| # | Severity | Bites | Blocker | Evidence |
|---|---|---|---|---|
| LGL-01 | S1 | now | **No legal pages**: Privacy Policy, Terms of Service, License Agreement footer links are literal `#` placeholders (live site + dist); no refund policy; no company identification (name/number) anywhere; "About/Contact" are dead links | [V] live pages; `Footer.tsx:12`; grep of `dist/` |
| LGL-02 | S1 | now | **No EULA in the installers** (NSIS uses WELCOME/COMPONENTS/DIRECTORY/INSTFILES/FINISH only) and no licence document in the repo — shipped binaries have no enforceable licence | [V] `plugins/ZTame/installer/ZTame.nsi` |
| LGL-03 | S2 | months | No evidence of a registered entity (brand "Zarlino Audio"; bundle IDs `com.khriticalresearch.*`; publisher string only). Contracts, bank, tax, Paystack merchant all hinge on this | [U] no registration artefacts found |
| LGL-04 | S2 | months | Third-party licence exposure: JUCE is dual-licensed AGPLv3/commercial (no purchase evidence); VST3 SDK present (Steinberg licence); ONNX/models/sample sources unverified; no licence register | [V] `JUCE/LICENSE.md`; `sdk/VST3_SDK/` |
| LGL-05 | S2 | now | **Unsigned binaries**: Azure Artifact Signing pipeline exists but is disabled (`ZARLINO_SIGNING_ENABLED` unset); SmartScreen warnings; no code-signing certificate on record | [V] `ztame-release-gate.yml` |
| LGL-06 | S3 | months | Trademark for "Zarlino Audio", "ZTame", "ZScorch" unverified; affiliate program pays 25% but its terms document was not found | [U] |
| LGL-07 | S3 | months | Data protection: waitlist emails + license emails stored in KV; feedback PII flows into a **public** GitHub repo (`zarlino-feedback`); no privacy notice or processor list; Ghana DPA 2012 / GDPR-style obligations unaddressed | [V] `EmailCapture.tsx`; `FEEDBACK_REPO` |

### E. Product & engineering

| # | Severity | Bites | Blocker | Evidence |
|---|---|---|---|---|
| PRD-01 | S1 | now | **Release process is founder-personal**: packaging script `tools/build-release-local.ps1` no longer exists (searched whole workspace); local builds are manual MSVC/Ninja incantations; CI release gates cover only some plugins (ZTame gate on main; ZScorch gate not merged) | [V] file search; `.github/workflows/` |
| PRD-02 | S1 | now | Human listening gates: perceptual validation requires the founder's ears (doctor WARNs by design; ZVocal spec lists listening tests + D-3 artefact panel as "human-blocked"); no external evaluator enrolled | [V] perceptual module; `zvocals.md` memory; doctor 3 WARNs |
| PRD-03 | S2 | weeks | Source control state: monorepo on a feature branch (`fix/meter-provider-usage`) with **210 dirty entries**; website 67 dirty on a CI branch; exec-os 43 dirty on main. Cloud builds are stale vs local work; three data-loss class incidents in repo history | [V] git commands this audit |
| PRD-04 | S2 | months | Pipeline breadth: only Windows VST3; Mac/AU and AAX not started (no Mac in the loop); 20-plugin roadmap is a spec (2,044 planned QA units, unbuilt); zarlino-source/RACK_AI unfinished | [V] `docs/platform/zarlino-qa-platform-spec-2.md`; repo states |
| PRD-05 | S2 | months | No versioned releases on GitHub (0 releases); version drift live (site 1.0.0 vs source 2.0.0 for ZScorch); stale promo text shipped; downloads are static files with no update channel | [V] inventory; `products.ts`; live copy |
| PRD-06 | S3 | months | No crash/telemetry pipeline from users' machines; bug reports = public GitHub issues only; no crash triage loop | [V] feedback flow |

### F. Sales

| # | Severity | Bites | Blocker | Evidence |
|---|---|---|---|---|
| SLS-01 | S1 | now | Checkout disabled everywhere (single flag), so there is **no path from visitor to money** the moment checkout is off; nothing sells while off | [V] `sales.ts` + live pages |
| SLS-02 | S2 | now | No sales funnel system: no CRM pipeline for prospects, no trial→paid flow, no abandoned-cart, no receipts/invoicing, no upgrade path, bundle disabled | [V] website + exec-os contacts (address book only) |
| SLS-03 | S2 | months | Single channel (own site, GHS only). No distributor/reseller presence (KVR/Plugin Boutique/etc.), no alternative currency (USD blocked by Paystack GHS-only account), which caps reach and price flexibility | [V] Paystack constraint notes; no channel anywhere |
| SLS-04 | S3 | now | Licence issuance depends on the website worker + private key; no separate license portal for customers (resend key / transfer machine) | [V] `/api/license` design |

### G. Marketing

| # | Severity | Bites | Blocker | Evidence |
|---|---|---|---|---|
| MKT-01 | S1 | weeks | **Email is impossible.** Waitlist + product signups are stored in KV, but no email provider is wired, while the site explicitly promises "we'll email you the moment checkout opens" | [V] `EmailCapture.tsx` comment; no mail tool found |
| MKT-02 | S2 | now | Dead marketing surfaces on the live site: social links (Twitter/Instagram/YouTube/Discord) all `#`; About/Contact dead; testimonials intentionally empty (no social proof at all) | [V] live footer; `products.ts` |
| MKT-03 | S2 | now | Promo copy is stale/wrong on the flagship page ("free until Aug 25, 2026", "$49 FREE") — active trust damage with zero maintenance | [V] live site |
| MKT-04 | S3 | months | No content engine (no blog/devlog cadence), no analytics beyond Cloudflare counters, no campaign tooling in production (exec-os `marketing.*` tools exist but nothing runs), no launch calendar | [I] tools exist; no runs evidenced |
| MKT-05 | S3 | months | Affiliate program live but dormant while sales are off; commission terms page missing; no partner recruiting | [V] affiliate code; no terms |

### H. People & organisation

| # | Severity | Bites | Blocker | Evidence |
|---|---|---|---|---|
| PPL-01 | S1 | immediate | The company = one human + AI. No employee, contractor, advisor, or second custodian holds any account, decision or process. No hiring pipeline, no onboarding docs for a human | [V] whole audit |
| PPL-02 | S2 | now | Founder-dependent judgment gates (sound/UI reviews, pricing, legal, ship decisions) block product flow — doctor's warnings ("task contracts", "perceptual gaps") exist precisely because only the founder can close them | [V] doctor WARN set; review flows |
| PPL-03 | S3 | months | No compensation, equity, contractor IP-assignment or confidentiality documents (none found) — future hiring starts from zero legal ground | [U] |

### I. Knowledge & source control

| # | Severity | Bites | Blocker | Evidence |
|---|---|---|---|---|
| KNW-01 | S2 | weeks | The cloud-migration mandate (14 phases, 21 deliverables) is mostly NOT STARTED (durable jobs, cloud builds, Azure execution, artifact arch, verification, local decommission, rollback plan). The "cloud-only" goal is ~30% done | [V] `docs/platform/cloud-migration/README.md` tracker |
| KNW-02 | S2 | immediate | Runbooks that exist (`OPERATOR_RUNBOOK.md`, `INSTANCE_AND_RETIREMENT.md`) assume the founder with credentials; there is **no company-level document**: accounts, vendors, renewals, money, legal, escalation matrix | [V] docs folder |
| KNW-03 | S3 | months | Uncommitted work is the norm (320+ dirty files across 3 repos); "the only uncommitted change was lost" already happened in repo history; three branch-switch wipe incidents | [V] git state; repo memory |
| KNW-04 | S3 | months | Knowledge lives in chat transcripts and scratch files (`_*.txt`, `notes/`); recovery used chat-transcript replay in the past | [V] repo memory |

### J. Governance & control

| # | Severity | Bites | Blocker | Evidence |
|---|---|---|---|---|
| GOV-01 | S1 | immediate | Decision rights all sit with the founder (approvals queue, FULL_ACCESS toggle, CEO escalation "hard-line only"); with the founder gone, approvals queue forever and escalations go nowhere | [V] exec-os control notes; CEO mandate |
| GOV-02 | S2 | now | No spending policy, approval limits, treasury rules, or definition of what the AI may decide vs. what needs a human (beyond code-level gates) | [I] |
| GOV-03 | S3 | months | No board/advisor/quarterly review; no external obligation to keep the company honest (which is also why nobody notices the stale promo, dead legal links, etc.) | [V] observable state |

---

## 4. Team health checks

### Product team — **verdict: founder-shaped, half-automated**
- **Exists:** the strongest area. Real DSP skills (5 dev skills), real test suites per plugin,
  release gate CI for ZTame, ship-check tooling, perceptual framework, an AI org with engineering
  roles and evidence gates. [V]
- **Missing:** a release manager who isn't the founder; a working packaging pipeline
  (`build-release-local.ps1` gone); listening-test evaluators; Mac/AAX capability; backlog hygiene
  (210 dirty files); version/release discipline.
- **Health score: 5/10** — can build; cannot *ship and maintain* without the founder.

### Sales team — **verdict: doesn't exist (switched off)**
- **Exists:** checkout plumbing (Paystack GHS), license issuing, affiliate program (25%), pricing
  config, server-authoritative price map, payout tooling (`initiate_transfer`, approval-gated). [V]
- **Missing:** an ON switch (sales disabled), any pipeline/CRM use, distribution channels,
  currency reach (GHS-only), receipts/invoicing, refund process, a sales motion of any kind.
- **Health score: 1/10** — a storefront with the lights off and no seller.

### Marketing team — **verdict: doesn't exist (no channel that reaches a human)**
- **Exists:** a well-built website, SEO/sitemap, waitlist capture (stored), affiliate landing page,
  exec-os marketing tools (3, idle), brand voice assets. [V]
- **Missing:** email sending (the one channel that matters for a catalog), social accounts wired,
  content cadence, testimonials/case studies, analytics/attribution beyond counters, promo
  currency (stale expired promo live), launch plan, any campaign executed.
- **Health score: 2/10** — can collect interest; cannot speak.

---

## 5. "Generating and managing its own runway" — what that actually requires

Runway = cash ÷ net burn. Today: revenue ≈ 0 (checkout off), burn = small but unmanaged, cash =
founder's pocket, tracked only by a manual calculator. To generate and *manage* runway the company
needs four engines, none of which fully exist:

1. **Revenue engine** — checkout ON, a suite of sellable products (Mac/AU widens the market),
   2–3 distribution channels, price/refund policies, an email list that converts.
2. **Cost engine** — vendor register, paid tiers where they remove risk (Cloudflare paid tier,
   VM), AI budget governance (already built: $5/day, $80/mo knobs), subscriptions review.
3. **Treasury engine** — entity + business bank + settlement routing, monthly close, tax set-aside,
   reserve policy, and an explicit risk budget if the trading desk is ever ever real-money
   (currently demo-only — keep it that way until (3) exists).
4. **Management engine** — live runway dashboard from real numbers, monthly financial report
   (the OS can produce it), and a spending policy the AI can execute within.

---

## 6. From toy to company — roadmap

### Phase 0 — "Survive the week" (0–14 days, all founder-present tasks)

> **UPDATE 2026-10-03 — Phase 0 executed.** Shipped + verified: external monitoring (15-min checks
> incl. the WhatsApp-bridge liveness; alerts via the founder channel AND an independent ntfy topic),
> daily cloud backup of the OS database to R2 (30 deep), nightly local backups (git bundles +
> uncommitted work + secrets; 14-day retention), sales OPENED with the stale promo removed everywhere,
> legal v1 pages live (`/legal/privacy|terms|license|refunds`) with the footer wired, EULA added to
> the ZTame installer, `INTERNAL_SECRET` rotated to a Worker secret, and the exposed DeepSeek key
> removed from disk. Remaining founder-only items (vault + sealed kit copies, custodian, DeepSeek
> top-up, backup card, CF_API_TOKEN + DeepSeek key rotation, one real purchase+refund test, offsite
> backup copy) are tracked in `company-readiness/continuity-kit/FOUNDER_ACTIONS_PHASE0.md`.
- [ ] **Continuity Kit v0** (sealed): every account + recovery codes + registrar + cards + KYC docs
      encrypted into a vault; one physical copy held by a trusted second person.
- [ ] **Second custodian on the critical 5**: Cloudflare account member, GitHub (after org move),
      domain registrar, Paystack contact/bank alerts, email recovery. Two 2FA paths always.
- [ ] **Monitoring**: external uptime checks (os.zarlinoaudio.com/api/health, storefront, downloads)
      alerting to a phone/email that is not only the founder's.
- [ ] **Backups**: scheduled daily snapshot → R2 + nightly `git push` guard; quarterly `git bundle`
      offsite copy. (Snapshot endpoint already exists.)
- [ ] **Money failsafe**: vendor/card register; backup card on file; 3-month DeepSeek top-up;
      decide Azure VM (start it as the always-on host, or delete it and keep the laptop + kit).
- [ ] **Storefront truth**: fix "free until Aug 25" copy; decide sales ON/OFF and write the honest
      line; if ON, run an end-to-end Paystack test purchase + refund test.
- [ ] **Legal minimum**: publish Privacy, Terms, EULA (+embed EULA page in installers), Refund
      policy — even v1 templates — and remove dead `#` links.
- [ ] **Security sweep**: rotate the invalid `CF_API_TOKEN`; move the hard-coded key out of
      `collaborative-mql5-ide/server.py`; re-scan repos for committed secrets.

### Phase 1 — "Company skeleton" (30 days)
- [ ] Register the entity; open a business bank account; link Paystack settlement + card to it;
      business email on the domain (`support@`, `admin@`); move ALL accounts off the personal
      Gmail into business identities.
- [ ] Move GitHub repos into a **real organisation** with ≥2 owners + recovery mailbox.
- [ ] Legal pack: ToS/Privacy/DPA note, affiliate terms, contractor IP-assignment template,
      licence register (JUCE tier decision — commercial licence or accept AGPL consequences;
      Steinberg VST3; models/samples).
- [ ] Release engineering: recreate the packaging script; make **CI the only release path**; tag
      releases; enable code signing (e.g. Azure Artifact Signing ≈ $120/yr) or sign the risk.
- [ ] Repo hygiene: merge the 3 feature branches, clear 320+ dirty files, retire stale branches.

### Phase 2 — "Revenue machine" (90 days)
- [ ] **Open sales** (flip `SALES_ENABLED`), add the bundle, add USD billing path if possible
      (Paystack-USD enablement or a second processor). Test purchase → license → refund loop.
- [ ] Distribution: KVR + one major reseller + 10 creator outreach targets; reviewer units.
- [ ] Marketing system: wire an email provider to `/api/waitlist` (the site already promises it),
      welcome/release flows, devlog cadence, fix socials, collect first permitted testimonials.
- [ ] Support system: help desk + SLA; support email answered by someone other than the founder
      (start with a contractor); refund workflow.
- [ ] RevOps: nightly revenue sync into the OS finance ledger; monthly financial report; live
      runway dashboard from real numbers; spending policy encoded in the OS gates.

### Phase 3 — "Company machine" (6–12 months)
- [ ] First humans: support/community (part-time) + DSP contractor + content freelancer — onboarded
      with the Continuity Kit, skills, and role-scoped access (exec-os needs multi-user roles).
- [ ] Product expansion along the QA spec: finish ZVocal (P1–P7), zarlino-source, Mac/AU (rented
      Mac CI), then decide on AAX and RACK_AI (fund or kill).
- [ ] Always-on host: replace the laptop with the VM (or equivalent) running daemon + WhatsApp
      bridge + scheduled backups; verify with the INSTANCE_AND_RETIREMENT procedure.
- [ ] Trading desk: keep demo until (a) entity + treasury exist, (b) real risk budget is documented,
      (c) guardrails + independent custodian exist — then run it as a treasury experiment, never
      commingled with operating cash.
- [ ] **The Quarterly Go-Concern Test**: simulate a founder-absent month (no logins, no reviews, no
      money moves) and score the checklist below.

---

## 7. The Go-Concern Test (pass = the company can run without the founder)

1. A named second human can log into: Cloudflare, GitHub org, registrar, Paystack, email, vault.
2. Storefront + license issuance survive a 30-day founder silence (monitored, alerting someone).
3. Checkout on: a stranger can buy, receive a key, get a refund, reach support.
4. Legal: privacy/terms/EULA/refund live; entity identified on the site; licences documented.
5. Money: one business bank account; settlement to it; monthly close happens without founder.
6. AI ops: DeepSeek funded ≥ 3 months; budget caps enforced; escalations delivered to a group, not
   one person; approvals have a deputy.
7. Release: CI can build, sign, package and release a patch without the laptop.
8. Backups: daily snapshot + offsite copy verified by a restore drill.
9. Marketing: email list actually emails; socials live; content cadence owned by a role.
10. Knowledge: a stranger onboards from docs alone (no chat archaeology).
11. Team: ≥ 2 humans with defined scopes; ≥ 1 non-founder with decision rights.
12. Runway: live dashboard (cash, burn, runway months) that a deputy maintains.

---

## 8. What would have happened if the founder left *yesterday* (one honest paragraph)

Within days, the AI stops working (empty DeepSeek balance, no one to top it up). Within weeks, the
WhatsApp channel goes silent if the laptop closes, and no human sees escalations. Within months,
the renewal/card cycle fails and the storefront, the OS and license issuance go dark together,
taking the domain with them. The code survives only if it was pushed; the company's identity,
money, machines and judgment do not. There is no legal entity to inherit, no accounts anyone else
can open, no email channel to refund or apologise through, and no product that can ship. **That is
the gap between a founder's tool and a company — and it is closable in 90 days of focused work
starting with Phase 0.**
