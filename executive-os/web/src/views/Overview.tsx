import { useEffect, useState } from "react";
import { api, type Overview, type PipelineGate, type ProviderBalance } from "../api";
import type { ViewKey } from "../App";
import { Card, ErrorBox, Stat, fmtMoney } from "../components/ui";

const STATUS_CLASS: Record<string, string> = { GREEN: "green", YELLOW: "yellow", RED: "red" };

export function OverviewView({
  overview,
  onNavigate,
  refresh,
}: {
  overview: Overview | null;
  onNavigate: (v: ViewKey) => void;
  refresh: () => void;
}) {
  const [brief, setBrief] = useState<string | null>(null);
  const [briefError, setBriefError] = useState<string | null>(null);
  const [gates, setGates] = useState<PipelineGate[] | null>(null);

  useEffect(() => {
    setBrief(null);
    setBriefError(null);
    api.pipeline()
      .then((p) => setGates(p.gates))
      .catch(() => setGates([]));
  }, []);

  const runBrief = async () => {
    setBriefError(null);
    try {
      const r = await api.brief();
      setBrief(r.brief);
    } catch (err) {
      setBriefError(err instanceof Error ? err.message : String(err));
    }
  };

  if (!overview) return <ErrorBox message="Unable to load executive state. Is the Worker running?" />;

  return (
    <div className="grid" style={{ gap: 16 }}>
      <div className="status-banner">
        <span className="big" style={{ color: `var(--${STATUS_CLASS[overview.status] ?? "yellow"})` }}>{overview.status}</span>
        <div style={{ flex: 1 }}>
          <div style={{ fontWeight: 600 }}>ZARLINO EXECUTIVE STATUS</div>
          <div className="faint small">
            Objective: <b>{overview.currentObjective ?? "UNKNOWN"}</b> · Autonomy: {overview.autonomyMode}
            {overview.shippedCount > 0 && <span className="pill green">SHIPPED: {overview.shippedCount}</span>}
            {overview.delegateMode && <span className="pill yellow">DELEGATE ON</span>}
            {overview.fullAccess && <span className="pill red">FULL ACCESS</span>}
            {overview.paused && <span className="error"> · PAUSED</span>}
            {overview.spendingDisabled && <span className="error"> · SPENDING OFF</span>}
            {overview.deploymentsDisabled && <span className="error"> · DEPLOY OFF</span>}
          </div>
        </div>
        <button className="btn" onClick={refresh}>Refresh</button>
      </div>

      <div className="grid grid-4">
        <Card><Stat label="Cash" value={fmtMoney(overview.cash)} unknown={overview.cash === null} /></Card>
        <Card><Stat label="Revenue / mo" value={fmtMoney(overview.revenue)} unknown={overview.revenue === null} /></Card>
        <Card><Stat label="Expenses / mo" value={fmtMoney(overview.expenses)} unknown={overview.expenses === null} /></Card>
        <Card><Stat label="Runway" value={overview.runwayMonths !== null ? `${overview.runwayMonths?.toFixed(1)} mo` : "UNKNOWN"} unknown={overview.runwayMonths === null} /></Card>
      </div>

      <DownloadsLicensedCard overview={overview} />

      <AiFuelCard overview={overview} />

      <AiFuelCard overview={overview} />

      <div className="grid grid-2">
        <Card title="Top Priorities">
          {overview.topPriorities.length ? (
            <ol style={{ margin: 0, paddingLeft: 18 }}>
              {overview.topPriorities.map((p, i) => <li key={i}>{p}</li>)}
            </ol>
          ) : <span className="faint">None set.</span>}
        </Card>
        <Card title="Critical Risks">
          {overview.criticalRisks.length ? (
            <ul style={{ margin: 0, paddingLeft: 18 }}>
              {overview.criticalRisks.map((r) => <li key={r.id}><b>[{r.level}]</b> {r.title}</li>)}
            </ul>
          ) : <span className="faint">No critical risks tracked.</span>}
        </Card>
      </div>

      <div className="grid grid-2">
        <Card title="Shipped Products">
          {overview.shipped.length === 0 ? (
            <span className="faint">Nothing shipped yet — use the Ship button in the Pipeline.</span>
          ) : (
            <table>
              <thead><tr><th>Product</th><th>Version</th></tr></thead>
              <tbody>
                {overview.shipped.map((p) => (
                  <tr key={p.id}>
                    <td data-label="Product">{p.name}</td>
                    <td data-label="Version" className="mono">{p.version ?? "—"}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
          <div className="row mt">
            <button className="btn small primary" onClick={() => onNavigate("pipeline")}>Open Pipeline</button>
          </div>
        </Card>

        <FounderFilesCard onNavigate={onNavigate} />
      </div>

      <div className="grid grid-2">
        <Card title="Blocked Work">
          <Stat label="Blocked tasks" value={overview.blockedTasks} />
          <div className="row mt">
            <button className="btn small" onClick={() => onNavigate("tasks")}>View Tasks</button>
            <button className="btn small" onClick={() => onNavigate("engineering")}>Engineering</button>
          </div>
        </Card>
        <Card title="Pending Founder Decisions">
          <Stat label="Approvals waiting" value={overview.pendingApprovals} />
          <div className="row mt">
            <button className="btn small primary" onClick={() => onNavigate("approvals")}>View Approvals</button>
            <button className="btn small" onClick={() => onNavigate("decisions")}>View Decisions</button>
          </div>
        </Card>
      </div>

      <Card title="Pipeline Progress (10-gate)">
        {!gates ? <span className="faint small">loading…</span> : (
          <div className="pipeline-progress">
            {["ZScorch", "ZTame", "ZVocals", "PresenceField"].map((plugin) => {
              const pluginGates = (gates ?? []).filter((g) => g.plugin_id === plugin);
              const passed = pluginGates.filter((g) => g.status === "PASSED").length;
              const total = Math.max(pluginGates.length, 10);
              const pct = Math.round((passed / total) * 100);
              return (
                <div key={plugin}>
                  <div className="row-between">
                    <span className="small">{plugin}</span>
                    <span className="mono faint small">{passed}/{total}</span>
                  </div>
                  <div className={`progress ${passed === total ? "" : "warn"}`}><i style={{ width: `${pct}%` }} /></div>
                </div>
              );
            })}
          </div>
        )}
        <div className="row mt">
          <button className="btn small primary" onClick={() => onNavigate("pipeline")}>Open Pipeline</button>
        </div>
      </Card>

      <Card title="Recent Decisions">
        {overview.recentDecisions.length ? (
          <table>
            <thead><tr><th>When</th><th>Decision</th><th>Category</th><th>Status</th></tr></thead>
            <tbody>
              {overview.recentDecisions.map((d) => (
                <tr key={d.id}>
                  <td data-label="When" className="mono faint">{d.id.slice(0, 8)}</td>
                  <td data-label="Decision">{d.title}</td>
                  <td data-label="Category" className="faint">{d.category}</td>
                  <td data-label="Status"><span className="pill">{d.status}</span></td>
                </tr>
              ))}
            </tbody>
          </table>
        ) : <span className="faint">No decisions yet.</span>}
      </Card>

      <Card title="Daily Executive Brief">
        <div className="row mb">
          <button className="btn primary" onClick={() => void runBrief()}>Generate Brief</button>
          <span className="faint small">Scheduled automatically every morning.</span>
        </div>
        {briefError && <ErrorBox message={briefError} />}
        {brief && <pre className="dump">{brief}</pre>}
      </Card>

      <div className="row" style={{ gap: 10 }}>
        <button className="btn" onClick={() => onNavigate("chat")}>Ask Executive</button>
        <button className="btn" onClick={() => onNavigate("finance")}>View Finance</button>
        <button className="btn" onClick={() => onNavigate("tasks")}>View Tasks</button>
        <button className="btn" onClick={() => onNavigate("activity")}>Activity</button>
      </div>
    </div>
  );
}

const KIND_ICON: Record<string, string> = { image: "🖼️", text: "📄", audio: "🎧", other: "📦" };
const fmtSize = (b: number) => (b >= 1024 * 1024 ? `${(b / 1024 / 1024).toFixed(1)} MB` : `${Math.max(1, Math.round(b / 1024))} KB`);

/** Live AI budget gauge — USD cost + token usage vs their daily budgets. */
function AiFuelCard({ overview }: { overview: Overview }) {
  const daily = overview.aiDailyBudget ?? 8;
  const spent = overview.aiSpendToday ?? 0;
  const pct = Math.min(100, Math.round((spent / daily) * 100));
  const left = Math.max(0, daily - spent);
  const cls = pct >= 90 ? "fuel-low" : pct >= 60 ? "fuel-mid" : "fuel-ok";
  const quips: string[] =
    pct >= 90
      ? ["🔥 Running hot — the tank is nearly dry.", "Careful: the Executive is almost out of tokens."]
      : pct >= 60
        ? ["⛽ Half tank and cruising.", "The lenses are burning a steady flame."]
        : ["⛽ Tank is full — ask the Executive anything.", "Fueled up and ready for a big task."];
  const quip = quips[pct % 2] ?? quips[0];

  // Token budget (soft guide — USD is what enforces).
  const tDaily = overview.aiDailyTokenBudget ?? 30_000_000;
  const tUsed = overview.aiTokensToday ?? 0;
  const tPct = Math.min(100, Math.round((tUsed / tDaily) * 100));
  const tLeft = Math.max(0, tDaily - tUsed);
  const tCls = tPct >= 90 ? "fuel-low" : tPct >= 60 ? "fuel-mid" : "fuel-ok";
  const fmtTok = (n: number) => (n >= 1_000_000 ? `${(n / 1_000_000).toFixed(2)}M` : n >= 1_000 ? `${(n / 1_000).toFixed(1)}k` : `${n}`);

  // Provider balance (cached server-side; refresh hits DeepSeek live).
  const [balance, setBalance] = useState<ProviderBalance | null>(overview.aiProviderBalance ?? null);
  const [balanceLoading, setBalanceLoading] = useState(false);
  const loadBalance = (force: boolean) => {
    setBalanceLoading(true);
    (force ? api.providerBalanceRefresh() : api.providerBalance())
      .then(setBalance)
      .catch(() => setBalance(null))
      .finally(() => setBalanceLoading(false));
  };
  useEffect(() => {
    // Overview already carries the cache; only fetch if it's missing.
    if (!overview.aiProviderBalance) loadBalance(false);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Extend-budget editor — persisted server-side, takes effect instantly.
  const [editing, setEditing] = useState(false);
  const [dDaily, setDDaily] = useState(`${daily}`);
  const [dMonthly, setDMonthly] = useState(`${overview.aiMonthlyBudget ?? 150}`);
  const [budgetMsg, setBudgetMsg] = useState<string | null>(null);
  const [budgetErr, setBudgetErr] = useState<string | null>(null);
  const saveBudget = async () => {
    setBudgetErr(null);
    const dd = Number(dDaily);
    const dm = Number(dMonthly);
    if (!Number.isFinite(dd) || dd <= 0 || !Number.isFinite(dm) || dm <= 0) {
      setBudgetErr("Both values must be positive numbers.");
      return;
    }
    try {
      await api.setBudget({ aiDailyBudget: dd, aiMonthlyBudget: dm });
      setBudgetMsg(`Budget extended → $${dd.toFixed(2)}/day, $${dm.toFixed(2)}/mo. Saved for good — no redeploy needed.`);
      setEditing(false);
    } catch (err) {
      setBudgetErr(err instanceof Error ? err.message : String(err));
    }
  };

  const bal = balance?.balances[0];
  const balLabel = bal
    ? `${bal.total} ${bal.currency}${balance?.totalUsdEstimate != null ? ` (~$${balance.totalUsdEstimate})` : ""}`
    : balance?.error
      ? "unavailable"
      : "—";

  return (
    <Card title={`AI Fuel — ${pct}% of daily budget used`}>
      <div className="row-between">
        <Stat label="Spent today" value={`$${spent.toFixed(3)}`} />
        <Stat label="Daily budget" value={`$${daily.toFixed(2)}`} />
        <Stat label="Left today" value={`$${left.toFixed(3)}`} />
      </div>
      <div className={`fuel-bar ${cls}`}>
        <i style={{ width: `${pct}%` }} />
      </div>

      <div className="row-between mt">
        <Stat label="Tokens today" value={fmtTok(tUsed)} />
        <Stat label="Token budget" value={fmtTok(tDaily)} />
        <Stat label="Tokens left" value={fmtTok(tLeft)} />
      </div>
      <div className={`fuel-bar ${tCls}`}>
        <i style={{ width: `${tPct}%` }} />
      </div>
      <div className="faint small mt">{quip}</div>

      {/* Provider balance — the real "can we keep working" signal. */}
      <div className="row-between mt">
        <Stat
          label="Provider balance"
          value={balLabel}
          unknown={!bal && !balance?.error}
        />
        <button className="btn small" onClick={() => loadBalance(true)} disabled={balanceLoading} title="Re-check with the provider now">
          {balanceLoading ? "Checking…" : balance?.cached ? "↻ Refresh" : "↻ Refresh"}
        </button>
      </div>
      {balance?.cached && balance.error && <div className="faint small">{balance.error}</div>}
      {balance?.fetchedAt && (
        <div className="faint small">Checked {new Date(balance.fetchedAt).toLocaleTimeString()}{balance.cached ? " (cached ≤1 min)" : ""}</div>
      )}

      {/* Extend budget — Founder self-service, persists across redeploys. */}
      {!editing ? (
        <div className="row mt">
          <button className="btn small primary" onClick={() => { setEditing(true); setBudgetMsg(null); setBudgetErr(null); }}>
            ⛽ Extend AI budget
          </button>
        </div>
      ) : (
        <div className="budget-edit mt" style={{ display: "grid", gap: 8 }}>
          <div className="row" style={{ gap: 8, flexWrap: "wrap" }}>
            <label className="small muted">
              Daily ($)
              <input type="number" min={1} step="1" value={dDaily} onChange={(e) => setDDaily(e.target.value)}
                style={{ width: 90, marginLeft: 6, padding: "4px 6px", background: "var(--bg-elev-2)", color: "var(--fg)", border: "1px solid var(--border)", borderRadius: 6 }} />
            </label>
            <label className="small muted">
              Monthly ($)
              <input type="number" min={1} step="1" value={dMonthly} onChange={(e) => setDMonthly(e.target.value)}
                style={{ width: 110, marginLeft: 6, padding: "4px 6px", background: "var(--bg-elev-2)", color: "var(--fg)", border: "1px solid var(--border)", borderRadius: 6 }} />
            </label>
            <button className="btn small primary" onClick={() => void saveBudget()}>Save</button>
            <button className="btn small" onClick={() => setEditing(false)}>Cancel</button>
          </div>
          <div className="faint small">The Executive enforces these instantly — no redeploy needed. Existing spend stays tracked.</div>
          {budgetMsg && <div className="small" style={{ color: "var(--ok)" }}>{budgetMsg}</div>}
          {budgetErr && <div className="small error">{budgetErr}</div>}
        </div>
      )}
    </Card>
  );
}

/** Downloads & Licensed — headline product traction counters, Founder-editable. */
function DownloadsLicensedCard({ overview }: { overview: Overview }) {
  const [dl, setDl] = useState("");
  const [lc, setLc] = useState("");
  const [saving, setSaving] = useState<string | null>(null);
  const [msg, setMsg] = useState<string | null>(null);

  const downloads = overview.downloads ?? 0;
  const licensed = overview.licensed ?? 0;
  const rate = downloads > 0 ? Math.round((licensed / downloads) * 100) : 0;

  const save = async (name: "downloads" | "licensed") => {
    const raw = name === "downloads" ? dl : lc;
    const v = Math.max(0, Math.round(Number(raw)));
    if (!Number.isFinite(v) || raw.trim() === "") return;
    setSaving(name);
    setMsg(null);
    try {
      await api.recordMetric(name, v);
      setMsg(`${name === "downloads" ? "Downloads" : "Licensed"} updated → ${v}`);
      if (name === "downloads") setDl(""); else setLc("");
      setTimeout(() => setMsg(null), 4000);
    } catch (err) {
      setMsg(err instanceof Error ? err.message : String(err));
    } finally {
      setSaving(null);
    }
  };

  return (
    <Card title="Downloads & Licensed">
      <div className="grid grid-4">
        <Card><Stat label="Downloads" value={downloads} /></Card>
        <Card><Stat label="Licensed" value={licensed} /></Card>
        <Card><Stat label="License rate" value={`${rate}%`} /></Card>
        <Card><Stat label="Unlicensed" value={Math.max(0, downloads - licensed)} /></Card>
      </div>
      <div className="row mt" style={{ gap: 8, flexWrap: "wrap" }}>
        <input
          type="number" min={0} className="metric-input"
          placeholder={`Downloads (${downloads})`} value={dl} onChange={(e) => setDl(e.target.value)}
        />
        <button className="btn small primary" disabled={saving !== null || dl.trim() === ""} onClick={() => void save("downloads")}>
          {saving === "downloads" ? "…" : "Set Downloads"}
        </button>
        <input
          type="number" min={0} className="metric-input"
          placeholder={`Licensed (${licensed})`} value={lc} onChange={(e) => setLc(e.target.value)}
        />
        <button className="btn small primary" disabled={saving !== null || lc.trim() === ""} onClick={() => void save("licensed")}>
          {saving === "licensed" ? "…" : "Set Licensed"}
        </button>
        {msg && <span className="small ok">{msg}</span>}
      </div>
      <p className="small muted mt">
        Downloads vs licensed seats — keep these current as the storefront reports real numbers
        (the Executive sees them in its overview too).
      </p>
    </Card>
  );
}

/** Recent files uploaded from the web console — the Executive can inspect them. */
function FounderFilesCard({ onNavigate }: { onNavigate: (v: ViewKey) => void }) {  const [files, setFiles] = useState<Array<{ id: string; name: string; kind: string; size_bytes: number; created_at: string }> | null>(null);

  const load = () => {
    api.files().then(setFiles).catch(() => setFiles([]));
  };
  useEffect(load, []);

  const remove = async (id: string) => {
    try {
      await api.fileDelete(id);
      load();
    } catch { /* non-fatal */ }
  };

  return (
    <Card title="Founder Files">
      <p className="small muted mb">
        Files you upload from chat (📎) land here. The Executive can read text files and
        analyze images with vision.
      </p>
      {!files ? (
        <span className="faint small">loading…</span>
      ) : files.length === 0 ? (
        <span className="faint">No uploads yet — attach a file in the chat.</span>
      ) : (
        <div className="table-scroll">
          <table>
            <thead><tr><th>File</th><th>Kind</th><th>Size</th><th></th></tr></thead>
            <tbody>
              {files.slice(0, 8).map((f) => (
                <tr key={f.id}>
                  <td data-label="File">
                    <span className="fc-ico">{KIND_ICON[f.kind] ?? "📦"}</span>{" "}
                    <span className="fc-name">{f.name}</span>
                  </td>
                  <td data-label="Kind"><span className="pill">{f.kind}</span></td>
                  <td data-label="Size" className="mono faint small">{fmtSize(f.size_bytes)}</td>
                  <td data-label="Actions">
                    <button className="btn small danger" onClick={() => void remove(f.id)} title="Delete">✕</button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
      <div className="row mt">
        <button className="btn small" onClick={() => onNavigate("chat")}>Attach in Chat</button>
      </div>
    </Card>
  );
}
