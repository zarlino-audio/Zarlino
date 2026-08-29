import { useCallback, useEffect, useState } from "react";
import {
  api,
  clearToken,
  getToken,
  setToken,
  type Approval,
  type BudgetOverrides,
  type DoctorLog,
  type DoctorReport,
  type Overview,
  type Task,
} from "./api";

type Tab = "overview" | "doctor" | "chat" | "tasks" | "approvals" | "settings";

const TABS: Array<{ id: Tab; label: string }> = [
  { id: "overview", label: "Overview" },
  { id: "doctor", label: "System Doctor" },
  { id: "chat", label: "Chat" },
  { id: "tasks", label: "Tasks" },
  { id: "approvals", label: "Approvals" },
  { id: "settings", label: "Settings" },
];

export function App() {
  const [token, setTokenState] = useState(getToken());
  const [tab, setTab] = useState<Tab>("overview");

  if (!token) {
    return <Login onLogin={(t) => { setToken(t); setTokenState(t); }} />;
  }

  return (
    <div>
      <header>
        <h1>Zarlino <span>Executive OS</span></h1>
        <nav>
          {TABS.map((t) => (
            <button key={t.id} className={tab === t.id ? "active" : ""} onClick={() => setTab(t.id)}>
              {t.label}
            </button>
          ))}
          <button
            className="ghost"
            onClick={() => { clearToken(); setTokenState(""); }}
            title="Sign out (removes the stored founder token from this browser)"
          >
            Sign out
          </button>
        </nav>
      </header>

      {tab === "overview" && <OverviewView />}
      {tab === "doctor" && <DoctorView />}
      {tab === "chat" && <ChatView />}
      {tab === "tasks" && <TasksView />}
      {tab === "approvals" && <ApprovalsView />}
      {tab === "settings" && <SettingsView />}
    </div>
  );
}

// ---- login -----------------------------------------------------------------

function Login({ onLogin }: { onLogin: (token: string) => void }) {
  const [value, setValue] = useState("");
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);

  const submit = async () => {
    const t = value.trim();
    if (!t) return;
    setBusy(true);
    setError("");
    try {
      // Validate the token against the live worker before storing it.
      const health = await fetch("/api/health").then((r) => r.json());
      if (!health?.ok) throw new Error("Worker unreachable");
      // Token is validated by the first authorized request; store optimistically.
      onLogin(t);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };

  return (
    <div>
      <header>
        <h1>Zarlino <span>Executive OS</span></h1>
      </header>
      <div className="card" style={{ maxWidth: 460 }}>
        <h2>Founder sign in</h2>
        <p className="muted">
          Paste your founder auth token (the value of <span className="mono">FOUNDER_AUTH_TOKEN</span>).
          It is stored only in this browser and sent as a bearer token to the Executive OS worker.
        </p>
        {error && <div className="error-box">{error}</div>}
        <label>Auth token</label>
        <input
          type="password"
          value={value}
          onChange={(e) => setValue(e.target.value)}
          onKeyDown={(e) => e.key === "Enter" && submit()}
          placeholder="founder_…"
          autoFocus
        />
        <div className="row" style={{ marginTop: 14 }}>
          <button className="action" onClick={submit} disabled={busy || !value.trim()}>
            {busy ? "Connecting…" : "Sign in"}
          </button>
        </div>
      </div>
    </div>
  );
}

// ---- overview ---------------------------------------------------------------

function OverviewView() {
  const [data, setData] = useState<Overview | null>(null);
  const [error, setError] = useState("");

  const load = useCallback(async () => {
    setError("");
    try {
      setData(await api.overview());
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  }, []);

  useEffect(() => {
    void load();
  }, [load]);

  if (error) return <div className="error-box">{error}</div>;
  if (!data) return <p className="muted">Loading overview…</p>;

  return (
    <div>
      <div className="card">
        <div className="row" style={{ justifyContent: "space-between" }}>
          <h2>Executive state</h2>
          <span className={`badge ${data.state.status === "GREEN" ? "ok" : "warn"}`}>{data.state.status}</span>
        </div>
        <p className="muted">
          Mode: <span className="mono">{data.state.mode}</span> · Products:{" "}
          {data.products.map((p) => `${p.name} (${p.status})`).join(", ") || "none"}
        </p>
      </div>

      <div className="grid">
        {Object.entries(data.counters).map(([k, v]) => (
          <div className="metric" key={k}>
            <div className="value">{v}</div>
            <div className="key">{k}</div>
          </div>
        ))}
      </div>

      <div className="card section-gap">
        <h2>Recent audit trail</h2>
        {data.recentAudit.length === 0 ? (
          <p className="muted">No audit entries yet.</p>
        ) : (
          <table>
            <thead>
              <tr><th>Time</th><th>Actor</th><th>Action</th></tr>
            </thead>
            <tbody>
              {data.recentAudit.map((a) => (
                <tr key={a.id}>
                  <td className="mono muted">{new Date(a.created_at).toLocaleString()}</td>
                  <td>{a.actor}</td>
                  <td className="mono">{a.action}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
    </div>
  );
}

// ---- doctor -----------------------------------------------------------------

function DoctorView() {
  const [report, setReport] = useState<DoctorReport | null>(null);
  const [logs, setLogs] = useState<DoctorLog[]>([]);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [autoFix, setAutoFix] = useState(true);

  const loadLogs = useCallback(async () => {
    try {
      setLogs((await api.doctorLogs()).logs);
    } catch {
      /* non-fatal */
    }
  }, []);

  useEffect(() => {
    void loadLogs();
  }, [loadLogs]);

  const run = async () => {
    setBusy(true);
    setError("");
    try {
      setReport(await api.doctorRun(autoFix));
      await loadLogs();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };

  const runMaintenance = async () => {
    setBusy(true);
    setError("");
    try {
      const out = await api.maintenance();
      setError(`Maintenance complete: ${JSON.stringify(out)}`);
      await loadLogs();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };

  return (
    <div>
      <div className="card">
        <div className="row" style={{ justifyContent: "space-between" }}>
          <h2>System Doctor</h2>
          <div className="row">
            <label style={{ margin: 0, display: "flex", alignItems: "center", gap: 6 }}>
              <input type="checkbox" checked={autoFix} onChange={(e) => setAutoFix(e.target.checked)} style={{ width: "auto" }} />
              auto-fix
            </label>
            <button className="action" onClick={run} disabled={busy}>
              {busy ? "Running…" : "Run Doctor"}
            </button>
            <button className="ghost" onClick={runMaintenance} disabled={busy}>
              Maintenance sweep
            </button>
          </div>
        </div>
        <p className="muted">
          Runs every 6h automatically on the worker (cron + alarm) and on every wake. Checks schema
          migrations, DB tables, stale runs, expired leases/sessions, budget overrides, metrics, secrets
          and KV health — and auto-fixes what it can.
        </p>
      </div>

      {error && <div className="error-box">{error}</div>}

      {report && (
        <div className="card">
          <div className="row" style={{ justifyContent: "space-between" }}>
            <h2>
              Report —{" "}
              <span className={report.healthy ? "badge ok" : "badge warn"}>
                {report.healthy ? "HEALTHY" : "ISSUES FOUND"}
              </span>
            </h2>
            <span className="muted mono">
              {new Date(report.generatedAt).toLocaleString()} · trigger={report.trigger} · autoFix={String(report.autoFix)}
            </span>
          </div>
          <table>
            <thead>
              <tr><th>Status</th><th>Check</th><th>Detail</th></tr>
            </thead>
            <tbody>
              {report.checks.map((c) => (
                <tr key={c.id}>
                  <td><span className={`badge ${c.status === "OK" ? "ok" : c.status === "WARN" ? "warn" : "fail"}`}>{c.status}</span></td>
                  <td className="mono">{c.id}</td>
                  <td>
                    {c.detail}
                    {c.fixed && <div className="muted">↳ FIXED: {c.fixed}</div>}
                    {c.action && <div className="muted">↳ ACTION: {c.action}</div>}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
          <p className="muted mono">
            Summary: {report.summary.ok}/{report.summary.total} ok · {report.summary.warn} warn ·{" "}
            {report.summary.fail} fail · {report.summary.fixed} fixed
          </p>
        </div>
      )}

      <div className="card">
        <h2>Doctor run history</h2>
        {logs.length === 0 ? (
          <p className="muted">No runs recorded yet.</p>
        ) : (
          <table>
            <thead>
              <tr><th>Time</th><th>Trigger</th><th>Auto-fix</th><th>Summary</th></tr>
            </thead>
            <tbody>
              {logs.map((l) => {
                let summary = l.summary_json;
                try {
                  summary = JSON.stringify(JSON.parse(l.summary_json));
                } catch { /* keep raw */ }
                return (
                  <tr key={l.id}>
                    <td className="mono muted">{new Date(l.run_at).toLocaleString()}</td>
                    <td className="mono">{l.trigger}</td>
                    <td>{l.auto_fix ? "yes" : "no"}</td>
                    <td className="mono">{summary}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        )}
      </div>
    </div>
  );
}

// ---- chat -------------------------------------------------------------------

function ChatView() {
  const [sessionId, setSessionId] = useState("default");
  const [input, setInput] = useState("");
  const [messages, setMessages] = useState<Array<{ role: "user" | "assistant"; content: string }>>([]);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");

  const send = async () => {
    const text = input.trim();
    if (!text || busy) return;
    setInput("");
    setError("");
    setMessages((m) => [...m, { role: "user", content: text }]);
    setBusy(true);
    try {
      const reply = await api.chat(text, sessionId);
      setMessages((m) => [...m, { role: "assistant", content: reply.reply }]);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };

  return (
    <div>
      <div className="card">
        <h2>Chat with the Executive</h2>
        <label>Session id</label>
        <input value={sessionId} onChange={(e) => setSessionId(e.target.value)} className="mono" />
        <div className="chat-log" style={{ marginTop: 12 }}>
          {messages.length === 0 && <p className="muted">No messages yet. Ask the Executive anything.</p>}
          {messages.map((m, i) => (
            <div key={i} className={`msg ${m.role}`}>
              {m.content}
            </div>
          ))}
          {busy && <div className="msg assistant muted">Thinking…</div>}
        </div>
        {error && <div className="error-box">{error}</div>}
        <div className="row">
          <textarea
            value={input}
            onChange={(e) => setInput(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === "Enter" && !e.shiftKey) {
                e.preventDefault();
                void send();
              }
            }}
            placeholder="Message the Executive… (Enter to send, Shift+Enter for newline)"
            disabled={busy}
          />
          <button className="action" onClick={send} disabled={busy || !input.trim()}>
            Send
          </button>
        </div>
      </div>
    </div>
  );
}

// ---- tasks ------------------------------------------------------------------

function TasksView() {
  const [tasks, setTasks] = useState<Task[]>([]);
  const [title, setTitle] = useState("");
  const [priority, setPriority] = useState("P2");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");

  const load = useCallback(async () => {
    try {
      setTasks((await api.tasks()).tasks);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  }, []);

  useEffect(() => {
    void load();
  }, [load]);

  const create = async () => {
    if (!title.trim() || busy) return;
    setBusy(true);
    setError("");
    try {
      await api.createTask(title.trim(), undefined, priority);
      setTitle("");
      await load();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };

  return (
    <div>
      <div className="card">
        <h2>Create task</h2>
        {error && <div className="error-box">{error}</div>}
        <label>Title</label>
        <input value={title} onChange={(e) => setTitle(e.target.value)} placeholder="e.g. Investigate the noisy threshold in ZScorch" />
        <div className="row" style={{ marginTop: 10 }}>
          <label style={{ margin: 0 }}>Priority</label>
          <select
            value={priority}
            onChange={(e) => setPriority(e.target.value)}
            style={{ background: "var(--bg)", color: "var(--text)", border: "1px solid var(--border)", borderRadius: 8, padding: "8px 10px" }}
          >
            <option value="P0">P0 — critical</option>
            <option value="P1">P1 — high</option>
            <option value="P2">P2 — medium</option>
            <option value="P3">P3 — low</option>
          </select>
          <button className="action" onClick={create} disabled={busy || !title.trim()}>
            Create
          </button>
        </div>
      </div>

      <div className="card">
        <h2>Open tasks</h2>
        {tasks.length === 0 ? (
          <p className="muted">No tasks yet.</p>
        ) : (
          <table>
            <thead>
              <tr><th>Title</th><th>Status</th><th>Priority</th><th>Created</th></tr>
            </thead>
            <tbody>
              {tasks.map((t) => (
                <tr key={t.id}>
                  <td>{t.title}</td>
                  <td><span className={`badge ${t.status === "DONE" ? "ok" : "warn"}`}>{t.status}</span></td>
                  <td><span className="badge">{t.priority}</span></td>
                  <td className="mono muted">{new Date(t.created_at).toLocaleString()}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
    </div>
  );
}

// ---- approvals ---------------------------------------------------------------

function ApprovalsView() {
  const [approvals, setApprovals] = useState<Approval[]>([]);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");

  const load = useCallback(async () => {
    try {
      setApprovals((await api.approvals()).approvals);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  }, []);

  useEffect(() => {
    void load();
  }, [load]);

  const decide = async (id: string, decision: string) => {
    setBusy(true);
    setError("");
    try {
      await api.decideApproval(id, decision);
      await load();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };

  return (
    <div>
      <div className="card">
        <h2>Approval requests</h2>
        {error && <div className="error-box">{error}</div>}
        {approvals.length === 0 ? (
          <p className="muted">No approval requests.</p>
        ) : (
          <table>
            <thead>
              <tr><th>Title</th><th>Status</th><th>Priority</th><th>Detail</th><th>Action</th></tr>
            </thead>
            <tbody>
              {approvals.map((a) => (
                <tr key={a.id}>
                  <td>{a.title || a.action_label}</td>
                  <td><span className={`badge ${a.status === "APPROVED" ? "ok" : a.status === "REJECTED" ? "fail" : "warn"}`}>{a.status}</span></td>
                  <td><span className="badge">{a.priority}</span></td>
                  <td className="muted">{a.detail}</td>
                  <td>
                    {a.status === "PENDING" ? (
                      <div className="row">
                        <button className="ghost" disabled={busy} onClick={() => decide(a.id, "APPROVED")}>Approve</button>
                        <button className="ghost" disabled={busy} onClick={() => decide(a.id, "REJECTED")}>Reject</button>
                      </div>
                    ) : (
                      <span className="muted">{a.decided_by ?? ""}</span>
                    )}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
    </div>
  );
}

// ---- settings ---------------------------------------------------------------

function SettingsView() {
  const [overrides, setOverrides] = useState<BudgetOverrides>({});
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [saved, setSaved] = useState(false);

  const load = useCallback(async () => {
    try {
      const s = await api.settings();
      setOverrides(s.budgetOverrides);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  }, []);

  useEffect(() => {
    void load();
  }, [load]);

  const set = (key: keyof BudgetOverrides, value: string) => {
    setOverrides((o) => ({ ...o, [key]: value === "" ? undefined : Number(value) }));
    setSaved(false);
  };

  const save = async () => {
    setBusy(true);
    setError("");
    setSaved(false);
    try {
      await api.saveSettings(overrides);
      setSaved(true);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };

  const fields: Array<{ key: keyof BudgetOverrides; label: string; hint: string }> = [
    { key: "aiDailyBudget", label: "AI daily budget ($)", hint: "Hard cap on model spend per day" },
    { key: "aiMonthlyBudget", label: "AI monthly budget ($)", hint: "Hard cap on model spend per month" },
    { key: "aiPerTaskBudget", label: "AI per-task budget ($)", hint: "Cap on a single task's spend" },
    { key: "aiPerRunBudget", label: "AI per-run budget ($)", hint: "Cap on a single agent run's spend" },
  ];

  return (
    <div>
      <div className="card" style={{ maxWidth: 520 }}>
        <h2>Budget overrides</h2>
        <p className="muted">Leave a field empty to use the worker defaults (set in wrangler.toml).</p>
        {error && <div className="error-box">{error}</div>}
        {saved && <div className="error-box" style={{ borderColor: "var(--ok)", color: "var(--ok)" }}>Saved.</div>}
        {fields.map((f) => (
          <div key={f.key}>
            <label>{f.label} <span className="muted">— {f.hint}</span></label>
            <input
              type="number"
              value={overrides[f.key] ?? ""}
              onChange={(e) => set(f.key, e.target.value)}
              placeholder="default"
            />
          </div>
        ))}
        <div className="row" style={{ marginTop: 16 }}>
          <button className="action" onClick={save} disabled={busy}>
            {busy ? "Saving…" : "Save"}
          </button>
        </div>
      </div>
    </div>
  );
}
