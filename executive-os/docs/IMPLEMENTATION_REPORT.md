# Zarlino Executive OS — Implementation Report

Status: **Initial full scaffold** — buildable, testable, deployable foundation built in
vertical slices per spec §43. This report states exactly what exists in the repository
and what has been tested. Nothing is claimed that is not in the repo and covered by tests.

---

## 19. Downloads & Licensed counters + plugin gap research (2026-08-28, deployed e4dcf8af)

### Downloads & Licensed counter (migration v16)
- `metrics.updated_at` added + seeded `metric_downloads = 238` and `metric_licensed = 83`
  (INSERT OR IGNORE by deterministic id — Founder/Executive updates survive redeploys).
- Overview exposes `downloads` / `licensed`; new **Downloads & Licensed** card in the
  Overview view shows Downloads, Licensed, License rate, Unlicensed + an inline
  editor (`POST /api/finance/metrics`) so the Founder keeps the numbers current.
- Store: `latestMetricByName`, `upsertMetric`; agent: `recordProductMetric`.
- Tests: `test/metrics.test.ts` (3) — full suite now **201/201 (25 files)**.

### Plugin gap research → 36 populated bug/feature requests
- `docs/PLUGIN_GAP_RESEARCH.md` — **24 feature/performance gaps** across ZScorch (6),
  ZTame (6), ZVocals (6), PresenceField (6), RACK_AI + platform (6), grounded in the
  reverse-engineered specs + competitor research (Soothe2/Soothe3, Gullfoss, Saturn 2,
  Ozone Imager, Polyverse Wider, S1/Vocal Rider, SSL Vocalstrip 2, Nectar 4, Unify,
  Acon, Accentize).
- **36 bug/feature requests created as tasks** (auto_run = ON → the factory works them
  2-at-a-time within the AI budget): 6 per plugin (ZScorch/ZTame/ZVocals/PresenceField/
  RACK_AI) + 6 platform/QA (AAX, CLAP, NKS, CPU benchmark matrix, undo/A-B, golden refs).
  Verified live: 36/36 created.

## 18. Auto-run — tasks complete themselves, no Run clicks (2026-08-28, deployed b9f69cba)

The user asked: "I don't need to press run on individual tasks — once it doesn't need my
approval everything should be auto completed." Implemented as a server-side auto-runner:

### Migration v15 — `tasks.auto_run`
- `ALTER TABLE tasks ADD COLUMN auto_run INTEGER NOT NULL DEFAULT 1` (1 = auto-execute).
- Indexes on `tasks(auto_run, status)` and `agent_jobs(task_id)`.
- Founder-decision tasks are forced to `auto_run = 0` (id + title rules: `%founder%decision%`,
  `%A/B listening%`) so release decisions / listening verdicts always wait for the Founder.
  Seed also marks `tsk_zscorch_gate10` and `tsk_ztame_p8` manual.

### The auto-run sweep (`autoRunEligibleTasks`)
- Picks `auto_run=1` OPEN/IN_PROGRESS tasks (priority then oldest), skips tasks that already
  have a RUNNING run or whose decision has a PENDING approval, and starts a bounded long-horizon
  run for each — **capped at 2 concurrent auto-runs** (shares the daily AI budget).
- Runs mark the task IN_PROGRESS on start; `onRunCompleted` marks it DONE.
- Triggers: task creation (API + `projects.create_task` / `executive.create_task` tools via
  `onTaskCreated`), task PATCH re-enabling auto, `onApprovalDecided`, `runMaintenance`, and
  every DO wake (`onStart`). Runs that create follow-up tasks keep the loop going.

### Critical fix — background runs were being dropped
Runs started fire-and-forget from `onStart` **stalled at iteration 1** (heartbeat written, then
the first model fetch was abandoned when the DO went idle after the wake → the run sat RUNNING
until the stale reaper flagged it). API-started runs worked (verified: a diagnostic run
completed 4 iters/4 tools). Root cause: no alarm-heartbeat keeping the DO alive for the run.
**Fix:** `runAgentJob` now acquires the agents SDK `this.keepAlive()` (alarm heartbeats) at
start and disposes it in `finally`, so the DO stays alive for the whole run from ANY context.
Verified live: auto-runs now execute — `run_23a4` (73 iters / 96 tools → DONE, task
"Create affiliate dashboard" marked DONE), `run_ae69` DONE, `run_b00d`/`run_cd85` RUNNING.

### Web console (Tasks view)
- Every card shows a **🤖 auto / ✋ manual** toggle (PATCH `autoRun`) so you can flip any task.
- Quick-create has an **auto-run checkbox** (default ON) + an explainer line.
- Tasks + Executive Runs auto-refresh every 20s (paused when the tab is hidden), so completions
  appear on screen with no interaction.
- Maintenance button reports how many tasks it auto-started.

### Tests
- `test/autorun.test.ts` (6 tests): migration v15 defaults + idempotency, founder-decision
  exclusion, `listAutoRunnableTasks` ordering/skipping, running-run guard, pending-approval skip.
- Full suite: **198/198 passing (24 files)**.

---

## 1. Architecture (implemented)

- **One Executive Agent** (`src/agent/executive-agent.ts`) — a Cloudflare Agents SDK
  `Agent` (Durable Object, SQLite-backed state) with CEO/CFO/CTO lenses + Chief of Staff.
- **Worker** (`src/index.ts`) — REST API (`/api/*`), Agents SDK routing (`/agents/*`),
  static asset serving of the web console.
- **REST API** (`src/api/router.ts`) — the full endpoint set from spec §30, typed with
  zod, bearer-token authenticated.
- **Model gateway** (`src/core/model/`) — `ModelGateway`, providers (DeepSeek, OpenAI,
  Anthropic, Workers AI, Mock), task→tier routing, cost estimation, budget STOP.
- **Authority engine** (`src/core/authority/engine.ts`) — ALLOW/APPROVAL/DENY with the
  spec §41 autonomy policy, emergency switches, and the standard action set.
- **Decision engine** (`src/core/decision/engine.ts`) — the spec §6 pipeline.
- **Memory** (`src/core/memory/`) — L0–L3 model, retrieval scoring, immutable doctrine.
- **Approvals** (`src/core/approvals/queue.ts`) — PENDING→APPROVED/REJECTED/MODIFIED/CANCELLED.
- **Tasks** (`src/core/tasks/model.ts`) — decision→tasks, prioritization, blocked detection.
- **Tools** (`src/core/tools/` + `src/integrations/`) — typed tools (GitHub, Cloudflare,
  website, finance, projects, marketing, executive) through one permissioned registry.
- **Durable store** (`src/db/`) — 24+ normalized tables, migrations, `ExecutiveStore`
  portable across DO SQLite and `node:sqlite`.
- **Scheduling** — daily brief + weekly review crons, durable fibers.
- **Web console** (`web/`) — React + Vite dark executive dashboard: Overview, Ask
  Executive (chat), Decisions, Tasks, Projects, Finance, Products, Engineering,
  Marketing, Memory, Doctrine, Approvals, Activity, Settings.
- **Absorbed orchestrator** — the 10-gate plugin pipeline seeded as a structured
  `PLUGIN_PIPELINE` workstream; products seeded from the real `specs/catalog.md`.

## 2. What was built (deliverables, spec §47)

1. Complete source code — `src/`, `web/src/`
2. Database schema — `src/db/schema.ts`
3. Migrations — `migrations/0001_initial.sql` (+ runner)
4. Cloudflare configuration — `wrangler.jsonc`, `.dev.vars.example`
5. Frontend — `web/` (React + Vite)
6. Agent implementation — `src/agent/executive-agent.ts`
7. Memory system — `src/core/memory/`
8. Decision engine — `src/core/decision/`
9. Authority engine — `src/core/authority/`
10. Approval system — `src/core/approvals/`
11. Tool layer — `src/core/tools/` + `src/integrations/`
12. Model gateway — `src/core/model/`
13. Cost tracking — `src/core/model/cost.ts`
14. Audit system — `src/core/observability/` + `audit_events` table
15. Test suite — `test/` (vitest)
16. CI/CD — `.github/workflows/executive-os-ci.yml`
17. Documentation — `README.md`, `docs/architecture.md`, `docs/deployment.md`, `docs/security.md`
18. Deployment instructions — `docs/deployment.md`
19. Environment-variable template — `.dev.vars.example`
20. Founder benchmark suite — `src/benchmark/` + `test/benchmark.test.ts`

## 3. What was tested

| Area | Test file | Covers |
|------|-----------|--------|
| Authority engine | `test/authority.test.ts` | ALLOW/APPROVAL/DENY, thresholds, emergency, doctrine, action set |
| Decision engine | `test/decision.test.ts` | full pipeline, escalation, options, context, risk |
| Risk engine | `test/risk.test.ts` | LOW/MEDIUM/HIGH/CRITICAL classification |
| Financial calc | `test/finance.test.ts` | runway/ROI/EV/CAC/payback, UNKNOWN handling |
| Memory retrieval | `test/memory.test.ts` | relevance, layers, expiry, lesson boost |
| Doctrine protection | `test/doctrine.test.ts` | proposal→approve, versioning, immutability |
| Approval workflow | `test/approvals.test.ts` | transitions, invalid transitions |
| Tool system | `test/tools.test.ts` | schema validation, permissioning, failure handling |
| Model routing | `test/routing.test.ts` | task→tier, provider resolution |
| Cost tracking | `test/cost.test.ts` | estimation, budget STOP |
| Store | `test/store.test.ts` | migrations, seed, CRUD, costs, audit, gates |
| Integration | `test/integration.test.ts` | founder→decision→approval→tasks→audit, tool registry, budget STOP |
| Benchmark | `test/benchmark.test.ts` | decision similarity, forbidden actions, escalation |

## 4. What passed / what remains

**Verified on 2026-08-24 (this machine):**
- `tsc --noEmit` → **0 errors** (strict, noUncheckedIndexedAccess).
- `vitest run` → **148/148 tests pass across 20 files** (unit + integration + benchmark + auth + cloudflare + reasoner + model-tools + web + github + vision).
- Web console `vite build` → succeeds (233 kB JS / 68 kB gzip).
- `wrangler deploy --dry-run` → succeeds: Worker bundles, the `ExecutiveAgent`
  Durable Object binding is wired, assets (`web/dist`) are picked up, and all
  routing/budget/autonomy vars are present.
- **Live end-to-end with real DeepSeek** (`wrangler dev`): health/overview/doctrine/
  products/activity all return real seeded data; a chat decision ran the full
  pipeline (classify → 4 lenses → synthesis → authority → tasks) with all four
  lens assessments produced, doctrine cited correctly (D-001/D-004), CFO labeled
  Known vs UNKNOWN without fabricating, 95% confidence synthesis, follow-up tasks
  created, costs + audit recorded (~$0.008/decision).
- Two real runtime bugs were found and fixed during live testing: (1) DO RPC calls
  in the router were not awaited (silent empty responses) — now all awaited;
  (2) deepseek-reasoner needed larger `maxTokens` (reasoning consumes the budget
  before content, lenses were coming back empty) — lens/synthesis raised to 2000.

**Pipeline-state sync (migration v3, 2026-08-24, deployed Version 8acf4b0c):**
- Root cause of "UNKNOWN — insufficient data" answers: the seed marked every pipeline
  gate `NOT_STARTED` and seeded no tasks/risks/approvals, so the agent could not see
  that both plugins are at Gate 10.
- `src/db/schema.ts` gained `PIPELINE_STATE_SQL` (migration v3) + `migrations/0003_pipeline_state.sql`;
  the runner (`src/db/migrations.ts`) applies it automatically on the deployed DO's next wake.
- Live data now reflects `pipeline/ZScorch/STATUS.md` + `pipeline/ZTame/STATUS.md`:
  - ZScorch/ZTame gates 1-9 PASSED, Gate 10 IN_PROGRESS (ZScorch `ready_for_release`;
    ZTame `rework_complete` with P8 audit open).
  - Tasks: `tsk_ztame_p8` (P0 BLOCKED — founder A/B listening test), `tsk_ztame_signing` (P1),
    `tsk_zscorch_gate10` (P1 — release decision), `tsk_zscorch_signing` (P2).
  - Risks: `risk_ztame_p8` (HIGH), `risk_ztame_signing` (HIGH), `risk_zscorch_signing` (MEDIUM),
    `risk_golden_refs` (LOW).
  - Pending approval: `appr_zscorch_gate10` (ZScorch v1.0.0 Gate 10 release).
- The seed (`src/seed/seed.ts`) was updated to the same facts for fresh databases, guarded
  so migration v3 + seed never double-insert (deterministic IDs, existence checks).
- Verified live: overview reports openTasks 5 / blockedTasks 1 / pendingApprovals 1 /
  criticalRisk "ZTame suppression quality (P8) unresolved"; `STATUS` command now returns
  the grounded overview instead of UNKNOWN.

**Full Access autonomy (2026-08-25, deployed Version 7da50121):**
- New Founder-granted **full access** flag (`full_access` on executive state, migration v8
  `0008_full_access.sql`; `POST /api/executive/fullaccess {enabled}`; Settings toggle + red
  FULL ACCESS badge in Overview). Implies delegate mode.
- Authority engine (`AuthorityConfig.fullAccess`): when ON — deploy ALLOW, HIGH-risk
  REVERSIBLE actions ALLOW, spending ALLOW (still respects the spending-disabled
  emergency switch). The hard NEVER-gates always hold: delete, ownership transfer,
  doctrine changes (founder-approved), CRITICAL risk, irreversible HIGH, and every
  emergency switch (pause / spending-disabled / deployments-disabled).
- The model is told its full-access status in the authority prompt block.
- **ENABLED LIVE**: `POST /api/executive/fullaccess {enabled:true}` → fullAccess:true +
  delegateMode:true, persisted in overview + emergency state.

**Unlimited-but-safe tool loop + long-horizon agent runs (2026-08-25, deployed Version 5abce21d):**
- **No hard 8-iteration cap**: `queryWithTools` is now `runToolLoop` — the model keeps
  calling tools until the task is complete, bounded by safety rails instead of a low
  ceiling: `maxToolIterations` (env `MAX_TOOL_ITERATIONS`, default 200),
  `maxAgentRunMs` (env `MAX_AGENT_RUN_MS`, default 600 000 ms wall-clock),
  a no-progress guard (same tool+args repeated 5× → STOPPED), and the existing AI
  budget tracker (daily/monthly/per-run) that stops runaway spend.
- **Long-horizon runs (migration v9 `0009_agent_runs.sql`)**: `agent_jobs` table with
  status RUNNING/DONE/FAILED/STOPPED/TIMEOUT, iteration + tool counters, result/error,
  and timestamps. `POST /api/agents/run {prompt, taskId?, maxIterations?}` starts a
  persistent background job (`startAgentRun` → `runAgentJob` with heartbeats);
  `GET /api/agents/runs` + `GET /api/agents/runs/:id` track it live;
  `POST /api/agents/runs/:id/continue` resumes a STOPPED/TIMEOUT/FAILED run in place
  (appends the prior attempt as context). Tasks view: ▶ Stream (live chat loop) and
  ⏳ Run (long-horizon) per task + a "Long-Horizon Runs" card with Continue.
- **Migration-v9 table collision fixed**: the v1 schema already reserved `agent_runs`
  (decision-log shape with `session_id`/`model`/`tokens_json`, never written to), so
  `CREATE TABLE IF NOT EXISTS agent_runs` silently no-oped and the index on `status`
  failed ("no such column: status"). The long-horizon table is therefore named
  **`agent_jobs`** (same row shape) and the continue path uses `resetAgentRun` (reopen
  the same row) instead of a duplicate INSERT.
- Verified live on the deployed worker: a real run "list ZScorch repo status + open
  GitHub issues" ran 2 iterations / 4 tools → DONE with a real summary; a 1-iteration
  run correctly STOPPED with "tell me to continue and I'll resume"; Continue on it
  reopened the same row and completed DONE with real output; tool executions recorded
  (`/api/tools/executions`, 25 rows, ALLOW/SUCCESS).

**Expanded toolset + web search/automation + Cloudflare + blocked-tool fixes (2026-08-25, deployed Version 67aa69cd):**
- **More tools (13 new)**. GitHub: `list_releases`, `get_file`, `list_commits`,
  `list_repos`. Cloudflare: `list_workers`, `list_zones`, `get_zone`. Website:
  `get_page` (fetch a page + readable text). Web (new `src/integrations/web.ts`):
  `web.search`, `web.fetch_page`, `web.check_url`, `web.crawl_links`. Local agent also
  gained `local_web_search` + `local_web_fetch`.
- **Web search + web automation**: `web.search` uses DuckDuckGo HTML (no API key) with a
  **Wikipedia fallback** — DDG CAPTCHA-blocks datacenter IPs, so the tool detects the
  anomaly/challenge page and retries via the Wikipedia search API; `web.fetch_page`
  returns title + readable text (HTML stripped), `web.check_url` probes status/redirect/
  latency, `web.crawl_links` extracts outbound links.
- **Cloudflare tools fixed**: the scripts-list `id` field IS the script name
  (e.g. `zarlino-executive-os`), so `get_worker`/`get_deployments`/`get_analytics` now
  validate the name against the real list and throw a helpful error listing available
  workers (the model previously guessed "zarlino"/"www"/"traffic" and 404'd). Added
  `list_workers` so the model discovers names first. **Analytics**: the legacy REST
  endpoint is deprecated (returns the script as multipart, which crashed JSON parsing) —
  `get_analytics` now tries the GraphQL Analytics API, then the REST endpoint, then
  degrades to a clear "unavailable (needs Workers Analytics token scope)" message
  instead of a parse error.
- **Blocked tools fixed — root cause**: `setFullAccess`/`setDelegateMode` left a stale
  `autonomy_mode=APPROVAL_ONLY` in the DB, and `evaluateAction` escalated ALL non-read
  actions to APPROVAL *before* the full-access checks ran — so even with Full Access ON,
  create/analyze/execute tools stayed blocked. Fixes: (1) `evaluateAction` treats
  fullAccess/delegate as overriding a stale APPROVAL_ONLY mode; (2) enabling full access
  or delegate now also resets `autonomy_mode` to `CONTROLLED`. Live state now:
  fullAccess=true, delegateMode=true, autonomyMode=CONTROLLED.
- Verified live: agent autonomously created GitHub issue #4 (no approval block, later
  closed as a test artifact), `web.search`/`web.fetch_page`/`web.check_url` all
  SUCCESS/ALLOW with real results (DDG + Wikipedia fallback), `cloudflare.list_workers`
  → 2 real workers, `get_deployments` SUCCESS, `get_analytics` graceful degradation,
  and 0 blocked/denied in the latest 25 executions (23 of them new tools).

**Website source + Cloudflare worker source access (2026-08-25, deployed Version b296d8f3):**
- **GitHub tools are now repo-aware**: every repo-scoped tool (read_repo, list_issues,
  get_issue, create/update_issue, list_pulls, read_commit, list_workflows,
  trigger_workflow, list_branches, create_branch, write_file, create/merge_pull_request,
  list_workflow_runs, get_workflow_run, list_releases, get_file, list_commits) accepts
  optional `owner`/`repo` params defaulting to GITHUB_OWNER/GITHUB_REPO — so the agent
  can read/write the public `zarlino-website` and `zarlino-feedback` repos as well as
  the private `Zarlino` repo. Fixed the stale default (was `zarlino-plugins`, now
  `Zarlino`).
- **New `github.get_website_source`**: convenience wrapper that reads any file from
  `zarlino-audio/zarlino-website` (the website source) directly.
- **`github.list_repos` fixed**: `zarlino-audio` is a USER account, not an org, so
  `/orgs/zarlino-audio/repos` 404s — the tool now falls back to `/user/repos`.
- **New `cloudflare.get_worker_source`**: downloads a Worker's deployed source +
  metadata by script name. The CF download endpoint returns multipart/form-data with
  `index.js` + `metadata` parts — the tool parses it via a boundary-safe parser (only
  reads each part's first header block, so script code containing `name="metadata"`
  can't confuse it). The `zarlino-website` worker IS the deployed website source.
- Verified live: the agent enumerated all 3 repos (zarlino-website public, zarlino-
  feedback public, Zarlino private), read website `package.json` (Astro) +
  `src/pages/index.astro`, and downloaded + summarized the deployed zarlino-website
  worker source (routes, KV counters, admin/stats endpoint, license promo expiry).
  143/143 tests (19 files).

**Vision tool (DeepSeek V4 vision) + more tools (2026-08-25, deployed Version 83036b12):**
- **DeepSeek vision discovered**: the API exposes `deepseek-v4-flash-vision-exp` for
  image inputs (the text models reject images: "This model does not support image").
  Verified: image_url with a base64 data URL works; the model returns content +
  reasoning_content + usage.
- **New `vision.analyze_image`** (new `src/integrations/vision.ts`): pass `imageUrl`
  (public or data: URL) OR `imageBase64` + `mimeType`, plus an optional prompt —
  returns the DeepSeek vision analysis (with reasoning). Configurable model via
  `DEEPSEEK_VISION_MODEL` (wrangler var, default `deepseek-v4-flash-vision-exp`).
  **New `vision.analyze_url_image`**: the worker downloads the image itself first
  (handles URLs the model can't reach) then sends it as base64. Both are read/LOW.
- **More tools (9 new)**: GitHub — `list_collaborators`, `get_commit_diff` (patch),
  `get_file_metadata`. Cloudflare — `get_dns_records` (by zone name), `get_account`.
  Internal reads — `marketing.list_campaigns`, `executive.list_approvals`,
  `executive.list_decisions`, `executive.list_risks`, `executive.list_tasks`,
  `executive.list_products`, `executive.list_activity` (audit). All read/LOW → ALLOW.
- Verified live: agent used `vision.analyze_url_image` to describe the Google logo
  (colors, layout, bytes, source), and read real approvals (48+ resolved, 3 rejected)
  + products via the new internal tools — all SUCCESS/ALLOW. 147/147 tests (20 files).

**Live web → local machine bridge (migration v10, 2026-08-25, deployed Version 23f83d94):**
- **Request a task on the live web and have the agent inspect plugin UI locally** —
  a full two-way bridge between the Cloudflare Executive and this dev machine.
- **`local_tasks` table (migration v10 `0010_local_tasks.sql`)**: PENDING → CLAIMED →
  RUNNING → DONE/FAILED with claimed_by / result / error / timestamps. Store methods:
  create/claim (atomic oldest-PENDING)/heartbeat/finish/get/list.
- **Agent methods**: `requestLocalTask` (queue), `listLocalTasks`, `getLocalTask`,
  `claimLocalTask` (daemon), `heartbeatLocalTask`, `submitLocalResult` (daemon posts
  back). **Tools**: `executive.request_local_task`, `executive.get_local_task`,
  `executive.list_local_tasks` (the live agent can queue + poll local work).
- **Router**: `POST/GET /api/local/tasks`, `POST /api/local/tasks/claim`,
  `GET/POST /api/local/tasks/:id`. (Gotcha re-hit: DO RPC MUST be awaited — the create
  route initially returned an empty 200 because the RPC wasn't awaited.)
- **Local daemon** (`scripts/local-daemon.ts`, `npm run agent:daemon` / `agent:daemon:loop`):
  claims PENDING tasks from the live worker, runs them through the local agent (DeepSeek
  function-calling loop with `local_*` tools), and posts the result back. `local-agent.ts`
  refactored to export TOOLS/runTurn/buildSystem; `runTurn` takes a max-tool-calls cap
  (30 for daemon). New `local_plugin_ui` tool: list/read/report a plugin's UI source
  (`plugins/<Name>/Source`, default ZScorch) — the daemon's system prompt now tells the
  model the plugin layout and to prefer `local_plugin_ui`.
- **Web console**: Tasks view gained a **"Local Work (this machine)"** card — request a
  local task (textarea), auto-refresh statuses every 4s, and expandable result reports.
- Verified live: (1) web console → create → daemon → ZScorch UI report (4269 chars:
  controls/params/ranges/defaults); (2) live agent `executive.request_local_task` queued
  a ZTame task, daemon inspected locally and posted a 3046-char UI report; (3) agent
  queued ZVocals → daemon produced a 5169-char report. All tasks DONE on the live worker.
  148/148 tests (20 files).

**Ship flow + coherent tracking + agent-at-work (2026-08-25, deployed Version fbf0dafe):**
- **Ship button**: `POST /api/pipeline/ship {pluginId, version?, note?}` → agent.shipPlugin →
  store.shipProduct (product → SHIPPED + version, product_status history row, Gate 10 →
  SHIPPED). Pipeline view has a 🚀 Ship button per plugin (with version prompt); Overview
  shows SHIPPED count + shipped products; STATUS command reports "Shipped: N".
- **Coherent tracking (migration v7 `0007_ship_state.sql`)**: ZScorch + ZTame are live on
  zarlinoaudio.com (v1.0.0) but the system reported them unshipped — v7 reconciles
  (products → SHIPPED v1.0.0, Gate 10 → SHIPPED, product_status history) and the seed does
  the same for fresh DBs. Fixed latent bug: product_status table had no `updated_at` column
  (ALTER in v7). Gate plugin_id is the product NAME ("ZScorch"), not the id — shipProduct
  now uses it correctly.
- **Deploy the technical agent on real work + see it work**: Tasks view gained an assignee
  (technical-role) dropdown and a **▶ Run with Executive** button per open task — it streams
  the agent actually doing the task with tools (repo/website/Cloudflare) into a live panel.
  New `GET /api/tools/executions` + a **"Agent at Work — Recent Tool Executions"** card in
  the Pipeline view proves the work: every tool run with SUCCESS/FAILED status.
- Verified live: overview shippedCount=2 (ZScorch/ZTame v1.0.0), gate 10 SHIPPED, bogus ship
  → 404, task-run streamed "website is up, HTTP 200, 38ms… DONE" with
  website.get_status + github.list_branches recorded SUCCESS.

**Tool execution in chat + Flash/Pro model routing (2026-08-24, deployed Version 092fc23d):**
- **Tool calls now EXECUTE in chat** (they used to be printed as prose). The chat query
  path (`queryWithTools`, wired into both `answerQuery` and the streaming `runStreamTask`)
  uses OpenAI-compatible function calling: the registry's typed tools are exposed as
  function schemas (`registry.functionSchemas()` via zod 4 `z.toJSONSchema`), the model
  requests tool calls, they execute through the authority-gated registry, and results are
  fed back (up to 8 iterations) until a final answer. Approval/denial results are surfaced
  to the model so it tells the Founder what's waiting in the Approvals console.
- **DeepSeek constraint**: tool names must match `^[a-zA-Z0-9_-]+$` (no dots) — the
  registry sanitizes names (`website.get_status` → `website_get_status`) for the model and
  `resolveName()` maps back before execution.
- **Smarter command detection**: `isCommandLike` now only treats a message as a command
  when a command word is the LEADING intent (first word, short imperative ≤3 words, or
  pause/resume/audit verbs) — so "check the website status" no longer hijacks into the
  canned overview, while bare "STATUS"/"risks" still work.
- **Model routing (V4 Flash / V4 Pro)**: agentic work — chat, tool loops, classification,
  summarization, briefings — uses **deepseek-chat** (Flash, cheap+medium tiers);
  high-intelligence decisions — financial/technical/strategic reasoning, lens analysis,
  synthesis — use **deepseek-reasoner** (Pro, strong+strongest tiers). Documented in
  `wrangler.jsonc`.
- Verified live: "check website status" → tool executed, replied "OK"; "list branches" →
  real 4-branch answer; bare STATUS still deterministic.

**Natural chat fix (2026-08-24, deployed Version 9b517587):** follow-ups felt "mock/canned" for
  two reasons, both fixed: (1) the model over-classified imperatives ("hello", "reply with X")
  as `command`, routing them to the deterministic handler (always the same overview) — added a
  keyword guard (`isCommandLike`) so `command` is only honored when a real command keyword is
  present, else downgraded to `query`; (2) the query path sent NO conversation history — added
  `chatContext(sessionId, message)` (last 24 turns from chat_messages) to both the streaming and
  non-streaming model calls. Verified live: "hello" → natural model reply; follow-up "What was my
  first message?" → "Your very first message was hello"; STATUS still deterministic.

**Streaming + chat history + repo write tools + local agent (2026-08-24, deployed Version 0abdb5d9):**
- **Streaming chat**: `/api/chat/stream` SSE. Async generators don't survive DO RPC, so
  the agent buffers deltas in a `chat_streams` row (migration v6, `0006_chat_stream.sql`)
  via `openStream`/`readStream` (count-based: `afterIndex` = chunks consumed,
  `nextIndex = all.length`); the Worker polls and forwards as SSE. Console renders
  token-by-token with a cursor. Verified live (STATUS streams meta→delta→done).
- **Chat history**: `chat_messages` are persisted on every message; new endpoints
  `GET /api/chat/sessions`, `GET /api/chat/messages?sessionId=…`, `POST /api/chat/clear`;
  agent methods `listChatSessions`/`listChatMessages`/`clearChat`; store `listChatSessions`
  (grouped, most-recent-first). Console: session picker + New Chat + Clear, session id
  persisted in localStorage so transcripts survive reloads.
- **Repo write tools** (agents actually work at repo level): `github.list_branches`,
  `github.create_branch`, `github.write_file` (Contents API, base64, auto-SHA),`github.create_pull_request`, `github.merge_pull_request`,
  `github.list_workflow_runs`, `github.get_workflow_run` — branch→file→PR→CI→merge loop.
  Verified live: 4 branches, and the repo tools surfaced a FAILED "ZTame Release Gate" run.
- **Local agent CLI** (`scripts/local-agent.ts`, `npm run agent:local`): a terminal agent
  that ACTS locally — `local_list_dir`, `local_read_file`, `local_write_file` (sandboxed to
  the repo), `local_run_command`, `local_run_tests`, `local_build_console`,
  `local_exec_overview` (live OS grounding), `local_deploy_website` (wrangler). DeepSeek
  function-calling loop (max 12 tool iterations), REPL + one-shot modes, confirmations for
  write/command/deploy (skip with `--yes`). Tool names must match `^[a-zA-Z0-9_-]+$`
  (DeepSeek rejects dots). Verified: read local files and answered accurately.

**Delegate mode + chat markdown (2026-08-24, deployed Version 94e7d65c):**
- **Delegate mode** (Founder-granted, migration v5 `0005_delegate_mode.sql`): a
  `delegate_mode` flag on executive state + `POST /api/executive/delegate { enabled }`
  + Settings toggle. When ON, the authority engine allows deployments and MEDIUM-risk
  reversible modify/execute autonomously; HIGH/CRITICAL risk, irreversible actions,
  and all emergency switches still gate. The model is told its current delegate status
  in the authority prompt block.
- **New GitHub tools**: `github.list_workflows` (read) + `github.trigger_workflow`
  (execute, MEDIUM) — with delegate ON the agent can trigger real CI/release pipelines
  (verified live: found `ztame-release-gate.yml`).
- **Chat markdown rendering** (`web/src/components/Markdown.tsx`): the Ask Executive chat
  now renders model markdown (**bold**, ***bold-italic***, `code`, lists, headers) instead
  of leaking raw `***` markers; HTML-escaped, no raw HTML.
- Verified live: delegate toggle persists (overview reflects it), list_workflows returns
  the ZTame Release Gate workflow, console bundle contains the markdown + delegate UI.

**Live external tools wired (2026-08-24, deployed Version d3ef720b):**
- **GITHUB_TOKEN** (`wrangler secret put`) + **CF_API_TOKEN** (`wrangler secret put`),
  CF_ACCOUNT_ID var — repo + Cloudflare tools now work against the real infra.
  Verified live: `github.read_repo` (zarlino-audio/Zarlino, private), `github.list_issues`
  (1 open), `cloudflare.get_worker` (both workers), `cloudflare.get_deployments` (10).
- **Bug fixed — tool execution PK collision**: the registry writes RUNNING then
  SUCCESS/FAILED with the same executionId; `recordToolExecution` was a plain INSERT
  → UNIQUE constraint crash on every executing tool. Now an upsert
  (`ON CONFLICT(id) DO UPDATE`), regression-tested.
- **Bug fixed — cloudflare.get_worker**: `GET /scripts/{name}` returns the script as
  multipart/form-data (no JSON metadata); the tool now pulls metadata from the JSON
  scripts-list endpoint (`/workers/scripts?per_page=50`) and filters by name. The
  initial multipart-parser attempt was abandoned (script code contains `name="metadata"`),
  replaced by the list-based approach; unit-tested with a stubbed fetch.
- `cloudflare.get_analytics` requires Founder approval by policy (analyze → approval).

**Closed loop + technical roles (2026-08-24, deployed Version 8e00e4ee):**
- **Technical roles onboarded**: 12 roles seeded as persons (`person_role_*`) — Product
  Manager, Architect, DSP/UI/Parameter Engineer, Code Reviewer, QA, Performance,
  Security & Licensing, Documentation, Release, Research — via seed + migration v4
  (`PERSON_ROLES_SQL`, `migrations/0004_technical_roles.sql`). Tasks can be assigned to
  them (`assignee_person_id`) from the dashboard or chat.
- **Pipeline tools**: `pipeline.list_gates` (read) + `pipeline.update_gate` (modify)
  added to the agent's tool registry; the agent can now manage the 10-gate pipeline
  from chat. Modifications by the agent route through the authority engine → Founder
  approval (verified live: tool call created a pending approval, founder approved via
  dashboard).
- **API**: `GET /api/pipeline`, `PATCH /api/pipeline/gates/:id`, `GET /api/persons`;
  agent methods `listPipelineGates`/`updatePipelineGate`/`listPersons`.
- **Dashboard**: new **Pipeline** view (per-plugin 10-gate progress + status selectors
  wired to the API, technical-roles card, closed-loop explainer) registered in the nav.
- **Integrations**: GitHub tools now default to `zarlino-audio/Zarlino` (the real repo;
  private → set `GITHUB_TOKEN` via `wrangler secret put`, graceful hint on 401/403/404);
  `WEBSITE_URL` + `CF_ACCOUNT_ID` wired as vars; Cloudflare tools degrade gracefully
  when `CF_API_TOKEN` is missing.
- **Verified live**: 12 roles listed, `/api/pipeline` returns 40 gates (ZScorch 9/10,
  ZTame 9/10 passed), REST PATCH + tool-based gate updates work end-to-end. The founder
  has already approved the ZScorch v1.0.0 Gate 10 approval through the dashboard.

**Auth v2 + admin integration (2026-08-24, shipped live):**
- Founder **username/password** auth replaces the shared-token-only model: PBKDF2
  (SHA-256, 100k iterations, per-user salt) in `src/core/auth/passwords.ts`, sessions
  in the `auth_sessions` table (30-day expiry), 5-failed-attempt lockout; DB migration
  v2 (`migrations/0002_auth.sql`). The first visit forces account setup
  (`GET /api/auth/status` → `setupRequired: true`); the legacy `FOUNDER_AUTH_TOKEN`
  still works as a bearer credential.
- Console (`web/`) gained a setup/login screen (App.tsx), sign-out + change-password
  (Settings view), token/user in localStorage.
- **Integrated into the existing website admin**: `zarlino-website/.../AdminDashboard.tsx`
  now embeds the Executive OS (section card + iframe + "Open Executive OS" button),
  live at https://zarlinoaudio.com/admin. Deployed to production:
  - Exec OS worker Version 115817a8 (auth v2), live auth-status verified → setupRequired.
  - Website Version d55f5f44 (integration), confirmed in the live AdminDashboard bundle.

**Mobile UI + file uploads + polish (2026-08-25, deployed Version 4e006a94):**
- **Mobile-first UI**: bottom navigation bar on small screens (`.mobile-nav`, 15 buttons)
  that replaces the sidebar (`display:none` under 760px), stacked grids, scrollable
  tables, larger touch targets, chat height tuned for mobile viewport, and a fade-up
  entrance animation for cards/messages. A live status ticker runs across the bottom.
- **File uploads (migration v11 `0011_uploaded_files.sql`)**: `uploaded_files` table
  (id/name/mime/size_bytes/data_base64/kind/source/timestamps). `POST /api/files` uploads
  (5 MB cap, base64), `GET /api/files` lists metadata, `GET /api/files/:id` returns full
  content (images get clean `imageBase64` + `mimeType` so `vision.analyze_image` works),
  `DELETE /api/files/:id` removes. Chat has a 📎 attach button + hidden file input that
  uploads and drops a file chip into the message for the agent to inspect via
  `executive.get_file`. Overview gained a **Founder Files** card (list + delete + kind
  icon + formatted size).
- **Agent + tools**: `executive.list_files`, `executive.get_file` (returns dataUrl +
  imageBase64 + mimeType), and the upload/list/get/delete agent methods; authority
  engine defaults allow them. Vision-from-upload verified: the earlier failure (model
  given the full data URL as imageBase64) was fixed by returning clean base64/mime.
- **Fixed — file DELETE returned `{deleted:false}`**: root cause was an off-by-one in
  the router — `/api/files/:id` is a 3-segment path (`["api","files","<id>"]`) so the id
  lives at `parts[2]`, but the route read `parts[3]` (always `undefined`), so every
  single-file GET 404'd and every DELETE no-op'd. All other `:id` routes (decisions,
  tasks, gates, agent runs, local tasks — 4-segment paths) used the correct `parts[3]`.
  Also hardened `deleteUploadedFile` in the store to an existence-check + delete instead
  of a fragile COUNT(*) before/after comparison. 149/149 tests (20 files).
- Verified live: mobile bottom nav (390px viewport), chat attach + file input, ticker,
  Founder Files card, upload (text + image kinds), list, and delete round-trip.

**Bigger AI budget + entertainment polish (2026-08-25, deployed Version 18f4fded):**
- **Daily AI budget raised $2.00 → $8.00** (monthly $50 → $150, per-task $0.50 → $1.00,
  per-run $1.00 → $2.00) in `wrangler.jsonc` + `src/config.ts` defaults — the Founder was
  hitting the $2 stop-wall mid-tool-loop, which killed long-horizon runs and multi-turn
  vision work. The `overview()` endpoint now also exposes `aiSpendToday` /
  `aiSpendMonth` / `aiDailyBudget` / `aiMonthlyBudget` so the UI can show live spend.
- **Entertainment layer (all client-side, no new deps)**:
  - **Boot splash**: a cinematic terminal-style intro on login ("ZARLINO EXECUTIVE OS
    v11.0 → initializing lenses → loading doctrine → syncing gates → warming DeepSeek
    cores → EXECUTIVE ONLINE"), types out line by line, auto-advances in ~2s, skippable
    by click.
  - **Rotating status taglines**: the ticker now rotates 16 fun quips every 9s with a
    fade (was one static line picked by hour-of-day).
  - **Chat "thinking" indicator**: while the Executive works, a bubble shows bouncing
    accent dots + a rotating phrase ("Consulting the lenses…", "Doing the reversible
    math…", "Asking the CFO lens…").
  - **Confetti bursts** (`web/src/components/Confetti.tsx`, ~28 CSS pieces, zero deps)
    on: successful login, file upload, and 🚀 Ship.
  - **AI Fuel gauge** on Overview: spent-today vs daily budget, animated fuel bar
    (green→yellow→red as spend climbs, pulses when ≥90%), plus rotating fuel quips
    ("⛽ Tank is full — ask the Executive anything.").
  - **Micro-interactions**: card/border transitions, button press + primary glow, nav
    hover transitions, progress-bar shimmer, status-banner ambient glow.
  - **More fun chat suggestions**: "Make me laugh while you check the pipeline.",
    "Give me a 5-second status haiku." (verified live: the Executive returned a real
    status haiku).
- 149/149 tests (20 files); tsc clean; web build 36 modules. Verified live on mobile
  (390px) + desktop: boot splash, fuel gauge (25% used / $8.00 / $5.99 left), rotating
  ticker, thinking indicator, status haiku reply, mobile nav intact.

**True mobile-app layout — zero horizontal scroll (2026-08-25, deployed Version b1b2eb39):**
- **Root cause of left/right scrolling**: the mobile CSS forced `table { min-width:
  480px; display: block; overflow-x: auto }` — every table became 480px wide on a ~390px
  phone, blowing out the page width and forcing sideways scrolling on every view.
- **Fix — tables become stacked, labeled cards on mobile** (the native app pattern, no
  horizontal scroll): under 760px the `<thead>` is hidden and each `<tr>` renders as a
  rounded card (bg-elev-2, 10px radius, 10px gap) with every `<td>` stacked vertically
  showing a small uppercase `data-label` above its value (`td::before { content:
  attr(data-label) }`). All 20+ tables across 12 views got `data-label` attributes.
  Long mono strings (UUIDs/base64/tool names) wrap via `overflow-wrap:anywhere`.
- **Global guarantees**: `html, body { overflow-x: hidden; max-width: 100vw }`; the
  Pipeline progress grid is now 4-across on desktop → 2×2 on mobile (was 4×120px min =
  480px overflow). Desktop tables are untouched (thead + normal rows still render).
- Verified live on a 390px viewport across **all 15 views**: `scrollWidth == clientWidth
  == 390` everywhere (zero horizontal overflow); Doctrine/Products/Decisions/Tasks/
  Pipeline/Finance/Approvals/Activity all render as labeled cards. Desktop (1280px)
  still renders classic tables with no overflow. 149/149 tests.
- **Bonus bug fixed (deployed Version ad2ebd86)**: signing out crashed with React error
  #300 — the keyboard-shortcut `useEffect` was declared AFTER the `if (!authed)` early
  return, violating React's rules of hooks (hook count changed on the authed toggle).
  Moved the effect above the early return; sign-out now cleanly shows the login screen.

**Token tracking + token budget display (2026-08-25, deployed Version 255c2f93):**
- **Gap 1 — vision calls were untracked**: `vision.analyze_image` / `vision.analyze_url_image`
  called DeepSeek directly (raw fetch), bypassing the model gateway, so their tokens/cost
  never landed in `model_costs`. `visionComplete` now accepts the store and self-reports a
  `ModelCallRecord` (provider/model/input+output tokens from the API `usage`, cost via
  `estimateCost`, latency) whenever a store is available.
- **Gap 2 — no token aggregation**: the store only summed cost. Added `tokensToday()` and
  `tokensThisMonth()` (SUM input_tokens + output_tokens from `model_costs`).
- **Gap 3 — no token budget concept or display**: added `AI_DAILY_TOKEN_BUDGET` /
  `AI_MONTHLY_TOKEN_BUDGET` vars + config fields (defaults 30M/day, 900M/month, calibrated
  to the $8/day cost budget at DeepSeek pricing ≈ $0.27–1.10 per 1M tokens). The overview
  endpoint now returns `aiTokensToday` / `aiTokensMonth` / `aiDailyTokenBudget` /
  `aiMonthlyTokenBudget`, and the **AI Fuel gauge** on Overview shows two bars: USD
  (spent / budget / left) and tokens (used / budget / left). Finance view's AI Cost card
  also shows tokens today + this month.
- Verified live: cost $7.96/$8.00 (left $0.04) and tokens 28.44M/30M (left 1.56M) —
  internally consistent (28M tokens at ≈$0.28/M avg = $8). 151/151 tests (20 files;
  +2 new: store token aggregation, vision self-report).

**5-pass production hardening — survive 6 months without the CEO (2026-08-25, deployed Version ae9ecc3b):**
- **Migration v12 `0012_resiliency.sql`** (`RESILIENCY_SQL`): `agent_jobs` + `checkpoint_json` +
  `last_heartbeat`; `local_tasks` + `lease_expires_at` + `meta_json`.
- **Pass 1 — agentic capabilities**: model providers now retry transient failures
  (5xx/429/network) with exponential backoff + jitter, wired to the previously-dead
  `maxRetries`/`requestTimeoutMs` config. Long-horizon runs persist a durable transcript
  checkpoint after every tool call (`checkpointAgentRun`) and resume from it after a
  restart. Smarter no-progress guard (exact-call repeats ≥5 OR same tool name ≥7 in the
  last 12 messages = oscillation). Tool results truncated to 8 KB before feeding the
  model. On DONE: linked Task auto-marked DONE + a L1 LESSON memory recorded.
- **Pass 2 — restart robustness**: local-task claims now carry a **lease** (10 min, extended
  by heartbeats); expired leases are **reaped back to PENDING** so a machine that went
  offline mid-task loses nothing (`reapExpiredLocalTasks`). Stale RUNNING agent runs
  (no heartbeat > 15 min, e.g. DO restarted) are reaped to FAILED with "use Continue"
  (`reapStaleAgentRuns`). `onStart()` runs the maintenance sweep on every DO wake.
  Daemon now **heartbeats every 60s while working** (was a broken one-shot — the RUNNING
  status was silently rejected by the API schema, now allowed) + a `--watchdog` restart
  mode. New `runMaintenance()` sweep: stale runs + expired leases + expired sessions.
- **Pass 3 — CEO idea → implementation**: `executive.delegate_idea` tool converts a
  high-level idea into a structured local brief (goal/context/acceptance/deliverable,
  stored as `meta_json`) with a PLAN→IMPLEMENT→VERIFY→REPORT workflow in the local
  agent's system prompt. `requestLocalTask`/local task API accept the same brief.
  Closing a local task auto-marks the linked Task DONE + records a memory lesson.
- **Pass 4 — production / 6-month survival**: deep health endpoint `GET /api/system/health`
  (status, stuck counts, budget spend, open tasks, approvals); `POST /api/system/maintenance`
  (manual sweep); `GET/POST /api/system/snapshot` — full portable JSON export/import of all
  31 tables for backup/restore. Optional hard token-budget STOP (`AI_TOKEN_BUDGET_ENFORCE`,
  default soft/off) via a gateway guard before spending. `docs/RUNBOOK.md` — operator
  runbook for unattended survival.
- **Bug found live**: the local-task RUNNING heartbeat was silently failing for months —
  the API schema only allowed DONE/FAILED, so the daemon's `.catch(() => null)` masked it.
  Now RUNNING is a valid heartbeat (extends the lease) and is verified live.
- **Tests**: new `test/hardening.test.ts` (19 tests) — lease/reaper lifecycle, stale-run
  recovery, checkpoint durability, maintenance sweep, structured briefs, task/lesson
  closure, health stuck-counts, snapshot round-trip, provider retry/backoff, token-budget
  STOP (on/off), migration idempotency. **Total 170/170 tests (21 files)**; tsc clean.
- Verified live: deep health returns `stuck:{staleAgentRuns,expiredLeases}`; snapshot
  exports 31 tables (64 tasks, 12 agent jobs, 13 local tasks, 9 memories); structured
  delegation brief stored end-to-end; claim→RUNNING heartbeat (lease extended)→DONE
  lifecycle works; an expired test lease was auto-re-queued with
  "Re-queued: the previous worker lease expired (machine likely offline)"; a real daemon
  run held its lease via periodic heartbeats while inspecting ZScorch UI locally.

- **Remains (next slices, spec §43)**:
  - Slice 4 hardening: live GitHub/Cloudflare tools against real credentials, OAuth or
    short-lived tokens, per-account rate limiting.
  - Slice 5: on-demand vs cron briefings already wired; add email/notification delivery.
  - Slice 6: controlled autonomy tuning — per-tool autonomous budgets, per-run max
    actions, escalation rules refined with real usage.
  - WebSocket real-time (spec §2) via `agents` `AgentClient` + `@callable` (REST path is
    fully functional; WS is the documented next step).
  - Sub-agents for parallel lens analysis (spec §28) — the reasoner interface is ready.
  - Workers AI / AI Gateway live end-to-end validation with real keys.
  - Durable-Object integration tests under the Cloudflare runtime (miniflare / wrangler
    `unstable_dev`) — the same code is covered by `node:sqlite` tests today.

## 5. Known limitations

- Financial figures are **not** seeded — they are UNKNOWN until the Founder records a
  snapshot (by design, spec §8).
- Cost estimates use a static pricing table; reconcile with provider invoices.
- Auth is username/password with 30-day sessions (single founder account). Multi-user
  roles and fine-grained permissions are future work; the legacy `FOUNDER_AUTH_TOKEN`
  remains accepted as a bearer credential.
- The mock provider is deterministic; model-quality behavior requires real keys.
- The frontend chat is request/response (no streaming yet).

## 6. Required credentials

| Secret | Where | Used for |
|--------|-------|----------|
| `FOUNDER_AUTH_TOKEN` | console/API | all `/api/*` auth |
| `DEEPSEEK_API_KEY` | model gateway | DeepSeek model calls |
| `OPENAI_API_KEY` | model gateway | OpenAI model calls |
| `ANTHROPIC_API_KEY` | model gateway | Anthropic model calls |
| `GITHUB_TOKEN` | tools | GitHub integration |
| `CF_API_TOKEN` + `CF_ACCOUNT_ID` | tools + Workers AI | Cloudflare integration |

## 7. Deployment steps

See `docs/deployment.md`: local `wrangler dev`, staging via `--env staging`, production
behind manual CI approval.

## 8. Security considerations

See `docs/security.md`: bearer auth, backend authority, immutable history, doctrine
protection, budget STOP, emergency pause, secret isolation.

## 9. Estimated operating cost

- **Compute**: Workers + Durable Objects free tier covers modest usage (e.g. 100k
  requests/mo, one DO instance). Paid tier ≈ $5/mo minimum at low usage.
- **Model calls**: depends on volume and provider. At DeepSeek pricing and ~100
  decisions + 1 daily brief/mo, roughly $1–10/mo. Budgets are enforced (`AI_*_BUDGET`).
- **AI Gateway / observability**: optional, metered; typically < $5/mo at low volume.
- **Total estimated**: **$5–20/mo** at low volume, dominated by model usage.
