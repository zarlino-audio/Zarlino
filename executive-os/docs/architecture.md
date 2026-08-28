# Architecture

## 1. Principles

1. **One Executive Agent** with four reasoning lenses (CEO, CFO, CTO, Chief of Staff).
   The Founder receives one consolidated recommendation, never four contradictory answers.
2. **Durable by default.** Executive state lives in SQLite inside a Durable Object
   (`ExecutiveAgent`, instance `"zarlino"`). Records survive eviction, restarts, and
   code deploys.
3. **The LLM is one component.** Intelligence is distributed across data, memory,
   doctrine, the decision engine, tools, permissions, state, feedback, and evaluation
   (spec §46).
4. **Prompts are not security controls.** Authority is enforced in backend code.
5. **Model-agnostic.** All LLM access flows through `ModelGateway`; provider names
   never appear in business logic.

## 2. Runtime topology

```
Worker (src/index.ts)
  ├── /api/*            REST API (spec §30) → getAgentByName(ExecutiveAgent, "zarlino")
  ├── /agents/*         routeAgentRequest (Agents SDK routing / WebSockets)
  └── assets            web console (web/dist)
```

`ExecutiveAgent` is a `DurableObject`-backed `Agent` from the `agents` SDK. It owns:

- **State** (`Agent.setState`): small, syncable executive state (status, priority list,
  emergency flags, last-brief timestamps).
- **SQLite** (`this.sql`): the full relational schema (entities from spec §4).
- **Scheduling** (`schedule()` / `scheduleEvery()`): daily brief cron, weekly review cron.
- **Durable execution** (`runFiber`): brief generation survives eviction; interrupted
  fibers recover via `onFiberRecovered`.

## 3. Module map

| Module | Responsibility |
|--------|----------------|
| `core/model` | `ModelGateway` (routing + cost + providers), task→tier map, budget STOP |
| `core/authority` | `evaluateAction()` — ALLOW / APPROVAL / DENY from permission + risk + thresholds + emergency |
| `core/decision` | `DecisionEngine` — the 16-step pipeline; risk classification |
| `core/memory` | `retrieve()` (relevance/recency/importance/kind), doctrine proposal/approval (immutable) |
| `core/finance` | runway/ROI/EV/CAC/payback — pure, never fabricates, UNKNOWN on missing data |
| `core/approvals` | approval state machine (PENDING→APPROVED/REJECTED/MODIFIED/CANCELLED) |
| `core/tasks` | tasks from decisions, prioritization, blocked detection |
| `core/tools` | typed `Tool` interface + `ToolRegistry` (validation → authority → execution → audit) |
| `db` | `ExecutiveStore` over the `Sql` abstraction (DO `this.sql` in prod, `node:sqlite` in tests) |
| `agent` | `ExecutiveAgent`, reasoner, prompts, context loader, briefings |
| `integrations` | GitHub, Cloudflare, website REST tools |
| `benchmark` | Founder decision benchmark dataset + evaluator |

## 4. Decision pipeline (spec §6)

```
REQUEST → CLASSIFY → IDENTIFY OBJECTIVE → LOAD STATE → LOAD MEMORY →
LOAD DOCTRINE → IDENTIFY CONSTRAINTS → GENERATE OPTIONS → CEO → CFO → CTO →
RISK → CHIEF-OF-STAFF SYNTHESIS → SELECT ACTION → AUTHORITY CHECK →
EXECUTE / ESCALATE → LOG DECISION → MEASURE OUTCOME
```

- Deterministic orchestration lives in `DecisionEngine` (pure, testable).
- LLM-dependent steps (classification, lens analyses, synthesis, option generation)
  live behind `DecisionReasoner`, implemented by `GatewayReasoner`.
- The authority check is real code: `evaluateAction()` against the action spec.
  `APPROVAL` → an `ApprovalRequest` is created and the decision is `ESCALATED`.
  `ALLOW` → the Chief of Staff converts the execution plan into tasks immediately.
- Outcome measurement: every decision stores a `success_metric` and an
  `outcome_status`; verified outcomes become `LESSON` memories (decision memory, spec §20).

## 5. Memory (spec §5)

| Layer | Content | Lifetime | Backing |
|-------|---------|----------|---------|
| L0 Working | current task/session context | short | Agent state + chat messages |
| L1 Operational | cash, revenue, tasks, blockers, approvals | frequent | `financial_snapshots`, `metrics`, `tasks`, `risks` |
| L2 Institutional | product architecture, brand, roadmap, lessons | persistent | `memories` |
| L3 Doctrine | decision principles | permanent, protected | `doctrine_rules` |

Retrieval scores by semantic relevance (token overlap), entity relevance, recency,
importance, kind (LESSON/POLICY boost), and confidence. Doctrine is loaded for every
decision and can only change through the propose→Founder-approve flow.

## 6. Model routing (spec §3, §35)

```
classification / summarization → cheap
routine operational / chat / briefing → medium
financial / technical reasoning, lens analysis → strong
strategic reasoning / executive synthesis → strongest
```

`MODEL_TIER_*` vars map tiers to `provider:model`. Cloudflare AI Gateway is supported
as a proxy (`AI_GATEWAY_ENABLED`). Every call is cost-estimated and budget-checked
(daily / monthly / per-task / per-run); exceeding a budget **STOPs** and notifies —
never silently continues.

## 7. Data model (spec §4)

All 24+ entities from spec §4 are normalized tables (see `migrations/0001_initial.sql`),
each with `id`, `created_at`, `updated_at`, `source` (provenance), and where applicable
`confidence`, `status`. JSON columns are used only for flexible metadata. History is
immutable: corrections are appended, never in-place (spec §21).

## 8. Absorbed orchestrator pipeline

The 10-gate plugin pipeline from `/orchestrator` is seeded as a `PLUGIN_PIPELINE`
workstream with one `pipeline_gates` row per (plugin, gate). Products come from the
real `specs/catalog.md`. This turns the old prompt-based orchestrator into structured,
queryable, gate-tracked work managed by the Chief of Staff.
