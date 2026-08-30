/** Polished Chat view: bubbles, thinking indicator, quick prompts, markdown, suggestions. */
import { useEffect, useRef, useState } from "react";
import { api } from "../api";
import { Markdown } from "../lib/entertain";

interface ViewProps {
  data: unknown;
  mutate: () => Promise<void>;
  notify: (msg: string, ok?: boolean) => void;
}

const QUICK_PROMPTS = [
  "Give me the daily brief",
  "What's blocking the ZTame P8 release?",
  "Summarize open contradictions",
  "Run a quick health check",
  "What did the system do for me today?",
];

const SUGGESTIONS = [
  "Give me the daily brief",
  "What's the state of the pipeline?",
  "Any open approvals I should look at?",
  "Find contradictions to resolve",
];

export function ChatView({ data: _data, mutate: _mutate }: ViewProps) {
  const [sessionId, setSessionId] = useState(() => localStorage.getItem("exec_os_chat_session") ?? "default");
  const [messages, setMessages] = useState<Array<{ role: "user" | "assistant"; content: string }>>([]);
  const [input, setInput] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [startedAt, setStartedAt] = useState<number | null>(null);
  const [elapsed, setElapsed] = useState(0);
  const logRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    localStorage.setItem("exec_os_chat_session", sessionId);
  }, [sessionId]);

  useEffect(() => {
    if (busy) {
      setStartedAt(Date.now());
      const t = setInterval(() => setElapsed(Math.floor((Date.now() - (startedAt ?? Date.now())) / 1000)), 1000);
      return () => clearInterval(t);
    }
    setStartedAt(null);
  }, [busy]); // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    logRef.current?.scrollTo({ top: logRef.current.scrollHeight, behavior: "smooth" });
  }, [messages, busy]);

  const send = async (text: string) => {
    const content = text.trim();
    if (!content || busy) return;
    setInput("");
    setError("");
    setMessages((m) => [...m, { role: "user", content }]);
    setBusy(true);
    try {
      const reply = await api.chat(content, sessionId);
      setMessages((m) => [...m, { role: "assistant", content: reply.reply }]);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };

  const copy = (text: string) => {
    void navigator.clipboard?.writeText(text);
  };

  return (
    <div className="card chat-view">
      <div className="row" style={{ justifyContent: "space-between" }}>
        <h2>Chat with the Executive</h2>
        <div className="row">
          <input
            className="mono"
            value={sessionId}
            onChange={(e) => setSessionId(e.target.value)}
            style={{ width: 180 }}
            placeholder="session id"
          />
          <button className="ghost" onClick={() => { setMessages([]); }}>New chat</button>
        </div>
      </div>

      <div className="chat-log" ref={logRef}>
        {messages.length === 0 && (
          <div className="chat-hero">
            <p className="muted">Ask the Executive anything — it can use live tools, run the doctor, check the pipeline, and more.</p>
            <div className="suggestion-row">
              {SUGGESTIONS.map((s) => (
                <button key={s} className="suggestion" onClick={() => send(s)}>{s}</button>
              ))}
            </div>
          </div>
        )}
        {messages.map((m, i) => (
          <div key={i} className={`msg ${m.role}`}>
            {m.role === "assistant" ? <Markdown text={m.content} /> : <span>{m.content}</span>}
            <div className="msg-tools">
              <button className="mini" onClick={() => copy(m.content)}>⧉ copy</button>
            </div>
          </div>
        ))}
        {busy && (
          <div className="msg assistant">
            <div className="thinking">
              <span className="dot" /><span className="dot" /><span className="dot" />
              <span className="muted"> thinking… {elapsed}s</span>
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

      <div className="row" style={{ marginTop: 8 }}>
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
        />
        <button className="action" onClick={() => send(input)} disabled={busy || !input.trim()}>Send</button>
      </div>
    </div>
  );
}
