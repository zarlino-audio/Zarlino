/** Ops views: Files, Local Work & Agent Runs, System Doctor, Productivity. */
import { useState } from "react";
import { api, type Contradiction, type DataBundle, type DoctorReport } from "../api";
import { Badge, fmt, timeAgo } from "../lib/entertain";

interface ViewProps {
  data: DataBundle;
  mutate: () => Promise<void>;
  notify: (msg: string, ok?: boolean) => void;
}

// ---- Files ------------------------------------------------------------------

export function FilesView({ data, mutate: _mutate, notify: _notify }: ViewProps) {
  const files = data.files ?? [];
  return (
    <div>
      <div className="card">
        <h2>Uploaded files</h2>
        {files.length === 0 ? (
          <p className="muted">No uploaded files.</p>
        ) : (
          <table>
            <thead>
              <tr><th>Name</th><th>Kind</th><th>MIME</th><th>Size</th><th>Uploaded</th></tr>
            </thead>
            <tbody>
              {files.map((f) => (
                <tr key={f.id}>
                  <td>{f.name}</td>
                  <td><Badge>{f.kind ?? "—"}</Badge></td>
                  <td className="mono muted">{f.mime ?? "—"}</td>
                  <td className="mono">{f.sizeBytes !== undefined ? fmt(Math.round(f.sizeBytes / 1024)) + " KB" : "—"}</td>
                  <td className="mono muted">{timeAgo(f.created_at)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
        <p className="muted">Files are uploaded from Chat (📎 attach) or via the API.</p>
      </div>
    </div>
  );
}

// ---- Local Work & Agent Runs ------------------------------------------------

export function LocalRunsView({ data, mutate, notify }: ViewProps) {
  const localTasks = data.localTasks ?? [];
  const runs = data.runs ?? [];
  const busy = useState(false);
  const la = data.localAgent as { host?: string | null; online?: boolean; seenAgoMs?: number | null; pending?: number; lastSeen?: string | null } | undefined;

  const resume = async (id: string) => {
    try {
      await api.continueRun(id);
      await mutate();
      notify("Run resumed — it will execute on the next alarm/wake cycle");
    } catch (e) {
      notify(e instanceof Error ? e.message : String(e), false);
    }
  };

  return (
    <div>
      <div className={`card ${la?.online ? "" : "agent-offline-card"}`}>
        <h2>Local agent (daemon)</h2>
        {la?.online ? (
          <p>
            <Badge tone="ok">ONLINE</Badge>{" "}
            <span className="mono">host {la.host ?? "—"}</span> · last seen{" "}
            {la.seenAgoMs != null ? `${Math.max(1, Math.round(la.seenAgoMs / 1000))}s ago` : "—"} ·{" "}
            <span className="muted">{la.pending ?? 0} pending task(s)</span>
          </p>
        ) : (
          <p>
            <Badge tone="fail">OFFLINE</Badge>{" "}
            <span className="muted">
              The on-machine daemon is not reporting. Start it from executive-os/ with{" "}
              <code>npm run agent:daemon:loop</code> so it claims tasks, heartbeats, and reports results.
            </span>
          </p>
        )}
      </div>

      <div className="card">
        <h2>Local machine tasks</h2>
        {localTasks.length === 0 ? (
          <p className="muted">No local tasks. The local daemon claims PENDING tasks and reports results back here.</p>
        ) : (
          <table>
            <thead>
              <tr><th>Prompt</th><th>Status</th><th>Claimed by</th><th>Updated</th></tr>
            </thead>
            <tbody>
              {localTasks.map((t, i) => (
                <tr key={i}>
                  <td>{String(t.prompt ?? "")}</td>
                  <td><Badge tone={String(t.status) === "DONE" ? "ok" : String(t.status) === "FAILED" ? "fail" : "warn"}>{String(t.status)}</Badge></td>
                  <td className="mono muted">{String(t.claimed_by ?? "—")}</td>
                  <td className="mono muted">{timeAgo(String(t.updated_at ?? ""))}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
      <div className="card">
        <h2>Long-horizon agent runs</h2>
        {runs.length === 0 ? (
          <p className="muted">No agent runs yet.</p>
        ) : (
          <table>
            <thead>
              <tr><th>Run</th><th>Status</th><th>Iters</th><th>Tools</th><th>Updated</th><th>Action</th></tr>
            </thead>
            <tbody>
              {runs.map((r, i) => (
                <tr key={i}>
                  <td className="mono">{String(r.id ?? "")}</td>
                  <td><Badge tone={String(r.status) === "DONE" ? "ok" : String(r.status) === "RUNNING" ? "warn" : "fail"}>{String(r.status)}</Badge></td>
                  <td className="mono">{String(r.iterations ?? "0")}</td>
                  <td className="mono">{String(r.tools_called ?? "0")}</td>
                  <td className="mono muted">{timeAgo(String(r.updated_at ?? ""))}</td>
                  <td>
                    {["STOPPED", "TIMEOUT", "FAILED"].includes(String(r.status)) && (
                      <button className="mini" disabled={busy[0]} onClick={() => resume(String(r.id))}>Continue</button>
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

// ---- System Doctor ----------------------------------------------------------

export function DoctorView({ data, mutate, notify }: ViewProps) {
  const [report, setReport] = useState<DoctorReport | null>(null);
  const [autoFix, setAutoFix] = useState(true);
  const [busy, setBusy] = useState(false);

  const run = async () => {
    setBusy(true);
    try {
      setReport(await api.doctorRun(autoFix));
      await mutate();
    } catch (e) {
      notify(e instanceof Error ? e.message : String(e), false);
    } finally {
      setBusy(false);
    }
  };

  const maintenance = async () => {
    setBusy(true);
    try {
      await api.maintenance();
      await mutate();
      notify("Maintenance sweep complete");
    } catch (e) {
      notify(e instanceof Error ? e.message : String(e), false);
    } finally {
      setBusy(false);
    }
  };

  const last = data.doctorLastRun;
  return (
    <div>
      <div className="card">
        <div className="row" style={{ justifyContent: "space-between" }}>
          <h2>System Doctor</h2>
          <div className="row">
            <label style={{ margin: 0, display: "flex", alignItems: "center", gap: 6 }}>
              <input type="checkbox" checked={autoFix} onChange={(e) => setAutoFix(e.target.checked)} style={{ width: "auto" }} /> auto-fix
            </label>
            <button className="action" onClick={run} disabled={busy}>{busy ? "Running…" : "Run Doctor"}</button>
            <button className="ghost" onClick={maintenance} disabled={busy}>Maintenance</button>
          </div>
        </div>
        {last ? (
          <p className="muted">
            Last run <b>{timeAgo(last.runAt)}</b> · trigger {last.trigger} ·{" "}
            {last.summary ? <span className="mono">{JSON.stringify(last.summary)}</span> : null}
          </p>
        ) : (
          <p className="muted">Doctor runs on DO wake and every 6h (cron).</p>
        )}
      </div>
      {report && (
        <div className="card">
          <h2>Report — <Badge tone={report.healthy ? "ok" : "warn"}>{report.healthy ? "HEALTHY" : "ISSUES"}</Badge></h2>
          <table>
            <thead>
              <tr><th>Status</th><th>Check</th><th>Detail</th></tr>
            </thead>
            <tbody>
              {report.checks.map((c) => (
                <tr key={c.id}>
                  <td><Badge tone={c.status === "OK" ? "ok" : c.status === "WARN" ? "warn" : "fail"}>{c.status}</Badge></td>
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
          <p className="muted mono">{report.summary.ok}/{report.summary.total} ok · {report.summary.warn} warn · {report.summary.fail} fail · {report.summary.fixed} fixed</p>
        </div>
      )}
    </div>
  );
}

// ---- Productivity -----------------------------------------------------------

export function ProductivityView({ data, mutate, notify }: ViewProps) {
  const budgets = data.productivity?.budgets ?? [];
  const contradictions = data.productivity?.contradictions ?? [];
  const assignments = data.productivity?.assignments ?? [];
  const [busy, setBusy] = useState(false);
  const [resolutionInputs, setResolutionInputs] = useState<Record<string, string>>({});

  const optimize = async () => {
    setBusy(true);
    try {
      const r = await api.productivityOptimize();
      await mutate();
      notify(`Optimization: ${r.summary.applied} applied · ${r.summary.recommended} recommended`);
    } catch (e) {
      notify(e instanceof Error ? e.message : String(e), false);
    } finally {
      setBusy(false);
    }
  };

  const detect = async () => {
    setBusy(true);
    try {
      const r = await api.productivityDetect();
      await mutate();
      notify(`Contradiction scan: ${r.created} new finding(s)`);
    } catch (e) {
      notify(e instanceof Error ? e.message : String(e), false);
    } finally {
      setBusy(false);
    }
  };

  const resolve = async (c: Contradiction, status: string) => {
    const resolution = (resolutionInputs[c.id] ?? "").trim();
    if (!resolution) return;
    setBusy(true);
    try {
      await api.productivityResolve(c.id, resolution, status);
      await mutate();
    } catch (e) {
      notify(e instanceof Error ? e.message : String(e), false);
    } finally {
      setBusy(false);
    }
  };

  return (
    <div>
      <div className="card">
        <div className="row" style={{ justifyContent: "space-between" }}>
          <h2>Productivity Manager</h2>
          <div className="row">
            <button className="action" onClick={optimize} disabled={busy}>Optimize</button>
            <button className="ghost" onClick={detect} disabled={busy}>Scan contradictions</button>
          </div>
        </div>
        <div className="grid">
          <div className="metric"><div className="value">{assignments.filter((a) => a.status === "ASSIGNED" || a.status === "IN_PROGRESS").length}</div><div className="key">open assignments</div></div>
          <div className="metric"><div className="value">{contradictions.filter((c) => c.status === "OPEN").length}</div><div className="key">open contradictions</div></div>
          <div className="metric"><div className="value">{data.counters.assignmentsOpen ?? 0}</div><div className="key">counters</div></div>
        </div>
      </div>

      <div className="card">
        <h2>Token budgets</h2>
        {budgets.length === 0 ? (
          <p className="muted">No budgets tracked yet — run Optimize to seed the global budget.</p>
        ) : (
          <table>
            <thead>
              <tr><th>Scope</th><th>Monthly</th><th>Used</th><th>Usage</th><th>Tier</th></tr>
            </thead>
            <tbody>
              {budgets.map((b) => (
                <tr key={`${b.scope}:${b.scopeId ?? ""}`}>
                  <td className="mono">{b.scope}{b.scopeId ? `:${b.scopeId}` : ""}</td>
                  <td className="mono">{b.monthlyBudget?.toLocaleString() ?? "—"}</td>
                  <td className="mono">{b.tokensUsedMonth.toLocaleString()}</td>
                  <td><Badge tone={b.status === "OVER" ? "fail" : b.status === "WARN" ? "warn" : "ok"}>{b.usagePctMonth}%</Badge></td>
                  <td className="mono">{b.modelTier ?? "—"}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>

      <div className="card">
        <h2>Contradictions & misconceptions</h2>
        {contradictions.length === 0 ? (
          <p className="muted">No contradiction records. Run a scan.</p>
        ) : (
          <table>
            <thead>
              <tr><th>Status</th><th>Kind</th><th>Conflict</th><th>Resolution</th></tr>
            </thead>
            <tbody>
              {contradictions.slice(0, 40).map((c) => (
                <tr key={c.id}>
                  <td><Badge tone={c.status === "OPEN" ? "warn" : c.status === "RESOLVED" ? "ok" : undefined}>{c.status}</Badge></td>
                  <td className="mono">{c.kind}</td>
                  <td>
                    <div><b>{c.claim_a}</b></div>
                    <div className="muted">{c.claim_b}</div>
                  </td>
                  <td>
                    {c.status === "OPEN" ? (
                      <div className="row">
                        <input placeholder="Resolution…" value={resolutionInputs[c.id] ?? ""} onChange={(e) => setResolutionInputs((r) => ({ ...r, [c.id]: e.target.value }))} />
                        <button className="mini" disabled={busy || !(resolutionInputs[c.id] ?? "").trim()} onClick={() => resolve(c, "RESOLVED")}>Resolve</button>
                        <button className="mini" disabled={busy || !(resolutionInputs[c.id] ?? "").trim()} onClick={() => resolve(c, "DISMISSED")}>Dismiss</button>
                      </div>
                    ) : (
                      <span className="muted">{c.resolution ?? ""}</span>
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
