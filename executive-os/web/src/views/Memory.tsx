import { useEffect, useState } from "react";
import { api, type MemoryEntry } from "../api";
import { Card, ErrorBox, Spinner, fmtDate } from "../components/ui";

export function MemoryView() {
  const [entries, setEntries] = useState<MemoryEntry[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [layer, setLayer] = useState("L1");
  const [kind, setKind] = useState("FACT");
  const [title, setTitle] = useState("");
  const [body, setBody] = useState("");
  const [filter, setFilter] = useState("");

  const load = async () => {
    try {
      setEntries(await api.memory());
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  useEffect(() => { void load(); }, []);

  const add = async () => {
    if (!title.trim() || !body.trim()) return;
    try {
      await api.addMemory({ layer: layer as MemoryEntry["layer"], kind: kind as MemoryEntry["kind"], title: title.trim(), body: body.trim() });
      setTitle(""); setBody("");
      await load();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  const visible = (entries ?? []).filter((e) => {
    if (!filter) return true;
    const f = filter.toLowerCase();
    return e.title.toLowerCase().includes(f) || e.body.toLowerCase().includes(f);
  });

  return (
    <div className="grid grid-2">
      <div>
        <Card title="Add Memory">
          <label className="field">
            <span className="lbl">Layer</span>
            <select value={layer} onChange={(e) => setLayer(e.target.value)}>
              <option value="L0">L0 — Working Context</option>
              <option value="L1">L1 — Operational</option>
              <option value="L2">L2 — Institutional</option>
              <option value="L3">L3 — Doctrine (protected)</option>
            </select>
          </label>
          <label className="field">
            <span className="lbl">Kind</span>
            <select value={kind} onChange={(e) => setKind(e.target.value)}>
              <option>FACT</option><option>INFERENCE</option><option>ASSUMPTION</option>
              <option>ESTIMATE</option><option>UNKNOWN</option><option>LESSON</option><option>POLICY</option>
            </select>
          </label>
          <label className="field">
            <span className="lbl">Title</span>
            <input type="text" value={title} onChange={(e) => setTitle(e.target.value)} placeholder="ZTame conversion lesson" />
          </label>
          <label className="field">
            <span className="lbl">Body</span>
            <textarea value={body} onChange={(e) => setBody(e.target.value)} placeholder="Creative generated traffic but failed conversion…" />
          </label>
          <button className="btn primary" disabled={!title.trim() || !body.trim()} onClick={() => void add()}>Save Memory</button>
          {error && <div className="mt"><ErrorBox message={error} /></div>}
        </Card>
      </div>

      <div>
        <Card title="Memory Store">
          <input
            type="text"
            className="mb"
            placeholder="Filter memory…"
            value={filter}
            onChange={(e) => setFilter(e.target.value)}
          />
          {!entries ? <Spinner /> : visible.length === 0 ? <span className="faint">No memory entries.</span> : visible.map((e) => (
            <div key={e.id} className="card" style={{ marginBottom: 10, background: "var(--bg-elev-2)" }}>
              <div className="row-between">
                <b className="small">{e.title}</b>
                <span className="mono faint">{e.layer}/{e.kind}</span>
              </div>
              <p className="small muted">{e.body}</p>
              <div className="faint small">importance {e.importance.toFixed(2)} · {fmtDate(e.created_at)}</div>
            </div>
          ))}
        </Card>
      </div>
    </div>
  );
}
