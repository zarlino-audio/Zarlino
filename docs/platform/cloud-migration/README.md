# Zarlino Cloud-Only MCP Migration — Trackers

Source mandate: `zarlino-cloud-only-mcp-migration-mandate.md` (this folder)
- 1,271 lines / 24,626 chars, recovered 2026-09-18 from VS Code chat state after the client dropped the pasted attachment from the prompt.
- SHA-256 of recovered file: `43891C047BD6082CAC4C7743943F28DA316A9108E5B16D39EE468DCFEC7CB92F`
- Attachment id: `chat-paste-ca54aceb-f71b-4f1a-825b-5fae27090a75` (workspace state.vscdb, `memento/interactive-session` → history[38])

Reporting standard (mandate §30): every significant operation is reported as
**ACTION / EXPECTED RESULT / ACTUAL RESULT / INDEPENDENT VERIFICATION / EVIDENCE / STATUS**
where STATUS ∈ {VERIFIED, PARTIALLY VERIFIED, BLOCKED, FAILED, NOT APPLICABLE}.

---

## Phase tracker (mandate §26)

| # | Phase | Status | Notes |
|---|-------|--------|-------|
| 1 | Inventory | COMPLETED | `01-infrastructure-inventory.md` — live-verified 2026-09-18; sub-items BLOCKED: GitHub admin scopes, `az` CLI, CF zone list |
| 2 | Repository preservation | COMPLETED v1 | Repo already private (verified); secret scan performed → `02-github-audit-and-secret-scan.md` (SEC-1..3 open; admin-scope follow-ups BLOCKED) |
| 3 | Secrets | BLOCKED | Needs secret-store decisions + live worker verification; CF_API_TOKEN invalid (see below) |
| 4 | Cloud state | NOT STARTED | KV live verification pending |
| 5 | MCP read plane | NOT STARTED | |
| 6 | MCP write plane | NOT STARTED | |
| 7 | Durable jobs | NOT STARTED | Root cause of phantom IDs identified (local daemon leases) |
| 8 | GitHub cloud builds | NOT STARTED | ZTame release-gate workflow exists on origin/main |
| 9 | Azure execution | BLOCKED | `az` CLI not installed; Azure signing secrets exist in CI |
| 10 | Artifact system | NOT STARTED | |
| 11 | Perceptual validation | NOT STARTED | Tool surface already exists (`perceptual.*` tools) |
| 12 | CEO console | NOT STARTED | Console exists in `executive-os/web` |
| 13 | Independent verification | NOT STARTED | Cloud Doctor exists for local checks only |
| 14 | Local decommission | NOT STARTED | `executive-os/scripts/local-daemon.ts` is the target |

## Deliverables tracker (mandate §31)

| # | Deliverable | Status | File |
|---|-------------|--------|------|
| 1 | Final architecture document | NOT STARTED | |
| 2 | Complete infrastructure inventory | COMPLETED v1 | `01-infrastructure-inventory.md` |
| 3 | Security/secret migration report | PARTIAL | findings SEC-1..SEC-7 in `02-github-audit-and-secret-scan.md`; migration plan is approval-gated |
| 4 | GitHub audit report | COMPLETED v1 | `02-github-audit-and-secret-scan.md` |
| 5 | Cloudflare configuration report | PARTIAL | inventory §3 — workers/KV/R2/Pages verified; zones BLOCKED |
| 6 | Azure execution architecture | NOT STARTED | |
| 7 | MCP tool catalog | NOT STARTED | raw material: `executive-os/src/agent/tools.ts` (71 tools) |
| 8 | MCP authorization policy | NOT STARTED | raw material: risk levels in tool registry |
| 9 | Durable job specification | NOT STARTED | |
| 10 | GitHub Actions build pipeline | NOT STARTED | |
| 11 | Cloud audio-render pipeline | NOT STARTED | |
| 12 | Artifact-storage architecture | NOT STARTED | |
| 13 | Evaluator enrollment system | NOT STARTED | |
| 14 | Perceptual-validation integration | NOT STARTED | |
| 15 | Cloud Doctor | NOT STARTED | existing System Doctor = local/worker scope |
| 16 | Incident disposition report | IN PROGRESS | inventory §11 |
| 17 | CEO-console architecture | NOT STARTED | |
| 18 | Migration verification report | NOT STARTED | |
| 19 | Rollback plan | NOT STARTED | |
| 20 | Local-machine decommission report | NOT STARTED | |
| 21 | Final operating runbook | NOT STARTED | existing: `executive-os/docs/OPERATOR_RUNBOOK.md` |

---

## Session status log

### 2026-09-29 — Phase B (cloud runtime completeness; founder-approved "proceed with B")

1. **Funding unblocked + Doctor blindness fixed.** Deployed `a5ed3f70` + `49e30d25`: `provider.balance` is recency-aware (a 402 counts only until a success follows it — a top-up no longer leaves the Doctor red for weeks); limit-reached runs of closed tasks are excluded from resumable counts/lists/resets; `POST /api/agents/runs/:id/continue` now 409s for RUNNING runs and DONE/CANCELLED tasks and looks runs up by id. Verified live: chat probe `ALIVE`; laptop daemon executed real cloud-issued task `ltk_mumsw8oq` (hostname → DONE); both FAILs → **OK; Doctor now 24 OK / 3 WARN / 0 FAIL** (WARNs = founder judgment: 10 task contracts, 7 contradictions, 2 perceptual evidence gaps). Incidents `inc_provider_balance` + `inc_interrupted_runs` RESOLVED with evidence.
2. **Secrets**: `ZARLINO_LICENSE_PRIVATE_KEY` set on the exec worker (ed25519; local pair verified against the .pub). `ADMIN_TOKEN` ROTATED — the old value was committed to the PUBLIC website repo (compromised); new value is a website-worker secret, literal scrubbed from `wrangler.toml` (commit `7023478`); live-verified new→200 / bogus→401. Founder copy: `Desktop\zarlino-executive-os\.local\website-admin-token.txt`.
3. **Artifact system (mandate §12/§25) live**: R2 bucket `zarlino-artifacts` + `ARTIFACTS` binding + `/api/artifacts` (upload/list/fetch/delete, generated `artifacts/` keys, traversal-safe, 25 MB cap, audited) — commit `245e452`; live smoke test passed end-to-end.
4. **CI**: canonical repo workflow (typecheck/tests/console build/dry-run) — first run **GREEN**. Release gates: root cause of every ZTame/ZScorch gate failure found — `Azure/artifact-signing-action@v3` does not exist (latest v2) and GitHub resolves `uses:` at job setup; pinned `@v2` on `fix/meter-provider-usage` (`b30776ea`) and `chore/ci-code-signing-pipeline` (`317d2787`); gate re-run dispatched (build verifying at close).
5. **Remote box (phase 9 prep)**: `remote-provision.ps1` bug fixed — it installed with `--omit=dev` while the daemon runs via `tsx` (a devDependency), so the daemon could never start on the box; now full `npm ci` + explicit tsx check. Runbook updated. **BLOCKED: the Azure VM is deallocated/unreachable (`102.37.153.164:3389` no answer, 2026-09-29) — founder must start it in the Azure portal, then run `.\remote-provision.ps1 -CloneWorkspace` on the box** (secrets typed there; registers `remote_daemon_01`).

### 2026-09-21 — OS consolidation session (merge plan Phase A, founder-approved)

1. **Twin-source + live audit.** Found: (a) monorepo `executive-os` worktree regressed 2026-09-18 to a v25-era snapshot (3rd incident of this class) while HEAD 6c3da05c held the true v32 lineage; (b) standalone repo stale at 2026-09-08 but sole holder of the uncommitted remote-runtime kit; (c) live worker 4+ commits behind (chat v2 deploy, Sep 13).
2. **Verified merge candidate** (isolated git worktree, zero risk to the live tree): HEAD + 3 untracked feature files (evidence.ts, task-evidence-schema.ts, pricing.ts) + `_gov_patch_*` re-applied → tsc 0, vitest 186/187 (1 fail = CRLF artifact, later fixed).
3. **Canonical repo established.** `zarlino-audio/zarlino-executive-os` main: merge commit `05eecdd` (+6787/−391, 54 files) + launcher fix `afa9f50`. Verified after assembly: **tsc 0 errors, vitest 192/192 (21 files)**. Remote kit ported (instance identity, per-instance heartbeats, DELETE /api/org/agents/:id, finish attribution, path normalization); `.gitattributes` + CRLF-robust test added.
4. **DEPLOYED LIVE: worker version `21c96546`.** Evidence: `DELETE /api/org/agents/nonexistent_probe_01` → `404 {"error":"Agent not found"}`; `GET /api/tasks/:id/evidence` → `{"taskId":…,"evidence":[]}`; task list carries `close_reason` + `evidence{total,verified}`; doctor 27 checks (2 pre-existing FAILs unchanged); org registry fresh (`executive_do_01`, `local_daemon_01`).
5. **Monorepo copy retired**: commit `117fa732` (102 files, −34,343 lines) pushed; folder deleted; safety archive `Deploy\_archive_executive-os-retired-20260921.tgz` (19 MB). **All local branches pushed** (fix/meter-provider-usage=6c3da05c + 7 new branches incl. ZTame v1.1 history, wip/zscorch-dsp-reengineer).
6. **Laptop daemon re-homed to canonical** (`Desktop\zarlino-executive-os`) with `ZARLINO_WORKSPACE` in the launcher; startup entry re-installed; verified online + heartbeat fresh + pending 0.

BLOCKED/NEXT (Phase B+): DeepSeek top-up (funds — all agent work stays 402 until funded); RDP box provisioning (`remote_daemon_01`); R2 enable; `ZARLINO_LICENSE_PRIVATE_KEY` on exec worker; 199 dirty plugin files remain uncommitted in the monorepo (plugin workstream, outside OS scope).

### 2026-09-18 — Session 1057a212

1. **Recovered the lost mandate** (see top). STATUS: VERIFIED.
2. **Phase 1 inventory started** — workspace, repos, workers, CI, licenses, incidents mapped; live GitHub + Cloudflare verification performed where credentials allowed.
3. **Blockers/risks found (preliminary):**
   - `CF_API_TOKEN` in `executive-os/.dev.vars` is invalid (CF API: 401 `Invalid API Token`). Wrangler OAuth (zarlino001@gmail.com) works for deploys.
   - Production `zarlino-executive-os` worker is **missing `ZARLINO_LICENSE_PRIVATE_KEY`** (secret list shows only CF_API_TOKEN, DEEPSEEK_API_KEY, FOUNDER_AUTH_TOKEN, GITHUB_TOKEN, PAYSTACK_SECRET_KEY).
   - Local working tree: 262 uncommitted changes (26 modified, 231 untracked) — local `main` is otherwise in sync with `origin/main` (`0d56116c`); remote last push 2026-08-30.
   - `gh` and `az` CLIs not installed on the migration machine.
   - Website `wrangler.toml` commits an `ADMIN_TOKEN` literal (secret in git).

### 2026-09-18 — Phase 1 live verification results (summary; full detail in `01-infrastructure-inventory.md`)

- **Cloudflare**: account `ae604b60…`; KV namespaces `ZARLINO_KV`, `Admin`; **R2 NOT enabled (error 10042)**; no Pages projects; exec-os worker 10 deployments (latest 2026-09-13T07:25Z), website worker 10 (latest 2026-09-08T11:24Z).
- **Live exec-os Doctor** (27 checks: 20 OK / 5 WARN / 2 FAIL):
  - FAIL `provider.balance` — DeepSeek HTTP 402 Insufficient Balance since 2026-09-05T18:00:56Z (cloud agent cannot work until topped up).
  - FAIL `jobs.interrupted_runs` — 2 resumable runs the maintenance sweep keeps retrying.
  - WARNs: `secrets.required` (missing ZARLINO_LICENSE_PRIVATE_KEY in exec-os), `org.task_contracts` (4 tasks incomplete contracts), `org.incidents_open` (2 open), `productivity.contradictions` (1), `perceptual.evidence_gaps` (prod_zscorch, prod_ztame).
- **Website**: live pages + all 6 download URLs = 200; `/api/health` = 500 (follow-up); website worker HAS ZARLINO_LICENSE_PRIVATE_KEY.
- **Tool surface**: 71 registered tools across 13 namespaces (`executive-os/src/agent/tools.ts`) — basis for the MCP tool catalog.
- **Approval-required next actions**: top up DeepSeek balance (CRITICAL/financial), enable R2, rotate ADMIN_TOKEN, set exec-os license key via `wrangler secret put` (CRITICAL/security), provision scoped service tokens, commit/push 262 WIP changes. See inventory §11.
- **Phase 2 audit executed** (`02-github-audit-and-secret-scan.md`): root repo already **private**; findings — SEC-1 `ADMIN_TOKEN` committed in `zarlino-website/wrangler.toml` (also in history → treat as compromised), SEC-2 live keys in local `.dev.vars` (git-ignored, never committed), SEC-3 hard-coded `INTERNAL_SECRET` in exec-os source; **no private-key blocks anywhere in git history**; `tools/keys/*private*.pem` exists only locally (ignored) — Phase 3 migration source.
