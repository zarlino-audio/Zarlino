/** Settings: budget overrides + change password. */
import { useEffect, useState } from "react";
import { api, type BudgetOverrides } from "../api";

export function SettingsView() {
  const [overrides, setOverrides] = useState<BudgetOverrides>({});
  const [loaded, setLoaded] = useState(false);
  const [saved, setSaved] = useState(false);
  const [error, setError] = useState("");

  const [curPassword, setCurPassword] = useState("");
  const [newPassword, setNewPassword] = useState("");
  const [confirmPassword, setConfirmPassword] = useState("");
  const [pwMsg, setPwMsg] = useState<{ ok: boolean; text: string } | null>(null);
  const [pwBusy, setPwBusy] = useState(false);

  useEffect(() => {
    void (async () => {
      try {
        const s = await api.settings();
        setOverrides(s.budgetOverrides);
      } catch (e) {
        setError(e instanceof Error ? e.message : String(e));
      } finally {
        setLoaded(true);
      }
    })();
  }, []);

  const set = (key: keyof BudgetOverrides, value: string) => {
    setOverrides((o) => ({ ...o, [key]: value === "" ? undefined : Number(value) }));
    setSaved(false);
  };

  const save = async () => {
    try {
      await api.saveSettings(overrides);
      setSaved(true);
      setError("");
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  };

  const changePassword = async () => {
    if (newPassword.length < 4) { setPwMsg({ ok: false, text: "New password must be at least 4 characters." }); return; }
    if (newPassword !== confirmPassword) { setPwMsg({ ok: false, text: "New password and confirmation do not match." }); return; }
    setPwBusy(true);
    setPwMsg(null);
    try {
      await api.changePassword(curPassword, newPassword);
      setPwMsg({ ok: true, text: "Password changed. Other sessions were signed out." });
      setCurPassword(""); setNewPassword(""); setConfirmPassword("");
    } catch (e) {
      setPwMsg({ ok: false, text: e instanceof Error ? e.message : String(e) });
    } finally {
      setPwBusy(false);
    }
  };

  const fields: Array<{ key: keyof BudgetOverrides; label: string; hint: string }> = [
    { key: "aiDailyBudget", label: "AI daily budget ($)", hint: "Hard cap per day" },
    { key: "aiMonthlyBudget", label: "AI monthly budget ($)", hint: "Hard cap per month" },
    { key: "aiPerTaskBudget", label: "AI per-task budget ($)", hint: "Per task" },
    { key: "aiPerRunBudget", label: "AI per-run budget ($)", hint: "Per agent run" },
  ];

  return (
    <div>
      <div className="card" style={{ maxWidth: 540 }}>
        <h2>Budget overrides</h2>
        {!loaded && <p className="muted">Loading…</p>}
        {error && <div className="error-box">{error}</div>}
        {saved && <div className="error-box" style={{ borderColor: "var(--ok)", color: "var(--ok)" }}>Saved.</div>}
        {fields.map((f) => (
          <div key={f.key}>
            <label>{f.label} <span className="muted">— {f.hint}</span></label>
            <input type="number" value={overrides[f.key] ?? ""} onChange={(e) => set(f.key, e.target.value)} placeholder="default" />
          </div>
        ))}
        <div className="row" style={{ marginTop: 16 }}>
          <button className="action" onClick={save} disabled={!loaded}>Save</button>
        </div>
      </div>

      <div className="card" style={{ maxWidth: 540 }}>
        <h2>Change password</h2>
        <p className="muted">Rotate your Founder password. Other signed-in sessions end immediately.</p>
        {pwMsg && <div className="error-box" style={pwMsg.ok ? { borderColor: "var(--ok)", color: "var(--ok)" } : undefined}>{pwMsg.text}</div>}
        <label>Current password</label>
        <input type="password" value={curPassword} onChange={(e) => setCurPassword(e.target.value)} autoComplete="current-password" />
        <label>New password</label>
        <input type="password" value={newPassword} onChange={(e) => setNewPassword(e.target.value)} autoComplete="new-password" />
        <label>Confirm new password</label>
        <input type="password" value={confirmPassword} onChange={(e) => setConfirmPassword(e.target.value)} autoComplete="new-password" />
        <div className="row" style={{ marginTop: 16 }}>
          <button className="action" onClick={changePassword} disabled={pwBusy || !curPassword || !newPassword || !confirmPassword}>
            {pwBusy ? "Changing…" : "Change password"}
          </button>
        </div>
      </div>
    </div>
  );
}
