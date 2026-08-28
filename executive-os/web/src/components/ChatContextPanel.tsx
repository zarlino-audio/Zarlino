/**
 * Chat Context Panel — the Executive's live sidebar while you talk to it.
 *
 * Consolidates the data the Founder most often needs mid-conversation into one
 * always-available rail so you don't have to bounce between tabs:
 *   🖊️ Approvals  — pending founder decisions, approve/reject inline
 *   🧠 Memory     — what the Executive remembers (recent)
 *   📜 Doctrine   — the governing principles (L3)
 *   📈 Activity   — recent actions the Executive took
 *   ✅ Tasks      — open work, with a jump to the full Tasks view
 *
 * Each section also links to its full tab. Refreshes every 30s + on demand.
 */
import { useCallback, useEffect, useState } from "react";
import { api, type Approval, type AuditEvent, type DoctrineRule, type MemoryEntry, type Task } from "../api";
import type { ViewKey } from "../App";
import { decodeEntities } from "./Markdown";

type TabKey = "approvals" | "memory" | "doctrine" | "activity" | "tasks";

const TABS: Array<{ key: TabKey; label: string; icon: string }> = [
  { key: "approvals", label: "Approvals", icon: "🖊️" },
  { key: "memory", label: "Memory", icon: "🧠" },
  { key: "doctrine", label: "Doctrine", icon: "📜" },
  { key: "activity", label: "Activity", icon: "📈" },
  { key: "tasks", label: "Tasks", icon: "✅" },
];

const TAB_VIEW: Record<TabKey, ViewKey> = {
  approvals: "approvals",
  memory: "memory",
  doctrine: "doctrine",
  activity: "activity",
  tasks: "tasks",
};

const fmtRel = (iso: string): string => {
  const d = Date.now() - Date.parse(iso);
  if (!Number.isFinite(d)) return "";
  if (d < 60_000) return "just now";
  if (d < 3_600_000) return `${Math.floor(d / 60_000)}m ago`;
  if (d < 86_400_000) return `${Math.floor(d / 3_600_000)}h ago`;
  return `${Math.floor(d / 86_400_000)}d ago`;
};

const STATUS_CLASS: Record<string, string> = { OPEN: "green", IN_PROGRESS: "yellow", BLOCKED: "red", DONE: "" };

export function ChatContextPanel({
  onNavigate,
  onClose,
  className,
}: {
  onNavigate: (v: ViewKey) => void;
  onClose?: () => void;
  className?: string;
}) {
  const [tab, setTab] = useState<TabKey>("approvals");
  const [approvals, setApprovals] = useState<Approval[] | null>(null);
  const [memory, setMemory] = useState<MemoryEntry[] | null>(null);
  const [doctrine, setDoctrine] = useState<DoctrineRule[] | null>(null);
  const [activity, setActivity] = useState<AuditEvent[] | null>(null);
  const [tasks, setTasks] = useState<Task[] | null>(null);
  const [note, setNote] = useState<Record<string, string>>({});
  const [err, setErr] = useState<string | null>(null);

  const load = useCallback(async () => {
    try {
      const [a, m, d, ac, t] = await Promise.all([
        api.approvals(),
        api.memory(),
        api.doctrine(),
        api.activity(),
        api.tasks(),
      ]);
      setApprovals(a);
      setMemory(m);
      setDoctrine(d);
      setActivity(ac);
      setTasks(t);
      setErr(null);
    } catch (e) {
      setErr(e instanceof Error ? e.message : String(e));
    }
  }, []);

  useEffect(() => {
    void load();
    const t = setInterval(() => { if (!document.hidden) void load(); }, 60_000);
    return () => clearInterval(t);
  }, [load]);

  const decide = async (id: string, action: "approve" | "reject") => {
    try {
      if (action === "approve") await api.approve(id, note[id]);
      else await api.reject(id, note[id]);
      setNote((n) => ({ ...n, [id]: "" }));
      await load();
    } catch (e) {
      setErr(e instanceof Error ? e.message : String(e));
    }
  };

  const pending = (approvals ?? []).filter((a) => a.status === "PENDING");
  const openTasks = (tasks ?? []).filter((t) => ["OPEN", "IN_PROGRESS", "BLOCKED"].includes(t.status));
  const activeDoctrine = (doctrine ?? []).filter((r) => r.status === "ACTIVE");
  const recentMem = (memory ?? []).slice().sort((a, b) => b.importance - a.importance || b.created_at.localeCompare(a.created_at)).slice(0, 6);
  const recentAct = (activity ?? []).slice(0, 8);

  const badges: Partial<Record<TabKey, number>> = {
    approvals: pending.length,
    tasks: openTasks.length,
  };

  const fullLink = (label: string) => (
    <button className="ctx-full" onClick={() => onNavigate(TAB_VIEW[tab])}>
      Open full {label} view →
    </button>
  );

  return (
    <aside className={`ctx-panel ${className ?? ""}`}>
      <div className="ctx-head">
        <span className="ctx-title">Live context</span>
        <div className="row" style={{ gap: 6 }}>
          <button className="btn small" onClick={() => void load()} title="Refresh context">↻</button>
          {onClose && (
            <button className="btn small ctx-close" onClick={onClose} title="Close context panel">✕ Close</button>
          )}
        </div>
      </div>
      <div className="ctx-tabs">
        {TABS.map((t) => (
          <button key={t.key} className={`ctx-tab ${tab === t.key ? "active" : ""}`} onClick={() => setTab(t.key)}>
            <span>{t.icon}</span>
            <span>{t.label}</span>
            {(badges[t.key] ?? 0) > 0 && <span className="ctx-badge">{badges[t.key]}</span>}
          </button>
        ))}
      </div>
      <div className="ctx-body">
        {err && <div className="small error">{err}</div>}

        {tab === "approvals" && (
          <div className="ctx-list">
            {approvals === null ? (
              <span className="faint small">loading…</span>
            ) : pending.length === 0 ? (
              <div className="ctx-empty">✓ No decisions waiting on you.</div>
            ) : (
              pending.map((a) => (
                <div key={a.id} className="ctx-item">
                  <div className="ctx-item-head">
                    <b className="small">{decodeEntities(a.action_label)}</b>
                    <span className="pill yellow">PENDING</span>
                  </div>
                  <p className="small muted">{decodeEntities(a.reason)}</p>
                  <div className="small faint">Rec: {decodeEntities(a.recommendation)}</div>
                  <input
                    type="text"
                    className="mt"
                    placeholder="Note (optional)"
                    value={note[a.id] ?? ""}
                    onChange={(e) => setNote((n) => ({ ...n, [a.id]: e.target.value }))}
                  />
                  <div className="row mt">
                    <button className="btn small primary" onClick={() => void decide(a.id, "approve")}>Approve</button>
                    <button className="btn small danger" onClick={() => void decide(a.id, "reject")}>Reject</button>
                  </div>
                </div>
              ))
            )}
            {fullLink("approvals")}
          </div>
        )}

        {tab === "memory" && (
          <div className="ctx-list">
            {memory === null ? (
              <span className="faint small">loading…</span>
            ) : recentMem.length === 0 ? (
              <div className="ctx-empty">No memory yet — the Executive learns as it works.</div>
            ) : (
              recentMem.map((e) => (
                <div key={e.id} className="ctx-item">
                  <div className="ctx-item-head">
                    <b className="small">{decodeEntities(e.title)}</b>
                    <span className="mono faint">{e.layer}/{e.kind}</span>
                  </div>
                  <p className="small muted">{(decodeEntities(e.body).length > 140 ? `${decodeEntities(e.body).slice(0, 140)}…` : decodeEntities(e.body))}</p>
                </div>
              ))
            )}
            {fullLink("memory")}
          </div>
        )}

        {tab === "doctrine" && (
          <div className="ctx-list">
            {doctrine === null ? (
              <span className="faint small">loading…</span>
            ) : activeDoctrine.length === 0 ? (
              <div className="ctx-empty">No active doctrine.</div>
            ) : (
              activeDoctrine.slice(0, 6).map((r) => (
                <div key={r.id} className="ctx-item">
                  <div className="ctx-item-head">
                    <span className="mono small">{r.code} v{r.version}</span>
                    <span className="faint small">{r.category}</span>
                  </div>
                  <p className="small">{decodeEntities(r.principle)}</p>
                </div>
              ))
            )}
            {fullLink("doctrine")}
          </div>
        )}

        {tab === "activity" && (
          <div className="ctx-list">
            {activity === null ? (
              <span className="faint small">loading…</span>
            ) : recentAct.length === 0 ? (
              <div className="ctx-empty">No activity yet.</div>
            ) : (
              recentAct.map((e) => (
                <div key={e.id} className="ctx-item ctx-row">
                  <span className="ctx-act-ico">{(e.action || "").startsWith("ai.") ? "🤖" : (e.action || "").startsWith("task") ? "✅" : "⚙️"}</span>
                  <div className="ctx-act-body">
                    <div className="small">{decodeEntities(e.action)}</div>
                    <div className="faint small">{decodeEntities(e.actor)} · {fmtRel(e.created_at)}</div>
                  </div>
                </div>
              ))
            )}
            {fullLink("activity")}
          </div>
        )}

        {tab === "tasks" && (
          <div className="ctx-list">
            {tasks === null ? (
              <span className="faint small">loading…</span>
            ) : openTasks.length === 0 ? (
              <div className="ctx-empty">No open tasks. All clear.</div>
            ) : (
              openTasks.slice(0, 6).map((t) => (
                <div key={t.id} className="ctx-item">
                  <div className="ctx-item-head">
                    <b className="small">{decodeEntities(t.title)}</b>
                    <span className={`pill ${STATUS_CLASS[t.status] ?? ""}`}>{t.status.replace("_", " ")}</span>
                  </div>
                  <div className="faint small">priority {t.priority} · {fmtRel(t.created_at)}</div>
                </div>
              ))
            )}
            {fullLink("tasks")}
          </div>
        )}
      </div>
    </aside>
  );
}
