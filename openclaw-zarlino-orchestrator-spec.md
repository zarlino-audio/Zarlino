# ZarlinoDSP Build Orchestrator — Spec (OpenClaw Fork)

## 1. Purpose

This system replaces "one model does everything" with a routing layer sitting in front of the ZarlinoDSP build pipeline. It is a **fork of the open-source OpenClaw agent framework**, stripped of its multi-channel messaging surface down to a **single hardened WhatsApp channel** (your ID only), and re-purposed as a deterministic, cost-aware build/QA controller for Zarlino plugin development.

It is **not** a general-purpose assistant platform for Popsin or KRI — this spec is scoped specifically to extending the existing ZarlinoDSP pipeline (20-module framework, Catch2 test harness, QA Platform's three pillars: Reliability, Efficiency, Experience; target of 1,500+ automated test units).

No local models are used. All inference runs against two cloud providers:
- **DeepSeek API** — code generation, vision/image inspection, cheap summarization
- **OpenAI (GPT) API** — expensive reasoning, DSP architecture, escalation tier

---

## 2. Why fork OpenClaw specifically

OpenClaw already provides, out of the box:
- A **Gateway** process — a control plane that multiplexes sessions, tool dispatch, and routing
- **`sessions_spawn`** — native support for a parent agent spawning child agent sessions
- A **Skills Marketplace / skill library** pattern for reusable, shareable capabilities
- A **Channel Adapter** abstraction — of which we keep exactly **one**: the WhatsApp adapter (via the same Baileys-based integration OpenClaw uses natively), hardened to a single allowlisted ID (yours). Every other channel adapter (Slack, Telegram, Discord, iMessage, WebChat) is removed — this isn't a multi-user or multi-channel system.

**The one thing we do NOT inherit as-is:** OpenClaw's native `sessions_spawn` is non-deterministic — the parent LLM decides when to spawn a child, when to retry, when to give up. That is unacceptable for a build pipeline, where "did the compile fail" must trigger a fix-loop deterministically, not "whenever the model feels like checking." So this fork adds a **deterministic Task Router** on top of the Gateway that owns control flow explicitly — model calls only ever produce a decision or artifact, never a flow-control action. WhatsApp messages can *trigger* a task or *approve/reject* a pending gate, but they never bypass the Task Router's state machine.

Fork scope:
| Keep from OpenClaw | Replace | Add |
|---|---|---|
| Gateway process, session lifecycle, tool dispatch | All channel adapters except WhatsApp → removed (Slack/Telegram/Discord/iMessage/WebChat) | Deterministic Task Router (state machine, not LLM-decided) |
| WhatsApp channel adapter → hardened to a single allowlisted ID | `sessions_spawn`'s free-form spawning → replaced with fixed pipeline stages that spawn known task types | Complexity Scorer (Section 5) |
| Skills Marketplace pattern → repurposed as ZarlinoDSP "Coding Standards" skill library | | Model Router (DeepSeek/GPT only, Section 4) |
| Tool dispatch mechanism | | Memory layer (Section 6) |
| | | WhatsApp command/notification schema (Section 8) |

---

## 3. Architecture

```
              You (WhatsApp, your ID) ◄──── notifications/approvals
                        │  commands
                        ▼
       Repo/CLI Adapter ──┴── WhatsApp Adapter (allowlisted)
                                │
                       OpenClaw Gateway (forked)
                                │
                  ┌─────────────┼─────────────┐
                  │             │             │
             Task Router   Memory/RAG     Tool Runner
             (deterministic  (Section 6)   (Section 7)
              state machine)
                  │
       ┌──────────┼──────────┬─────────┬─────────┬──────────┐
       │          │          │         │         │          │
   DSP/Code    Vision    Reasoning   Test      Release   Complexity
   (DeepSeek) (DeepSeek)   (GPT)    (Tool      (Tool      Scorer
                                     Runner)    Runner)   (Section 5)
```

WhatsApp is bidirectional: commands flow in (Section 8.1), status pushes and approval requests flow out (Section 8.2) — it never talks directly to a model, only to the Gateway/Task Router.

The Task Router is a **finite state machine**, not an LLM decision loop. States and transitions are fixed; only the *content* flowing through each state comes from a model or tool call.

---

## 4. Model responsibilities & routing table

| Task | Model/Tool | Rationale |
|---|---|---|
| DSP architecture, filter topology, equations | GPT | High-stakes, low-frequency, benefits from strongest reasoning |
| C++ implementation, CMake, unit tests | DeepSeek Coder | High-frequency, well-specified, cheap |
| Compiler error fixes | DeepSeek Coder | Pattern-matchable, iterative |
| Screenshot/spectrogram/GUI inspection | DeepSeek Vision | Narrow visual QA task |
| Audio QA summary, commit messages, docs | DeepSeek (cheap mode) | Low-stakes summarization |
| Release go/no-go decision | GPT | High-impact, needs full report synthesis |
| Compile / test execution | Tool Runner (no LLM) | Deterministic, not a model task |

GPT only ever sees **final reports**, never raw compiler output, raw test logs, or intermediate diffs — those are pre-digested by DeepSeek or the Tool Runner before reaching the reasoning tier.

---

## 5. Complexity Scorer (escalation policy) — fully specified

This is the piece that decides, per task, whether DeepSeek handles it alone or GPT gets escalated in. It runs **before** model dispatch, as a lightweight rule-based scorer — no LLM call needed to compute the score itself (keeps it cheap and deterministic).

### 5.1 Signals

| Signal | Source | Weight |
|---|---|---|
| **Known pattern match** — has this exact bug/task signature appeared in Memory before, with a recorded resolution? | Memory layer lookup (Section 6) | −40 (known) / +40 (novel) |
| **File fan-out** — number of files touched by the task | Tool Runner (git diff scope) | +5 per file beyond 3, capped at +30 |
| **DSP-domain flag** — does the task involve filter design, oversampling, aliasing, or numerical stability (vs. plumbing/UI/boilerplate)? | Task metadata (set by Task Router based on which pipeline stage triggered it) | +30 if true |
| **Retry count** — how many times has DeepSeek already attempted this specific task and failed QA/compile? | Task Router state | +25 per prior failed attempt, capped at +50 |
| **QA gate severity** — which gate did it fail (G1 = basic compile, G4 = mid-tier correctness, G9 = release-blocking)? | ZarlinoDSP QA Platform gate ID | G1: +0, G4: +15, G9: +35 |
| **Uncertainty marker** — did DeepSeek's own output include a hedge (e.g., flagged low confidence, contradictory results across two runs)? | DeepSeek response metadata | +20 |

### 5.2 Score → action

| Score | Action |
|---|---|
| < 30 | Stays on DeepSeek, no escalation |
| 30–59 | DeepSeek retries once with expanded context (relevant Memory entries injected), re-score |
| 60–89 | Escalate to GPT for this task only; result written back to Memory as a new "known pattern" regardless of outcome |
| ≥ 90 | Escalate to GPT **and** flag for your manual review before the pipeline continues (e.g., new oversampling algorithm design, a subtle aliasing bug with no historical match) |

### 5.3 Policy notes

- Escalation is **per-task**, not per-session — a single Pultec EQ build might escalate one sub-task (filter topology derivation) while a dozen others stay local.
- Every escalation (successful or not) writes a Memory entry, so the same class of problem should score lower next time — the system is designed to need GPT less over time, not more.
- The scorer itself must remain arithmetic/rule-based, not an LLM judgment call — this keeps escalation decisions auditable and free.

---

## 6. Memory / RAG layer (token-efficient design)

Given no local models and a cost-sensitivity goal, the memory layer avoids running a full vector DB + embedding model locally. Instead:

- **Storage**: flat markdown/JSON files on disk, organized exactly as in the original design:
  `Memory/Projects/`, `Plugins/`, `Algorithms/`, `Bugs/`, `UI Standards/`, `Coding Standards/`, `QA Reports/`, `Past Releases/`
- **Indexing**: a lightweight **SQLite + FTS5** (full-text search) index over these files — no embedding model, no vector DB infrastructure, near-zero cost, fast enough for a single-operator build pipeline.
- **Semantic fallback**: only when FTS5 keyword search returns nothing useful does the system fall back to OpenAI's embeddings API (cheap, ~$0.02/1M tokens) to do a one-off similarity search — this keeps embeddings cost-optional rather than a standing bill.
- **Context injection discipline**: only the top 2–3 matched Memory entries are ever injected into a model call, never whole folders — this is the main token-cost lever, more so than the retrieval method itself.

This gives you retrieval that's "good enough" for a single-project, single-operator scale, at near-zero infrastructure and token cost, with a documented upgrade path (swap FTS5 for a real vector store) if the Memory corpus outgrows keyword search.

### 6.1 SQLite schema

```sql
CREATE TABLE memory_entries (
  id          INTEGER PRIMARY KEY,
  category    TEXT NOT NULL,   -- Projects|Plugins|Algorithms|Bugs|UI Standards|Coding Standards|QA Reports|Past Releases
  title       TEXT NOT NULL,
  file_path   TEXT NOT NULL,   -- source-of-truth markdown lives on disk, DB only indexes it
  tags        TEXT,            -- comma-separated, e.g. "aliasing,oversampling,ZPunch"
  created_at  TEXT NOT NULL,
  updated_at  TEXT NOT NULL
);

CREATE VIRTUAL TABLE memory_fts USING fts5(
  title, body, tags,
  content='memory_entries', content_rowid='id'
);

-- keep FTS in sync automatically
CREATE TRIGGER memory_ai AFTER INSERT ON memory_entries BEGIN
  INSERT INTO memory_fts(rowid, title, body, tags) VALUES (new.id, new.title, '', new.tags);
END;
CREATE TRIGGER memory_ad AFTER DELETE ON memory_entries BEGIN
  INSERT INTO memory_fts(memory_fts, rowid, title, body, tags) VALUES('delete', old.id, old.title, '', old.tags);
END;
CREATE TRIGGER memory_au AFTER UPDATE ON memory_entries BEGIN
  INSERT INTO memory_fts(memory_fts, rowid, title, body, tags) VALUES('delete', old.id, old.title, '', old.tags);
  INSERT INTO memory_fts(rowid, title, body, tags) VALUES (new.id, new.title, '', new.tags);
END;
```

`body` is populated at index time by reading the markdown file at `file_path` — the DB never becomes the source of truth, it's a disposable, rebuildable index. A `memory.reindex` tool wipes and rebuilds it from disk on demand, so schema changes are never a migration problem.

---

## 7. Tool Runner — concrete tool contracts

Each tool is invoked with a fixed JSON contract; the Task Router calls these directly, never through model discretion.

### 7.1 Compiler
```
invoke: build.compile
input:  { module: string, target: "debug"|"release", cmake_flags: string[] }
output: { success: bool, errors: [{file, line, message}], warnings: [...], duration_ms: int }
```
On `success: false` → Task Router routes `errors[]` to DeepSeek Coder for a fix, then re-invokes `build.compile`. Loop capped at 5 attempts before escalating to GPT per Section 5.

### 7.2 CTest / Catch2 harness
```
invoke: test.run
input:  { module: string, gate: "G1"|"G4"|"G9", test_filter: string? }
output: { passed: int, failed: int, failures: [{test_name, message}], coverage_pct: float }
```

### 7.3 Plugin Validator
```
invoke: validate.plugin
input:  { plugin_path: string, format: "VST3"|"AU"|"AAX" }
output: { valid: bool, issues: string[] }
```

### 7.4 Plugin Host (offline render)
```
invoke: host.render
input:  { plugin_path: string, test_signal: "sine"|"impulse"|"pink_noise"|"sweep", sample_rate: int, duration_s: float }
output: { audio_path: string, render_time_ms: int }
```

### 7.5 Image Comparison (feeds DeepSeek Vision)
```
invoke: qa.screenshot_diff
input:  { plugin_path: string, reference_image: string?, resolution: [int,int] }
output: { screenshot_path: string, diff_score: float? }
```
`screenshot_path` is then passed to DeepSeek Vision with a fixed prompt template (clipping/alignment/scaling/color/missing-asset checklist — mirrors the existing QA Platform "Experience" pillar).

### 7.6 Audio Renderer / FFT Analyzer / Spectrogram Generator
```
invoke: audio.analyze
input:  { audio_path: string, checks: ("cpu"|"latency"|"aliasing"|"denormals"|"automation")[] }
output: { cpu_pct: float, latency_ms: float, aliasing_db: float?, denormal_events: int, automation_smoothness: float? }
```
Maps directly to the QA Platform's "Reliability" and "Efficiency" pillars and existing RAII denormal-protection test gates.

### 7.7 Git / GitHub
```
invoke: vcs.diff_scope   → { files_changed: string[], lines_changed: int }
invoke: vcs.commit       → { sha: string }
invoke: vcs.pr_create    → { pr_url: string }
```
`vcs.diff_scope` output feeds directly into the Complexity Scorer's "file fan-out" signal (Section 5.1).

### 7.8 API retry/backoff policy (DeepSeek & GPT)

- **Exponential backoff with jitter**: base 2s, doubling each retry, capped at 60s, ±20% random jitter to avoid thundering-herd retries.
- **Max retries**: 5 per call. After exhaustion, the task is marked `blocked_on_api` (not failed) and queued — it resumes automatically once the provider is reachable again, rather than requiring a manual re-trigger.
- **Error handling by type**:
  - `429` (rate limit) → honor `Retry-After` header if present, otherwise use the backoff schedule above.
  - `5xx` (provider outage) → same backoff, but after 3 consecutive `5xx` a **circuit breaker** opens for that provider (DeepSeek or GPT independently) for 5 minutes — no further calls attempted, all tasks for that provider queue instead of hammering a down API.
  - Timeout → treated as a `5xx` for backoff purposes.
- **Provider fallback**: if GPT is circuit-broken and a task is mid-escalation (Section 5.2), it stays queued rather than silently falling back to DeepSeek for a task DeepSeek already failed — escalation exists precisely because DeepSeek wasn't sufficient.
- **WhatsApp notification**: a circuit breaker opening triggers a status push ("GPT API unreachable, N tasks queued") so you're not left wondering why the pipeline stalled.

---

## 8. WhatsApp channel — spec

### 8.1 Inbound (commands)

Uses the same Baileys-based WhatsApp integration pattern OpenClaw already supports natively — kept as the single retained channel adapter. **Hard allowlist: only your WhatsApp ID is accepted; every other sender is dropped silently at the adapter, before it ever reaches the Gateway.**

Fixed command grammar (parsed deterministically, not LLM-interpreted, to avoid prompt-injection risk from message content):

| Command | Effect |
|---|---|
| `build <plugin name>` | Task Router starts the "new plugin" pipeline template (Section 9) |
| `status` | Returns current task graph state — which stage each in-flight build is at |
| `approve` / `reject` | Resolves a pending gate that's waiting on you (see 8.2) — only accepted when a gate is actually awaiting confirmation, otherwise ignored |
| `retry <task_id>` | Manually re-triggers a failed task instead of waiting for the automatic retry loop |
| free text | Never sent to a model directly — logged, and you get a reply telling you which fixed commands are supported |

### 8.2 Outbound (notifications & approvals)

The Task Router pushes to WhatsApp at fixed trigger points — it does not narrate every internal step, only decision-relevant ones:

| Trigger | Message |
|---|---|
| Compile loop exhausted (5 attempts) before GPT escalation | "Build stalled on `<module>`, escalating to GPT." |
| Complexity Score ≥ 90 (Section 5.2) | "New DSP problem, no historical match — needs your review before continuing." + summary |
| G9 (release-blocking) gate reached | "`<plugin>` passed all gates. GPT recommends: accept/reject/redesign. Reply `approve` or `reject`." + **one attached image**: the plugin GUI screenshot from `qa.screenshot_diff` (Section 7.5) — the single highest-value visual for a release decision. **Pipeline blocks here until you respond, regardless of GPT's recommendation** (this resolves the open question from the prior draft about whether G9 sign-off needs manual confirmation — it now always does, via WhatsApp) |
| Pipeline complete | Final QA report summary (pass/fail counts, CPU/latency numbers, link to full report) — text only |

### 8.3 Security notes

- Allowlist is a single hardcoded WhatsApp ID, checked at the adapter layer, not the model layer — a compromised or spoofed message can't reach the model even if the ID check were somehow bypassed downstream, because commands are parsed against the fixed grammar in 8.1, not free-form instructions.
- No plugin source, credentials, or Memory contents are ever sent over WhatsApp — only status text, report summaries, and (for G9 only) a single GUI screenshot.

---

## 9. Example workflow: "Build a Pultec EQ"

1. **Task Router** receives the request, creates a task graph (not an LLM decision — a fixed template for "new plugin" requests).
2. **GPT** (Reasoning tier) designs the DSP: filter topology, equations, parameter behavior. Written to `Memory/Algorithms/`.
3. **DeepSeek Coder** implements C++ from the GPT spec.
4. **`build.compile`** runs. On failure → DeepSeek Coder fixes → re-compile loop (max 5, then Complexity Scorer escalates per Section 5.2).
5. **`test.run`** (gate G1 → G4 → G9 in sequence).
6. **`qa.screenshot_diff`** generates screenshots → **DeepSeek Vision** checks clipping/alignment/scaling/color/missing assets.
7. **`audio.analyze`** measures CPU, latency, aliasing, denormals, automation smoothness.
8. **GPT** reviews the final report bundle only (not raw logs) and returns accept / reject / redesign.
9. Every step's outcome — pass or fail — is written to the relevant `Memory/` folder, so the next plugin build starts with more known patterns and a lower average Complexity Score.

---

## 10. Fork versioning strategy

- **Pin, don't track**: the fork pins to a specific upstream OpenClaw commit/tag rather than auto-pulling `main`. Upstream moves fast (100k+ stars, active development) and an unreviewed pull could silently reintroduce non-deterministic spawning behavior this spec deliberately removes.
- **Branch structure**: `upstream/main` (untouched mirror of the real OpenClaw repo) + `zarlino/main` (the integration branch with the Task Router, Complexity Scorer, WhatsApp-only channel, etc.). Fork-specific changes live as a reviewable diff against `upstream/main`, not scattered edits.
- **Rebase cadence**: quarterly, manual — pull upstream, diff for breaking changes to the Gateway/tool-dispatch APIs this system depends on, re-run the full ZarlinoDSP test suite before merging. No automatic rebasing.
- **Changelog**: a `FORK_CHANGES.md` in the repo root tracking every deviation from upstream and why, so a future contributor (or future-you) doesn't have to reverse-engineer which parts are "real OpenClaw" vs. Zarlino-specific.

All previously open items are now resolved: the Memory schema is specified (Section 6.1), the retry/backoff policy is specified (Section 7.8), the WhatsApp image question is resolved (Section 8.2 — one screenshot at G9 only), and versioning is specified above.
