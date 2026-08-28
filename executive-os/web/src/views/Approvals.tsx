import { useEffect, useState } from "react";
import { api, type Approval } from "../api";
import { Card, ErrorBox, Spinner, fmtDate } from "../components/ui";

export function ApprovalsView() {
  const [approvals, setApprovals] = useState<Approval[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [note, setNote] = useState<Record<string, string>>({});

  const load = async () => {
    try {
      setApprovals(await api.approvals());
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  useEffect(() => { void load(); }, []);

  const decide = async (id: string, action: "approve" | "reject") => {
    try {
      if (action === "approve") await api.approve(id, note[id]);
      else await api.reject(id, note[id]);
      await load();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  const pending = (approvals ?? []).filter((a) => a.status === "PENDING");
  const decided = (approvals ?? []).filter((a) => a.status !== "PENDING");

  return (
    <div className="grid grid-2">
      <div>
        <Card title={`Pending Approvals (${pending.length})`}>
          {!approvals ? <Spinner /> : pending.length === 0 ? (
            <span className="faint">Nothing awaiting your decision.</span>
          ) : (
            pending.map((a) => (
              <div key={a.id} className="card" style={{ marginBottom: 12, background: "var(--bg-elev-2)" }}>
                <div className="row-between">
                  <b>{a.action_label}</b>
                  <span className="pill yellow">PENDING</span>
                </div>
                <p className="small muted">{a.reason}</p>
                <div className="kv small">
                  <span className="k">Expected upside</span><span>{a.expected_upside ?? "—"}</span>
                  <span className="k">Max downside</span><span>{a.max_downside ?? "—"}</span>
                  <span className="k">Budget available</span><span>{a.budget_available ?? "—"}</span>
                  <span className="k">Recommendation</span><span>{a.recommendation}</span>
                  <span className="k">Requested</span><span>{fmtDate(a.created_at)}</span>
                </div>
                <input
                  type="text"
                  className="mt"
                  placeholder="Note (optional)"
                  value={note[a.id] ?? ""}
                  onChange={(e) => setNote((n) => ({ ...n, [a.id]: e.target.value }))}
                />
                <div className="row mt">
                  <button className="btn small primary" onClick={() => void decide(a.id, "approve")}>Approve</button>
                  <button className="btn small danger" onClick={() => void decide(a.id, "reject")}>Reject</button>
                </div>
              </div>
            ))
          )}
          {error && <div className="mt"><ErrorBox message={error} /></div>}
        </Card>
      </div>

      <div>
        <Card title={`Decision History (${decided.length})`}>
          {decided.length === 0 ? <span className="faint">No decisions yet.</span> : (
            <table>
              <thead><tr><th>Action</th><th>Status</th><th>Note</th><th>When</th></tr></thead>
              <tbody>
                {decided.slice(0, 30).map((a) => (
                  <tr key={a.id}>
                    <td data-label="Action">{a.action_label}</td>
                    <td data-label="Status"><span className="pill">{a.status}</span></td>
                    <td data-label="Note" className="small faint">{a.decision_note ?? "—"}</td>
                    <td data-label="When" className="mono faint">{fmtDate(a.created_at)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </Card>
      </div>
    </div>
  );
}
