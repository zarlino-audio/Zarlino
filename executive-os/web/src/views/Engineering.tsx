import { useEffect, useState } from "react";
import { api } from "../api";
import { Card, ErrorBox, Spinner, levelClass } from "../components/ui";

interface Issue {
  id: string;
  title: string;
  severity: string;
  status: string;
}

export function EngineeringView() {
  const [issues, setIssues] = useState<Issue[] | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    api.engineering()
      .then(setIssues)
      .catch((err) => setError(err instanceof Error ? err.message : String(err)));
  }, []);

  return (
    <Card title="Engineering Issues">
      {error && <ErrorBox message={error} />}
      {!issues ? <Spinner /> : issues.length === 0 ? (
        <span className="faint">No engineering issues tracked.</span>
      ) : (
        <table>
          <thead><tr><th>Severity</th><th>Issue</th><th>Status</th></tr></thead>
          <tbody>
            {issues.map((i) => (
              <tr key={i.id}>
                <td data-label="Severity"><span className={`pill ${levelClass(i.severity)}`}>{i.severity}</span></td>
                <td data-label="Issue">{i.title}</td>
                <td data-label="Status"><span className="pill">{i.status}</span></td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </Card>
  );
}
