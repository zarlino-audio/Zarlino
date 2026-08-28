# Deployment

## Environments

| Environment | Purpose | Credentials |
|-------------|---------|-------------|
| Local (`wrangler dev`) | Development | `.dev.vars` |
| Staging (`wrangler deploy --env staging`) | Integration | separate `wrangler.staging.jsonc` + staging secrets |
| Production (`wrangler deploy`) | Live | separate Worker + production secrets |

**Never allow local development to access production credentials by default.** Secrets
are injected per environment; `.dev.vars` is git-ignored and the CI deploys only from
the `main` branch through environment-gated workflows.

## Prerequisites

- Node ≥ 20, npm
- A Cloudflare account with Workers + Durable Objects enabled
- At least one model provider key (DeepSeek, OpenAI, Anthropic, or Workers AI) — or none
  to use the built-in mock provider

## 1. Local development

```sh
cd executive-os
npm install
cp .dev.vars.example .dev.vars      # fill in keys + FOUNDER_AUTH_TOKEN
npm run dev                          # Worker on http://localhost:8787

# console (separate terminal):
npm --prefix web install
npm --prefix web run dev             # http://localhost:5173 (proxies /api → :8787)
```

Health check: `GET http://localhost:8787/api/health`.

## 2. Configure secrets (never committed)

```sh
wrangler secret put DEEPSEEK_API_KEY
wrangler secret put OPENAI_API_KEY
wrangler secret put ANTHROPIC_API_KEY
wrangler secret put GITHUB_TOKEN
wrangler secret put CF_API_TOKEN
wrangler secret put CF_ACCOUNT_ID
wrangler secret put FOUNDER_AUTH_TOKEN   # generate: node -e "console.log(require('crypto').randomBytes(32).toString('hex'))"
```

Non-secret routing/budget/threshold config lives in `wrangler.jsonc` `vars`.

## 3. Staging

1. Create `wrangler.staging.jsonc` (extends the base config with a staging name,
   a separate DO binding, and `ENVIRONMENT=staging`).
2. Set the same secrets under the staging Worker name.
3. Deploy: `npx wrangler deploy --env staging`.

## 4. Production

1. CI must pass (`.github/workflows/executive-os-ci.yml`: typecheck, unit + integration
   tests, web build).
2. Production deploy is a **manual approval** step (GitHub `environment: production`).
3. `npx wrangler deploy` (uses `wrangler.jsonc`).

## 5. CI/CD

`.github/workflows/executive-os-ci.yml` (paths: `executive-os/**`):

- `lint-typecheck` — `tsc --noEmit` + web build
- `unit-integration` — `vitest run`
- `deploy-staging` — automatic on `main` (environment: staging)
- `deploy-production` — manual approval (environment: production)

Secrets used by CI: `CLOUDFLARE_API_TOKEN`, `CLOUDFLARE_ACCOUNT_ID`.

## 6. Data & migrations

- Schema is applied idempotently by `ExecutiveAgent.onStart()` (`db/migrations.ts`).
- The checked-in migration is `migrations/0001_initial.sql`.
- To add a migration: bump `SCHEMA_VERSION`, append to `INITIAL_SCHEMA_SQL` **and**
  `migrations/0002_*.sql`. Migration runner applies the new version on next wake.
- Durable Object SQLite persists across deploys; do not `wrangler delete` the Worker
  or state is lost.

## 7. Operational notes

- Scheduled briefings: daily (default `0 8 * * *`) and weekly (default `0 9 * * 1`),
  configured via `DAILY_BRIEF_CRON` / `WEEKLY_REVIEW_CRON`. Cron schedules are
  idempotent — safe to set in `onStart()` on every wake.
- Long-running work (brief generation) runs in durable fibers (`runFiber`) so it
  survives DO eviction; interrupted fibers recover via `onFiberRecovered`.
- Monitor AI cost in the console (Finance → AI Cost) and via Workers observability.
