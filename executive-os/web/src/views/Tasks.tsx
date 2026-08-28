import { useEffect, useRef, useState } from "react";
import { api, type AgentRun, type LocalTask, type Person, type Task } from "../api";
import { ErrorBox, Spinner, fmtDate } from "../components/ui";
import { Markdown } from "../components/Markdown";

const PRIORITY_ORDER: Record<string, number> = { P0: 0, P1: 1, P2: 2, P3: 3 };
const PRIORITY_CLASS: Record<string, string> = { P0: "pill red", P1: "pill yellow", P2: "pill", P3: "pill" };
const STATUS_PILL: Record<string, string> = {
  OPEN: "pill",
  IN_PROGRESS: "pill yellow",
  BLOCKED: "pill red",
  DONE: "pill green",
  FAILED: "pill red",
  PENDING: "pill",
  RUNNING: "pill yellow",
  CLAIMED: "pill yellow",
  STOPPED: "pill",
  TIMEOUT: "pill yellow",
};

type Column = "OPEN" | "IN_PROGRESS" | "BLOCKED";
const COLUMNS: Array<{ key: Column; label: string; icon: string }> = [
  { key: "OPEN", label: "To Do", icon: "📥" },
  { key: "IN_PROGRESS", label: "In Progress", icon: "⏳" },
  { key: "BLOCKED", label: "Blocked", icon: "🚧" },
];

function taskBrief(t: LocalTask): { goal?: string; acceptance?: string } | null {
  try {
    return t.meta_json ? (JSON.parse(t.meta_json) as { goal?: string; acceptance?: string }) : null;
  } catch {
    return null;
  }
}

export function TasksView() {
  const [tasks, setTasks] = useState<Task[] | null>(null);
  const [persons, setPersons] = useState<Person[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [title, setTitle] = useState("");
  const [priority, setPriority] = useState("P2");
  const [autoOnCreate, setAutoOnCreate] = useState(true);
  // Streaming "run with executive" state per task.
  const [runningId, setRunningId] = useState<string | null>(null);
  const [runOutput, setRunOutput] = useState<Record<string, string>>({});
  // Persistent long-horizon runs.
  const [runs, setRuns] = useState<AgentRun[] | null>(null);
  // Local machine tasks (live web → local daemon bridge).
  const [localTasks, setLocalTasks] = useState<LocalTask[] | null>(null);
  const [localPrompt, setLocalPrompt] = useState("");
  const [localError, setLocalError] = useState<string | null>(null);
  const [msg, setMsg] = useState<string | null>(null);
  const [showCompleted, setShowCompleted] = useState(false);
  const logRef = useRef<HTMLDivElement>(null);

  const loadRuns = async () => {
    try {
      setRuns(await api.agentRuns());
    } catch {
      /* non-fatal */
    }
  };

  const loadLocal = async () => {
    try {
      setLocalTasks(await api.localTasks());
    } catch {
      /* non-fatal */
    }
  };

  const load = async () => {
    try {
      const all = await api.tasks();
      setTasks(all);
      api.persons().then(setPersons).catch(() => {});
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  useEffect(() => {
    void load();
    void loadRuns();
    void loadLocal();
  }, []);
  // Auto-refresh local task statuses (daemon results appear live), throttled.
  useEffect(() => {
    const t = setInterval(() => void loadLocal(), 6000);
    return () => clearInterval(t);
  }, []);
  // Auto-refresh tasks + executive runs so auto-executions complete on screen
  // without any click (the whole point of the auto-run sweep).
  useEffect(() => {
    const t = setInterval(() => {
      if (document.hidden) return;
      void load();
      void loadRuns();
    }, 20_000);
    return () => clearInterval(t);
  }, []);

  /** Queue a task for the LOCAL machine (plugin UI inspection, repo work). */
  const requestLocal = async () => {
    if (!localPrompt.trim()) return;
    try {
      setLocalError(null);
      await api.localTaskCreate(localPrompt.trim());
      setLocalPrompt("");
      await loadLocal();
    } catch (err) {
      setLocalError(err instanceof Error ? err.message : String(err));
    }
  };

  /** Run the self-healing maintenance sweep (reap stale runs + expired leases). */
  const runMaintenance = async () => {
    try {
      const r = await api.maintenance();
      setLocalError(null);
      setMsg(
        `${r.reapedLocal.length} lease(s) re-queued · ${r.staleRuns.length} stale run(s) reaped · ${r.expiredSessions} session(s) purged` +
          (r.autoStarted.length > 0 ? ` · 🤖 auto-started ${r.autoStarted.length} task(s)` : "")
      );
      setTimeout(() => setMsg(null), 5000);
      await Promise.all([loadLocal(), loadRuns(), load()]);
    } catch (err) {
      setLocalError(err instanceof Error ? err.message : String(err));
    }
  };

  /** Start a persistent long-horizon run on a task (resumable, safe-bounded). */
  const longRun = async (task: Task) => {
    if (runningId) return;
    try {
      const prompt = `Work on this task end-to-end using your tools until it is complete: ${task.title}. ${task.description ?? ""}`;
      await api.agentRun(prompt, task.id);
      await loadRuns();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  const continueRun = async (runId: string) => {
    try {
      await api.agentRunContinue(runId);
      await loadRuns();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  const create = async () => {
    if (!title.trim()) return;
    try {
      await api.createTask({ title: title.trim(), priority, autoRun: autoOnCreate });
      setTitle("");
      await load();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  /** Flip whether a task runs autonomously (no approval) vs manual. */
  const toggleAuto = async (id: string, on: boolean) => {
    try {
      await api.updateTask(id, { autoRun: on });
      await load();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  const setStatus = async (id: string, status: string) => {
    try {
      await api.updateTask(id, { status });
      await load();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  const assign = async (id: string, assigneePersonId: string) => {
    try {
      await api.updateTask(id, { assigneePersonId });
      await load();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  /** Deploy the Executive on a task live: it executes tools, we watch it stream. */
  const runWithExecutive = async (task: Task) => {
    if (runningId) return;
    setRunningId(task.id);
    setRunOutput((r) => ({ ...r, [task.id]: "" }));
    setError(null);
    const prompt = `Execute this task using your tools — do the ACTUAL work (read/write repo files, run checks, use GitHub/website/Cloudflare tools as needed), then report exactly what you did and the outcome.\n\nTASK: ${task.title}\n${task.description ?? ""}`;
    try {
      await api.chatStream(prompt, undefined, {
        onMeta: () => {},
        onDelta: (text) => setRunOutput((r) => ({ ...r, [task.id]: (r[task.id] ?? "") + text })),
        onDone: () => { setRunningId(null); void load(); },
        onError: (m) => { setError(m); setRunningId(null); },
      });
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
      setRunningId(null);
    }
  };

  // Auto-scroll the inline run output as it streams.
  useEffect(() => {
    if (logRef.current) logRef.current.scrollTop = logRef.current.scrollHeight;
  }, [runOutput, runningId]);

  const open = (tasks ?? []).filter((t) => ["OPEN", "IN_PROGRESS", "BLOCKED"].includes(t.status));
  const closed = (tasks ?? []).filter((t) => ["DONE", "CANCELLED"].includes(t.status));
  const byCol = (key: Column) =>
    open.filter((t) => t.status === key).sort((a, b) => (PRIORITY_ORDER[a.priority] ?? 9) - (PRIORITY_ORDER[b.priority] ?? 9));
  const running = (runs ?? []).filter((r) => r.status === "RUNNING");

  const TaskCard = ({ t }: { t: Task }) => (
    <div className={`task-card ${runningId === t.id ? "task-running" : ""}`}>
      <div className="task-card-head">
        <span className={PRIORITY_CLASS[t.priority] ?? "pill"}>{t.priority}</span>
        <span className={`pill ${STATUS_PILL[t.status] ?? ""}`}>{t.status.replace("_", " ")}</span>
        <button
          className={`auto-toggle ${t.auto_run ? "on" : ""}`}
          onClick={() => void toggleAuto(t.id, !t.auto_run)}
          title={t.auto_run ? "Auto-run ON — the Executive executes this without your approval" : "Auto-run OFF — this needs a manual Run"}
        >
          {t.auto_run ? "🤖 auto" : "✋ manual"}
        </button>
      </div>
      <div className="task-card-title">{t.title}</div>
      {t.description && <div className="faint small task-card-desc">{t.description}</div>}
      <div className="row task-card-actions">
        <select
          className="gate-status task-assign"
          value={t.assignee_person_id ?? ""}
          onChange={(e) => void assign(t.id, e.target.value)}
          title="Assignee"
        >
          <option value="">unassigned</option>
          {(persons ?? []).map((p) => <option key={p.id} value={p.id}>{p.name}</option>)}
        </select>
        {t.status !== "IN_PROGRESS" && <button className="btn small" onClick={() => void setStatus(t.id, "IN_PROGRESS")} title="Move to In Progress">▶</button>}
        {t.status !== "BLOCKED" && <button className="btn small" onClick={() => void setStatus(t.id, "BLOCKED")} title="Block">🚧</button>}
        <button className="btn small green" onClick={() => void setStatus(t.id, "DONE")} title="Mark Done">✔</button>
        <button className="btn small primary" disabled={runningId !== null} onClick={() => void runWithExecutive(t)} title="Run the Executive on this task (live stream)">
          {runningId === t.id ? "…" : "▶ Run"}
        </button>
        <button className="btn small" disabled={runningId !== null} onClick={() => void longRun(t)} title="Persistent long-horizon run (resumable)">⏳</button>
      </div>
      {runningId === t.id && (
        <div className="msg executive mt" ref={logRef}>
          <div className="who">Executive working…</div>
          {runOutput[t.id] ? <Markdown text={runOutput[t.id]} /> : <span className="cursor" />}
        </div>
      )}
    </div>
  );

  return (
    <div>
      {error && <div className="mb"><ErrorBox message={error} /></div>}

      {/* Quick create + health */}
      <div className="card task-create">
        <div className="row" style={{ gap: 8, flexWrap: "wrap" }}>
          <input
            type="text"
            className="task-create-input"
            placeholder="New task…  (e.g. Ship PresenceField Gate 4) — auto-runs unless you switch it off"
            value={title}
            onChange={(e) => setTitle(e.target.value)}
            onKeyDown={(e) => { if (e.key === "Enter") void create(); }}
          />
          <select value={priority} onChange={(e) => setPriority(e.target.value)} className="task-create-pri" title="Priority">
            <option value="P0">P0</option>
            <option value="P1">P1</option>
            <option value="P2">P2</option>
            <option value="P3">P3</option>
          </select>
          <label className="task-auto-create" title="Execute without your approval">
            <input type="checkbox" checked={autoOnCreate} onChange={(e) => setAutoOnCreate(e.target.checked)} />
            <span>🤖 auto-run</span>
          </label>
          <button className="btn primary" disabled={!title.trim()} onClick={() => void create()}>Add</button>
        </div>
        <p className="small muted mt">
          Tasks marked <b>🤖 auto</b> are executed by the Executive automatically (no Run click) and marked done when
          complete — they only pause when your approval is required. Flip a card to <b>✋ manual</b> to keep it on the
          board for you to run yourself.
        </p>
        {msg && <div className="ok small mt">{msg}</div>}
      </div>

      {/* Summary chips */}
      <div className="row task-summary">
        <span className="pill green">✔ {closed.length} done</span>
        <span className="pill">📥 {byCol("OPEN").length} to do</span>
        <span className="pill yellow">⏳ {byCol("IN_PROGRESS").length} in progress</span>
        <span className="pill red">🚧 {byCol("BLOCKED").length} blocked</span>
        <span className="spacer" />
        <button className="btn small" onClick={() => void runMaintenance()} title="Reap stale runs, re-queue expired leases, purge old sessions">🩺 Maintenance</button>
      </div>

      {/* Workflow board */}
      <div className="task-board">
        {COLUMNS.map((col) => {
          const items = byCol(col.key);
          return (
            <div key={col.key} className="task-col">
              <div className="task-col-head">
                <span>{col.icon} {col.label}</span>
                <span className="pill">{items.length}</span>
              </div>
              <div className="task-col-body">
                {items.length === 0 ? (
                  <div className="faint small task-col-empty">Nothing here.</div>
                ) : (
                  items.map((t) => <TaskCard key={t.id} t={t} />)
                )}
              </div>
            </div>
          );
        })}
      </div>

      {/* Executive runs */}
      <CardSimple title="Executive Runs">
        {!runs ? <Spinner /> : runs.length === 0 ? (
          <span className="faint">No runs yet — 🤖 auto tasks start themselves; use ⏳ on a card to start one manually.</span>
        ) : (
          <div className="run-list">
            {running.length > 0 && (
              <div className="run-live">
                <span className="pill yellow">● running</span>
                {running.map((r) => <span key={r.id} className="faint small mono">{r.id.slice(0, 8)} · {r.iterations} iters · {r.tools_called} tools</span>)}
              </div>
            )}
            <table>
              <thead><tr><th>Run</th><th>Status</th><th>Iters</th><th>Tools</th><th>When</th><th></th></tr></thead>
              <tbody>
                {runs.slice(0, 10).map((r) => (
                  <tr key={r.id}>
                    <td data-label="Run" className="mono small">{r.id.slice(0, 12)}</td>
                    <td data-label="Status"><span className={`pill ${STATUS_PILL[r.status] ?? ""}`}>{r.status}</span></td>
                    <td data-label="Iters" className="mono">{r.iterations}</td>
                    <td data-label="Tools" className="mono">{r.tools_called}</td>
                    <td data-label="When" className="mono faint small">{fmtDate(r.started_at)}</td>
                    <td data-label="Actions">
                      {r.status !== "RUNNING" && r.status !== "DONE" && (
                        <button className="btn small" onClick={() => void continueRun(r.id)} title="Resume (fresh budget, from checkpoint)">Continue</button>
                      )}
                      {r.status === "DONE" && r.result && (
                        <details>
                          <summary className="faint small">report</summary>
                          <div className="msg executive"><Markdown text={r.result} /></div>
                        </details>
                      )}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </CardSimple>

      {/* Local work (this machine) */}
      <CardSimple title="Local Work — this machine" className="mt">
        <p className="small muted mb">
          Queued for the <b>local machine</b> (repo + plugin UI source). The daemon
          (<code>npm run agent:daemon -- --loop --yes</code>) claims tasks and posts reports here.
        </p>
        <div className="row" style={{ gap: 8, flexWrap: "wrap" }}>
          <input
            type="text"
            className="task-create-input"
            placeholder="e.g. Inspect the ZScorch plugin UI locally and report its controls…"
            value={localPrompt}
            onChange={(e) => setLocalPrompt(e.target.value)}
            onKeyDown={(e) => { if (e.key === "Enter") void requestLocal(); }}
          />
          <button className="btn primary" disabled={!localPrompt.trim()} onClick={() => void requestLocal()}>Queue</button>
          <button className="btn small" onClick={() => void loadLocal()}>↻</button>
        </div>
        {localError && <div className="mt"><ErrorBox message={localError} /></div>}
        <div className="mt">
          {!localTasks ? <Spinner /> : localTasks.length === 0 ? (
            <span className="faint">No local tasks yet.</span>
          ) : (
            <div className="local-list">
              {localTasks.slice(0, 12).map((t) => {
                const brief = taskBrief(t);
                const leaseExpired = t.lease_expires_at && new Date(t.lease_expires_at).getTime() < Date.now();
                return (
                  <div key={t.id} className="local-row">
                    <span className={`pill ${STATUS_PILL[t.status] ?? ""}`}>{t.status}</span>
                    <div className="local-row-body">
                      <div className="small">{t.prompt.slice(0, 140)}</div>
                      {brief?.goal && brief.goal !== t.prompt && <div className="faint small">🎯 {brief.goal.slice(0, 120)}</div>}
                      {brief?.acceptance && <div className="faint small">✔ done when: {brief.acceptance.slice(0, 120)}</div>}
                      {leaseExpired && <div className="faint small">⏳ lease expired — will re-queue</div>}
                      {t.error && <div className="err-text small">{t.error.slice(0, 160)}</div>}
                      {t.result && (
                        <details>
                          <summary className="faint">report ({t.result.length} chars)</summary>
                          <div className="msg executive"><Markdown text={t.result} /></div>
                        </details>
                      )}
                    </div>
                    <span className="mono faint small">{fmtDate(t.created_at).slice(5)}</span>
                  </div>
                );
              })}
            </div>
          )}
        </div>
      </CardSimple>

      {/* Completed — collapsed by default (thin things out). */}
      <CardSimple title={`Completed (${closed.length})`} className="mt">
        <button className="btn small" onClick={() => setShowCompleted((s) => !s)}>
          {showCompleted ? "Hide" : `Show (${closed.length})`}
        </button>
        {showCompleted && closed.length > 0 && (
          <table className="mt">
            <thead><tr><th>Task</th><th>Status</th><th>When</th></tr></thead>
            <tbody>
              {closed.slice(0, 30).map((t) => (
                <tr key={t.id}>
                  <td data-label="Task">{t.title}</td>
                  <td data-label="Status"><span className="pill">{t.status}</span></td>
                  <td data-label="When" className="mono faint">{fmtDate(t.created_at)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </CardSimple>
    </div>
  );
}

/** Minimal card (title + children) without the old table-heavy chrome. */
function CardSimple({ title, children, className }: { title: string; children: React.ReactNode; className?: string }) {
  return (
    <div className={`card ${className ?? ""}`}>
      <h3>{title}</h3>
      {children}
    </div>
  );
}
