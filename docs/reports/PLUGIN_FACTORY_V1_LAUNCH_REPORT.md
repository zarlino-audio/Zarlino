# Zarlino Plugin Factory â€” V1 launch report

**Date:** 2026-10-21
**Slice:** Engineering Slice 3 (customer-facing Factory, launchable V1)
**Verdict:** **LAUNCHABLE WITH CONDITIONS** â€” see Â§9. The customer flow, the
deterministic design pipeline, the build/QA gate and the data isolation are
implemented and proven by automated tests against fakes at the external
boundaries. Live cloud execution (a real dispatch, a real Paystack charge, a real
DAW load of a *newly* built artifact) could not be re-run from this environment
because it holds no GitHub, Cloudflare or Paystack credential; the procedure and
the evidence that *was* captured are stated explicitly in Â§6 and Â§10.

Everything below separates **VERIFIED** (I ran it and can point at the output)
from **ASSUMED** (reasoned, not executed here).

---

## 1. Production architecture actually implemented

```
browser
  â”‚  HTTPS, session cookie (HttpOnly/Secure/SameSite=Lax), same-origin CSRF
  â–¼
zarlinoaudio.com â€” Cloudflare Worker (zarlino-website)
  â”œâ”€ /factory/                 Astro page + React island (auth, dashboard, plans)
  â”œâ”€ /factory/project/         the workspace (describe â†’ plan â†’ refine â†’ UI â†’ build)
  â”œâ”€ /factory/billing/return/  payment return (server-side verification)
  â””â”€ /api/factory/*            customer API (see Â§7)
        â”œâ”€ FactoryIdentity DO   (one, "global")
        â””â”€ FactoryWorkspace DO  (one per customer, "user:<id>")
  â”‚  x-factory-gateway: <service secret>
  â–¼
os.zarlinoaudio.com â€” Executive OS Worker (zarlino-executive-os)
  â”œâ”€ POST /api/factory/interpret        DeepSeek, metered via meterModelCall
  â”œâ”€ POST /api/factory/builds           durable record + GitHub dispatch
  â”œâ”€ GET  /api/factory/builds/:id       reconcile (R2 evidence, run, deadline)
  â””â”€ POST /api/factory/builds/:id/completion   workflow callback (own token)
  â”‚  GITHUB_TOKEN (actions:write)
  â–¼
.github/workflows/factory-build.yml
  â”œâ”€ assemble  (NO secrets): kernel validation â†’ project generation â†’ VST3
  â”‚             compile â†’ 19-gate quality check ON THE BUILT BUNDLE â†’ package
  â””â”€ publish   (R2 token only): upload artifact + evidence, report completion
  â–¼
r2://zarlino-artifacts/factory-builds/<buildId>/{artifact, build-record.json, qa-report.json, â€¦}
  â–²
  â””â”€â”€ downloaded through the website Worker after owner + READY + QA + retention checks
```

## 2. Reused Zarlino infrastructure (not rebuilt)

| Reused | From | Use |
|---|---|---|
| Certified DSP modules (11) | `factory/modules`, `shared/dsp`, ZScorch `Saturator` | the only DSP a customer product can contain |
| PluginSpec schema + validation rules | `factory/src/PluginSpec.cpp`, `ModuleRegistry.cpp` | ported 1:1 into `src/factory/spec.ts` (same codes/paths) |
| UiPlan template rules | `factory/src/UiPlan.cpp` | ported into `src/factory/plan.ts` (strict nesting, same priority order) |
| Universal shell + UI compiler + project generator + QA gate | `factory/shell`, `factory/tools` | unchanged; the build still runs them |
| Cloud build workflow | `.github/workflows/factory-build.yml` | unchanged dispatch contract |
| R2 artifact upload tool | `factory/tools/r2-upload.mjs` | unchanged |
| Paystack account + currency config | website `wrangler.toml` | Pro checkout |
| Founder/admin HMAC session | website `src/auth.ts` (`adminAuthorized`) | founder metrics endpoint |
| DeepSeek client + cost meter | Executive OS `integrations/deepseek.ts`, `core/model/meter.ts`, `core/model/pricing.ts` | AI calls and their cost ledger |
| Durable Object + node:sqlite SQL pattern | Executive OS `db/sql.ts`, store/tests | the customer stores and their tests |
| GitHub dispatch + run reconciliation pattern | Executive OS `core/remote/execution.ts`, `core/factory/builds.ts` | factory dispatch/status |
| Site deploy + route contract + post-deploy gate | website `routes/contract.json`, `scripts/post-deploy-check.mjs` | acceptance now covers the factory routes |

## 3. New infrastructure

**Customer application (website repo, commit `27a750d`)**

| File | Responsibility |
|---|---|
| `src/factory/catalog.ts` | the 11 certified modules as data (parity-checked against the kernel) |
| `src/factory/spec.ts` | deterministic PluginSpec validation: schema + module resolution, kernel-identical codes and JSON paths, canonical form + SHA-256 identity |
| `src/factory/plan.ts` | human-readable build plan + the three interface projections (strictly nested, same priority order as the kernel) |
| `src/factory/ai.ts` | model gateway client, out-of-scope detection, validate-and-repair loop (â‰¤3 attempts) |
| `src/factory/pricing.ts` | **the one** pricing/entitlement and variable-cost configuration (FREE/PRO) |
| `src/factory/store.ts` | schema + typed repository for both stores; every workspace function takes `userId` |
| `src/factory/ops.ts` | the store operations (identity + workspace) shared by the DOs and tests |
| `src/factory/durable.ts` | `FactoryIdentity` / `FactoryWorkspace` Durable Objects (SQLite, own migrations) |
| `src/factory/auth.ts` | PBKDF2 credentials, hashed sessions, cookie/CSRF helpers, rate-limit policy |
| `src/factory/service.ts` | the flow: projects, interpret, build, status fold, download gate, billing, metrics |
| `src/factory/http.ts` | `/api/factory/*` router: CSRF â†’ rate limit â†’ session â†’ service â†’ JSON |
| `src/factory/billing.ts` | Paystack initialize/verify, HMAC-SHA512 webhook verification |
| `src/factory/explain.ts` | failure translation (raw evidence retained, never shown first) |
| `src/components/factory/*`, `src/pages/factory/*` | the customer UI |

**Orchestrator (Executive OS repo, commit `1ba6304`)**: migration v40
(`factory_builds`, `factory_build_events`), `core/factory/{builds,interpret,dispatch,routes}.ts`,
the `/api/factory/*` gateway, and `deepseekComplete` gaining
`response_format`/`fetchImpl`.

**Kernel (plugin repo, commit `8f0e1460`)**: `ZarlinoFactory_Check --catalog-json`,
the committed `factory/catalog/modules.json`, the parity gate in
`factory-gate.yml`, and `docs/factory/PLUGIN_FACTORY_V1.md`.

## 4. Automated test results â€” VERIFIED

| Suite | Files | Tests | Result |
|---|---|---|---|
| Website factory app (`zarlino-website`, `npx vitest run`) | 5 | **100** | PASS |
| â€” `spec.test.ts` (validation, identity, determinism) | | 39 | PASS |
| â€” `plan.test.ts` (plan + template nesting/density) | | 13 | PASS |
| â€” `auth.test.ts` (credentials, sessions, cookies) | | 18 | PASS |
| â€” `catalog-parity.test.ts` (TS catalogue vs kernel artefact) | | 3 | PASS |
| â€” `launch-simulation.test.ts` (10 launch checks + refinement + metrics + 16 security cases) | | 27 | PASS |
| Website strict typecheck (`tsc -p tsconfig.factory.json`) | â€” | â€” | PASS |
| Website production build (`astro build`) | â€” | 18 pages | PASS |
| Website worker bundle (`wrangler deploy --dry-run`) | â€” | 9 bindings | PASS |
| Executive OS full suite (`npx vitest run`) | 47 | **421** | PASS |
| â€” factory gateway (`test/factory-gateway.test.ts`) | | 20 | PASS |
| â€” factory build lifecycle (`test/factory-builds.test.ts`) | | 8 | PASS |
| Executive OS typecheck (`tsc --noEmit`) | â€” | â€” | PASS |

Kernel/factory C++ suite: **not re-run here** (no MSVC on this machine). Its last
CI run is green (Slice 1/2 evidence: `Factory.Unit` 10 suites / 3238 assertions,
recorded in commit `f6902a8f`). The new `--catalog-json` code path is
compile-verified by the factory gate in CI, not locally â€” **ASSUMED until that
run**.

**The catalogue gate already paid for itself once.** While building the
comparison tool I found that the TypeScript catalogue had declared the
compressor's `Detection` control as a two-position switch; the kernel builds it
continuous (it uses the eight-argument `makeParameter`, which never sets
`stepped`). The site would have previewed a switch the plugin does not have.
Fixed in `71622bc`; the kernel's only stepped parameters are `equalPower`,
`shape`, `slope`, `polarity` and the saturation `mode`.

## 5. End-to-end evidence â€” VERIFIED (simulated externals) / NOT RE-RUN (live)

`test/factory/launch-simulation.test.ts` drives the **real** HTTP router, CSRF,
rate limits, sessions, entitlement checks, AI repair loop, build request, status
fold, download gate and payment webhook. Only three externals are simulated: the
model, the build orchestrator, and R2.

| Launch check | Case | Result |
|---|---|---|
| 1. Free user builds and downloads a valid plugin | `1. a free user signs upâ€¦` | PASS |
| 2. A second user builds a materially different plugin | `2. a second userâ€¦` (different spec, template, hash, product) | PASS |
| 3. Pro payment updates entitlements | `3. a verified Pro paymentâ€¦` (grant once, replay grants nothing) | PASS |
| 4. Insufficient entitlement blocks an expensive action | `4. an exhausted allowanceâ€¦` (402 `BUILD_ALLOWANCE`, no dispatch) | PASS |
| 5. Unsupported DSP fails safely before build | `5. an unsupported processorâ€¦` (422, zero model calls, zero dispatches) | PASS |
| 6. Malicious input cannot escape PluginSpec constraints | `6. a malicious or malformed designâ€¦` (3 attempts, all rejected, no dispatch, no spec stored) | PASS |
| 7. Failed QA produces no releasable artifact | `7. a failed quality gateâ€¦` (FAILED, download 409 `NOT_READY`) | PASS |
| 8. User A cannot access User B's data | `8. user A cannot reachâ€¦` (project/spec/build/artifact/rebuild/delete all 404) | PASS |
| 9. A READY artifact is served installable | `9. a READY artifactâ€¦` (octet-stream, `nosniff`, sha256 header, no-store) | PASS |
| 10. Build and AI costs appear in metering | `10. build and AI costsâ€¦` (itemised usage rows + build row) | PASS |

Additional verified behaviours: a build that finishes *immediately* is folded to
READY exactly like a polled one; conversational refinement creates a new spec
revision and the next build uses the new identity (and the model receives the
current document in refine mode); an identical spec+template is *reused* â€” the
same immutable build is handed back, still downloadable, costing no allowance;
a held build reports BUILDING then READY; the funnel, build durations, unit costs
and module-usage counters are recorded and readable by the founder.

Two product defects were found and fixed by these tests rather than by a
customer: a reused build originally got a *copy* row with no artifact record
(its download would have 409'd), and the compressor's `Detection` control was
declared stepped in the catalogue while the kernel builds it continuous.

### Live end-to-end (real dispatch â†’ real VST3 â†’ download) â€” NOT RE-RUN

No GitHub token, Cloudflare token or Paystack key is present in this
environment, so I could not dispatch a build or deploy. What *is* on record from
the last real cloud run (workflow run `39497544404`, 2026-10-20, captured under
`work/logs-39497544404/`):

```
gating the built bundle at ...\VST3\Example Channel Strip.vst3
=== Example Channel Strip quality gate ===
PASS  product.initialise  - 9 host parameters, 9 controls
PASS  audio.smoke / audio.silence / audio.impulse / audio.nonfinite
PASS  params.extremes / params.automation (400 steps)
PASS  channels.mode / rates.sweep (6) / blocks.sweep (8 + oversized)
PASS  lifecycle.prepare (bit-identical) / bypass.host (bit-exact)
PASS  state.roundtrip (1417 bytes) / latency.consistency
PASS  gain.explosion / cpu.ceiling (0.0089x real time) / realtime.tail
PASS  category.limiter / bundle.integrity (8,039,936 bytes, PE64)
=== QUALITY GATE PASSED ===
```

That run proved the compiled Windows VST3 **loads through JUCE's plugin host and
processes audio correctly**. It then failed in *publish* because the artifact name
contains spaces; that defect is fixed on `main` (commit `a610adeb`) and the fix
has **not** yet been exercised by a new live run â€” **ASSUMED**.

## 6. Measured numbers â€” VERIFIED sources, one ESTIMATE

| Quantity | Value | Source |
|---|---|---|
| VST3 compile (windows-2022, Release, channel strip) | **292.5 s** | run `39497544404`, `compile took 292.485 s` |
| Quality gate on the built bundle | **0.093 s** | same run |
| End-to-end build wall time (measured stages) | â‰ˆ **4.9 min** | 292.5 s + 0.1 s + packaging/upload |
| Artifact size | **8,039,936 bytes** (7.7 MB) | `bundle.integrity` gate |
| Build CPU headroom | 0.0089Ã— real time | `cpu.ceiling` gate |
| Estimated variable cost per build | **â‰ˆ $0.81** | `estimateBuildVariableCost`: 5 billed minutes Ã— $0.16 + $0.01 + storage |
| Estimated AI cost per interpretation | **â‰ˆ $0.0014** | prompt â‰ˆ2.5k + completion â‰ˆ0.7k at DeepSeek list prices â€” **ESTIMATE, no live key** |
| Contribution margin at Pro (GHS 199 â‰ˆ $12.8) | â‰ˆ **$11.3/month** after 1 build + 40 AI calls | ESTIMATE from the two rows above |

The earlier run (`39495305694`) measured 398.1 s compile; the difference is
runner warmth, so budget 8â€“15 minutes end to end including queueing.

## 7. Exact production endpoints

**Public pages** (`https://zarlinoaudio.com`)
* `/factory/` â€” account + dashboard + plans
* `/factory/project/?id=<projectId>` â€” the workspace
* `/factory/billing/return/?reference=<ref>` â€” payment return

**Customer API** (`https://zarlinoaudio.com`, JSON, session cookie)
* `POST /api/factory/auth/signup` Â· `POST /api/factory/auth/login` Â· `POST /api/factory/auth/logout`
* `GET  /api/factory/me`
* `GET  /api/factory/plans`
* `GET  /api/factory/projects` Â· `POST /api/factory/projects`
* `GET|PATCH|DELETE /api/factory/projects/:id`
* `POST /api/factory/projects/:id/interpret`
* `POST /api/factory/projects/:id/spec`
* `GET  /api/factory/projects/:id/templates`
* `POST /api/factory/projects/:id/builds`
* `GET  /api/factory/builds` Â· `GET /api/factory/builds/:id`
* `GET  /api/factory/builds/:id/artifact` (owner + READY + QA + retention)
* `POST /api/factory/billing/checkout` Â· `GET /api/factory/billing/verify?reference=`
* `POST /api/factory/billing/webhook` (Paystack, HMAC-verified)
* `GET  /api/factory/admin/metrics` (founder session)

**Orchestrator** (`https://os.zarlinoaudio.com`, service or report token)
* `POST /api/factory/interpret` Â· `POST /api/factory/builds`
* `GET  /api/factory/builds/:id` Â· `POST /api/factory/builds/:id/completion`

## 8. Release identifiers

| Component | Commit |
|---|---|
| `zarlino-audio/zarlino-website` (customer app) | **`038571f`** (feature `27a750d`, security `c01ff33`, catalogue fix `71622bc`, analytics `31317c4`, reuse fix `038571f`) |
| `zarlino-audio/zarlino-executive-os` (gateway) | **`5840651`** (feature `1ba6304` + key binding) |
| `zarlino-audio/Zarlino` (kernel catalogue + gate + docs) | **`369bfd0d`** (feature `8f0e1460`, comparison + report `369bfd0d`) |

Deployment is manual (as documented in `docs/factory/PLUGIN_FACTORY_V1.md`); no
release tag has been created.

## 9. Security review

An independent review agent examined the three commits (website `27a750d`, OS
`1ba6304`, kernel `8f0e1460`) plus the working-tree changes. It reported **no
Critical and no High findings**. Result:

| # | Severity | Area | Finding | Status |
|---|---|---|---|---|
| 1 | ðŸŸ  MEDIUM | entitlement enforcement (`service.ts` + `ops.ts`) | allowance was checked in one store round trip and spent in another, so concurrent requests each saw the pre-spend snapshot (10 parallel build requests all passed on a 2-build plan) | **FIXED** in `c01ff33`: `createBuild` checks and inserts inside one workspace operation; `reserveAiRequest` increments before the model call. Tests: two builds race for the last slot (202 + 402), 41 reservations against a 40 allowance (40 + refusal) |
| 2 | ðŸŸ¡ LOW | artifact key (`builds.ts`, `service.ts`) | `completeBuild` stored the artifact key verbatim and `isSafeFactoryArtifactKey()` was never called; the website checked only the `factory-builds/` prefix, not the build id | **FIXED** in `5850651` + `c01ff33`: both sides now require `factory-builds/<this build id>/` and reject traversal/backslashes; tests for a foreign key and a traversal key |
| 3 | âšª LOW | signup (`http.ts`) | `409 EMAIL_TAKEN` is an account-enumeration oracle | **ACCEPTED (disclosed)**: throttled to 5/hour per IP, login is generic and timing-uniform, and closing it properly needs a transactional email path V1 does not have. Listed in Â§10 |
| 4 | â€” (unscored) | status polling (`http.ts`) | `GET /api/factory/builds/:id` had no limit, and each poll spends shared GitHub API quota | **FIXED** in `c01ff33`: per-account limit of 300 per 10 minutes |
| 5 | ðŸŸ  MEDIUM (found by test, not by the review) | build reuse (`service.ts`) | a reused build was recorded as a *copy* with `reused_from` set but no artifact row, and its key belonged to the original build â€” the download gate would have refused it, showing a customer a link that 409s | **FIXED**: reuse now returns the original immutable build (same id, same artifact, still downloadable) and spends no allowance |

Verified sound by the review (with the guard that makes each safe): cross-account
access (workspace addressed only by the session's user id; every statement filters
`user_id`; foreign and missing rows are the same 404, so no oracle);
`buildId â†’ userId` index (written, never used to address a workspace); path
traversal and key construction; `Content-Disposition` header injection (sanitised);
downloads fail closed (owner + READY + `qa_passed` + artifact row + retention); the
QA gate rule and terminal-state finality; credentials and sessions; CSRF and
preflight; Paystack (HMAC over raw body, provider re-verification, amount floor,
server-set plan metadata, idempotent grants, ownership check); SQL construction
(no interpolation, clamped limits); prompt injection (data envelope + deterministic
re-validation, and a spec cannot express paths, commands or flags); GitHub dispatch
(inputs are env values, never `run:` lines; assemble job holds no secrets and
`contents: read`); secret handling and log content; rate-limit identity
(`cf-connecting-ip` is edge-set); founder metrics and OS gateway auth (fail-closed,
constant-time, separate credentials).

The six security questions the brief lists map to tests as follows:

| Brief requirement | Test |
|---|---|
| cross-account artifact access | `8. user A cannot reachâ€¦` (download 404) + `refuses a stored artifact key that points at another build` |
| cross-account project access | `8. user A cannot reachâ€¦` (GET/POST spec/rebuild/delete all 404) |
| ID enumeration | `8.` (list endpoint returns only own builds) + the same-404 rule |
| malformed PluginSpecs | `6. a malicious or malformed designâ€¦`, plus 39 spec-validation cases |
| prompt injection â†’ arbitrary code | `6.` (rejected 3Ã—, nothing dispatched) + review of the OS prompt/envelope |
| path traversal / shell injection | `serves a JSON 404 for an unknown factory path` + the artifact-key binding tests + review of the workflow (inputs as env, no `run:` interpolation) |
| build dispatch abuse / duplicate paid execution | `cannot spend the last build allowance twice when two builds race` + `reuses an identical immutable buildâ€¦` |
| webhook replay | `verifies webhook signatures, ignores replays and never trusts the body` |
| payment spoofing | `refuses an underpaid transaction` + `refuses a payment reference that belongs to another account` |
| rate-limit bypass | `rate-limits signup from one address` (+ per-account limits on build / interpret / status / download) |
| secret exposure / build-log leakage | `never exposes a stored secret through the customer API` + `never exposes raw logs or internal keys in a public build view` |

## 10. Known non-blocking limitations

1. **Live cloud path not re-run in this session** (no credentials): dispatch,
   publish-after-the-space fix, Paystack charge and a DAW load of a newly built
   artifact. Procedures are in Â§10.1.
2. **FL Studio / second-host verification not performed.** The gate loads the
   built VST3 through JUCE's host and validates audio; that is strong but it is
   not a DAW. Manual procedure in Â§10.2.
3. **No application-level export/restore for the customer stores.** Cloudflare
   replicates DO SQLite; a per-customer export tool is a V1 gap.
4. **Single-region, single-DO-per-customer design.** Fine for launch volume; the
   identity DO serialises all signups/logins/rate-limit writes.
5. **No code signing / installer** for factory artifacts (VST3 zip only).
6. **`build-record.json` presence is the READY trigger.** It is written only by
   the publish job after the gate passes; if that job were ever changed to upload
   it earlier, the gate rule in `completeBuild` (qaPassed && qaReport.passed)
   still holds, but the audit trail would be weaker.
7. **Founder metrics are read from a 30-day event window** and are not a billing
   ledger; plan changes are recorded as entitlement rows.
8. **Signup reveals whether an address is already registered** (`409` + a clear
   message) â€” an account-enumeration oracle, throttled to 5/hour/IP, with a
   generic, timing-uniform login. Closing it needs a transactional email path.

### 10.1 Live launch verification procedure (to run on a machine with credentials)

```powershell
# 1. Deploy both workers (see docs/factory/PLUGIN_FACTORY_V1.md Â§4) and set:
#    FACTORY_GATEWAY_SECRET (both), BUILD_REPORT_TOKEN (OS), PAYSTACK_SECRET_KEY (site)
# 2. Sign up two fresh accounts on https://zarlinoaudio.com/factory/
# 3. Account A: "a warm punchy compressor with a gentle saturation and one warmth knob"
#    -> expect a plan with 2 modules, 3-4 controls, 3 templates
#    -> choose Standard -> Authorise and build
# 4. Watch /api/factory/builds/<id>; expect QUEUED -> READY within ~5-10 min
# 5. Download the zip; import to the DAW; play audio through it
# 6. Account B: "airy vocal cleanup with a de-esser" -> expect an honest refusal
# 7. Account A: attempt GET /api/factory/builds/<B's build> -> expect 404
# 8. Check GET /api/factory/admin/metrics (founder session) -> funnel counts move
# 9. Confirm r2 objects exist: factory-builds/<id>/{build-record.json,qa-report.json}
```

### 10.2 DAW verification procedure

Install the downloaded bundle to `C:\Program Files\Common Files\VST3\`, rescan in
FL Studio (Options â†’ Manage plugins â†’ Find more plugins), load it on a stereo
track, confirm the three interface variants draw all declared controls, automate
a control, bypass, save/reload the project, then repeat in one other Windows VST3
host (the gate's own JUCE host pass is the automated proxy).

## 11. What I would fix before calling this fully GA

* Run Â§10.1 end to end and attach the run id, the `build-record.json`, and a
  screenshot of the plugin loaded in FL Studio.
* Add the per-customer export/restore tool (limitation 3).
* Add a synthetic canary that builds a fixed spec nightly, so `publish` regressions
  (like the space-in-name defect) surface without a customer.

