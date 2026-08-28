# Zarlino Executive OS

An institutional intelligence and execution layer for **Zarlino Audio** — a single
Executive Agent with CEO / CFO / CTO reasoning lenses and a Chief of Staff
coordination layer. It is **not** a chatbot and **not** a collection of
disconnected AI personalities. It is a decision-making and operating system.

> «Make Zarlino operationally intelligent even when the Founder is unavailable,
> while keeping strategic control and irreversible authority with the Founder.»

## What it does

1. Understands Zarlino's current state (durable, structured).
2. Remembers institutional knowledge (4-layer memory).
3. Applies Founder-defined decision principles (protected doctrine).
4. Reasons across strategy / finance / technology / operations (one consolidated answer).
5. Creates and prioritizes work (Chief of Staff → tasks).
6. Uses approved, typed, permissioned tools.
7. Executes low-risk actions autonomously.
8. Escalates high-risk decisions to the Founder (approval queue).
9. Records every decision and outcome (immutable audit trail).
10. Learns from verified outcomes without silently changing its core doctrine.

## Architecture at a glance

```
Founder ──► Web Executive Console ──► Worker / API ──► ExecutiveAgent (Durable Object)
                                                          │
                                    ┌─────────────────────┼─────────────────────┐
                                  SQLite               Memory               Scheduler
                                    └─────────────────────┼─────────────────────┘
                                                     Executive Engine
                                              ┌───────────┼───────────┐
                                             CEO         CFO         CTO
                                              └───────────┼───────────┘
                                                      Decision
                                                         │
                                                    Chief of Staff
                                              ┌──────────┼──────────┐
                                            Tasks      Tools     Approvals
```

- **One agent, four lenses.** CEO/CFO/CTO are analytical disciplines; Chief of Staff is the
  coordination/execution layer. The system returns ONE consolidated recommendation.
- **Cloudflare Agents SDK** (`agents`) for durable state, SQLite, scheduling, and
  durable execution (fibers). No hand-rolled agent infrastructure.
- **Model-agnostic gateway** — DeepSeek, OpenAI, Anthropic, Workers AI, and future
  providers, routed per task by cost/latency/reasoning/reliability. Cloudflare AI
  Gateway is supported as a proxy.
- **Backend-enforced authority** — prompts are not security controls. Every tool and
  action passes the authority engine; approvals are a real queue.

## Repository layout

```
executive-os/
├── src/
│   ├── index.ts                 Worker entry (REST API + agent routing + assets)
│   ├── agent/
│   │   ├── executive-agent.ts   The Executive Agent (Durable Object)
│   │   ├── reasoner.ts          LLM reasoner (lenses, classification, synthesis)
│   │   ├── tools.ts             Tool registry wiring (internal + integrations)
│   │   ├── context.ts           Loads L1/L2/L3 context for decisions
│   │   ├── briefings.ts         Daily brief + weekly review generators
│   │   └── prompts.ts           Separated prompt blocks (spec §26)
│   ├── api/                     REST API router + auth (spec §30)
│   ├── core/                    Pure, testable engine modules:
│   │   ├── authority/           Permission engine (spec §11)
│   │   ├── decision/            Decision pipeline + risk engine (§6, §23)
│   │   ├── memory/              Retrieval + doctrine protection (§5, §27)
│   │   ├── model/               Model gateway, routing, cost (§3, §34, §35)
│   │   ├── finance/             Financial calculations (§8)
│   │   ├── approvals/           Approval state machine (§12)
│   │   ├── tasks/               Task model (§10)
│   │   ├── tools/               Typed tool system + registry (§13)
│   │   └── observability/       Audit events (§25)
│   ├── db/                      Schema, migrations, ExecutiveStore (§31)
│   ├── integrations/            GitHub / Cloudflare / website tools (§14–15)
│   ├── seed/                    Verified seed data (doctrine, catalog, pipeline)
│   └── benchmark/               Founder decision benchmark (§33)
├── web/                         React + Vite executive console (§16, §29)
├── migrations/0001_initial.sql  Checked-in schema
├── test/                        Unit + integration + benchmark tests (§32)
├── docs/                        architecture, deployment, security, report
└── wrangler.jsonc               Cloudflare configuration
```

## Live deployment

The Executive OS is deployed to Cloudflare production:

**https://zarlino-executive-os.zarlino001.workers.dev**

- **First visit forces you to create your founder username and password**; afterwards
  it's a normal username/password sign-in (PBKDF2-hashed, session tokens, 30-day expiry,
  lockout after 5 failed attempts).
- It is also embedded in the existing admin dashboard at **https://zarlinoaudio.com/admin**
  (Executive OS section with an embedded console + "Open Executive OS" button).
- The legacy `FOUNDER_AUTH_TOKEN` still works as a bearer credential for scripts/APIs.
- Production secrets are set with `wrangler secret put` — never in source.
- Re-deploy after changes: `npm --prefix web run build && npx wrangler deploy`.

## Quick start

Prerequirements: Node ≥ 20, a Cloudflare account (for deploy), and at least one
model provider key (optional — the system falls back to a deterministic mock
without keys so you can run everything locally).

```sh
cd executive-os
npm install
cp .dev.vars.example .dev.vars      # add keys + FOUNDER_AUTH_TOKEN
npm run dev                          # wrangler dev (worker on :8787)
# in a second terminal:
npm --prefix web install
npm --prefix web run dev             # console on :5173 → proxies /api to :8787
```

Open http://localhost:5173 — sign in with the `FOUNDER_AUTH_TOKEN` (the console
asks you for it if the API returns 401; set it in Settings or via the
`Authorization: Bearer <token>` header).

Without any API keys the Executive uses the **mock provider** so every flow —
chat, decisions, approvals, briefings — works end to end.

## Tests

```sh
npm test          # vitest: unit + integration + benchmark
npm run typecheck # tsc --noEmit
```

## Deploy

See [`docs/deployment.md`](docs/deployment.md). CI (`.github/workflows/executive-os-ci.yml`)
runs lint/typecheck/tests/build, deploys **staging** automatically, and requires
**manual approval** before **production**.

## Security model

See [`docs/security.md`](docs/security.md). Highlights: bearer-token auth, backend
authority enforcement, no silent doctrine changes, immutable history via
append-only corrections, budget STOP on AI spend, emergency pause.

## Relationship to the Plugin Factory Orchestrator

The Executive OS **absorbs** the previous prompt-based
`/orchestrator` system: the 10-gate plugin pipeline is seeded as a
`PLUGIN_PIPELINE` workstream (`pipeline_gates`), products are seeded from the
real catalog, and the Chief of Staff manages gates as work. The `/orchestrator`
docs remain as the source of the gate definitions.
