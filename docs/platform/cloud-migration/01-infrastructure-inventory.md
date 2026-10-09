# 01 — Infrastructure Inventory (Phase 1)

- **Date**: 2026-09-18 (verification window ~00:35–01:15 UTC)
- **Author**: migration agent (DeepSeek V4 Flash session `1057a212`), mandate `zarlino-cloud-only-mcp-migration-mandate.md`
- **Method**: static inspection of the workspace + live read-only verification via GitHub API, Cloudflare wrangler (OAuth) and public HTTP checks. No write operations were performed on any cloud system.
- **Status legend**: VERIFIED (independently evidenced) · PARTIALLY VERIFIED · BLOCKED · NOT VERIFIED

Secret values never appear in this document. Presence/absence of secrets and their storage locations only.

---

## 1. System map

| Component | What it is | Where it runs | Status |
| --- | --- | --- | --- |
| Root monorepo | JUCE plugin workspace + shared DSP/UI + CI (14 plugins incl. ZTame, ZScorch, ZVocal, PresenceField) | `github.com/zarlino-audio/Zarlino` (private) | VERIFIED |
| `executive-os/` | Zarlino Executive OS — business/engineering agent: REST API + chat + tools + Doctor + console | Cloudflare Worker `zarlino-executive-os` + DO (SQLite) | VERIFIED |
| `zarlino-website/` | Marketing site (Astro 5, static) + license/checkout API | Cloudflare Worker `zarlino-website` @ `https://zarlinoaudio.com` | VERIFIED |
| Local dev daemon | CLI agent that claims `local_tasks` from the Executive and executes them on the founder's machine | Founder PC (`scripts/local-daemon.ts`, VBS startup scripts) | VERIFIED (this is the target of the migration) |
| GitHub Actions | ZTame release gate, executive-os CI, website deploy | GitHub runners (windows-2022 etc.) | VERIFIED |
| Azure | Artifact Signing service used by ZTame release gate (subscription behind GH secrets) | Azure (via GitHub Actions secrets) | PARTIALLY VERIFIED (config only; az CLI absent locally) |
| R2 | Planned artifact storage | — | **NOT ENABLED on the Cloudflare account** (API error 10042) |
| CEO Console | Founder web console (React+Vite) served by the Executive worker | `https://zarlino-executive-os.zarlino001.workers.dev` | VERIFIED (live) |

## 2. Repositories

| Repo | Remote | Default branch | Visibility | HEAD (verified) | Notes |
| --- | --- | --- | --- | --- | --- |
| Zarlino (root) | `zarlino-audio/Zarlino` | main | **private** (already) | local `main` == `origin/main` == `0d56116c` | 14 branches, 12 open issues, 8 open PRs, 0 releases, 3.4 MB |
| zarlino-website | `zarlino-audio/zarlino-website` | main | (not checked via API; deploy via its own Actions) | local HEAD `4c56011` (2026-09-11) | 11 uncommitted local changes |
| JUCE | `juce-framework/JUCE` | — | upstream third-party | — | framework clone |

Root working tree: **262 uncommitted entries** (26 modified, 231 untracked). `origin/main` last pushed 2026-08-30. The cloud build pipeline would build stale code relative to local WIP.

Notable drift: `.github/workflows/executive-os-ci.yml` **exists on origin/main but is absent from the local working tree** (uncommitted deletion). Local checkout only contains `ztame-release-gate.yml`.

GitHub Actions workflows registered on the repo (API): `executive-os-ci` (active), `ZTame Release Gate` (active), `ZScorch Release Gate` (active; not present on `main` tree — exists on another branch).

Actions secrets list via API: empty result — token is fine-grained and lacks admin scope, so this is **NOT VERIFIED**, not evidence of absence.

## 3. Cloudflare account (OAuth-verified)

- Account: `Zarlino001@gmail.com's Account` — id `ae604b602fdbcb1b48b41e975c6dbd19`
- Wrangler OAuth session valid (config `%APPDATA%\xdg.config\.wrangler\config\default.toml`), scopes include workers/kv/pages/d1/zone write.
- **`CF_API_TOKEN` stored in `executive-os/.dev.vars` is invalid** — CF API `GET /user/tokens/verify` → 401 `Invalid API Token` (also evidenced by exec-os Doctor env expectations).
- KV namespaces (account): `ZARLINO_KV` (`66bc47e33fe145f697cf1f80912f269e`), `Admin` (`5967bcac55c54c86b03324af2b24fddc`).
- R2: **not enabled** (`/accounts/.../r2/buckets` → code 10042 "Please enable R2 through the Cloudflare Dashboard").
- Pages projects: none found.
- Zones/domains: **BLOCKED** — zone list via stored OAuth session returned 403; verify with dashboard or a scoped API token. `zarlinoaudio.com` is verifiably served (live checks, §3b).

### 3a. Worker `zarlino-executive-os`

- Entry `executive-os/src/index.ts`; Durable Object `ExecutiveAgent` (SQLite-backed), binding `EXECUTIVE_AGENT`.
- KV binding `ZARLINO_KV` (same namespace as website); static assets `web/dist` (console); cron `0 */6 * * *`; observability on.
- Vars (committed): `WEBSITE_URL`, AI budget vars. Secrets in production (names verified): `CF_API_TOKEN`, `DEEPSEEK_API_KEY`, `FOUNDER_AUTH_TOKEN`, `GITHUB_TOKEN`, `PAYSTACK_SECRET_KEY`.
- **Missing in production: `ZARLINO_LICENSE_PRIVATE_KEY`** (Doctor `secrets.required` WARN; secret list verified).
- Deployments: 10 retained; latest **2026-09-13T07:25:18Z**, version `53ae8b95-b4ac-4355-81a7-1ea7abee4f6e` (author zarlino001@gmail.com).
- Live `GET /api/health` (public): `ok: true`, `doctorHealthy: false`, schema checks **total 27 / ok 20 / warn 5 / fail 2**.
  - FAIL `jobs.interrupted_runs` (HIGH): 2 resumable long-horizon runs (limit-reached, auto-continue pending).
  - FAIL `provider.balance` (HIGH): **AI provider rejecting paid work — HTTP 402 Insufficient Balance since 2026-09-05T18:00:56Z**. Cloud runs and the local daemon keep failing until the DeepSeek account is topped up.
  - WARN `secrets.required`: missing `ZARLINO_LICENSE_PRIVATE_KEY`.
  - WARN `org.task_contracts`: 4 tasks without full contract (`tsk_mthya6zx_835aqni2pbs8`, `tsk_mthoyxw3_ggu7dio9douh`, `task_db01e6b4-76ce-4a8a-83d3-17b15afa0bee`, `tsk_ztame_signing`).
  - WARN `org.incidents_open`: 2 open HIGH/CRITICAL incidents (`inc_provider_balance`, `inc_interrupted_runs`).
  - WARN `productivity.contradictions`: 1 open contradiction.
  - WARN `perceptual.evidence_gaps`: `prod_zscorch`, `prod_ztame` lack external perceptual evidence.
- Tool surface: **71 tools** in `src/agent/tools.ts` via typed registry (`src/core/tools/registry.ts`, zod schemas, risk levels, audit). Namespaces: finance(4), projects(5), marketing(3), executive(16), pipeline(2), calendar(4), notes(3), reminders(4), contacts(2), website(1), review(3), productivity(10), perceptual(14). Includes the local bridge: `executive.request_local_task`, `executive.get_local_task`, `executive.list_local_tasks`.
- Persistence: DO SQLite, schema v27 migrations, 12 core tables.
- Console (`web/`) views incl. chat UI v2, reviews, approvals, doctor, productivity.

### 3b. Worker `zarlino-website`

- `zarlino-website/src/index.ts`: routes incl. `POST /api/license` (signs license blobs; requires `ZARLINO_LICENSE_PRIVATE_KEY`; increments KV counters `licenses:*`, `views:*`), admin stats at `/api/admin/stats` (needs `ADMIN_TOKEN`), sitemap/beacon handlers.
- wrangler.toml vars: `ADMIN_TOKEN` ⚠️ **literal token committed to git** (finding SEC-1), `PAYSTACK_PUBLIC_KEY` (live pk, public by design), `PAYSTACK_CURRENCY=GHS`; KV binding shares `ZARLINO_KV`.
- Secrets in production (names verified): `GITHUB_TOKEN`, `PAYSTACK_SECRET_KEY`, `ZARLINO_LICENSE_PRIVATE_KEY` (present here — unlike exec-os).
- Deployments: 10 retained; latest **2026-09-08T11:24:12Z**, version `7002bfc3-f3ff-448d-a875-cafeb4806a95`; deploys via `.github/workflows/deploy.yml` on push to main (needs GH secrets `CLOUDFLARE_API_TOKEN`, `CLOUDFLARE_ACCOUNT_ID`).
- Live checks (HEAD): `/`, `/plugins/ztame`, `/plugins/zscorch` → 200. Downloads: ZTame + ZScorch installer/VST3/standalone (6 URLs) → all **200** (no broken links found today). `GET /api/health` → **500** (endpoint exists but errors — follow-up).
- Payments: Paystack, **GHS-only account** (balance in Ghana Cedis) — documented constraint; checkout + callback + license issuance flow present.

## 4. Licenses

- Crypto: RSA; private keys material exists locally at `tools/keys/ztame_license_public.pem` / `ztame_license_private.pem`; blob format documented in `zarlino-website/src/index.ts` (must match `tools/generate_license_keys.py`); license tooling `tools/ztame_license.py`.
- Evidence artifacts: `artifacts/license/ZTame-Proof-of-Correspondence.md`, `ZTame-Sample-Licenses.md`.
- Production status: website worker has the private key (VERIFIED); exec-os does not (VERIFIED). The incident "missing ZARLINO_LICENSE_PRIVATE_KEY" is therefore **still open for exec-os**; the website license path appears configured.

## 5. Build / CI / release

- `ztame-release-gate.yml` (origin/main): windows-2022, CMake VS2022 x64, build `ZTame` + `ZTame_Tests`, `ctest -L release-gate`, optional Azure Artifact Signing (gated on repo var `ZARLINO_SIGNING_ENABLED=true`; secrets `AZURE_TENANT_ID/CLIENT_ID/CLIENT_SECRET/SIGNING_ENDPOINT/SIGNING_ACCOUNT/CERTIFICATE_PROFILE`), packages VST3/standalone zips + NSIS installer, uploads artifacts. `ZARLINO_LICENSE_PUBLIC_KEY` GH secret embedded at configure time (placeholder if unset).
- `executive-os-ci.yml` (origin/main; missing in local tree): expected CI for the Executive OS.
- Website deploy: `.github/workflows/deploy.yml` (npm ci, build, deploy dist to `zarlino-website` worker).
- Releases: none on GitHub (`releases: 0`).
- Local build/render tooling: `_rec/` render + verification scripts (PowerShell/Python), `tools/*` build helpers, `build*/` variant dirs — all local-machine dependent (Phase 8/9 to replace).

## 6. Local-machine dependencies (to eliminate)

1. **Local daemon**: `executive-os/scripts/local-daemon.ts` (+ `local-agent.ts`, `start-daemon.vbs`, `install-daemon-startup.ps1`, `stop-daemon.ps1`) — claims `local_tasks` leases, heartbeats, reports results. Source of the phantom task-ID failure mode the mandate §11 calls out.
2. **Local builds**: CMake/VS on this machine; `build-*` directories.
3. **Local renders**: `_rec/*.ps1` + Python analysis scripts; `artifacts/` produced locally.
4. **Ad-hoc scripts**: large number of `_*.mjs/_*.py` patch/probe scripts in `executive-os/scripts/` used for field-level repairs via local execution.

## 7. Secrets & credentials inventory (values redacted)

| Secret | Purpose | Present where | Notes |
| --- | --- | --- | --- |
| `DEEPSEEK_API_KEY` | AI provider | exec-os `.dev.vars` + prod | Provider currently 402 (balance) |
| `FOUNDER_AUTH_TOKEN` | API auth | exec-os `.dev.vars` + prod | |
| `GITHUB_TOKEN` | GitHub integration | exec-os `.dev.vars` + prod; website prod | fine-grained (no admin scope observed) |
| `CF_API_TOKEN` | Cloudflare API | exec-os `.dev.vars` + prod | **local copy invalid (401)**; prod copy unverified |
| `PAYSTACK_SECRET_KEY` | Payments | exec-os `.dev.vars` + prod; website prod | |
| `ZARLINO_LICENSE_PRIVATE_KEY` | License signing | website prod; **absent in exec-os prod** | source file exists locally `tools/keys/ztame_license_private.pem` |
| `ADMIN_TOKEN` | website admin API | **committed in `zarlino-website/wrangler.toml`** | SEC-1: rotate + move to secret |
| Azure signing creds (`AZURE_*`) | code signing | GitHub Actions secrets (repo/org) | not accessible locally |
| `CLOUDFLARE_API_TOKEN`, `CLOUDFLARE_ACCOUNT_ID` | website deploy | GitHub Actions secrets (zarlino-website repo) | not accessible locally |

machine-to-machine migration mechanisms are feasible via `wrangler secret put` (stdin, no echo) and GitHub Actions secrets API — no human copying required in principle (subject to admin-scope tokens).

## 8. Incident disposition inputs (mandate §19)

| Historical issue | Evidence today | Still relevant? | Migration impact |
| --- | --- | --- | --- |
| Phantom/unreliable local task IDs | Local daemon architecture present; mandate §11 targets it | YES | Durable job system (Phase 7) |
| Missing `ZARLINO_LICENSE_PRIVATE_KEY` | exec-os prod secret list + Doctor WARN | YES (exec-os) | Secret migration (Phase 3) |
| Unbound `ZARLINO_KV` | Bound in both wrangler.tomls; namespace exists; live counters route through it | Appears resolved in config; live write/read verification pending (Phase 4) | Cloud state verification |
| Interrupted jobs | Doctor FAIL: 2 resumable runs; incident `inc_interrupted_runs` open | YES | Durable job lifecycle + resume policy |
| Provider balance / failed runs | Doctor FAIL + incident `inc_provider_balance`; HTTP 402 since 2026-09-05 | YES (financial) | CEO approval to top up; budget policy |
| Source/binary drift | `origin/main` 2026-08-30 vs local 262-change tree; downloads served for ZTame 1.0 while repo shows 1.1 work; local flow build artifacts | YES | CI builds from GitHub (Phase 8) + release metadata |
| Broken plugin URLs | All 6 download URLs return 200 today | NO (for these URLs) | Keep link checks in Cloud Doctor |
| False license-delivery claims | website license endpoint configured; license issuance path exists; perceptual/licensing evidence trails present | TO VERIFY end-to-end later | Phase 3/20 acceptance tests |
| Website collection/dashboard errors | `/api/health` returns 500; admin dashboard reachable only with token; KV counters wired | PARTIAL | Cloud Doctor checks |
| Deployment inconsistencies | exec-os latest deploy 2026-09-13 vs website 2026-09-08; local CI workflow deletion; versions untagged (`Tag: -`) | YES | Deploy provenance + release metadata |
| Task contract gaps | Doctor WARN: 4 tasks without full contract | YES (governance data) | Task/approval model in MCP plane |

## 9. Migration-relevant gaps and blockers

1. **DeepSeek balance exhausted (402)** — the cloud agent cannot execute model calls; financial action required (CEO domain, mandate §10 CRITICAL). Blocks parts of Phases 5–12 verification.
2. **R2 not enabled** — artifact storage architecture (Phase 10) has no R2 substrate yet; needs account owner to enable R2 (billing).
3. **`CF_API_TOKEN` invalid** (local) — Cloudflare automation via API token is broken; wrangler OAuth works but is a human-account credential, not a service credential; Phase 3 should provision a scoped service token.
4. **`ADMIN_TOKEN` committed** — rotate immediately (HIGH), move to worker secret.
5. **No `gh`/`az` CLIs locally** — GitHub admin work (repo secret scanning enablement, visibility ops) and Azure work need either CLI installs or API-based equivalents.
6. **262 uncommitted local changes** — "GitHub records code truth" requires committing/pushing current WIP (Phase 2), and caution around the local deletion of `executive-os-ci.yml`.
7. **GitHub token scopes** — current token is repo-scoped (fine-grained); actions secrets list and org-level queries return 404/empty. Admin-scope token needed for Phase 2/3 automation.
8. **Perceptual evidence gap** for `prod_zscorch`, `prod_ztame` — external evaluator enrollment (Phases 11/13) has no evidence yet; tools exist (`perceptual.*`).

## 10. Evidence log (highlights)

- GitHub API: repo `private=true`, `default_branch=main`, `pushed_at=2026-08-30T12:14:56Z`; branches=14; issues=12; PRs=8; releases=0; workflows=[executive-os-ci, ZScorch Release Gate, ZTame Release Gate].
- `git rev-parse main origin/main` → both `0d56116c4cc6aab03566693122c6bc8f8c2a5483`; `git status --porcelain` → 262 entries.
- Wrangler OAuth `whoami` → account `ae604b602fdbcb1b48b41e975c6dbd19`.
- exec-os: 10 deployments (latest `2026-09-13T07:25:18Z`, v `53ae8b95…`); secret names as listed; R2 list → error 10042; KV list → `ZARLINO_KV`, `Admin`.
- Live `GET /api/health` → `{ok:true, doctorHealthy:false, schemaVersion:{total:27, ok:20, warn:5, fail:2}}`; non-OK list via authenticated `/api/doctor/logs`.
- Website: 10 deployments (latest `2026-09-08T11:24:12Z`, v `7002bfc3…`); secret names as listed; HTTP HEAD checks (site + 6 downloads = 200; `/api/health` = 500).
- Mandate recovery: saved file SHA-256 `43891C047BD6082CAC4C7743943F28DA316A9108E5B16D39EE468DCFEC7CB92F`.

## 11. Proposed next actions (approval-aware)

| # | Action | Risk | Gate |
| --- | --- | --- | --- |
| N1 | Commit + push the 262 local changes (or a curated subset) to restore source truth | MEDIUM | Founder review of diff |
| N2 | Rotate `ADMIN_TOKEN`; move to worker secret; purge from wrangler.toml | HIGH | Founder |
| N3 | Top up DeepSeek balance (unblocks the cloud agent) | CRITICAL (financial) | CEO |
| N4 | Enable R2 on the account | MEDIUM (billing) | CEO |
| N5 | Provision scoped service tokens (CF API token, admin GitHub token) as machine-to-machine credentials | HIGH | Founder |
| N6 | Set `ZARLINO_LICENSE_PRIVATE_KEY` on exec-os via `wrangler secret put` from `tools/keys/ztame_license_private.pem` (no human copying, no echo) | CRITICAL | Founder |
| N7 | Fix or remove website `/api/health` 500 | LOW | — |
| N8 | Re-run Cloud Doctor after N3/N4 to confirm FAIL/WARN reductions | LOW | — |
