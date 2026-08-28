import { useCallback, useEffect, useState } from "react";
import { api, type GateStatus, type PipelineGate, type Person, type ToolExecution } from "../api";
import { Confetti } from "../components/Confetti";
import { Card, ErrorBox, Spinner, fmtDate } from "../components/ui";

const GATE_STATUSES: GateStatus[] = ["NOT_STARTED", "IN_PROGRESS", "PASSED", "FAILED", "BLOCKED", "SHIPPED"];

function statusClass(s: GateStatus): string {
  switch (s) {
    case "PASSED":
    case "SHIPPED": return "pill green";
    case "IN_PROGRESS": return "pill yellow";
    case "FAILED":
    case "BLOCKED": return "pill red";
    default: return "pill";
  }
}

function GateRow({ gate, onUpdate }: { gate: PipelineGate; onUpdate: (id: string, status: GateStatus) => void }) {
  return (
    <tr>
      <td data-label="Gate #" className="mono faint">{gate.gate_number}</td>
      <td data-label="Gate">{gate.gate_name}</td>
      <td data-label="Set status">
        <select
          className="gate-status"
          value={gate.status}
          onChange={(e) => onUpdate(gate.id, e.target.value as GateStatus)}
        >
          {GATE_STATUSES.map((s) => <option key={s} value={s}>{s}</option>)}
        </select>
      </td>
      <td data-label="Current"><span className={statusClass(gate.status)}>{gate.status}</span></td>
      <td data-label="Note" className="faint small">{gate.note ?? ""}</td>
    </tr>
  );
}

export function PipelineView() {
  const [gates, setGates] = useState<PipelineGate[] | null>(null);
  const [persons, setPersons] = useState<Person[] | null>(null);
  const [executions, setExecutions] = useState<ToolExecution[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [msg, setMsg] = useState<string | null>(null);
  const [celebrate, setCelebrate] = useState(0);

  const load = useCallback(async () => {
    try {
      const [pg, pp, px] = await Promise.all([api.pipeline(), api.persons(), api.toolExecutions()]);
      setGates(pg.gates);
      setPersons(pp);
      setExecutions(px);
      setError(null);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  }, []);

  useEffect(() => { void load(); }, [load]);

  const updateGate = async (id: string, status: GateStatus) => {
    try {
      await api.updateGate(id, { status });
      await load();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  const ship = async (pluginId: string, name: string) => {
    const version = window.prompt(`Ship ${name}? Enter the release version (default 1.0.0):`, "1.0.0");
    if (version === null) return;
    try {
      await api.ship(pluginId, version || "1.0.0", `Shipped via dashboard (v${version || "1.0.0"})`);
      setMsg(`${name} shipped ✓`);
      setCelebrate((c) => c + 1);
      setTimeout(() => setMsg(null), 4000);
      await load();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  const plugins = gates ? Array.from(new Set(gates.map((g) => g.plugin_id))) : [];

  return (
    <div className="grid grid-2">
      {celebrate > 0 && <Confetti trigger={celebrate} />}
      <div>
        {error && <ErrorBox message={error} />}
        {msg && <div className="ok mb">{msg}</div>}
        {!gates ? <Spinner /> : plugins.map((plugin) => {
          const pluginGates = gates.filter((g) => g.plugin_id === plugin).sort((a, b) => a.gate_number - b.gate_number);
          const passed = pluginGates.filter((g) => g.status === "PASSED").length;
          const g10 = pluginGates.find((g) => g.gate_number === 10);
          const shipped = g10?.status === "SHIPPED";
          return (
            <Card key={plugin} title={shipped ? `${plugin} — SHIPPED ✓` : `${plugin} — 10-Gate Pipeline (${passed}/10 passed)`} className="mb">
              {shipped && (
                <div className="row-between mb">
                  <span className="pill green">{g10?.note ?? "Shipped"}</span>
                  {!shipped && <button className="btn small primary" onClick={() => void ship(plugin, plugin)}>🚀 Ship</button>}
                </div>
              )}
              <table>
                <thead><tr><th>#</th><th>Gate</th><th>Set status</th><th>Current</th><th>Note</th></tr></thead>
                <tbody>
                  {pluginGates.map((g) => <GateRow key={g.id} gate={g} onUpdate={(id, s) => void updateGate(id, s)} />)}
                </tbody>
              </table>
              {!shipped && (
                <div className="row mt">
                  <button className="btn small primary" disabled={!g10 || g10.status === "NOT_STARTED"} onClick={() => void ship(plugin, plugin)}>🚀 Ship {plugin}</button>
                  <span className="faint small">Marks the product SHIPPED, sets Gate 10 → SHIPPED, records shipment history.</span>
                </div>
              )}
            </Card>
          );
        })}
      </div>

      <div>
        <Card title="Technical Roles (onboarded)">
          <p className="small muted mb">Tasks can be assigned to these roles via the Executive — they mirror the orchestrator roster.</p>
          {!persons ? <Spinner /> : (
            <table>
              <thead><tr><th>Role</th><th>Scope</th></tr></thead>
              <tbody>
                {persons.map((p) => (
                  <tr key={p.id}>
                    <td data-label="Role">{p.name}</td>
                    <td data-label="Scope" className="faint small">{p.role ?? "—"}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </Card>

        <Card title="Agent at Work — Recent Tool Executions" className="mt">
          <p className="small muted mb">Every tool the Executive runs (from chat or a task) is recorded here — proof it actually did the work.</p>
          {!executions ? <Spinner /> : executions.length === 0 ? <span className="faint">No tool executions yet.</span> : (
            <table>
              <thead><tr><th>Tool</th><th>Status</th><th>When</th></tr></thead>
              <tbody>
                {executions.slice(0, 15).map((x) => (
                  <tr key={x.id}>
                    <td data-label="Tool" className="mono small">{x.tool_name}</td>
                    <td data-label="Status"><span className={x.status === "SUCCESS" ? "pill green" : x.status === "FAILED" || x.status === "DENY" ? "pill red" : "pill yellow"}>{x.status}</span></td>
                    <td data-label="When" className="mono faint small">{fmtDate(x.created_at)}</td>
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
