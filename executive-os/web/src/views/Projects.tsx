import { useEffect, useState } from "react";
import { api, type ProjectView as Project } from "../api";
import { Card, ErrorBox, Spinner } from "../components/ui";

export function ProjectsView() {
  const [projects, setProjects] = useState<Project[] | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    api.projects()
      .then(setProjects)
      .catch((err) => setError(err instanceof Error ? err.message : String(err)));
  }, []);

  return (
    <Card title="Projects">
      <p className="small muted mb">Projects are tracked alongside the absorbed 10-gate pipeline workstream.</p>
      {error && <ErrorBox message={error} />}
      {!projects ? <Spinner /> : projects.length === 0 ? (
        <span className="faint">No projects tracked.</span>
      ) : (
        <table>
          <thead><tr><th>Project</th><th>Status</th><th>Priority</th></tr></thead>
          <tbody>
            {projects.map((p) => (
              <tr key={p.id}>
                <td data-label="Project">{p.name}</td>
                <td data-label="Status"><span className="pill">{p.status}</span></td>
                <td data-label="Priority" className="mono">{p.priority}</td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </Card>
  );
}
