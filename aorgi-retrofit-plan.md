# AOrgI Retrofit Plan — Zarlino Artificial Organization (v1)

Status: **PHASE 1 + PHASE 2 OPERATIONAL (deployed 2026-09-06, versions bd9b89d4 → 0fcedfa6) → PHASE 3 NEXT** · Source of truth: `aoi-technical-spec.md` + AOrgI Master System Instruction.
Architecture version: AOrgI-1.0 · Date: 2026-09-06 · Owner: Chief Operating Agent (COA) — first runtime instance.

---

## 1. Infrastructure Baseline (verified against workspace, 2026-09-05)

| Domain | What exists |
|---|---|
| Agent runtime | `executive-os/` — Cloudflare Worker + **one** `ExecutiveAgent` Durable Object (`zarlino`), agents SDK, DeepSeek V4 (chat/flash/reasoner/vision), tool loop with checkpoint/resume. |
| Second runtime | `scripts/local-daemon.ts` + `scripts/local-agent.ts` — machine-local DeepSeek loop (Windows), claims `local_tasks`, runs `local_*` tools (build, render, FL launch), heartbeats live worker. |
| Orchestration | `agent_jobs` long-horizon runs (auto-run tasks, 2-concurrent factory), maintenance sweeps, cron `0 */6 * * *`, bounded `startAutoRunnable`, resume/continue. |
| Governance | Authority engine (ALLOW/APPROVAL/DENY + emergency switches + full_access/delegate), `approval_requests`, doctrine, `audit_events`, Doctor (19 checks, self-healing). |
| Memory | `memories` (semantic lessons), `chat_messages`, `model_costs`, DO SQLite (org tables), KV + R2-not-enabled. |
| Roles | 12 pipeline roles seeded as `persons` (`person_role_*`) — **data only, not agent instances**. |
| Tasks | `tasks` w/ `success_condition`, `assignee_person_id`, `dependency_task_id`, `auto_run`; `local_tasks` bridge; `productivity_assignments` skill matching; `review_items` (v21 critic queue); `contradictions` scan. |
| Blackboard | `/api/data/all` bundle + `/api/executive/overview` + 10 console views. |
| Commercial | zarlino-website (Astro): Paystack GHS checkout, affiliate program, admin dashboard embedding Executive OS. |
| Plugins | ZScorch 1.0.0 + ZTame 1.0.0 SHIPPED · ZVocals 0.1.0 · PresenceField 0.1.0 (dev) · RACK_AI — JUCE/CMake, 10-gate pipeline, NSIS, licensing. |
| QA | Perceptual Intelligence layer (v22: evaluators, tests, validations, failure taxonomy), 71/71 vitest, plugin ctest suites. |

## 2. Capability Mapping (existing → AOrgI model)

| AOrgI requirement | Status | Mapping / Gap |
|---|---|---|
| Roles + Agent instances + Human distinction | **PARTIAL** | Roles = `person_role_*` rows; no machine-readable role manifests; no agent-instance registry. **GAP-1 (highest value)** |
| Agent manifest (lineage/model/scope/autonomy/memory/verification) | **MISSING** | **GAP-1** |
| Capability governance (`CapabilityGrant`) | PARTIAL | Authority engine is global-per-action, not per-role×capability. Extend later (Phase 3). |
| Governance control plane | **EXISTS** | Authority + approvals + doctor + audit. Keep. |
| Autonomy L0–L3 per capability | PARTIAL | full_access/delegate global switches only. Per-capability levels arrive with GAP-1 manifests (Phase 3). |
| Reasoning trace | PARTIAL | `agent_jobs.checkpoint_json` transcripts; add structured trace fields with GAP-1. |
| 4-tier memory | PARTIAL | Working=context · Episodic=job transcripts · Semantic=`memories` · Org=SQLite. Versioned org writes: settings/executive_state — formalize in Phase 2. |
| Organizational blackboard | **EXISTS** | `/api/data/all` + overview. |
| Message protocol (STATUS/REQUEST/RESULT/DEVIATION/ESCALATION) | MISSING | Only if ≥2 peer agents exist. Defer until agent instances are real (anti-theater: today the DO and daemon are a client/server pair, not peers). |
| Task contract (acceptance/verification/escalation/autonomy) | **PARTIAL** | `success_condition` only. **GAP-2** — add contract fields to tasks. |
| Hierarchical planning | PARTIAL | auto-run decomposition exists; no `plan_id`/`PlanDeviationEvent`. Phase 3. |
| Critic system (different lineage) | PARTIAL | `review_items` queue + perceptual validations; no lineage tracking/discounting. Lineage lands with GAP-1. |
| Trust system (agent×capability) | MISSING | Phase 2 (needs registry first). |
| Negotiation/arbitration | MISSING | Phase 3 (no peer agents yet — would be theater now). |
| Scaling 12→36 | DEFERRED | Registry must exist first so scaling decisions are evidence-based (§8, §33). |

## 3. Honest Agent Count (no theater, §42)

Today exactly **two runtimes exist**: `executive_do_01` (cloud DO, performs the COA role) and `local_daemon_01` (machine daemon, performs local-capability execution). They are registered as agent instances. The **12 roles are manifests, not fake agents** — instances are instantiated only when measured demand requires (§33). The registry makes that measurable.

## 4. Phases (from §39, adapted to existing infra)

- **PHASE 1 (now):** org state = registry + manifests + task contracts. `departments`, `org_roles` (12 role manifests: id, department, lineage_id, model, autonomy default, capabilities, obligations, critic_role), `org_agents` (instance registry w/ heartbeat = real liveness), task-contract columns on `tasks`. Doctor check `org.registry`. REST `/api/org/*`. Tests. → **Implementation below.**
- **PHASE 2 (done):** versioned organizational memory writes + episodic `PostObjectiveRecord`; trust (agent×capability) evidence; critic lineage discounting; message contracts for the DO↔daemon pair.
- **PHASE 3:** per-capability autonomy grants (CapabilityGrant over the authority engine); hierarchical plans + deviation events; negotiation/arbitration (only after ≥2 peer instances exist).
- **PHASE 4:** collective-intelligence metrics (emergence/convergence/robustness/scalability/objective-achievement), chaos tests in staging, organizational analyst job.
- **PHASE 5:** autonomous topology optimization (deferred until trust + metrics exist).

## 5. Phase 1 implementation record

| Component | Status |
|---|---|
| Migration v23 `org_manifests` (departments + 12 role manifests + org_agents + task-contract ALTER + tsk_ztame_p8 first contract) | **IMPLEMENTED + TESTED** (79/79 vitest, `test/org.test.ts`) |
| Store: `listDepartments/listOrgRoles/listOrgAgents/upsertOrgAgent/setTaskContract/listTaskContracts` | **IMPLEMENTED + TESTED** |
| REST `/api/org/departments|roles|agents|task-contracts` (+ POST task-contract patch) | **IMPLEMENTED + INTEGRATED** (tsc clean, wrangler dry-run clean) |
| Doctor check `org.registry` (coherence + liveness, 2-day stale warning) | **IMPLEMENTED + TESTED** |
| Runtime heartbeats: DO self-registers `executive_do_01` (role_coa) on wake; daemon heartbeat upserts `local_daemon_01` (role_software_engineering) | **IMPLEMENTED + INTEGRATED** |
| Snapshot/backup includes `departments`, `org_roles`, `org_agents` | **IMPLEMENTED** |
| Production activation | **OPERATIONAL** — deployed 2026-09-06 (version `bd9b89d4`). Verified live: 6 departments, 12 roles, both runtime instances registered with fresh heartbeats (`executive_do_01` COA, `local_daemon_01` software-engineering), `tsk_ztame_p8` contract visible via `/api/org/task-contracts`. Doctor runs 20 checks (org.registry present; 2 pre-existing FAILs: provider balance + interrupted runs). |

Nothing is marked OPERATIONAL until it runs in production (§41).

Honest instance registry: exactly 2 runtimes exist — `executive_do_01` (COA role, cloud DO) and `local_daemon_01` (software-engineering role, local execution capability; flag: the daemon is a capability executor rather than a pure software-engineering instance — its role split is a future topology decision, not a hidden role invention, §2/§5). The 12 role manifests are templates; no fake agents are instantiated (§42).

## 5b. Phase 2 implementation record (migration v24, deployed 2026-09-06 as `0fcedfa6`)

| Component | Status |
|---|---|
| `org_memory` — versioned organizational memory, optimistic concurrency (READ→WRITE-WITH-VERSION→CONFLICT), supersedes version links | **OPERATIONAL** — live-verified: stale write rejected 409 `ORG_MEMORY_CONFLICT`, real `aorgi_deploy_log` entry at v3 |
| `post_objective_records` + doctor obligation check `org.post_objective` | **OPERATIONAL** (doctor runs 21 checks; no completed objectives yet, so no WARN) |
| `org_trust` — per agent × capability, evidence-counted, 30-day half-life decay (`src/core/org/trust.ts`) | **OPERATIONAL** — real feed wired: every local-task finish is trust evidence for `local_daemon_01.local_execution` |
| `org_messages` — STATUS/REQUEST/RESULT/DEVIATION/ESCALATION contracts | **OPERATIONAL** — real feed wired: every local-task finish emits RESULT (or DEVIATION on failure) from software-engineering → COA |
| Critic-lineage vote discounting (`src/core/org/voting.ts`, root-n per lineage) | **IMPLEMENTED + TESTED** — no peer critic votes exist yet, so no live consumption (would be theater) |
| REST `/api/org/memory|post-objectives|trust|messages` + `org` key in data bundle | **OPERATIONAL** |
| Tests | 91/91 vitest (12 new in `test/org-phase2.test.ts`), tsc clean, bundle clean |

Phase 2 notes: trust/message rows populate with real daemon work, not seeds (§42). PostObjectiveRecords appear when an objective actually completes — the doctor enforces the §38 obligation from then on.

## 6. First organizational objective after Phase 1

Run one real task through the registry: create a task contract for `tsk_ztame_p8` (the blocking A/B listening test) with acceptance criteria + verification owner (QA) + escalation path (founder), assigned to a role manifest — and verify the loop reports honestly in the console.
