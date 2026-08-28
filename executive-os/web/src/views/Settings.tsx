import { useEffect, useState } from "react";
import { api, type Overview } from "../api";
import { authHeaders, getUsername } from "../auth";
import { Card, ErrorBox } from "../components/ui";

export function SettingsView({ logout }: { logout: () => void }) {
  const [overview, setOverview] = useState<Overview | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [msg, setMsg] = useState<string | null>(null);

  // Financial snapshot form
  const [cash, setCash] = useState("");
  const [revenue, setRevenue] = useState("");
  const [expenses, setExpenses] = useState("");
  const [period, setPeriod] = useState(new Date().toISOString().slice(0, 7));
  const [confidence, setConfidence] = useState("ESTIMATE");
  const [notes, setNotes] = useState("");

  // Change-password form
  const [curPw, setCurPw] = useState("");
  const [newPw, setNewPw] = useState("");
  const [newPw2, setNewPw2] = useState("");

  useEffect(() => {
    api.overview().then(setOverview).catch((e) => setError(e instanceof Error ? e.message : String(e)));
  }, []);

  const flash = (m: string) => {
    setMsg(m);
    setTimeout(() => setMsg(null), 4000);
  };

  const changePassword = async () => {
    if (!curPw || !newPw || newPw !== newPw2) {
      setError(newPw !== newPw2 ? "New passwords do not match." : "Fill in all fields.");
      return;
    }
    setError(null);
    try {
      await api.changePassword(curPw, newPw);
      setCurPw(""); setNewPw(""); setNewPw2("");
      flash("Password updated.");
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  };

  const saveSnapshot = async () => {
    try {
      const num = (s: string) => (s.trim() === "" ? undefined : Number(s));
      const res = await fetch("/api/finance/snapshot", {
        method: "POST",
        headers: { "Content-Type": "application/json", ...authHeaders() },
        body: JSON.stringify({
          cash: num(cash), revenue: num(revenue), expenses: num(expenses),
          period, confidence, notes: notes.trim() || undefined,
        }),
      });
      if (res.status === 401) throw new Error("Unauthorized — please re-sign-in.");
      if (!res.ok) {
        const body = (await res.json().catch(() => null)) as { error?: string } | null;
        throw new Error(body?.error ?? `HTTP ${res.status}`);
      }
      setCash(""); setRevenue(""); setExpenses(""); setNotes("");
      flash("Financial snapshot recorded.");
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  };

  const emergency = async (fn: () => Promise<unknown>, ok: string) => {
    try {
      await fn();
      setOverview(await api.overview());
      flash(ok);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  };

  return (
    <div className="grid grid-2">
      <div>
        <Card title="Financial Snapshot (never invented — real figures only)">
          <label className="field">
            <span className="lbl">Period</span>
            <input type="text" value={period} onChange={(e) => setPeriod(e.target.value)} placeholder="2026-08" />
          </label>
          <label className="field">
            <span className="lbl">Cash (USD)</span>
            <input type="text" value={cash} onChange={(e) => setCash(e.target.value)} placeholder="4000" />
          </label>
          <label className="field">
            <span className="lbl">Revenue / month (USD)</span>
            <input type="text" value={revenue} onChange={(e) => setRevenue(e.target.value)} placeholder="0" />
          </label>
          <label className="field">
            <span className="lbl">Expenses / month (USD)</span>
            <input type="text" value={expenses} onChange={(e) => setExpenses(e.target.value)} placeholder="1800" />
          </label>
          <label className="field">
            <span className="lbl">Confidence</span>
            <select value={confidence} onChange={(e) => setConfidence(e.target.value)}>
              <option>FACT</option><option>ESTIMATE</option><option>ASSUMPTION</option><option>INFERENCE</option>
            </select>
          </label>
          <label className="field">
            <span className="lbl">Notes</span>
            <input type="text" value={notes} onChange={(e) => setNotes(e.target.value)} />
          </label>
          <button className="btn primary" onClick={() => void saveSnapshot()}>Save Snapshot</button>
        </Card>
      </div>

      <div>
        <Card title="Emergency Controls (spec §39 — enforced server-side)">
          <div className="kv mb">
            <span className="k">Autonomy mode</span><span>{overview?.autonomyMode ?? "…"}</span>
            <span className="k">Autonomous execution</span><span>{overview?.paused ? "PAUSED" : "active"}</span>
            <span className="k">Spending</span><span>{overview?.spendingDisabled ? "DISABLED" : "enabled"}</span>
            <span className="k">Deployments</span><span>{overview?.deploymentsDisabled ? "DISABLED" : "enabled"}</span>
            <span className="k">Delegate mode</span><span>{overview?.delegateMode ? "ON — agent may deploy & trigger work" : "off"}</span>
            <span className="k">Full access</span><span>{overview?.fullAccess ? "ON — fully autonomous (spend, deploy, HIGH-risk reversible)" : "off"}</span>
          </div>
          <div className="row">
            {overview?.paused
              ? <button className="btn primary" onClick={() => void emergency(() => api.resume(), "Resumed.")}>Resume</button>
              : <button className="btn danger" onClick={() => void emergency(() => api.pause("Paused from console"), "Paused.")}>Pause Autonomous Execution</button>}
            <button className="btn" onClick={() => void emergency(() => api.setSpending(!overview?.spendingDisabled), "Spending toggled.")}>
              {overview?.spendingDisabled ? "Enable Spending" : "Disable Spending"}
            </button>
            <button className="btn" onClick={() => void emergency(() => api.setDeployments(!overview?.deploymentsDisabled), "Deployments toggled.")}>
              {overview?.deploymentsDisabled ? "Enable Deployments" : "Disable Deployments"}
            </button>
            <button className="btn" onClick={() => void emergency(() => api.setDelegate(!overview?.delegateMode), "Delegate mode toggled.")}>
              {overview?.delegateMode ? "Disable Delegate Mode" : "Enable Delegate Mode"}
            </button>
            <button className="btn danger" onClick={() => void emergency(() => api.setFullAccess(!overview?.fullAccess), "Full access toggled.")}>
              {overview?.fullAccess ? "Disable Full Access" : "🚀 Enable Full Access"}
            </button>
          </div>
          <div className="row mt">
            <button className="btn small" onClick={() => void emergency(() => api.setAutonomy("CONTROLLED"), "Mode: CONTROLLED")}>Mode: Controlled</button>
            <button className="btn small" onClick={() => void emergency(() => api.setAutonomy("APPROVAL_ONLY"), "Mode: Approval-only")}>Mode: Approval-only</button>
            <button className="btn small danger" onClick={() => void emergency(() => api.setAutonomy("PAUSED"), "Mode: Paused")}>Mode: Paused</button>
          </div>
        </Card>

        <Card title="Founder Account">
          <div className="kv mb">
            <span className="k">Signed in as</span><span>{getUsername() || "founder"}</span>
          </div>
          <label className="field">
            <span className="lbl">Current password</span>
            <input type="password" value={curPw} onChange={(e) => setCurPw(e.target.value)} autoComplete="current-password" />
          </label>
          <label className="field">
            <span className="lbl">New password (min 8 chars)</span>
            <input type="password" value={newPw} onChange={(e) => setNewPw(e.target.value)} autoComplete="new-password" />
          </label>
          <label className="field">
            <span className="lbl">Confirm new password</span>
            <input type="password" value={newPw2} onChange={(e) => setNewPw2(e.target.value)} autoComplete="new-password" />
          </label>
          <div className="row">
            <button className="btn" onClick={() => void changePassword()}>Change Password</button>
            <button className="btn danger" onClick={logout}>Sign Out</button>
          </div>
        </Card>

        <Card title="About" className="mt">
          <div className="kv">
            <span className="k">System</span><span>Zarlino Executive OS</span>
            <span className="k">Architecture</span><span>One Executive Agent · CEO/CFO/CTO lenses · Chief of Staff</span>
            <span className="k">Runtime</span><span>Cloudflare Agents SDK + Durable Objects + SQLite</span>
            <span className="k">Authority</span><span>Backend-enforced permission engine</span>
          </div>
          {msg && <div className="ok mt">{msg}</div>}
          {error && <div className="mt"><ErrorBox message={error} /></div>}
        </Card>
      </div>
    </div>
  );
}
