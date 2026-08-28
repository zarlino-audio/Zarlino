import { useEffect, useState } from "react";
import { api, type AuditEvent } from "../api";
import { Card, ErrorBox, Spinner, fmtDate } from "../components/ui";

export function ActivityView() {
  const [events, setEvents] = useState<AuditEvent[] | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    api.activity()
      .then(setEvents)
      .catch((err) => setError(err instanceof Error ? err.message : String(err)));
  }, []);

  return (
    <Card title="Activity Timeline (spec §25)">
      <p className="small muted mb">
        Every agent run, decision, approval, tool call, and state change is recorded. The Founder can trace
        decision → evidence → memory → doctrine → reasoning → tool calls → approval → execution.
      </p>
      {error && <ErrorBox message={error} />}
      {!events ? <Spinner /> : events.length === 0 ? <span className="faint">No activity yet.</span> : (
        <table>
          <thead><tr><th>When</th><th>Actor</th><th>Action</th><th>Entity</th><th>Verdict</th></tr></thead>
          <tbody>
            {events.map((e) => (
              <tr key={e.id}>
                <td data-label="When" className="mono faint">{fmtDate(e.created_at)}</td>
                <td data-label="Actor" className="mono">{e.actor}</td>
                <td data-label="Action">{e.action}</td>
                <td data-label="Entity" className="mono faint">
                  {e.entity_type ? `${e.entity_type}:${e.entity_id?.slice(0, 8) ?? ""}` : "—"}
                </td>
                <td data-label="Verdict">{e.verdict ? <span className="pill">{e.verdict}</span> : "—"}</td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </Card>
  );
}
