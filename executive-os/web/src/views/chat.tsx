/**
 * High-end Chat — three-panel founder console:
 *   left:   conversation sessions (new / switch / delete / history)
 *   center: streaming replies (SSE), markdown, avatars, timestamps, copy,
 *           thinking phases, stop, quick prompts + suggestion cards
 *   right:  live context rail (approvals, contradictions, daemon status)
 */
import { useCallback, useEffect, useRef, useState } from "react";
import { api, chatSse, type ChatSession, type Contradiction, type DataBundle } from "../api";
import { Badge, Markdown, timeAgo } from "../lib/entertain";

interface ViewProps {
  data: DataBundle;
  mutate: () => Promise<void>;
  notify: (msg: string, ok?: boolean) => void;
}

const SUGGESTIONS = [
  "Give me the daily brief",
  "What's blocking the ZTame P8 release?",
  "Summarize open contradictions",
  "What did the daemon work on today?",
];

const QUICK_PROMPTS = [
  "Daily brief",
  "Pipeline status",
  "Open approvals",
  "Productivity report",
  "System health",
];

type Msg = { role: "user" | "assistant"; content: string; created_at?: string };

export function ChatView({ data, mutate: _mutate, notify }: ViewProps) {
  const [sessions, setSessions] = useState<ChatSession[]>([]);
  const [sessionId, setSessionId] = useState<string>(() => localStorage.getItem("exec_os_chat_session") ?? "default");
  const [messages, setMessages] = useState<Msg[]>([]);
  const [historyLoaded, setHistoryLoaded] = useState(false);
  const [input, setInput] = useState("");
  const [busy, setBusy] = useState(false);
  const [streaming, setStreaming] = useState(false);
  const [elapsed, setElapsed] = useState(0);
  const [error, setError] = useState("");
  const [rail, setRail] = useState<"approvals" | "contradictions" | "daemon">("contradictions");
  const logRef = useRef<HTMLDivElement>(null);
  const abortRef = useRef<AbortController | null>(null);
  const sessionIdRef = useRef(sessionId);
  useEffect(() => { sessionIdRef.current = sessionId; }, [sessionId]);

  const loadSessions = useCallback(async () => {
    try {
      const r = await api.chatSessions();
      setSessions(r.sessions);
    } catch {
      /* non-fatal */
    }
  }, []);

  const loadHistory = useCallback(async (sid: string) => {
    try {
      const r = await api.chatMessages(sid);
      setMessages(r.messages.map((m) => ({ role: m.role, content: m.content, created_at: m.created_at })));
    } catch {
      setMessages([]);
    } finally {
      setHistoryLoaded(true);
    }
  }, []);

  useEffect(() => {
    void loadSessions();
  }, [loadSessions]);

  useEffect(() => {
    localStorage.setItem("exec_os_chat_session", sessionId);
    setHistoryLoaded(false);
    void loadHistory(sessionId);
  }, [sessionId, loadHistory]);

  useEffect(() => {
    if (busy) {
      const t = setInterval(() => setElapsed((e) => e + 1), 1000);
      return () => clearInterval(t);
    }
    setElapsed(0);
  }, [busy]);

  useEffect(() => {
    const el = logRef.current;
    if (!el) return;
    // Only auto-scroll when the founder is already near the bottom, so reading
    // earlier messages while streaming isn't yanked back down.
    const nearBottom = el.scrollHeight - el.scrollTop - el.clientHeight < 120;
    if (nearBottom) el.scrollTo({ top: el.scrollHeight, behavior: "smooth" });
  }, [messages, streaming]);

  // Auto-dismiss transient chat errors so the red box doesn't sit forever.
  useEffect(() => {
    if (!error) return;
    const t = setTimeout(() => setError(""), 8000);
    return () => clearTimeout(t);
  }, [error]);

  const send = async (text: string) => {
    const content = text.trim();
    if (!content || busy) return;
    const sid = sessionIdRef.current; // capture — the founder may switch sessions mid-stream
    setInput("");
    setError("");
    setMessages((m) => [...m, { role: "user", content }]);
    setBusy(true);
    setStreaming(true);
    const acc: string[] = [];
    abortRef.current = new AbortController();
    try {
      const start = await api.chatStreamStart(content, sid);
      setMessages((m) => [...m, { role: "assistant", content: "" }]);
      await chatSse(
        start.streamId,
        (chunk) => {
          if (sessionIdRef.current !== sid) return; // dropped — session changed
          acc.push(chunk);
          const joined = acc.join("");
          setMessages((m) => {
            const next = [...m];
            if (next.length > 0 && next[next.length - 1]!.role === "assistant") {
              next[next.length - 1] = { role: "assistant", content: joined };
            }
            return next;
          });
        },
        abortRef.current.signal,
      );
      await loadHistory(sid);
      await loadSessions();
    } catch (e) {
      const isAbort = e instanceof DOMException && e.name === "AbortError";
      if (isAbort) {
        // Stop pressed — intentional, not a failure.
        await loadHistory(sid);
        return;
      }
      const msg = e instanceof Error ? e.message : String(e);
      setError(msg);
      notify(`Chat failed: ${msg}`, false);
    } finally {
      setBusy(false);
      setStreaming(false);
      abortRef.current = null;
    }
  };

  const stop = () => {
    abortRef.current?.abort();
    setBusy(false);
    setStreaming(false);
  };

  const newChat = async () => {
    const sid = `chat_${Date.now().toString(36)}`;
    localStorage.setItem("exec_os_chat_session", sid);
    setSessionId(sid);
    setMessages([]);
    setHistoryLoaded(true);
    await loadSessions();
  };

  const deleteSession = async (sid: string) => {
    if (!window.confirm("Delete this conversation? This cannot be undone.")) return;
    try {
      await api.clearChat(sid);
      if (sid === sessionId) {
        await newChat();
      } else {
        await loadSessions();
      }
    } catch (e) {
      notify(e instanceof Error ? e.message : String(e), false);
    }
  };

  const copy = async (text: string) => {
    try {
      if (!navigator.clipboard) throw new Error("Clipboard unavailable");
      await navigator.clipboard.writeText(text);
      notify("Copied");
    } catch {
      notify("Couldn't copy to clipboard", false);
    }
  };

  const contradictions: Contradiction[] = data.productivity?.contradictions ?? [];

  return (
    <div className="chat3">
      {/* ---- sessions sidebar ---- */}
      <aside className="chat3-side">
        <div className="chat3-side-head">
          <button className="action" style={{ width: "100%" }} onClick={newChat}>+ New chat</button>
        </div>
        <div className="chat3-session-list">
          {sessions.length === 0 && <p className="muted" style={{ padding: 12 }}>No conversations yet.</p>}
          {sessions.map((s) => (
            <div key={s.session_id} className={`chat3-session ${s.session_id === sessionId ? "active" : ""}`} onClick={() => setSessionId(s.session_id)}>
              <div className="chat3-session-title">
                {s.last_message || s.session_id}
              </div>
              <div className="chat3-session-meta">
                {s.message_count} msg · {timeAgo(s.last_at)}
                <button
                  className="mini"
                  onClick={(e) => { e.stopPropagation(); void deleteSession(s.session_id); }}
                  title="Delete conversation"
                >
                  ✕
                </button>
              </div>
            </div>
          ))}
        </div>
      </aside>

      {/* ---- main chat ---- */}
      <section className="chat3-main">
        <div className="chat3-log" ref={logRef}>
          {!historyLoaded && messages.length === 0 && (
            <div className="muted" style={{ padding: 20 }}>Loading conversation…</div>
          )}
          {messages.length === 0 && historyLoaded && (
            <div className="chat-hero">
              <h2>Ask the Executive</h2>
              <p className="muted">Live tools, doctor, pipeline, finance and local work — streamed as it thinks.</p>
              <div className="suggestion-row">
                {SUGGESTIONS.map((s) => (
                  <button key={s} className="suggestion" onClick={() => send(s)}>{s}</button>
                ))}
              </div>
            </div>
          )}
          {messages.map((m, i) => (
            <div key={i} className={`msg3 ${m.role}`}>
              <div className="msg3-avatar">{m.role === "user" ? "🧑‍💼" : "🧠"}</div>
              <div className="msg3-body">
                <div className="msg3-meta">
                  <b>{m.role === "user" ? "Founder" : "Executive"}</b>
                  {m.created_at && <span className="muted">· {new Date(m.created_at).toLocaleTimeString()}</span>}
                </div>
                <div className="msg3-content">
                  {m.role === "assistant" ? <Markdown text={m.content} /> : <span>{m.content}</span>}
                </div>
                <div className="msg-tools">
                  <button className="mini" onClick={() => void copy(m.content)}>⧉ copy</button>
                </div>
              </div>
            </div>
          ))}
          {busy && (
            <div className="msg3 assistant">
              <div className="msg3-avatar">🧠</div>
              <div className="msg3-body">
                <div className="thinking">
                  <span className="dot" /><span className="dot" /><span className="dot" />
                  <span className="muted"> executive thinking… {elapsed}s</span>
                  {streaming && (
                    <button className="mini" onClick={stop}>■ stop</button>
                  )}
                </div>
              </div>
            </div>
          )}
        </div>

        {error && <div className="error-box">{error}</div>}

        {messages.length > 0 && !busy && (
          <div className="quick-row">
            {QUICK_PROMPTS.map((q) => (
              <button key={q} className="quick-chip" onClick={() => send(q)}>{q}</button>
            ))}
          </div>
        )}

        <div className="chat3-composer">
          <textarea
            value={input}
            onChange={(e) => setInput(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === "Enter" && !e.shiftKey) {
                e.preventDefault();
                void send(input);
              }
            }}
            placeholder="Message the Executive… (Enter to send)"
            disabled={busy}
            rows={2}
          />
          <button className="action" onClick={() => send(input)} disabled={busy || !input.trim()}>
            Send
          </button>
        </div>
      </section>

      {/* ---- context rail ---- */}
      <aside className="chat3-rail">
        <div className="row tabs" style={{ marginBottom: 8 }}>
          {(["contradictions", "approvals", "daemon"] as const).map((t) => (
            <button key={t} className={`ghost ${rail === t ? "active" : ""}`} onClick={() => setRail(t)} style={{ fontSize: 11 }}>
              {t}
            </button>
          ))}
        </div>

        {rail === "contradictions" && (
          <div className="rail-panel">
            <h4>Open contradictions ({contradictions.filter((c) => c.status === "OPEN").length})</h4>
            {contradictions.filter((c) => c.status === "OPEN").slice(0, 8).map((c) => (
              <div className="rail-item" key={c.id}>
                <span className="mono" style={{ fontSize: 11 }}>{c.kind}</span>
                <div className="muted">{c.claim_a}</div>
              </div>
            ))}
            {contradictions.filter((c) => c.status === "OPEN").length === 0 && <p className="muted">None.</p>}
          </div>
        )}

        {rail === "approvals" && <ApprovalsRail notify={notify} />}

        {rail === "daemon" && (
          <div className="rail-panel">
            <h4>Local daemon</h4>
            <Badge tone={(data.localAgent as { online?: boolean } | undefined)?.online ? "ok" : "fail"}>
              {(data.localAgent as { online?: boolean } | undefined)?.online ? "ONLINE" : "OFFLINE"}
            </Badge>
            <p className="muted">
              host {(data.localAgent as { host?: string | null } | undefined)?.host ?? "—"} ·{" "}
              {(data.localAgent as { pending?: number } | undefined)?.pending ?? 0} pending
            </p>
            {(data.localTasks ?? []).filter((t) => String(t.status) === "DONE" || String(t.status) === "RUNNING" || String(t.status) === "CLAIMED")
              .slice(0, 6)
              .map((t, i) => (
                <div className="rail-item" key={i}>
                  <span className={`badge ${String(t.status) === "DONE" ? "ok" : "warn"}`}>{String(t.status)}</span>
                  <div className="muted">{String(t.prompt ?? "").slice(0, 90)}</div>
                </div>
              ))}
          </div>
        )}
      </aside>
    </div>
  );
}

// ---- approvals rail ---------------------------------------------------------

function ApprovalsRail({ notify }: { notify: (m: string, ok?: boolean) => void }) {
  const [list, setList] = useState<Array<Record<string, unknown>>>([]);
  useEffect(() => {
    void (async () => {
      try {
        const r = await api.approvals();
        setList((r.approvals as unknown as Array<Record<string, unknown>>).filter((a) => String(a.status) === "PENDING"));
      } catch {
        /* non-fatal */
      }
    })();
  }, []);
  const decide = async (id: string, d: string) => {
    try {
      await api.decideApproval(id, d);
      setList((l) => l.filter((a) => String(a.id) !== id));
      notify(`Approval ${d.toLowerCase()}`);
    } catch (e) {
      notify(e instanceof Error ? e.message : String(e), false);
    }
  };
  return (
    <div className="rail-panel">
      <h4>Pending approvals ({list.length})</h4>
      {list.length === 0 ? <p className="muted">None.</p> : list.map((a, i) => (
        <div className="rail-item" key={i}>
          <div>{String(a.title ?? a.action_label ?? "")}</div>
          <div className="row" style={{ marginTop: 4 }}>
            <button className="mini" onClick={() => decide(String(a.id), "APPROVED")}>Approve</button>
            <button className="mini" onClick={() => decide(String(a.id), "REJECTED")}>Reject</button>
          </div>
        </div>
      ))}
    </div>
  );
}
