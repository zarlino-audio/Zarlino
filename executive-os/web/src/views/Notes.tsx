import { useEffect, useState } from "react";
import { api, type Contact, type Note, type Reminder } from "../api";
import { ErrorBox, Spinner } from "../components/ui";

const fmtWhen = (iso: string) => {
  const d = new Date(iso);
  const diff = d.getTime() - Date.now();
  if (diff < 0) return `overdue ${Math.max(1, Math.round(-diff / 60000))}m ago`;
  if (diff < 3600_000) return `in ${Math.max(1, Math.round(diff / 60000))}m`;
  if (diff < 86_400_000) return `in ${Math.round(diff / 3600_000)}h`;
  return d.toLocaleDateString();
};

export function NotesView() {
  const [notes, setNotes] = useState<Note[] | null>(null);
  const [reminders, setReminders] = useState<Reminder[] | null>(null);
  const [contacts, setContacts] = useState<Contact[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [nTitle, setNTitle] = useState("");
  const [nBody, setNBody] = useState("");
  const [rTitle, setRTitle] = useState("");
  const [rDue, setRDue] = useState("");
  const [cName, setCName] = useState("");
  const [cRole, setCRole] = useState("");
  const [cCompany, setCCompany] = useState("");
  const [cEmail, setCEmail] = useState("");
  const [tab, setTab] = useState<"notes" | "reminders" | "contacts">("notes");

  const load = async () => {
    try {
      const [n, r, c] = await Promise.all([api.notes(), api.reminders(), api.contacts()]);
      setNotes(n);
      setReminders(r);
      setContacts(c);
      setError(null);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };
  useEffect(() => { void load(); }, []);
  useEffect(() => {
    const t = setInterval(() => { if (!document.hidden) void load(); }, 60_000);
    return () => clearInterval(t);
  }, []);

  const addNote = async () => {
    if (!nTitle.trim()) return;
    try {
      await api.noteCreate({ title: nTitle.trim(), body: nBody.trim() || undefined });
      setNTitle("");
      setNBody("");
      await load();
    } catch (err) { setError(err instanceof Error ? err.message : String(err)); }
  };

  const addReminder = async () => {
    if (!rTitle.trim() || !rDue) return;
    try {
      await api.reminderCreate({ title: rTitle.trim(), dueAt: new Date(rDue).toISOString() });
      setRTitle("");
      setRDue("");
      await load();
    } catch (err) { setError(err instanceof Error ? err.message : String(err)); }
  };

  const addContact = async () => {
    if (!cName.trim()) return;
    try {
      await api.contactAdd({ name: cName.trim(), role: cRole.trim() || undefined, company: cCompany.trim() || undefined, email: cEmail.trim() || undefined });
      setCName(""); setCRole(""); setCCompany(""); setCEmail("");
      await load();
    } catch (err) { setError(err instanceof Error ? err.message : String(err)); }
  };

  const overdue = (reminders ?? []).filter((r) => r.overdue && !r.done);

  return (
    <div>
      {error && <div className="mb"><ErrorBox message={error} /></div>}

      <div className="row mb" style={{ gap: 8 }}>
        <button className={`btn small ${tab === "notes" ? "active" : ""}`} onClick={() => setTab("notes")}>📝 Notes ({notes?.length ?? 0})</button>
        <button className={`btn small ${tab === "reminders" ? "active" : ""}`} onClick={() => setTab("reminders")}>
          ⏰ Reminders {overdue.length > 0 ? <span className="pill red">{overdue.length} overdue</span> : null}
        </button>
        <button className={`btn small ${tab === "contacts" ? "active" : ""}`} onClick={() => setTab("contacts")}>📇 Contacts ({contacts?.length ?? 0})</button>
        <span className="spacer" />
        <button className="btn small" onClick={() => void load()}>↻</button>
      </div>

      {tab === "notes" && (
        <div className="grid grid-2">
          <div className="card">
            <h3>Capture a note</h3>
            <label className="field">
              <span className="lbl">Title</span>
              <input type="text" value={nTitle} onChange={(e) => setNTitle(e.target.value)} placeholder="Meeting outcome, idea, decision context…" />
            </label>
            <label className="field">
              <span className="lbl">Body</span>
              <textarea rows={4} value={nBody} onChange={(e) => setNBody(e.target.value)} placeholder="What should the Executive remember from this?" />
            </label>
            <button className="btn primary" disabled={!nTitle.trim()} onClick={() => void addNote()}>Save note</button>
          </div>
          <div className="card">
            <h3>Saved notes</h3>
            {!notes ? <Spinner /> : notes.length === 0 ? <span className="faint">No notes yet.</span> : (
              <div className="local-list">
                {notes.slice(0, 20).map((n) => (
                  <div key={n.id} className="local-row">
                    <span>{n.pinned ? "📌" : "📝"}</span>
                    <div className="local-row-body">
                      <b className="small">{n.title}</b>
                      {n.body && <div className="small muted">{n.body.slice(0, 200)}</div>}
                      {n.tags.length > 0 && <div className="faint small">{n.tags.map((t) => `#${t}`).join(" ")}</div>}
                    </div>
                    <button className="btn small danger" onClick={() => void (async () => { try { await api.noteDelete(n.id); await load(); } catch (err) { setError(err instanceof Error ? err.message : String(err)); } })()}>✕</button>
                  </div>
                ))}
              </div>
            )}
          </div>
        </div>
      )}

      {tab === "reminders" && (
        <div className="grid grid-2">
          <div className="card">
            <h3>Set a reminder</h3>
            <label className="field">
              <span className="lbl">What to follow up</span>
              <input type="text" value={rTitle} onChange={(e) => setRTitle(e.target.value)} placeholder="Check ZTame signing status…" />
            </label>
            <label className="field">
              <span className="lbl">Due</span>
              <input type="datetime-local" value={rDue} onChange={(e) => setRDue(e.target.value)} />
            </label>
            <button className="btn primary" disabled={!rTitle.trim() || !rDue} onClick={() => void addReminder()}>Set reminder</button>
          </div>
          <div className="card">
            <h3>Open reminders</h3>
            {!reminders ? <Spinner /> : reminders.length === 0 ? <span className="faint">No reminders. The Executive also surfaces overdue ones in the daily brief.</span> : (
              <div className="local-list">
                {reminders.slice(0, 20).map((r) => (
                  <div key={r.id} className={`local-row ${r.overdue ? "rem-overdue" : ""}`}>
                    <span>{r.overdue ? "⏰" : "🔔"}</span>
                    <div className="local-row-body">
                      <div className="small">{r.title}</div>
                      <div className={`faint small ${r.overdue ? "err-text" : ""}`}>{fmtWhen(r.dueAt)}</div>
                    </div>
                    <button className="btn small green" disabled={r.done} onClick={() => void (async () => { try { await api.reminderDone(r.id); await load(); } catch (err) { setError(err instanceof Error ? err.message : String(err)); } })()}>✔</button>
                    <button className="btn small danger" onClick={() => void (async () => { try { await api.reminderDelete(r.id); await load(); } catch (err) { setError(err instanceof Error ? err.message : String(err)); } })()}>✕</button>
                  </div>
                ))}
              </div>
            )}
          </div>
        </div>
      )}

      {tab === "contacts" && (
        <div className="grid grid-2">
          <div className="card">
            <h3>Add a contact</h3>
            <label className="field"><span className="lbl">Name *</span><input type="text" value={cName} onChange={(e) => setCName(e.target.value)} placeholder="Partner / tester / reviewer" /></label>
            <div className="grid grid-2">
              <label className="field"><span className="lbl">Role</span><input type="text" value={cRole} onChange={(e) => setCRole(e.target.value)} placeholder="Reviewer" /></label>
              <label className="field"><span className="lbl">Company</span><input type="text" value={cCompany} onChange={(e) => setCCompany(e.target.value)} placeholder="Studio X" /></label>
            </div>
            <label className="field"><span className="lbl">Email</span><input type="text" value={cEmail} onChange={(e) => setCEmail(e.target.value)} placeholder="name@example.com" /></label>
            <button className="btn primary" disabled={!cName.trim()} onClick={() => void addContact()}>Add contact</button>
          </div>
          <div className="card">
            <h3>Contacts</h3>
            {!contacts ? <Spinner /> : contacts.length === 0 ? <span className="faint">No contacts yet.</span> : (
              <table>
                <thead><tr><th>Name</th><th>Role</th><th>Company</th><th>Contact</th><th></th></tr></thead>
                <tbody>
                  {contacts.slice(0, 50).map((c) => (
                    <tr key={c.id}>
                      <td data-label="Name"><b className="small">{c.name}</b></td>
                      <td data-label="Role" className="small">{c.role ?? "—"}</td>
                      <td data-label="Company" className="small">{c.company ?? "—"}</td>
                      <td data-label="Contact" className="small">{c.email ?? c.phone ?? "—"}</td>
                      <td data-label="Actions">
                        <button className="btn small danger" onClick={() => void (async () => { try { await api.contactDelete(c.id); await load(); } catch (err) { setError(err instanceof Error ? err.message : String(err)); } })()}>✕</button>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}
          </div>
        </div>
      )}
    </div>
  );
}
