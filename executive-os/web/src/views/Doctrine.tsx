import { useEffect, useState } from "react";
import { api, type DoctrineRule } from "../api";
import { Card, ErrorBox, Spinner } from "../components/ui";

export function DoctrineView() {
  const [rules, setRules] = useState<DoctrineRule[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [code, setCode] = useState("");
  const [principle, setPrinciple] = useState("");
  const [rationale, setRationale] = useState("");
  const [category, setCategory] = useState("GENERAL");

  const load = async () => {
    try {
      setRules(await api.doctrine());
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  useEffect(() => { void load(); }, []);

  const propose = async () => {
    if (!code.trim() || !principle.trim()) return;
    try {
      await api.proposeDoctrine({ code: code.trim(), principle: principle.trim(), rationale: rationale.trim() || undefined, category });
      setCode(""); setPrinciple(""); setRationale("");
      await load();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  const decide = async (id: string, action: "approve" | "reject") => {
    try {
      if (action === "approve") await api.approveDoctrine(id);
      else await api.rejectDoctrine(id);
      await load();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  const active = (rules ?? []).filter((r) => r.status === "ACTIVE");
  const proposed = (rules ?? []).filter((r) => r.status === "PROPOSED");

  return (
    <div className="grid grid-2">
      <div>
        <Card title="Active Doctrine (L3 — immutable without Founder approval)">
          {!rules ? <Spinner /> : active.length === 0 ? <span className="faint">No active doctrine.</span> : (
            <table>
              <thead><tr><th>Code</th><th>Principle</th><th>Category</th></tr></thead>
              <tbody>
                {active.map((r) => (
                  <tr key={r.id}>
                    <td data-label="Code" className="mono">{r.code} v{r.version}</td>
                    <td data-label="Principle">{r.principle}</td>
                    <td data-label="Category" className="faint">{r.category}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </Card>

        <Card title="Proposed Changes (require Founder approval)" className="mt">
          {proposed.length === 0 ? <span className="faint">No pending proposals.</span> : proposed.map((r) => (
            <div key={r.id} className="card" style={{ marginBottom: 10, background: "var(--bg-elev-2)" }}>
              <div className="row-between">
                <b>{r.code} v{r.version}</b>
                <span className="pill yellow">PROPOSED</span>
              </div>
              <p className="small">{r.principle}</p>
              {r.rationale && <p className="small muted">{r.rationale}</p>}
              <div className="row">
                <button className="btn small primary" onClick={() => void decide(r.id, "approve")}>Approve</button>
                <button className="btn small danger" onClick={() => void decide(r.id, "reject")}>Reject</button>
              </div>
            </div>
          ))}
          {error && <div className="mt"><ErrorBox message={error} /></div>}
        </Card>
      </div>

      <div>
        <Card title="Propose a Doctrine Change">
          <p className="small muted">
            The Executive Agent can never modify doctrine silently. Proposals become PROPOSED and only take
            effect after you approve (spec §5, §21, §41).
          </p>
          <label className="field">
            <span className="lbl">Code</span>
            <input type="text" value={code} onChange={(e) => setCode(e.target.value)} placeholder="D-011" />
          </label>
          <label className="field">
            <span className="lbl">Principle</span>
            <textarea value={principle} onChange={(e) => setPrinciple(e.target.value)} placeholder="e.g. Never ship a build that fails CI." />
          </label>
          <label className="field">
            <span className="lbl">Rationale (optional)</span>
            <input type="text" value={rationale} onChange={(e) => setRationale(e.target.value)} />
          </label>
          <label className="field">
            <span className="lbl">Category</span>
            <select value={category} onChange={(e) => setCategory(e.target.value)}>
              <option>GENERAL</option><option>EXECUTION</option><option>FINANCE</option>
              <option>STRATEGY</option><option>TECHNICAL</option><option>GOVERNANCE</option>
            </select>
          </label>
          <button className="btn primary" disabled={!code.trim() || !principle.trim()} onClick={() => void propose()}>
            Submit Proposal
          </button>
        </Card>
      </div>
    </div>
  );
}
