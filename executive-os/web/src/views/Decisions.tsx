import { useEffect, useState } from "react";
import { api, type Decision } from "../api";
import { Card, ErrorBox, Spinner, fmtDate } from "../components/ui";

export function DecisionsView() {
  const [decisions, setDecisions] = useState<Decision[] | null>(null);
  const [selected, setSelected] = useState<Decision | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [problem, setProblem] = useState("");
  const [cost, setCost] = useState("");
  const [running, setRunning] = useState(false);

  const load = async () => {
    try {
      setDecisions(await api.decisions());
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  useEffect(() => { void load(); }, []);

  const open = async (id: string) => {
    try {
      setSelected(await api.decision(id));
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  const run = async () => {
    if (!problem.trim() || running) return;
    setRunning(true);
    setError(null);
    try {
      const costNum = cost.trim() === "" ? undefined : Number(cost);
      const result = (await api.runDecision({
        problem: problem.trim(),
        ...(costNum !== undefined && !Number.isNaN(costNum) ? { estimatedCost: costNum } : {}),
      })) as unknown as { decision: Decision };
      setProblem("");
      setCost("");
      await load();
      setSelected(result.decision);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setRunning(false);
    }
  };

  return (
    <div className="grid grid-2">
      <div>
        <Card title="Run a Decision">
          <label className="field">
            <span className="lbl">Problem / request</span>
            <textarea
              value={problem}
              onChange={(e) => setProblem(e.target.value)}
              placeholder="e.g. Should we spend $250 validating the affiliate acquisition funnel?"
            />
          </label>
          <label className="field">
            <span className="lbl">Estimated cost (USD, optional)</span>
            <input type="text" value={cost} onChange={(e) => setCost(e.target.value)} placeholder="250" />
          </label>
          <button className="btn primary" disabled={running || !problem.trim()} onClick={() => void run()}>
            {running ? "Running pipeline…" : "Run Decision Pipeline"}
          </button>
          {error && <div className="mt"><ErrorBox message={error} /></div>}
        </Card>

        <Card title="Decision Register" className="mt">
          {!decisions ? <Spinner /> : decisions.length === 0 ? (
            <span className="faint">No decisions recorded.</span>
          ) : (
            <table>
              <thead><tr><th>ID</th><th>Title</th><th>Category</th><th>Status</th><th>When</th></tr></thead>
              <tbody>
                {decisions.map((d) => (
                  <tr key={d.id} style={{ cursor: "pointer" }} onClick={() => void open(d.id)}>
                    <td data-label="ID" className="mono faint">{d.id.slice(0, 8)}</td>
                    <td data-label="Title">{d.title}</td>
                    <td data-label="Category" className="faint">{d.category}</td>
                    <td data-label="Status"><span className="pill">{d.status}</span></td>
                    <td data-label="When" className="mono faint">{fmtDate(d.created_at)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </Card>
      </div>

      <div>
        <Card title="Decision Detail">
          {!selected ? (
            <span className="faint">Select a decision to inspect its full pipeline output (lenses, risks, authority, outcome).</span>
          ) : (
            <div>
              <div className="row-between">
                <b>{selected.title}</b>
                <span className="pill">{selected.status}</span>
              </div>
              <p className="muted small">{selected.problem}</p>
              <div className="kv mb">
                <span className="k">Category</span><span>{selected.category}</span>
                <span className="k">Confidence</span><span>{selected.confidence !== undefined ? `${Math.round(selected.confidence * 100)}%` : "UNKNOWN"}</span>
                <span className="k">Recommended</span><span>{selected.recommended_action ?? "—"}</span>
                <span className="k">Expected outcome</span><span>{selected.expected_outcome ?? "—"}</span>
                <span className="k">Approval required</span><span>{selected.approval_required ? "Yes" : "No"}</span>
                <span className="k">Outcome status</span><span>{selected.outcome_status ?? "PENDING"}</span>
              </div>
              {selected.ceo_assessment && <p className="small"><b>CEO:</b> {selected.ceo_assessment}</p>}
              {selected.cfo_assessment && <p className="small"><b>CFO:</b> {selected.cfo_assessment}</p>}
              {selected.cto_assessment && <p className="small"><b>CTO:</b> {selected.cto_assessment}</p>}
              {selected.risk_assessment && <p className="small"><b>Risk:</b> {selected.risk_assessment}</p>}
            </div>
          )}
        </Card>
      </div>
    </div>
  );
}
