import { useCallback, useEffect, useMemo, useState } from "react";
import { api, type CalendarEvent } from "../api";
import { ErrorBox, Spinner } from "../components/ui";

const DAYS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
const MONTHS = ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"];
const STATUS_PILL: Record<string, string> = { SCHEDULED: "pill", CANCELLED: "pill red", DONE: "pill green" };

const toLocalInput = (iso: string) => {
  const d = new Date(iso);
  const pad = (n: number) => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}T${pad(d.getHours())}:${pad(d.getMinutes())}`;
};

export function CalendarView() {
  const today = new Date();
  const [cursor, setCursor] = useState({ y: today.getFullYear(), m: today.getMonth() });
  const [selected, setSelected] = useState<string>(`${today.getFullYear()}-${String(today.getMonth() + 1).padStart(2, "0")}-${String(today.getDate()).padStart(2, "0")}`);
  const [events, setEvents] = useState<CalendarEvent[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  // Quick-add form
  const [title, setTitle] = useState("");
  const [startInput, setStartInput] = useState(toLocalInput(new Date().toISOString()).slice(0, 16));
  const [endInput, setEndInput] = useState("");
  const [location, setLocation] = useState("");

  const load = useCallback(async () => {
    try {
      const monthStart = new Date(Date.UTC(cursor.y, cursor.m, 1));
      const monthEnd = new Date(Date.UTC(cursor.y, cursor.m + 1, 0, 23, 59, 59));
      const evs = await api.calendar(monthStart.toISOString(), monthEnd.toISOString());
      setEvents(evs);
      setError(null);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  }, [cursor]);

  useEffect(() => { void load(); }, [load]);

  const cells = useMemo(() => {
    const first = new Date(Date.UTC(cursor.y, cursor.m, 1));
    const startDow = first.getUTCDay();
    const daysInMonth = new Date(Date.UTC(cursor.y, cursor.m + 1, 0)).getUTCDate();
    const out: Array<{ iso: string; day: number; inMonth: boolean }> = [];
    for (let i = 0; i < startDow; i++) out.push({ iso: "", day: 0, inMonth: false });
    for (let d = 1; d <= daysInMonth; d++) {
      out.push({ iso: `${cursor.y}-${String(cursor.m + 1).padStart(2, "0")}-${String(d).padStart(2, "0")}`, day: d, inMonth: true });
    }
    while (out.length % 7 !== 0) out.push({ iso: "", day: 0, inMonth: false });
    return out;
  }, [cursor]);

  const byDay = useMemo(() => {
    const map: Record<string, CalendarEvent[]> = {};
    for (const e of events ?? []) {
      const day = e.startAt.slice(0, 10);
      (map[day] ??= []).push(e);
    }
    for (const k of Object.keys(map)) map[k].sort((a, b) => a.startAt.localeCompare(b.startAt));
    return map;
  }, [events]);

  const dayEvents = byDay[selected] ?? [];
  const todayIso = `${today.getFullYear()}-${String(today.getMonth() + 1).padStart(2, "0")}-${String(today.getDate()).padStart(2, "0")}`;

  const nav = (delta: number) => {
    const d = new Date(Date.UTC(cursor.y, cursor.m + delta, 1));
    setCursor({ y: d.getUTCFullYear(), m: d.getUTCMonth() });
  };
  const goToday = () => {
    const t = new Date();
    setCursor({ y: t.getFullYear(), m: t.getMonth() });
    setSelected(`${t.getFullYear()}-${String(t.getMonth() + 1).padStart(2, "0")}-${String(t.getDate()).padStart(2, "0")}`);
  };

  const add = async () => {
    if (!title.trim() || !startInput) return;
    try {
      const startAt = new Date(startInput).toISOString();
      const endAt = endInput ? new Date(endInput).toISOString() : undefined;
      await api.calendarCreate({ title: title.trim(), startAt, endAt, location: location.trim() || undefined });
      setTitle("");
      setEndInput("");
      setLocation("");
      setSelected(startAt.slice(0, 10));
      await load();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  const remove = async (id: string) => {
    try {
      await api.calendarDelete(id);
      await load();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  const toggleDone = async (e: CalendarEvent) => {
    try {
      await api.calendarUpdate(e.id, { status: e.status === "DONE" ? "SCHEDULED" : "DONE" });
      await load();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  return (
    <div>
      {error && <div className="mb"><ErrorBox message={error} /></div>}
      <div className="calendar-wrap">
        <div className="calendar-head">
          <button className="btn small" onClick={() => nav(-1)}>‹ Prev</button>
          <h3 style={{ margin: "0 12px" }}>{MONTHS[cursor.m]} {cursor.y}</h3>
          <button className="btn small" onClick={() => nav(1)}>Next ›</button>
          <button className="btn small" onClick={goToday}>Today</button>
          <span className="spacer" />
          <span className="faint small">Click a day to view/add events</span>
        </div>
        <div className="cal-grid">
          {DAYS.map((d) => <div key={d} className="cal-dow">{d}</div>)}
          {cells.map((c, i) => (
            <button
              key={i}
              className={`cal-cell ${c.inMonth ? "" : "cal-dim"} ${c.iso === selected ? "cal-selected" : ""} ${c.iso === todayIso ? "cal-today" : ""}`}
              disabled={!c.inMonth}
              onClick={() => c.iso && setSelected(c.iso)}
            >
              <span className="cal-daynum">{c.day || ""}</span>
              {(byDay[c.iso] ?? []).slice(0, 3).map((e) => (
                <span key={e.id} className="cal-ev" title={e.title}>
                  {e.status === "DONE" ? "✔" : e.status === "CANCELLED" ? "✕" : "•"} {e.title.slice(0, 16)}
                </span>
              ))}
              {(byDay[c.iso] ?? []).length > 3 && <span className="faint small">+{(byDay[c.iso] ?? []).length - 3} more</span>}
            </button>
          ))}
        </div>
      </div>

      <div className="grid grid-2 mt">
        {/* Selected day agenda */}
        <div className="card">
          <h3>Agenda — {selected}</h3>
          {!events ? <Spinner /> : dayEvents.length === 0 ? (
            <span className="faint">No events this day.</span>
          ) : (
            dayEvents.map((e) => (
              <div key={e.id} className="cal-agenda-row">
                <span className={`pill ${STATUS_PILL[e.status] ?? ""}`}>{e.status}</span>
                <div className="cal-agenda-body">
                  <b className="small">{e.title}</b>
                  <div className="faint small">
                    {e.startAt.slice(11, 16)}{e.endAt ? ` – ${e.endAt.slice(11, 16)}` : ""}{e.location ? ` · ${e.location}` : ""}
                  </div>
                  {e.description && <div className="small muted">{e.description}</div>}
                </div>
                <div className="row">
                  <button className="btn small" onClick={() => void toggleDone(e)} title={e.status === "DONE" ? "Reopen" : "Mark done"}>{e.status === "DONE" ? "↩" : "✔"}</button>
                  <button className="btn small danger" onClick={() => void remove(e.id)} title="Delete">✕</button>
                </div>
              </div>
            ))
          )}
        </div>

        {/* Quick add */}
        <div className="card">
          <h3>Add Event</h3>
          <label className="field">
            <span className="lbl">Title</span>
            <input type="text" value={title} onChange={(e) => setTitle(e.target.value)} placeholder="Founder sync, ZTame listening test…" />
          </label>
          <div className="grid grid-2">
            <label className="field">
              <span className="lbl">Start</span>
              <input type="datetime-local" value={startInput} onChange={(e) => setStartInput(e.target.value)} />
            </label>
            <label className="field">
              <span className="lbl">End (optional)</span>
              <input type="datetime-local" value={endInput} onChange={(e) => setEndInput(e.target.value)} />
            </label>
          </div>
          <label className="field">
            <span className="lbl">Location (optional)</span>
            <input type="text" value={location} onChange={(e) => setLocation(e.target.value)} placeholder="Discord / Zoom / In-person" />
          </label>
          <button className="btn primary" disabled={!title.trim() || !startInput} onClick={() => void add()}>Add to calendar</button>
        </div>
      </div>
    </div>
  );
}
