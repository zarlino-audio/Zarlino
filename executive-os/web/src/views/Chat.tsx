import { useCallback, useEffect, useRef, useState } from "react";
import { api, type ChatSession, type Overview, type UploadedFile } from "../api";
import { getChatSession, setChatSession } from "../App";
import type { ViewKey } from "../App";
import { Confetti } from "../components/Confetti";
import { Markdown, decodeEntities } from "../components/Markdown";
import { ChatContextPanel } from "../components/ChatContextPanel";
import { ABCompareCard, audioUrlFor } from "../components/ABCompare";

interface Msg {
  id: string;
  role: "founder" | "executive";
  content: string;
  time?: string;
  streaming?: boolean;
  files?: UploadedFile[];
  cards?: QuestionCard[];
}

/** A follow-up question/action card the Executive attaches to a reply. */
export interface QuestionCard {
  label: string;
  prompt: string;
}

/**
 * Extract the agent's optional [QUESTION_CARDS] block from a reply.
 * Returns the display content (marker stripped) + parsed cards.
 */
export function parseQuestionCards(content: string): { text: string; cards: QuestionCard[] } {
  const m = content.match(/\[QUESTION_CARDS\]([\s\S]*?)\[\/QUESTION_CARDS\]/);
  if (!m) return { text: content, cards: [] };
  try {
    const data = JSON.parse(m[1].trim()) as { questions?: Array<{ label?: string; prompt?: string }> };
    const cards = (data.questions ?? [])
      .map((q) => ({
        label: decodeEntities((q.label ?? "").trim()),
        prompt: decodeEntities((q.prompt ?? q.label ?? "").trim()),
      }))
      .filter((c) => c.label.length > 0 && c.prompt.length > 0)
      .slice(0, 3);
    return { text: content.replace(m[0], "").trim(), cards };
  } catch {
    // Malformed block — hide it from display so it never leaks into prose.
    return { text: content.replace(m[0], "").trim(), cards: [] };
  }
}

const SUGGESTIONS = [
  "What should I focus on today?",
  "Can we afford another employee?",
  "What is blocking the launch?",
  "Give me the three biggest risks.",
  "Should we spend $250 validating affiliate acquisition?",
  "Review the current engineering situation.",
  "Give me a 5-second status haiku.",
];

/** Always-visible quick prompts above the composer (no scrolling needed). */
const QUICK_PROMPTS = [
  "Status check — where are we today?",
  "What's the #1 risk right now?",
  "Summarize the pipeline for me.",
];

const QUICK_ACTIONS: Array<{ label: string; icon: string; nav: ViewKey }> = [
  { label: "Overview", icon: "📡", nav: "overview" },
  { label: "Decisions", icon: "🧭", nav: "decisions" },
  { label: "Pipeline", icon: "🛰️", nav: "pipeline" },
  { label: "Finance", icon: "💰", nav: "finance" },
  { label: "Settings", icon: "⚙️", nav: "settings" },
];

/** Rotating "thinking" phrases shown while the Executive works. */
const THINKING = [
  "Consulting the lenses…",
  "Checking founder doctrine…",
  "Warming up DeepSeek cores…",
  "Reading the pipeline…",
  "Doing the reversible math…",
  "Asking the CFO lens…",
  "Querying the state machine…",
  "Polishing the answer…",
];

const KIND_ICON: Record<string, string> = { image: "🖼️", text: "📄", audio: "🎧", other: "📦" };
const fmtSize = (b: number) => (b >= 1024 * 1024 ? `${(b / 1024 / 1024).toFixed(1)} MB` : `${Math.max(1, Math.round(b / 1024))} KB`);
const fmtTime = (iso?: string) => (iso ? new Date(iso).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" }) : "");
const fmtRel = (iso: string): string => {
  const d = Date.now() - Date.parse(iso);
  if (!Number.isFinite(d)) return "";
  if (d < 60_000) return "just now";
  if (d < 3_600_000) return `${Math.floor(d / 60_000)}m ago`;
  if (d < 86_400_000) return `${Math.floor(d / 3_600_000)}h ago`;
  return `${Math.floor(d / 86_400_000)}d ago`;
};
let msgSeq = 0;
const nextMsgId = () => `m_${Date.now()}_${msgSeq++}`;

const STATUS_CLASS: Record<string, string> = { GREEN: "green", YELLOW: "yellow", RED: "red" };

/** Derive a readable conversation title from the first founder message. */
const deriveTitle = (msgs: Msg[]): string => {
  const first = msgs.find((m) => m.role === "founder");
  const raw = first?.content ?? "";
  const clean = decodeEntities(raw.replace(/\[Attached files[\s\S]*$/, "").trim());
  return clean.length > 44 ? `${clean.slice(0, 44)}…` : clean || "Untitled";
};

export function ChatView({
  overview,
  onNavigate,
}: {
  overview: Overview | null;
  onNavigate: (v: ViewKey) => void;
}) {
  const [messages, setMessages] = useState<Msg[]>([]);
  const [sessions, setSessions] = useState<ChatSession[] | null>(null);
  const [input, setInput] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [attachments, setAttachments] = useState<UploadedFile[]>([]);
  const [uploading, setUploading] = useState(false);
  const [celebrate, setCelebrate] = useState(0);
  const [thinkIdx, setThinkIdx] = useState(0);
  const [showSessions, setShowSessions] = useState(false);
  const [showContext, setShowContext] = useState(false);
  const [copiedId, setCopiedId] = useState<string | null>(null);
  const [thinkSecs, setThinkSecs] = useState(0);
  const [scrolledUp, setScrolledUp] = useState(false);
  const [showAB, setShowAB] = useState(false);
  const [abUrls, setAbUrls] = useState<Record<string, string>>({});
  const fileRef = useRef<HTMLInputElement>(null);
  const textRef = useRef<HTMLTextAreaElement>(null);
  const logRef = useRef<HTMLDivElement>(null);
  const dropRef = useRef<HTMLDivElement>(null);

  // Rotate the thinking phrase while busy.
  useEffect(() => {
    if (!busy) return;
    setThinkSecs(0);
    const t = setInterval(() => setThinkIdx((i) => (i + 1) % THINKING.length), 2200);
    const secs = setInterval(() => setThinkSecs((s) => s + 1), 1000);
    return () => {
      clearInterval(t);
      clearInterval(secs);
    };
  }, [busy]);

  // Track whether the log is scrolled above the latest message, so a
  // "jump to latest" button appears for people who can't scroll easily.
  useEffect(() => {
    const el = logRef.current;
    if (!el) return;
    const onScroll = () => {
      const nearBottom = el.scrollHeight - el.scrollTop - el.clientHeight < 120;
      setScrolledUp(!nearBottom);
    };
    el.addEventListener("scroll", onScroll, { passive: true });
    return () => el.removeEventListener("scroll", onScroll);
  }, []);

  const loadSessions = useCallback(async () => {
    try {
      setSessions(await api.chatSessions());
    } catch {
      /* non-fatal */
    }
  }, []);

  // Load history for the persisted session on mount (chat transcripts survive reloads).
  useEffect(() => {
    const session = getChatSession();
    if (session) {
      api.chatMessages(session)
        .then((msgs) => setMessages(msgs.map((m) => {
          const parsed = m.role === "executive" ? parseQuestionCards(m.content) : { text: m.content, cards: [] };
          return { id: nextMsgId(), role: m.role, content: parsed.text, time: m.created_at, cards: parsed.cards };
        })))
        .catch(() => {});
    }
    void loadSessions();
  }, [loadSessions]);

  // Close the session dropdown on outside click.
  useEffect(() => {
    const onDown = (e: MouseEvent) => {
      if (dropRef.current && !dropRef.current.contains(e.target as Node)) setShowSessions(false);
    };
    window.addEventListener("mousedown", onDown);
    return () => window.removeEventListener("mousedown", onDown);
  }, []);

  useEffect(() => {
    if (logRef.current) logRef.current.scrollTop = logRef.current.scrollHeight;
  }, [messages, busy]);

  // Auto-grow the composer textarea.
  const autoGrow = () => {
    const el = textRef.current;
    if (!el) return;
    el.style.height = "auto";
    el.style.height = `${Math.min(160, el.scrollHeight)}px`;
  };
  useEffect(autoGrow, [input]);

  const newChat = () => {
    if (busy) return;
    setMessages([]);
    setError(null);
    setAttachments([]);
    setChatSession("");
    setShowSessions(false);
    void loadSessions();
  };

  /** Read a selected file, base64 it, upload it, and attach to the pending message. */
  const attachFile = async (file: File) => {
    if (busy || uploading) return;
    if (file.size > 5 * 1024 * 1024) {
      setError("File too large — max 5 MB.");
      return;
    }
    setUploading(true);
    setError(null);
    try {
      const buf = await file.arrayBuffer();
      const bytes = new Uint8Array(buf);
      let binary = "";
      const CHUNK = 0x8000;
      for (let i = 0; i < bytes.length; i += CHUNK) {
        binary += String.fromCharCode(...bytes.subarray(i, i + CHUNK));
      }
      const b64 = btoa(binary);
      const result = await api.fileUpload(file.name, file.type || "application/octet-stream", b64);
      const uploaded: UploadedFile = {
        id: result.fileId,
        name: file.name,
        mime: file.type || "application/octet-stream",
        size_bytes: result.sizeBytes,
        kind: result.kind as UploadedFile["kind"],
        created_at: new Date().toISOString(),
      };
      setAttachments((a) => [...a, uploaded]);
      setCelebrate((c) => c + 1);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setUploading(false);
      if (fileRef.current) fileRef.current.value = "";
    }
  };

  const removeAttachment = (id: string) => setAttachments((a) => a.filter((f) => f.id !== id));

  const clear = async () => {
    if (busy) return;
    const session = getChatSession();
    if (session) {
      try {
        await api.chatClear(session);
      } catch {
        /* non-fatal */
      }
    }
    newChat();
  };

  const openSession = async (sessionId: string) => {
    if (busy || !sessionId) return;
    try {
      const msgs = await api.chatMessages(sessionId);
      setMessages(msgs.map((m) => {
        const parsed = m.role === "executive" ? parseQuestionCards(m.content) : { text: m.content, cards: [] };
        return { id: nextMsgId(), role: m.role, content: parsed.text, time: m.created_at, cards: parsed.cards };
      }));
      setChatSession(sessionId);
      setError(null);
      setShowSessions(false);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  const copyMessage = async (id: string, content: string) => {
    try {
      await navigator.clipboard.writeText(content);
      setCopiedId(id);
      setTimeout(() => setCopiedId((c) => (c === id ? null : c)), 1400);
    } catch {
      /* non-fatal */
    }
  };

  /** Scroll back to the latest message (for users who can't scroll easily). */
  const jumpToLatest = () => {
    const el = logRef.current;
    if (el) {
      el.scrollTo({ top: el.scrollHeight, behavior: "smooth" });
      setScrolledUp(false);
    }
  };

  const send = async (text?: string) => {
    const message = (text ?? input).trim();
    if ((!message && attachments.length === 0) || busy) return;
    setInput("");
    setBusy(true);
    setError(null);
    const withFiles = attachments.length > 0
      ? `${message}\n\n[Attached files for you to inspect via executive.get_file / executive.list_files: ${attachments.map((f) => `${f.name} (fileId=${f.id}, ${f.kind}, ${fmtSize(f.size_bytes)})`).join(", ")}]`
      : message;
    setMessages((m) => [...m, { id: nextMsgId(), role: "founder", content: withFiles, files: attachments, time: new Date().toISOString() }]);
    setAttachments([]);
    // Placeholder executive bubble that streams into.
    setMessages((m) => [...m, { id: nextMsgId(), role: "executive", content: "", streaming: true, time: new Date().toISOString() }]);
    let session = getChatSession() || undefined;
    try {
      await api.chatStream(withFiles, session, {
        onMeta: (sid) => {
          session = sid;
          setChatSession(sid);
        },
        onDelta: (text) => {
          setMessages((m) => {
            const next = [...m];
            const last = next[next.length - 1];
            if (last && last.role === "executive" && last.streaming) {
              next[next.length - 1] = { ...last, content: last.content + text };
            } else {
              next.push({ id: nextMsgId(), role: "executive", content: text, streaming: true, time: new Date().toISOString() });
            }
            return next;
          });
        },
        onDone: () => {
          setMessages((m) => {
            const next = [...m];
            const last = next[next.length - 1];
            if (last && last.role === "executive") {
              const parsed = parseQuestionCards(last.content);
              next[next.length - 1] = { ...last, streaming: false, content: parsed.text, cards: parsed.cards };
            }
            return next;
          });
          void loadSessions();
        },
        onError: (msg) => {
          setError(msg);
          setMessages((m) => {
            const next = [...m];
            const last = next[next.length - 1];
            if (last && last.role === "executive" && last.streaming && last.content === "") {
              next.pop();
            } else if (last && last.role === "executive") {
              next[next.length - 1] = { ...last, streaming: false };
            }
            return next;
          });
        },
      });
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
      setMessages((m) => {
        const next = [...m];
        const last = next[next.length - 1];
        if (last && last.role === "executive" && last.streaming) {
          next[next.length - 1] = { ...last, streaming: false };
        }
        return next;
      });
    } finally {
      setBusy(false);
      if (textRef.current) textRef.current.style.height = "auto";
    }
  };

  const currentSession = getChatSession();
  const spent = overview?.aiSpendToday ?? 0;
  const daily = overview?.aiDailyBudget ?? 8;
  const fuelPct = daily > 0 ? Math.min(100, Math.round((spent / daily) * 100)) : 0;

  return (
    <div className="chat">
      {celebrate > 0 && <Confetti trigger={celebrate} />}

      {/* Context strip — live state at a glance, clickable to jump. */}
      <div className="ctx-strip">
        {overview && (
          <>
            <button className="ctx-chip" onClick={() => onNavigate("overview")} title="Executive status">
              <span className={`pill ${STATUS_CLASS[overview.status] ?? "yellow"}`}>
                <span className="dot" />{overview.status}
              </span>
            </button>
            {overview.currentObjective && (
              <button className="ctx-chip" onClick={() => onNavigate("overview")} title="Current objective">
                🎯 <span className="ctx-chip-txt">{overview.currentObjective}</span>
              </button>
            )}
            <button className="ctx-chip" onClick={() => onNavigate("tasks")} title="Open tasks">
              ✅ {overview.openTasks} open
            </button>
            <button className="ctx-chip" onClick={() => onNavigate("approvals")} title="Pending approvals">
              🖊️ {overview.pendingApprovals} approval{overview.pendingApprovals === 1 ? "" : "s"}
            </button>
            <button className="ctx-chip" onClick={() => onNavigate("finance")} title="AI budget used today">
              ⛽ {fuelPct}% of daily AI budget
            </button>
          </>
        )}
        <span className="spacer" />
        {overview?.lastActivity && <span className="faint small ctx-strip-live">live · {fmtRel(overview.lastActivity)}</span>}
      </div>

      <div className="chat-shell">
        {/* Main chat column */}
        <div className="chat-main">
          <div className="chat-toolbar">
            <button className="btn small" onClick={() => setShowSessions((s) => !s)} title="Past conversations">
              🕘 History{sessions && sessions.length > 0 ? ` (${sessions.length})` : ""}
            </button>
            <button
              className={`btn small ${showAB ? "active" : ""}`}
              onClick={() => setShowAB((s) => !s)}
              title="Listen to A/B audio renders and pick a side"
            >
              🎧 A/B
            </button>
            <button className="btn small" disabled={busy} onClick={newChat} title="Start a new conversation">New Chat</button>
            {messages.length > 0 && (
              <button className="btn small danger" disabled={busy} onClick={() => void clear()} title="Delete this conversation">Clear</button>
            )}
            {messages.length > 0 && <span className="chat-title" title="This conversation">{deriveTitle(messages)}</span>}
            <span className="spacer" />
            <button
              className={`btn small ctx-toggle ${showContext ? "active" : ""}`}
              onClick={() => setShowContext((s) => !s)}
              title="Toggle live context panel"
            >
              ☰ Context
            </button>

            {showSessions && (
              <div className="session-drop" ref={dropRef}>
                <div className="session-drop-head">
                  Recent conversations
                  <button className="btn small" onClick={() => void loadSessions()} title="Refresh history">↻</button>
                </div>
                <button className="session-row session-new" onClick={newChat}>
                  <span className="session-ico">＋</span>
                  <span>New conversation</span>
                </button>
                {!sessions ? (
                  <div className="faint small" style={{ padding: 8 }}>loading…</div>
                ) : sessions.length === 0 ? (
                  <div className="faint small" style={{ padding: 8 }}>No past conversations yet.</div>
                ) : (
                  sessions.map((s) => (
                    <div key={s.session_id} className="session-row">
                      <button className="session-open" onClick={() => void openSession(s.session_id)}>
                        <div className="session-title">{decodeEntities(s.last_message) || "Untitled"}</div>
                        <div className="session-meta">{fmtRel(s.last_at)} · {s.message_count} msgs</div>
                      </button>
                      <button
                        className="session-del"
                        title="Delete this conversation"
                        onClick={() => void (async () => {
                          try {
                            await api.chatClear(s.session_id);
                            if (getChatSession() === s.session_id) { setMessages([]); setChatSession(""); }
                            await loadSessions();
                          } catch { /* non-fatal */ }
                        })()}
                      >
                        ✕
                      </button>
                    </div>
                  ))
                )}
              </div>
            )}
          </div>

          {showAB && (
            <div className="ab-slot">
              <ABCompareCard send={send} busy={busy} />
            </div>
          )}

          <div className="chat-log" ref={logRef}>
            {messages.length === 0 && (
              <div className="empty-hero">
                <div className="empty-hero-avatar">🎛️</div>
                <h2>Ask the Executive</h2>
                {overview?.currentObjective ? (
                  <p className="muted">
                    Working toward <b>{overview.currentObjective}</b> · status{" "}
                    <span className={`pill ${STATUS_CLASS[overview.status] ?? "yellow"}`}>{overview.status}</span>
                  </p>
                ) : (
                  <p className="muted">The Executive uses your live state, memory, and Founder Doctrine — not generic model knowledge.</p>
                )}
                <div className="chip-row">
                  {SUGGESTIONS.map((s) => (
                    <button key={s} disabled={busy} onClick={() => void send(s)}>{s}</button>
                  ))}
                </div>
                <div className="quick-actions">
                  <span className="faint small">Jump to</span>
                  {QUICK_ACTIONS.map((q) => (
                    <button key={q.nav} className="btn small" onClick={() => onNavigate(q.nav)}>
                      {q.icon} {q.label}
                    </button>
                  ))}
                </div>
                <p className="faint small">
                  Attach a screenshot or spec (📎) and the Executive can inspect it — images get analyzed with vision.
                </p>
              </div>
            )}
            {messages.map((m) => (
              <div key={m.id} className={`msg ${m.role}`}>
                <div className="msg-head">
                  <span className="msg-avatar">{m.role === "founder" ? "👤" : "🎛️"}</span>
                  <span className="who">{m.role === "founder" ? "Founder" : "Executive"}</span>
                  <span className="msg-time">{fmtTime(m.time)}</span>
                  {m.role === "executive" && !m.streaming && m.content && (
                    <button
                      className={`msg-copy ${copiedId === m.id ? "copied" : ""}`}
                      onClick={() => void copyMessage(m.id, m.content)}
                      title="Copy reply"
                    >
                      {copiedId === m.id ? "✓" : "⧉"}
                    </button>
                  )}
                </div>
                {m.files && m.files.length > 0 && (
                  <div className="row mb">
                    {m.files.map((f) => (
                      <span key={f.id} className="file-chip">
                        <span className="fc-ico">{KIND_ICON[f.kind] ?? "📦"}</span>
                        <span className="fc-name">{f.name}</span>
                        {f.kind === "audio" && <AudioInline file={f} urls={abUrls} onUrl={(u) => setAbUrls((m2) => ({ ...m2, [f.id]: u }))} />}
                      </span>
                    ))}
                  </div>
                )}
                {m.role === "executive" ? (
                  <>
                    {m.content.length > 0 && <Markdown text={m.content} />}
                    {m.streaming && (
                      <>
                        <span className="cursor" aria-label="streaming" />
                        <span className="thinking-secs">{thinkSecs}s</span>
                      </>
                    )}
                    {!m.streaming && m.cards && m.cards.length > 0 && (
                      <div className="qcards">
                        {m.cards.map((c, i) => (
                          <button key={i} className="qcard" disabled={busy} onClick={() => void send(c.prompt)} title={c.prompt}>
                            {c.label}
                          </button>
                        ))}
                      </div>
                    )}
                  </>
                ) : (
                  <div className="msg-body">{decodeEntities(m.content)}</div>
                )}
              </div>
            ))}
            {busy && messages.length === 0 && (
              <div className="msg executive thinking">
                <div className="msg-head">
                  <span className="msg-avatar">🎛️</span>
                  <span className="who">Executive</span>
                  <span className="thinking-secs">{thinkSecs}s</span>
                </div>
                <div className="thinking-row">
                  <span className="dots"><i /><i /><i /></span>
                  <span className="thinking-phrase" key={thinkIdx}>{THINKING[thinkIdx]}</span>
                </div>
              </div>
            )}
            {error && <div className="error">{error}</div>}
          </div>

          {scrolledUp && (
            <button className="jump-latest" onClick={jumpToLatest} title="Jump to the latest message">
              ↓ Latest
            </button>
          )}

          {messages.length > 0 && !busy && (
            <div className="quick-prompts">
              <span className="faint small">Quick:</span>
              {QUICK_PROMPTS.map((q) => (
                <button key={q} className="qp-chip" onClick={() => void send(q)}>{q}</button>
              ))}
            </div>
          )}

          <div className="chat-input">
            {attachments.length > 0 && (
              <div className="row" style={{ width: "100%" }}>
                {attachments.map((f) => (
                  <span key={f.id} className="file-chip">
                    <span className="fc-ico">{KIND_ICON[f.kind] ?? "📦"}</span>
                    <span className="fc-name">{f.name}</span>
                    <button onClick={() => removeAttachment(f.id)} title="Remove">✕</button>
                  </span>
                ))}
              </div>
            )}
            <input
              type="file"
              ref={fileRef}
              hidden
              onChange={(e) => { const f = e.target.files?.[0]; if (f) void attachFile(f); }}
            />
            <textarea
              ref={textRef}
              rows={1}
              placeholder="Ask the Executive…  (Enter to send · Shift+Enter for a new line)"
              value={input}
              onChange={(e) => setInput(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === "Enter" && !e.shiftKey) {
                  e.preventDefault();
                  void send();
                }
              }}
              disabled={busy}
            />
            <button
              className="btn"
              disabled={busy || uploading}
              onClick={() => fileRef.current?.click()}
              title="Attach a file (image, spec, doc) — the Executive can inspect it"
            >
              {uploading ? "…" : "📎"}
            </button>
            <button className="btn primary" disabled={busy} onClick={() => void send()}>Send</button>
          </div>
        </div>

        {/* Consolidated context rail — desktop inline, mobile drawer. */}
        <ChatContextPanel onNavigate={onNavigate} onClose={() => setShowContext(false)} className={showContext ? "open" : ""} />
      </div>
    </div>
  );
}

/**
 * Inline audio player for an uploaded audio file inside a chat message.
 * Fetches the stored base64 once, builds an object URL, and plays.
 */
function AudioInline({
  file,
  urls,
  onUrl,
}: {
  file: UploadedFile;
  urls: Record<string, string>;
  onUrl: (url: string) => void;
}) {
  const url = urls[file.id];
  const [err, setErr] = useState(false);
  useEffect(() => {
    if (url) return;
    audioUrlFor(file)
      .then(onUrl)
      .catch(() => setErr(true));
  }, [file.id, url]);
  if (err) return <span className="faint small"> (unavailable)</span>;
  if (!url) return <span className="faint small"> (loading…)</span>;
  return (
    <span className="msg-audio">
      <audio controls preload="none" src={url} style={{ height: 32 }} />
    </span>
  );
}
