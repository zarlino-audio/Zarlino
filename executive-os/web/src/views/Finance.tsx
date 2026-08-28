import { useEffect, useState } from "react";
import { api, type FinancialSnapshotView as Snapshot, type ProviderBalance } from "../api";
import { Card, ErrorBox, Spinner, Stat, fmtMoney } from "../components/ui";

export function FinanceView() {
  const [data, setData] = useState<{ snapshots: Snapshot[]; aiSpendToday: number; aiSpendMonth: number; aiTokensToday?: number; aiTokensMonth?: number } | null>(null);
  const [error, setError] = useState<string | null>(null);
  // Provider balance + effective budgets (Founder self-service).
  const [balance, setBalance] = useState<ProviderBalance | null>(null);
  const [budget, setBudget] = useState<{ daily: number; monthly: number } | null>(null);
  const [balanceLoading, setBalanceLoading] = useState(false);
  const [budgetMsg, setBudgetMsg] = useState<string | null>(null);
  const [budgetErr, setBudgetErr] = useState<string | null>(null);
  const [dDaily, setDDaily] = useState("");
  const [dMonthly, setDMonthly] = useState("");

  const load = async () => {
    try {
      const [fin, bal, b] = await Promise.all([
        api.finance(),
        api.providerBalance(),
        api.budget(),
      ]);
      setData(fin);
      setBalance(bal);
      setBudget({ daily: b.daily, monthly: b.monthly });
      setDDaily(`${b.daily}`);
      setDMonthly(`${b.monthly}`);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  useEffect(() => { void load(); }, []);

  const refreshBalance = async () => {
    setBalanceLoading(true);
    try {
      setBalance(await api.providerBalanceRefresh());
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setBalanceLoading(false);
    }
  };

  const saveBudget = async () => {
    setBudgetErr(null);
    const dd = Number(dDaily);
    const dm = Number(dMonthly);
    if (!Number.isFinite(dd) || dd <= 0 || !Number.isFinite(dm) || dm <= 0) {
      setBudgetErr("Both values must be positive numbers.");
      return;
    }
    try {
      const r = await api.setBudget({ aiDailyBudget: dd, aiMonthlyBudget: dm });
      setBudget({ daily: r.view.daily, monthly: r.view.monthly });
      setBudgetMsg(`AI budget updated → $${r.view.daily.toFixed(2)}/day, $${r.view.monthly.toFixed(2)}/mo.`);
    } catch (err) {
      setBudgetErr(err instanceof Error ? err.message : String(err));
    }
  };

  const latest = data?.snapshots[0];

  const fmtTok = (n?: number) => (n === undefined ? "—" : n >= 1_000_000 ? `${(n / 1_000_000).toFixed(2)}M` : n >= 1_000 ? `${(n / 1_000).toFixed(1)}k` : `${n}`);
  const bal = balance?.balances[0];
  const balLabel = bal
    ? `${bal.total} ${bal.currency}${balance?.totalUsdEstimate != null ? ` (~$${balance.totalUsdEstimate})` : ""}`
    : balance?.error ? "unavailable" : "—";

  return (
    <div>
      {error && <div className="mb"><ErrorBox message={error} /></div>}
      <div className="grid grid-4 mb">
        <Card><Stat label="Cash" value={fmtMoney(latest?.cash ?? null)} unknown={latest?.cash === null || !latest} /></Card>
        <Card><Stat label="Revenue / mo" value={fmtMoney(latest?.revenue ?? null)} unknown={latest?.revenue === null || !latest} /></Card>
        <Card><Stat label="Expenses / mo" value={fmtMoney(latest?.expenses ?? null)} unknown={latest?.expenses === null || !latest} /></Card>
        <Card><Stat label="Runway" value={latest?.runway_months !== null && latest?.runway_months !== undefined ? `${latest.runway_months?.toFixed(1)} mo` : "UNKNOWN"} unknown={latest?.runway_months === null || !latest} /></Card>
      </div>

      <div className="grid grid-2">
        <Card title="AI Cost & Tokens (spec §34)">
          {!data ? <Spinner /> : (
            <div className="kv">
              <span className="k">Spent today</span><span>${data.aiSpendToday.toFixed(4)}</span>
              <span className="k">Spent this month</span><span>${data.aiSpendMonth.toFixed(4)}</span>
              <span className="k">Daily budget</span><span>${(budget?.daily ?? 0).toFixed(2)}</span>
              <span className="k">Monthly budget</span><span>${(budget?.monthly ?? 0).toFixed(2)}</span>
              <span className="k">Tokens today</span><span>{fmtTok(data.aiTokensToday)}</span>
              <span className="k">Tokens this month</span><span>{fmtTok(data.aiTokensMonth)}</span>
              <span className="k">Confidence</span><span>{latest?.confidence ?? "UNKNOWN"}</span>
            </div>
          )}
        </Card>
        <Card title="Latest Snapshot Notes">
          <p className="small muted">{latest?.notes ?? "No notes. Record a financial snapshot in Settings to start tracking real numbers — never invented."}</p>
          <p className="faint small">All figures are labeled by confidence. Missing data is UNKNOWN.</p>
        </Card>
      </div>

      <Card title="AI Provider Balance & Budget" className="mt">
        <div className="row-between">
          <Stat label="Provider balance" value={balLabel} unknown={!bal && !balance?.error} />
          <button className="btn small" onClick={() => void refreshBalance()} disabled={balanceLoading}>
            {balanceLoading ? "Checking…" : "↻ Refresh"}
          </button>
        </div>
        {balance?.fetchedAt && (
          <div className="faint small">Checked {new Date(balance.fetchedAt).toLocaleTimeString()}{balance.cached ? " (cached ≤1 min)" : ""}</div>
        )}
        {balance?.cached && balance.error && <div className="faint small">{balance.error}</div>}
        <div className="row mt" style={{ gap: 8, flexWrap: "wrap" }}>
          <label className="small muted">
            Daily budget ($)
            <input type="number" min={1} step="1" value={dDaily} onChange={(e) => setDDaily(e.target.value)}
              style={{ width: 90, marginLeft: 6, padding: "4px 6px", background: "var(--bg-elev-2)", color: "var(--fg)", border: "1px solid var(--border)", borderRadius: 6 }} />
          </label>
          <label className="small muted">
            Monthly budget ($)
            <input type="number" min={1} step="1" value={dMonthly} onChange={(e) => setDMonthly(e.target.value)}
              style={{ width: 110, marginLeft: 6, padding: "4px 6px", background: "var(--bg-elev-2)", color: "var(--fg)", border: "1px solid var(--border)", borderRadius: 6 }} />
          </label>
          <button className="btn small primary" onClick={() => void saveBudget()}>Extend budget</button>
        </div>
        {budgetMsg && <div className="small mt" style={{ color: "var(--ok)" }}>{budgetMsg}</div>}
        {budgetErr && <div className="small mt error">{budgetErr}</div>}
        <p className="faint small mt">Raising the budget applies instantly to the Executive's cost guards — no redeploy needed. The provider balance is the actual credit left on your DeepSeek account.</p>
      </Card>

      <Card title="Snapshot History" className="mt">
        {!data ? <Spinner /> : data.snapshots.length === 0 ? <span className="faint">No snapshots recorded.</span> : (
          <table>
            <thead><tr><th>Period</th><th>Cash</th><th>Revenue</th><th>Expenses</th><th>Runway</th><th>Confidence</th></tr></thead>
            <tbody>
              {data.snapshots.map((s) => (
                <tr key={s.period + s.created_at}>
                  <td data-label="Period" className="mono">{s.period}</td>
                  <td data-label="Cash" className="mono">{fmtMoney(s.cash)}</td>
                  <td data-label="Revenue" className="mono">{fmtMoney(s.revenue)}</td>
                  <td data-label="Expenses" className="mono">{fmtMoney(s.expenses)}</td>
                  <td data-label="Runway" className="mono">{s.runway_months !== null && s.runway_months !== undefined ? `${s.runway_months.toFixed(1)} mo` : "UNKNOWN"}</td>
                  <td data-label="Confidence"><span className="pill">{s.confidence}</span></td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </Card>
    </div>
  );
}
