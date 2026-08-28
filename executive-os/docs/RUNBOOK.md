# Zarlino Executive OS — Operator Runbook (survive 6 months without the CEO)

This runbook is written so a *non-expert* operator can keep the Executive OS
running for months unattended. It covers what the system does on its own, what
to check when something looks wrong, and how to recover from a restart.

---

## 1. What the system does automatically (no human needed)

| Concern | Built-in behavior |
|---|---|
| **Daily AI budget** | $8/day (configurable). Spend is tracked in `model_costs`; when exceeded the model calls STOP and the Founder is notified. Budget resets at midnight UTC. |
| **Token budget (display)** | 30M tokens/day guide shown in the AI Fuel gauge on the Overview. Cost budgets are what actually enforce. |
| **Self-healing on restart** | Every DO wake runs a maintenance sweep: crash-interrupted long-horizon runs are marked FAILED (resumable via **Continue**), expired local-task leases are re-queued as PENDING, expired login sessions are deleted. |
| **Local task leases** | A local machine claims a task with a 10-minute lease. If the machine goes offline mid-task, the lease expires and the task returns to PENDING so *any* daemon can re-claim it. The daemon also heartbeats every 60s while working. |
| **Durable run checkpoints** | Long-horizon runs save their transcript after every tool call. After a DO restart, **Continue** resumes from the exact checkpoint instead of re-doing work. |
| **Model retry** | Transient 5xx/429/network failures are retried with exponential backoff (up to `MAX_RETRIES`, default 2), so a hiccup doesn't kill a run. |
| **Daily brief + weekly review** | Scheduled, durable, idempotent. |
| **Backups** | `GET /api/system/snapshot` exports the full state as JSON; `POST /api/system/snapshot` restores it. |

## 2. Manual recovery after the machine restarts

1. **Restart the local daemon** (the only always-running local process):
   ```powershell
   cd C:\Users\khrit\Desktop\plugin\executive-os
   npm run agent:daemon -- --loop --watchdog
   ```
   `--watchdog` auto-restarts the daemon if it crashes. `--loop` polls every 10s.
2. **Any task that was mid-flight on the old machine** was re-queued by the
   server when its lease expired — the daemon will pick it up automatically.
3. **Interrupted long-horizon runs**: open the Tasks view → Long-Horizon Runs →
   press **Continue** (resumes from the checkpoint). Or call
   `POST /api/agents/runs/:id/continue`.

## 3. Checking health

- **Web console** → Overview shows GREEN/YELLOW/RED, the AI Fuel gauge, open
  tasks, and pending approvals.
- **Deep health endpoint** (needs auth token):
  ```powershell
  $t = "<FOUNDER_AUTH_TOKEN>"
  Invoke-RestMethod -Uri "https://zarlino-executive-os.zarlino001.workers.dev/api/system/health" -Headers @{ Authorization = "Bearer $t" }
  ```
  Returns `status`, `stuck` (stale agent runs + expired leases), budget spend,
  open tasks, pending approvals.
- **Run maintenance manually** (safe, idempotent):
  ```powershell
  Invoke-RestMethod -Method Post -Uri "https://zarlino-executive-os.zarlino001.workers.dev/api/system/maintenance" -Headers @{ Authorization = "Bearer $t" }
  ```

## 4. Backup / restore (survive 6 months + full reset)

```powershell
# Backup the entire system state to a file:
Invoke-RestMethod -Uri ".../api/system/snapshot" -Headers @{ Authorization = "Bearer $t" } | ConvertTo-Json -Depth 10 | Set-Content backup.json

# Restore it later (full overwrite):
$snap = Get-Content backup.json -Raw | ConvertFrom-Json
Invoke-RestMethod -Method Post -Uri ".../api/system/snapshot" -Headers @{ Authorization = "Bearer $t" } -Body ($snap | ConvertTo-Json -Depth 10) -ContentType "application/json"
```

> Do this weekly if the system matters — the snapshot includes tasks, decisions,
> approvals, doctrine, memory, pipeline state, uploaded files, local tasks,
> agent runs, and founder credentials.

## 5. Emergency controls (enforced server-side)

| Control | Endpoint |
|---|---|
| Pause the Executive | `POST /api/executive/pause {reason}` |
| Disable spending | `POST /api/executive/spending {disabled:true}` |
| Disable deployments | `POST /api/executive/deployments {disabled:true}` |
| Autonomy mode | `POST /api/executive/autonomy {mode:"PAUSED"}` |
| Delegate mode | `POST /api/executive/delegate {enabled:false}` |
| Full access | `POST /api/executive/fullaccess {enabled:false}` |

These are in the Settings view of the web console too.

## 6. What needs the Founder (cannot be automated)

- Approving HIGH/CRITICAL or irreversible actions (Approvals view).
- Accepting/rejecting doctrine changes.
- Rotating the API keys/tokens (see Security note below).
- Signing the founder account back in after a full restore (credentials are
  included in the snapshot).

## 7. Key operational facts

- **Live URL**: https://zarlino-executive-os.zarlino001.workers.dev
- **DO**: single `ExecutiveAgent` instance named `zarlino`; SQLite-backed.
- **Daily spend resets** at 00:00 UTC.
- **Redeploy**: `cd executive-os && npm --prefix web run build && npx wrangler deploy`
- **Local daemon**: `npm run agent:daemon -- --loop --watchdog`
- **Rotate when convenient**: `GITHUB_TOKEN`, `CF_API_TOKEN`,
  `FOUNDER_AUTH_TOKEN`, `DEEPSEEK_API_KEY` (they have appeared in chat logs).

## 8. Secrets

All secrets live in Worker **secrets** (`wrangler secret put`) and local
`.dev.vars` (git-ignored). The `vars` block in `wrangler.jsonc` is
non-secret configuration only. See `.dev.vars.example` for the full list.
