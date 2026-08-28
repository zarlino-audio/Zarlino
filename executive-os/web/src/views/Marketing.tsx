import { useEffect, useState } from "react";
import { api } from "../api";
import { Card, ErrorBox, Spinner, fmtMoney } from "../components/ui";

interface Campaign {
  id: string;
  name: string;
  status: string;
  channel?: string;
  budget?: number;
  spent?: number;
  objective?: string;
}

export function MarketingView() {
  const [campaigns, setCampaigns] = useState<Campaign[] | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    api.marketing()
      .then(setCampaigns)
      .catch((err) => setError(err instanceof Error ? err.message : String(err)));
  }, []);

  return (
    <Card title="Marketing Campaigns">
      <p className="small muted mb">Spend is governed by the authority engine — experiments within budget are autonomous; larger spends escalate to you.</p>
      {error && <ErrorBox message={error} />}
      {!campaigns ? <Spinner /> : campaigns.length === 0 ? (
        <span className="faint">No campaigns tracked.</span>
      ) : (
        <table>
          <thead><tr><th>Campaign</th><th>Channel</th><th>Status</th><th>Budget</th><th>Spent</th></tr></thead>
          <tbody>
            {campaigns.map((c) => (
              <tr key={c.id}>
                <td data-label="Campaign">{c.name}</td>
                <td data-label="Channel" className="faint">{c.channel ?? "—"}</td>
                <td data-label="Status"><span className="pill">{c.status}</span></td>
                <td data-label="Budget" className="mono">{c.budget !== undefined ? fmtMoney(c.budget) : "—"}</td>
                <td data-label="Spent" className="mono">{c.spent !== undefined ? fmtMoney(c.spent) : "—"}</td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </Card>
  );
}
