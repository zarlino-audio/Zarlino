# Zarlino Plugin Factory - V1 production architecture and operations

This document describes what is running, where the seams are, and what to do when
something breaks. It is written to be followed at 2 a.m. by someone who did not
build it.

---

## 1. What the system is

A customer describes an audio processor in plain English. Zarlino turns that
description into a **PluginSpec** (strict, versioned JSON), shows the customer
exactly what will be built and how it will look, and - once the customer
authorises it - compiles a Windows VST3 **entirely from certified Zarlino DSP
modules** in the cloud, runs an automated audio/quality gate on the built bundle,
and releases the artifact only if that gate passes.

No generated C++ ever ships. The customer's text never reaches a compiler, a shell
or a file path: the only things that reach the build are numbers and identifiers
drawn from the certified module catalogue.

```
                       +----------------------------------------------+
  customer (browser)   |  zarlinoaudio.com  (Cloudflare Worker)       |
  -------------------> |  /factory/*            Astro pages + React   |
                       |  /api/factory/*        customer API          |
                       |                                              |
                       |  FactoryIdentity DO   accounts, sessions,    |
                       |    (one, "global")    plans, payments,       |
                       |                       rate limits, analytics |
                       |  FactoryWorkspace DO  projects, specs,       |
                       |    (one per customer) builds, artifacts,     |
                       |                       usage / metering       |
                       +---------------+------------------------------+
                                       | x-factory-gateway (service secret)
                                       v
                       +----------------------------------------------+
                       |  os.zarlinoaudio.com (Executive OS Worker)   |
                       |  /api/factory/interpret  -> DeepSeek, metered|
                       |  /api/factory/builds     -> GitHub dispatch  |
                       |  /api/factory/builds/:id -> status reconcile |
                       |  factory_builds (durable, migration v40)     |
                       +---------------+------------------------------+
                                       | GITHUB_TOKEN (actions:write)
                                       v
                       +----------------------------------------------+
                       | .github/workflows/factory-build.yml          |
                       |  assemble (NO secrets): validate spec with   |
                       |    the C++ kernel -> generate project ->      |
                       |    compile VST3 -> run the 19-gate quality    |
                       |    check on the BUILT BUNDLE -> package+hash  |
                       |  publish: upload evidence + artifact to R2,   |
                       |    then report completion                    |
                       +---------------+------------------------------+
                                       v
                       r2://zarlino-artifacts/factory-builds/<buildId>/
                         artifact/<Product>-v1.vst3.zip
                         build-record.json   (written only after the gate passed)
                         qa-report.json, provenance.json, ui-plan.json, spec.zfactory.json
```

The customer application downloads the artifact **from R2 through the Worker**,
after checking owner + `READY` + `qa_passed` + retention. There is no public
bucket and no signed URL that outlives the check.

---

## 2. Stores and the isolation rule

| Store | Instances | Holds |
|---|---|---|
| `FactoryIdentity` DO | one (`global`) | users, password hashes, sessions, plan + entitlement grants, payment events (idempotency), rate-limit buckets, funnel events, `buildId -> userId` index |
| `FactoryWorkspace` DO | **one per customer** (`user:<id>`) | projects, spec revisions, conversation, builds, artifact metadata, usage/metering |

**Isolation is structural.** The workspace object is addressed by
`idFromName("user:" + session.userId)`; the user id comes from the verified
session cookie and from nothing else. A request cannot name another customer's
object, so an identifier cannot be swapped to reach someone else's data. On top
of that, every workspace statement filters on `user_id`, and the service client
injects `userId` at the binding (a service function cannot forget it).

A cross-account read returns **404** - the same answer as a row that never
existed, so the API cannot be used to confirm that another customer's project
exists.

---

## 3. Authentication and authorisation

* **Customer auth** (this system): email + password, PBKDF2-HMAC-SHA-256 with a
  per-user 16-byte salt and 210,000 iterations; sessions are 256-bit random
  tokens stored only as SHA-256 hashes, in an HttpOnly + Secure + SameSite=Lax
  cookie with server-side expiry (14 days).
* **Founder/admin auth** (unchanged): `ADMIN_SESSION_KEY` HMAC session used by
  `/api/admin/*`. Customer code never reads it; the only route that does is
  `GET /api/factory/admin/metrics`, which requires a founder session.
* **Executive OS founder auth** is a third, separate credential
  (`FOUNDER_AUTH_TOKEN`). The factory gateway on the OS does **not** accept it -
  `/api/factory/*` on the OS requires `FACTORY_GATEWAY_SECRET` (service) or, for
  the completion callback only, `BUILD_REPORT_TOKEN`.
* CSRF: every mutating customer route requires a same-origin `Origin` (or
  `Sec-Fetch-Site: same-origin`). Cross-site POSTs are refused with 403 even with
  a valid cookie.

### Secrets (never in the repo)

| Where | Secret | Purpose |
|---|---|---|
| Website Worker | `FACTORY_GATEWAY_SECRET` | calls the OS factory gateway |
| Website Worker | `PAYSTACK_SECRET_KEY` | initialize/verify + webhook HMAC |
| Website Worker | `ADMIN_SESSION_KEY` | founder metrics |
| Executive OS | `FACTORY_GATEWAY_SECRET` (same value) | authenticates the website |
| Executive OS | `BUILD_REPORT_TOKEN` | authenticates the workflow callback |
| Executive OS | `GITHUB_TOKEN` | `actions:write` on `zarlino-audio/Zarlino` |
| Executive OS | `DEEPSEEK_API_KEY` | model calls (metered) |
| GitHub Actions | `CLOUDFLARE_API_TOKEN` (scoped to the artifacts bucket) | publish only |

The compile/test/package job holds **no** secrets at all. The publish job holds
only the scoped R2 token and never compiles.

---

## 4. Deploy

### Website

```powershell
cd zarlino-website
npm ci
npx tsc --noEmit -p tsconfig.factory.json     # strict check of the factory app
npx vitest run test/factory                    # unit + isolation + launch simulation + security
npm run build                                  # Astro (includes /factory/*)
npx wrangler deploy --dry-run --outdir .wrangler/ci-dist
npx wrangler secret put FACTORY_GATEWAY_SECRET
npx wrangler secret put PAYSTACK_SECRET_KEY
npx wrangler deploy
```

The first deploy of this version registers the two Durable Object classes
(`[[migrations]] tag = "v1"`). The objects create their own schema on first
touch - there is no manual migration step, and re-running it is a no-op.

### Executive OS

```powershell
cd zarlino-executive-os
npm ci
npm run typecheck
npx vitest run
npx wrangler secret put FACTORY_GATEWAY_SECRET     # same value as the site
npx wrangler secret put BUILD_REPORT_TOKEN
npx wrangler deploy                                # applies migration v40
```

The OS reconciles every non-terminal factory build on the 15-minute cron
(`POST /api/_factory-reconcile`, internal credential only), so a build reaches
TERMINAL state - and the customer sees FAILED or READY - even if they closed the
tab. Status therefore never depends on the browser polling.

### Post-deploy acceptance

`zarlino-website/scripts/post-deploy-check.mjs` consumes `routes/contract.json`,
which now includes `/factory/`, `/factory/project/`,
`/factory/billing/return/`, `/api/factory/plans` and `/api/factory/me` (401 when
signed out).

The factory has two CI gates, one per repository that owns the code:
`.github/workflows/factory-gate.yml` in `zarlino-audio/Zarlino` (kernel build,
`ctest Factory.Unit`, example-spec rendering, catalogue parity) and in
`zarlino-audio/zarlino-website` (strict typecheck, the factory test suites, the
Astro build, and a `wrangler deploy --dry-run` bundle check).

---

## 5. Monitoring

| Signal | Where | Expected |
|---|---|---|
| `GET /api/health` | website | 200 |
| `GET /api/ready` | website | 200, reports capability |
| `GET /api/factory/plans` | website | 200 JSON (public, cheap) |
| `GET /api/factory/admin/metrics` | website (founder session) | funnel, rates, unit costs, module usage |
| `GET /api/health` | OS | 200 |
| `factory_builds` rows | OS console | no row stuck non-terminal beyond `FACTORY_MAX_BUILD_MINUTES` |
| Doctor check `security.internal_secret` | OS | PASS (fails closed when unset) |
| Unhandled route errors | website observability log, prefix `[factory]` | none; each line is a name + 300-char message, never a body |

The funnel metrics endpoint returns signup -> prompt -> valid spec -> build
requested -> build passed -> download -> repeat build -> paid conversion, plus
build success rate, unsupported-request frequency, per-module usage, median build
duration, mean variable cost per build, mean AI cost per design and a modelled
monthly contribution margin that states its utilisation inputs.

---

## 6. Recovery and rollback

**Roll back the website code**

```powershell
cd zarlino-website
npx wrangler rollback            # to the previous deployment
```
Customer data lives in Durable Objects and is untouched by a code rollback. The
schema is additive only (`CREATE TABLE IF NOT EXISTS`), so an older worker keeps
working against the same objects.

**Roll back the OS**

```powershell
cd zarlino-executive-os
npx wrangler rollback
```
Migrations are forward-only and additive; a rollback to a build that does not
know about `factory_builds` simply ignores the table.

**Stop the factory without taking the site down**

1. `wrangler secret delete FACTORY_GATEWAY_SECRET` on the OS.
   -> `/api/factory/*` on the OS now refuses everyone (fail closed).
2. The website's build endpoint then returns `BUILD_SERVICE_UNAVAILABLE`
   (translated in the UI as "Build service unavailable") and no build is
   dispatched. Accounts, projects and existing downloads keep working.

**Stop new builds only**

Remove `workflow_dispatch` from `factory-build.yml` on `main`. In-flight builds
finish; new dispatches fail at GitHub and the build is recorded `FAILED`
(`DISPATCH_FAILED`) - never left half-built.

**Recover a build stuck in a non-terminal state**

The orchestrator does this automatically: on every status read it checks R2 for
`build-record.json` (the gate's own evidence), then the GitHub run conclusion,
then the build deadline. To force it:

```
GET /api/factory/builds/<buildId>          (website, as the owner)
GET /api/factory/builds/<buildId>          (OS, x-factory-gateway)
```
A build past `FACTORY_MAX_BUILD_MINUTES` (default 45) becomes `FAILED`
(`BUILD_TIMEOUT`). Because a re-dispatch uses the same immutable build id, create
a **new** build rather than trying to reuse a failed one.

**Recover a customer's access**

* Sessions are server-side: delete rows from `sessions` in the identity object to
  force re-login (an expired session is also removed on use).
* A plan can be corrected with the identity operation `grantPlan` (records an
  entitlement row and the payment event, both idempotent by reference).
* Never edit `usage_events` to "reset" an allowance: record a new grant instead,
  so the meter stays reconcilable.

**Data export / backup**

`/api/_backup-export` on the OS exports its own database daily to R2 (existing
pipeline). The customer stores are Cloudflare Durable Object SQLite with platform
replication; an application-level export is a known gap for V1 (see the launch
report, "known limitations").

---

## 7. Incident playbook

| Symptom | Likely cause | Action |
|---|---|---|
| Signups 500 | identity DO unavailable | check `wrangler tail`; DO classes are registered by the migration - confirm `[[migrations]]` is present in the deployed version |
| Assistant always "unavailable" | `DEEPSEEK_API_KEY` missing/exhausted on the OS | check `GET /api/ai/exec-probe` on the OS; top up the provider account |
| Every build fails `DISPATCH_FAILED` | `GITHUB_TOKEN` expired or lacking `actions:write` | rotate the token; verify `GET /repos/.../actions/workflows/factory-build.yml` returns 200 |
| Builds succeed on GitHub, stay `QUEUED` in the product | OS cannot read R2 evidence | confirm the OS `ARTIFACTS` binding still points at `zarlino-artifacts` and the publish job ran |
| Download returns 410 | artifact aged out of R2 retention | expected; ask the customer to rebuild (an identical spec hands back the existing build if it is still present, otherwise it is recompiled) |
| Downloads 503 | `FACTORY_ARTIFACTS` binding missing | re-deploy with the binding; no data loss |
| Webhook 401 | wrong `PAYSTACK_SECRET_KEY` | rotate to the live secret; a `charge.success` can always be re-verified by the customer's return page |

---

## 8. Deliberate non-goals for V1

* Novel DSP module generation (a customer cannot add a module that does not
  exist), and therefore reverb/delay/instruments/amp sims.
* macOS/AU/AAX targets, code signing, installers.
* Sharing a build between two customer accounts (a build is private to its owner).
* Application-level customer data export.