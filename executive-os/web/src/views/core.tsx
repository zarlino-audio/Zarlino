/** Core console views: Overview, Activity, Tasks, Approvals. */
import { useEffect, useMemo, useState } from "react";
import { api, type DataBundle, type Task } from "../api";
import { Badge, FuelGauge, fmt, timeAgo } from "../lib/entertain";

interface ViewProps {
  data: DataBundle;
  mutate: () => Promise<void>;
  notify: (msg: string, ok?: boolean) => void;
}

// ---- Overview ---------------------------------------------------------------

export function Overview({ data, mutate: _mutate }: ViewProps) {
  const c = data.counters;
  const budget = data.budget;
  const fuelPct = budget.monthly > 0 ? Math.round((budget.spentThisMonth / budget.monthly) * 100) : 0;
  return (
    <div>
      <div className="card">
        <div className="row" style={{ justifyContent: "space-between" }}>
          <h2>Executive state</h2>
          <div className="row">
            <Badge tone={(data.state?.status ?? "GREEN") === "GREEN" ? "ok" : "warn"}>{data.state?.status ?? "—"}</Badge>
            <Badge>{data.state?.autonomy_mode ?? "—"}</Badge>
          </div>
        </div>
        <div className="row" style={{ gap: 20, flexWrap: "wrap" }}>
          <FuelGauge used={budget.spentThisMonth} budget={budget.monthly} label="AI budget (month)" />
          <FuelGauge used={budget.tokensToday} budget={Math.max(budget.tokensToday, budget.daily || 30_000_000)} label="tokens today" />
        </div>
        <p className="muted">
          Spent today <b>${fmt(budget.spentToday, 2)}</b> · month <b>${fmt(budget.spentThisMonth, 2)}</b> of ${fmt(budget.monthly)} ({fuelPct}%) ·{" "}
          {fmt(budget.tokensToday)} tokens today · {fmt(budget.tokensThisMonth)} this month
          {data.doctorLastRun ? <> · Doctor last run <b>{timeAgo(data.doctorLastRun.runAt)}</b></> : null}
        </p>
      </div>

      <div className="grid">
        <Metric label="Open tasks" value={c.openTasks ?? 0} />
        <Metric label="Pending approvals" value={c.pendingApprovals ?? 0} />
        <Metric label="Open risks" value={c.openRisks ?? 0} />
        <Metric label="Products" value={c.products ?? 0} />
        <Metric label="Pipeline gates" value={c.gates ?? 0} />
        <Metric label="Agent runs" value={c.runsRunning ?? 0} />
        <Metric label="Local pending" value={c.localPending ?? 0} />
        <Metric label="Overdue reminders" value={c.overdueReminders ?? 0} />
        <Metric label="Contradictions" value={c.contradictionsOpen ?? 0} tone={(c.contradictionsOpen ?? 0) > 0 ? "warn" : undefined} />
        <Metric label="Assignments open" value={c.assignmentsOpen ?? 0} />
      </div>

      <div className="card section-gap">
        <h2>Products</h2>
        {data.products.length === 0 ? (
          <p className="muted">No products.</p>
        ) : (
          <div className="grid">
            {data.products.map((p) => (
              <div className="metric" key={p.id}>
                <div className="value" style={{ fontSize: 16 }}>{p.name}</div>
                <div className="key">
                  {p.status} · v{p.version ?? "—"} {p.price ? `· $${p.price}` : ""}
                </div>
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  );
}

function Metric({ label, value, tone }: { label: string; value: number; tone?: "ok" | "warn" | "fail" }) {
  return (
    <div className="metric">
      <div className="value" style={tone === "warn" ? { color: "var(--warn)" } : undefined}>{value}</div>
      <div className="key">{label}</div>
    </div>
  );
}

// ---- Activity ---------------------------------------------------------------

export function Activity({ data }: ViewProps) {
  const audit = data.audit ?? [];
  const tools = data.toolExecutions ?? [];
  return (
    <div>
      <div className="card">
        <h2>Audit trail</h2>
        {audit.length === 0 ? (
          <p className="muted">No audit entries yet.</p>
        ) : (
          <table>
            <thead>
              <tr><th>Time</th><th>Actor</th><th>Action</th><th>Entity</th></tr>
            </thead>
            <tbody>
              {audit.map((a, i) => (
                <tr key={i}>
                  <td className="mono muted">{timeAgo(String(a.created_at ?? ""))}</td>
                  <td>{String(a.actor ?? "")}</td>
                  <td className="mono">{String(a.action ?? "")}</td>
                  <td className="mono muted">{String(a.entity_type ?? "")}:{String(a.entity_id ?? "")}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
      <div className="card">
        <h2>Tool executions</h2>
        {tools.length === 0 ? (
          <p className="muted">No tool executions recorded.</p>
        ) : (
          <table>
            <thead>
              <tr><th>Time</th><th>Tool</th><th>Verdict</th><th>Status</th></tr>
            </thead>
            <tbody>
              {tools.map((t, i) => (
                <tr key={i}>
                  <td className="mono muted">{timeAgo(String(t.created_at ?? ""))}</td>
                  <td className="mono">{String(t.tool_name ?? "")}</td>
                  <td><Badge tone={String(t.verdict ?? "") === "APPROVED" ? "ok" : "warn"}>{String(t.verdict ?? "")}</Badge></td>
                  <td><Badge tone={String(t.status ?? "") === "SUCCESS" ? "ok" : "warn"}>{String(t.status ?? "")}</Badge></td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
    </div>
  );
}

// ---- Tasks ------------------------------------------------------------------

export function Tasks({ data, mutate, notify }: ViewProps) {
  const [title, setTitle] = useState("");
  const [priority, setPriority] = useState("P2");
  const [busy, setBusy] = useState(false);
  const tasks = useMemo(() => data.tasks ?? [], [data]);
  const open = tasks.filter((t) => t.status === "OPEN");
  const inProg = tasks.filter((t) => t.status === "IN_PROGRESS");
  const blocked = tasks.filter((t) => t.status === "BLOCKED");
  const done = tasks.filter((t) => t.status === "DONE");

  const create = async () => {
    if (!title.trim() || busy) return;
    setBusy(true);
    try {
      await api.createTask(title.trim(), undefined, priority);
      setTitle("");
      await mutate();
      notify("Task created");
    } catch (e) {
      notify(e instanceof Error ? e.message : String(e), false);
    } finally {
      setBusy(false);
    }
  };

  const setStatus = async (t: Task, status: string) => {
    try {
      await api.updateTask(t.id, { status });
      await mutate();
    } catch (e) {
      notify(e instanceof Error ? e.message : String(e), false);
    }
  };

  const board: Array<{ status: string; label: string; items: Task[] }> = [
    { status: "OPEN", label: "To Do", items: open },
    { status: "IN_PROGRESS", label: "In Progress", items: inProg },
    { status: "BLOCKED", label: "Blocked", items: blocked },
  ];

  return (
    <div>
      <div className="card">
        <h2>Create task</h2>
        <div className="row">
          <input value={title} onChange={(e) => setTitle(e.target.value)} placeholder="e.g. Investigate the noisy threshold in ZScorch" style={{ flex: 1 }} />
          <select value={priority} onChange={(e) => setPriority(e.target.value)} className="sel">
            <option value="P0">P0</option>
            <option value="P1">P1</option>
            <option value="P2">P2</option>
            <option value="P3">P3</option>
          </select>
          <button className="action" onClick={create} disabled={busy || !title.trim()}>Create</button>
        </div>
        <div className="row" style={{ marginTop: 10 }}>
          <span className="chip">🩺 {open.length} to do</span>
          <span className="chip">▶ {inProg.length} in progress</span>
          <span className="chip">⛔ {blocked.length} blocked</span>
          <span className="chip">✔ {done.length} done</span>
        </div>
      </div>

      <div className="task-board">
        {board.map((col) => (
          <div className="task-col" key={col.status}>
            <h3>{col.label} <span className="muted">({col.items.length})</span></h3>
            {col.items.length === 0 && <p className="muted">—</p>}
            {col.items.slice(0, 40).map((t) => (
              <div className="task-card" key={t.id}>
                <div className="task-title">{t.title}</div>
                <div className="row" style={{ gap: 6, marginTop: 8 }}>
                  <Badge tone={t.priority === "P0" ? "fail" : t.priority === "P1" ? "warn" : undefined}>{t.priority}</Badge>
                  <button className="mini" onClick={() => setStatus(t, col.status === "OPEN" ? "IN_PROGRESS" : col.status === "IN_PROGRESS" ? "DONE" : "OPEN")}>
                    {col.status === "OPEN" ? "▶ Start" : col.status === "IN_PROGRESS" ? "✔ Done" : "↩ Reopen"}
                  </button>
                  {col.status === "BLOCKED" && (
                    <button className="mini" onClick={() => setStatus(t, "IN_PROGRESS")}>Unblock</button>
                  )}
                  {t.status !== "BLOCKED" && (
                    <button className="mini" onClick={() => setStatus(t, "BLOCKED")}>⛔</button>
                  )}
                </div>
              </div>
            ))}
          </div>
        ))}
      </div>
    </div>
  );
}

// ---- Approvals --------------------------------------------------------------

export function Approvals({ mutate, notify }: ViewProps) {
  const [busy, setBusy] = useState(false);
  const [list, setList] = useState<Array<Record<string, unknown>>>([]);
  const [loaded, setLoaded] = useState(false);

  const load = async () => {
    try {
      const r = await api.approvals();
      setList(r.approvals as unknown as Array<Record<string, unknown>>);
    } catch {
      /* non-fatal */
    } finally {
      setLoaded(true);
    }
  };

  useEffect(() => {
    void load();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const decide = async (id: string, decision: string) => {
    setBusy(true);
    try {
      await api.decideApproval(id, decision);
      await load();
      await mutate();
      notify(`Approval ${decision.toLowerCase()}`);
    } catch (e) {
      notify(e instanceof Error ? e.message : String(e), false);
    } finally {
      setBusy(false);
    }
  };
  return (
    <div>
      <div className="card">
        <h2>Approval requests</h2>
        {!loaded ? (
          <p className="muted">Loading…</p>
        ) : list.length === 0 ? (
          <p className="muted">No approval requests.</p>
        ) : (
          <table>
            <thead>
              <tr><th>Title</th><th>Status</th><th>Priority</th><th>Detail</th><th>Action</th></tr>
            </thead>
            <tbody>
              {list.map((a, i) => (
                <tr key={i}>
                  <td>{String(a.title ?? a.action_label ?? "")}</td>
                  <td><Badge tone={String(a.status) === "APPROVED" ? "ok" : String(a.status) === "REJECTED" ? "fail" : "warn"}>{String(a.status)}</Badge></td>
                  <td><Badge>{String(a.priority ?? "")}</Badge></td>
                  <td className="muted">{String(a.detail ?? "")}</td>
                  <td>
                    {String(a.status) === "PENDING" ? (
                      <div className="row">
                        <button className="ghost" disabled={busy} onClick={() => decide(String(a.id), "APPROVED")}>Approve</button>
                        <button className="ghost" disabled={busy} onClick={() => decide(String(a.id), "REJECTED")}>Reject</button>
                      </div>
                    ) : (
                      <span className="muted">{String(a.decided_by ?? "")}</span>
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

