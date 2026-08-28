# Security

This document describes the security model of the Zarlino Executive OS (spec §24).

## Threat model

The system holds institutional state and can take **autonomous low-risk actions**
and **propose higher-risk actions**. The highest-impact risks are:

1. An attacker (or prompt injection) causing the agent to take an action it should not.
2. The model silently rewriting history or doctrine.
3. Secret leakage (API keys, founder token).
4. Budget blowout (uncontrolled model spend).
5. Data loss / deletion of durable state.

## Controls

### 1. Authentication & authorization

- Every `/api/*` request requires `Authorization: Bearer <FOUNDER_AUTH_TOKEN>`.
- In production a missing token denies all requests. The dev-only allowance
  (`ENVIRONMENT=development`) must never be shipped to production.
- The worker→agent RPC is internal (Durable Object RPC) — no public surface beyond
  the REST API and the agent route.

### 2. Authority engine (backend-enforced)

- Every tool/action declares `permission`, `riskLevel`, `costEstimate`, `reversible`,
  `auditRequired`.
- `evaluateAction()` decides ALLOW / APPROVAL / DENY using permission, configured
  thresholds, risk, reversibility, autonomy mode, and emergency switches.
- NEVER-autonomous permissions: `delete`, `ownership_transfer`.
- HIGH risk requires approval; CRITICAL is never autonomous.
- Emergency pause / disable-spending / disable-deployments are enforced server-side
  in the engine — **prompt instructions are not security controls** (spec §11).

### 3. Tool isolation

- The model can only call tools registered in `ToolRegistry`. Unknown tool names are
  rejected.
- Every tool input is validated against its zod schema before execution.
- External content (GitHub issues, web content, emails, documents) is treated as
  **untrusted** and cannot override system policies: tool results are data, never
  instructions. The base policy prompt says so, and the tool boundary enforces it.

### 4. Immutability & doctrine protection

- Historical decisions, financial records, audit logs, approvals, and execution
  records are never silently replaced. Corrections are appended with
  `original_record / correction / corrected_by / timestamp / reason` (spec §21).
- Doctrine changes are propose → Founder-approve only. `assertDoctrineImmutable`
  flags any attempt to silently change active doctrine (spec §5, §10-rule).

### 5. Secret isolation

- Secrets live in `.dev.vars` (local) and `wrangler secret put` (deploy). They are
  never in source, `wrangler.jsonc`, or the repo.
- `.gitignore` excludes `.dev.vars`, `.env`, `dist`, `.wrangler`.
- `.dev.vars.example` documents which secrets exist without values.

### 6. Cost controls

- Every model call records provider/model/tokens/cost/task/session.
- Budgets: `AI_DAILY_BUDGET`, `AI_MONTHLY_BUDGET`, `AI_PER_TASK_BUDGET`,
  `AI_PER_RUN_BUDGET`. On exceed: **STOP → notify → do not silently continue**
  (spec §34).

### 7. Validation & failure handling

- Input validation: zod at the API edge and at every tool boundary.
- Output validation: structured model output is JSON-parsed and fallback-safe
  (never trust raw model text as state).
- Idempotency: cron schedules are idempotent; durable fibers use `stash`/recovery;
  external actions are designed to be re-runnable.
- Retries/timeouts: provider calls have timeouts; the decision engine surfaces
  failures instead of fabricating.
- Rate limiting is applied at the Cloudflare edge (Workers) — configure per account.

### 8. Observability

- Every action writes an `audit_events` row (actor, action, entity, verdict, detail).
- Decision rationale stores **concise reasoning + evidence references**, never hidden
  chain-of-thought (spec §25).

## Operations checklist

- [ ] `FOUNDER_AUTH_TOKEN` set and strong (≥ 32 random bytes hex)
- [ ] Provider keys are secrets, not vars
- [ ] `ENVIRONMENT=production` on the production Worker
- [ ] Production deploy requires manual approval (GitHub environment)
- [ ] AI budgets configured and monitored
- [ ] Emergency controls (pause / disable spending / disable deployments) tested
- [ ] No secrets in git history (`git log -p | grep -i "api_key\|token"`)
