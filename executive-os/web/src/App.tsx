import { useEffect, useRef, useState } from "react";
import type { FormEvent } from "react";
import { api, AuthError, type Overview } from "./api";
import { clearAuth, clearToken, getToken, isLoggedIn, setToken, setUsername } from "./auth";
import { Confetti } from "./components/Confetti";
import { OverviewView } from "./views/Overview";
import { ChatView } from "./views/Chat";
import { DecisionsView } from "./views/Decisions";
import { TasksView } from "./views/Tasks";
import { ApprovalsView } from "./views/Approvals";
import { DoctrineView } from "./views/Doctrine";
import { MemoryView } from "./views/Memory";
import { FinanceView } from "./views/Finance";
import { ActivityView } from "./views/Activity";
import { ProductsView } from "./views/Products";
import { EngineeringView } from "./views/Engineering";
import { MarketingView } from "./views/Marketing";
import { ProjectsView } from "./views/Projects";
import { PipelineView } from "./views/Pipeline";
import { SettingsView } from "./views/Settings";
import { CalendarView } from "./views/Calendar";
import { NotesView } from "./views/Notes";

export type ViewKey =
  | "overview" | "chat" | "decisions" | "tasks" | "projects" | "pipeline" | "finance"
  | "products" | "engineering" | "marketing" | "memory" | "doctrine"
  | "approvals" | "activity" | "settings" | "calendar" | "notes";

const NAV: Array<{ key: ViewKey; label: string; icon: string; kbd?: string }> = [
  { key: "overview", label: "Overview", icon: "📡" },
  { key: "chat", label: "Ask", icon: "💬" },
  { key: "decisions", label: "Decisions", icon: "🧭" },
  { key: "tasks", label: "Tasks", icon: "✅" },
  { key: "projects", label: "Projects", icon: "🗂️" },
  { key: "pipeline", label: "Pipeline", icon: "🛰️" },
  { key: "finance", label: "Finance", icon: "💰" },
  { key: "products", label: "Products", icon: "🎛️" },
  { key: "engineering", label: "Engineering", icon: "🔧" },
  { key: "marketing", label: "Marketing", icon: "📣" },
  { key: "memory", label: "Memory", icon: "🧠" },
  { key: "doctrine", label: "Doctrine", icon: "📜" },
  { key: "approvals", label: "Approvals", icon: "🖊️" },
  { key: "activity", label: "Activity", icon: "📈" },
  { key: "calendar", label: "Calendar", icon: "📅" },
  { key: "notes", label: "Notes", icon: "📝" },
  { key: "settings", label: "Settings", icon: "⚙️" },
];

/** Fun, rotating status taglines for the ticker. */
const TAGLINES = [
  "All systems nominal — Zarlino Exec online.",
  "The Executive is watching the pipeline.",
  "Ship the gate. Polish what the market validates.",
  "Cash is king; hypotheses are the currency.",
  "Don't optimize before you measure.",
  "Every decision is a reversible experiment.",
  "ZScorch + ZTame are live. Onward.",
  "Still thinking. Still shipping. Still orange.",
  "Budget's up — go ask the Executive something bold.",
  "10 gates. 2 products. 1 doctrine. 0 panic.",
  "The lenses are reading the room. The room is GREEN.",
  "Fetching reality… it fetched successfully.",
  "If a gate could blush, it would be PASSED right now.",
  "Autonomy is a privilege. Use it wisely (and reversibly).",
  "The CFO lens sees $1.6 cash and still says 'let's go'.",
  "Doctrine > vibes. But the vibes are immaculate.",
];

const TITLES: Record<ViewKey, string> = {
  overview: "Executive Overview",
  chat: "Ask the Executive",
  decisions: "Decisions",
  tasks: "Tasks",
  projects: "Projects",
  pipeline: "10-Gate Pipeline",
  finance: "Finance",
  products: "Products",
  engineering: "Engineering",
  marketing: "Marketing",
  memory: "Memory",
  doctrine: "Founder Doctrine",
  approvals: "Approvals",
  activity: "Activity",
  calendar: "Calendar",
  notes: "Notes & Follow-ups",
  settings: "Settings",
};

export function App() {
  const [view, setView] = useState<ViewKey>("overview");
  const [authed, setAuthed] = useState<boolean>(isLoggedIn());
  const [overview, setOverview] = useState<Overview | null>(null);
  const [apiError, setApiError] = useState<string | null>(null);
  const [updated, setUpdated] = useState<string>("");
  const [booted, setBooted] = useState(false);
  const [tagIdx, setTagIdx] = useState(0);
  const [celebrate, setCelebrate] = useState(0);
  const [paletteOpen, setPaletteOpen] = useState(false);

  const load = async () => {
    try {
      const o = await api.overview();
      setOverview(o);
      setAuthed(true);
      setApiError(null);
      setUpdated(new Date().toISOString().slice(11, 19));
    } catch (err) {
      if (err instanceof AuthError) {
        clearToken();
        setAuthed(false);
        setOverview(null);
        return;
      }
      setApiError(err instanceof Error ? err.message : String(err));
    }
  };

  useEffect(() => {
    if (!isLoggedIn()) return;
    void load();
    // 60s poll (was 30s) to keep the DO SQLite rows-read budget sane; pause
    // entirely while the tab is hidden so background tabs don't hammer the DO.
    let t: ReturnType<typeof setInterval> | undefined;
    const tick = () => { if (!document.hidden) void load(); };
    t = setInterval(tick, 60_000);
    document.addEventListener("visibilitychange", tick);
    return () => { clearInterval(t); document.removeEventListener("visibilitychange", tick); };
  }, [authed]);

  // Play the boot splash once per page load (skippable, auto-advances).
  useEffect(() => {
    if (authed) {
      const t = setTimeout(() => setBooted(true), 2100);
      return () => clearTimeout(t);
    }
  }, [authed]);

  // Rotate the status tagline every 9 seconds for a living feel.
  useEffect(() => {
    const t = setInterval(() => setTagIdx((i) => (i + 1) % TAGLINES.length), 9000);
    return () => clearInterval(t);
  }, []);

  // Keyboard shortcuts: Cmd/Ctrl+K → chat; 1..9 → first nine views;
  // Ctrl+Shift+K (or "?") → command palette; Alt+←/→ → prev/next view.
  // NOTE: this must stay BEFORE the early return below — React hooks rules
  // forbid conditionally calling hooks (signing out used to crash #300).
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        setPaletteOpen(false);
        return;
      }
      if ((e.metaKey || e.ctrlKey) && e.shiftKey && e.key.toLowerCase() === "k") {
        e.preventDefault();
        setPaletteOpen(true);
        return;
      }
      if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === "k") {
        e.preventDefault();
        setView("chat");
        return;
      }
      if (e.altKey && (e.key === "ArrowRight" || e.key === "ArrowLeft")) {
        e.preventDefault();
        const idx = NAV.findIndex((n) => n.key === view);
        const next = e.key === "ArrowRight" ? (idx + 1) % NAV.length : (idx - 1 + NAV.length) % NAV.length;
        setView(NAV[next].key);
        return;
      }
      if (!e.metaKey && !e.ctrlKey && !e.altKey && e.key === "?" && !paletteOpen) {
        e.preventDefault();
        setPaletteOpen(true);
        return;
      }
      if (!e.metaKey && !e.ctrlKey && e.key >= "1" && e.key <= "9") {
        const item = NAV[Number(e.key) - 1];
        if (item) setView(item.key);
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [view, paletteOpen]);

  const logout = () => {
    const t = getToken();
    if (t) void api.logout(t).catch(() => {});
    clearAuth();
    setAuthed(false);
    setOverview(null);
    setApiError(null);
  };

  if (!authed) {
    return (
      <LoginScreen
        onLogin={() => {
          setCelebrate((c) => c + 1);
          setAuthed(true);
        }}
      />
    );
  }

  return (
    <div className="app">
      {!booted && authed && (
        <BootSplash onDone={() => setBooted(true)} />
      )}
      {celebrate > 0 && <Confetti trigger={celebrate} />}
      <aside className="sidebar">
        <div className="brand">
          <div className="logo">
            ZARLINO <span>EXEC</span>
          </div>
          <div className="sub">Executive Operating System</div>
        </div>
        <nav className="nav">
          {NAV.map((item) => (
            <button
              key={item.key}
              className={view === item.key ? "active" : ""}
              onClick={() => setView(item.key)}
              title={item.label}
            >
              <span className="nav-ico">{item.icon}</span>
              {item.label}
              {item.kbd && <span className="kbd">{item.kbd}</span>}
            </button>
          ))}
        </nav>
        <div className="status-foot">
          {overview ? <OverallPill status={overview.status} /> : "—"}
          {overview?.paused ? " · PAUSED" : ""}
        </div>
      </aside>

      <main className="main">
        <div className="topbar">
          <h1>{TITLES[view]}</h1>
          {apiError && <span className="error">API: {apiError}</span>}
          <span className="spacer" />
          {overview && <OverallPill status={overview.status} />}
          <button className="btn small palette-btn" onClick={() => setPaletteOpen(true)} title="Quick navigation (Ctrl+Shift+K or ?)">
            🔍 Jump to…
          </button>
          <span className="updated">updated {updated}Z</span>
        </div>
        <div className="content">
          <View view={view} onNavigate={setView} refresh={load} overview={overview} logout={logout} />
        </div>
        <div className="ticker" title="Executive status line">
          <span className="live-dot" />
          <span className="ticker-text" key={tagIdx}>{TAGLINES[tagIdx]}</span>
        </div>
      </main>

      {paletteOpen && (
        <CommandPalette
          onClose={() => setPaletteOpen(false)}
          onNavigate={(v) => {
            setView(v);
            setPaletteOpen(false);
          }}
        />
      )}

      {/* Mobile bottom navigation — organized, thumb-friendly. */}
      <nav className="mobile-nav" aria-label="Mobile navigation">
        {NAV.map((item) => (
          <button
            key={item.key}
            className={view === item.key ? "active" : ""}
            onClick={() => setView(item.key)}
            title={item.label}
          >
            <span className="nav-ico">{item.icon}</span>
            {item.label}
          </button>
        ))}
      </nav>
    </div>
  );
}

/** Terminal-style boot splash — a tiny cinematic intro, skippable. */
function BootSplash({ onDone }: { onDone: () => void }) {
  const lines = [
    "ZARLINO EXECUTIVE OS v11.0",
    "initializing lenses ░▒▓█",
    "loading founder doctrine… OK",
    "syncing pipeline gates… OK",
    "warming up DeepSeek cores… OK",
    "EXECUTIVE ONLINE — welcome back, founder.",
  ];
  const [shown, setShown] = useState(0);
  useEffect(() => {
    if (shown < lines.length) {
      const t = setTimeout(() => setShown((s) => s + 1), shown === 0 ? 350 : 260);
      return () => clearTimeout(t);
    }
    const t = setTimeout(onDone, 620);
    return () => clearTimeout(t);
  }, [shown, onDone]);
  return (
    <div className="boot" onClick={onDone} title="Click to skip">
      <div className="boot-inner">
        {lines.slice(0, shown).map((l, i) => (
          <div key={i} className="boot-line">{l}</div>
        ))}
        {shown < lines.length && <span className="cursor" />}
      </div>
      <div className="faint small" style={{ marginTop: 14 }}>click anywhere to skip ▸</div>
    </div>
  );
}

/** Founder gate — first visit forces account setup; afterwards sign-in. */
function LoginScreen({ onLogin }: { onLogin: () => void }) {
  const [mode, setMode] = useState<"loading" | "setup" | "login">("loading");
  const [username, setUsernameValue] = useState("");
  const [password, setPasswordValue] = useState("");
  const [confirm, setConfirmValue] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    api
      .authStatus()
      .then((s) => setMode(s.setupRequired ? "setup" : "login"))
      .catch(() => setMode("login"));
  }, []);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    if (busy || !username.trim() || !password) return;
    if (mode === "setup" && password !== confirm) {
      setError("Passwords do not match.");
      return;
    }
    setBusy(true);
    setError(null);
    try {
      const result =
        mode === "setup"
          ? await api.setupFounder(username.trim(), password)
          : await api.login(username.trim(), password);
      setToken(result.token);
      setUsername(result.username);
      onLogin();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setBusy(false);
    }
  };

  return (
    <div style={{ minHeight: "100vh", display: "flex", alignItems: "center", justifyContent: "center", padding: 24, background: "radial-gradient(1200px 500px at 50% -10%, rgba(232,134,58,0.08), transparent 60%), var(--bg)" }}>
      <form onSubmit={submit} className="card" style={{ width: 380, maxWidth: "100%" }}>
        <div style={{ display: "flex", alignItems: "center", gap: 10, marginBottom: 4 }}>
          <span style={{ fontSize: 22 }}>🎛️</span>
          <div className="logo" style={{ fontSize: 18, fontWeight: 700 }}>
            ZARLINO <span style={{ color: "var(--accent)" }}>EXEC</span>
          </div>
        </div>
        <div className="sub faint" style={{ fontFamily: "var(--mono)", fontSize: 11, marginBottom: 18 }}>
          Executive Operating System
          {mode === "setup" ? " · First visit — create your founder account" : " · Founder sign-in"}
        </div>

        {mode === "loading" ? (
          <p className="faint small">Checking…</p>
        ) : (
          <>
            {mode === "setup" && (
              <p className="small muted" style={{ marginTop: 0 }}>
                No founder account exists yet. Set your username and password now — they will be the
                only way to sign in.
              </p>
            )}
            <label className="field">
              <span className="lbl">Username</span>
              <input
                type="text"
                value={username}
                onChange={(e) => setUsernameValue(e.target.value)}
                placeholder={mode === "setup" ? "founder" : "Your username"}
                autoFocus
                autoComplete="username"
              />
            </label>
            <label className="field">
              <span className="lbl">Password</span>
              <input
                type="password"
                value={password}
                onChange={(e) => setPasswordValue(e.target.value)}
                placeholder={mode === "setup" ? "At least 8 characters" : "Your password"}
                autoComplete={mode === "setup" ? "new-password" : "current-password"}
              />
            </label>
            {mode === "setup" && (
              <label className="field">
                <span className="lbl">Confirm password</span>
                <input
                  type="password"
                  value={confirm}
                  onChange={(e) => setConfirmValue(e.target.value)}
                  placeholder="Repeat your password"
                  autoComplete="new-password"
                />
              </label>
            )}
            {error && <div className="error" style={{ marginBottom: 10 }}>{error}</div>}
            <button
              className="btn primary"
              type="submit"
              disabled={busy || !username.trim() || !password}
              style={{ width: "100%" }}
            >
              {busy ? "Please wait…" : mode === "setup" ? "Create founder account" : "Sign in"}
            </button>
          </>
        )}
      </form>
    </div>
  );
}

/**
 * Command palette — keyboard-first quick navigation for everyone, including
 * users who can't scroll. Type to filter, ↑/↓ to move, Enter to select,
 * Esc to close. Opens with Ctrl+Shift+K, "?", or the topbar 🔍 button.
 */
function CommandPalette({ onClose, onNavigate }: { onClose: () => void; onNavigate: (v: ViewKey) => void }) {
  const [q, setQ] = useState("");
  const [sel, setSel] = useState(0);
  const inputRef = useRef<HTMLInputElement>(null);

  const items = NAV.filter((n) => {
    const s = q.trim().toLowerCase();
    if (!s) return true;
    return n.label.toLowerCase().includes(s) || TITLES[n.key].toLowerCase().includes(s) || n.key.toLowerCase().includes(s);
  });

  useEffect(() => {
    setSel(0);
    inputRef.current?.focus();
  }, [q]);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === "ArrowDown") {
        e.preventDefault();
        setSel((s) => Math.min(items.length - 1, s + 1));
      } else if (e.key === "ArrowUp") {
        e.preventDefault();
        setSel((s) => Math.max(0, s - 1));
      } else if (e.key === "Enter") {
        e.preventDefault();
        const item = items[sel];
        if (item) onNavigate(item.key);
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [items, sel, onNavigate]);

  return (
    <div className="palette-backdrop" onClick={onClose}>
      <div className="palette" onClick={(e) => e.stopPropagation()} role="dialog" aria-label="Quick navigation">
        <div className="palette-input-row">
          <span className="palette-ico">🔍</span>
          <input
            ref={inputRef}
            className="palette-input"
            placeholder="Jump to a view…  (↑↓ to move · Enter to go · Esc to close)"
            value={q}
            onChange={(e) => setQ(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === "Escape") onClose();
            }}
          />
          <span className="palette-hint">Esc</span>
        </div>
        <div className="palette-list">
          {items.length === 0 ? (
            <div className="palette-empty">No matches for “{q}”.</div>
          ) : (
            items.map((n, i) => (
              <button
                key={n.key}
                className={`palette-item ${i === sel ? "active" : ""}`}
                onMouseEnter={() => setSel(i)}
                onClick={() => onNavigate(n.key)}
              >
                <span className="nav-ico">{n.icon}</span>
                <span className="palette-label">{n.label}</span>
                <span className="palette-title">{TITLES[n.key]}</span>
                <span className="kbd">{i < 9 ? i + 1 : ""}</span>
              </button>
            ))
          )}
        </div>
        <div className="palette-foot">
          <span className="faint small">Tip: Alt+←/→ cycles views · 1–9 jumps · Ctrl+K goes to chat</span>
        </div>
      </div>
    </div>
  );
}

function OverallPill({ status }: { status: Overview["status"] }) {
  const cls = status === "GREEN" ? "green" : status === "YELLOW" ? "yellow" : "red";
  return (
    <span className={`pill ${cls}`}>
      <span className="dot" />
      {status}
    </span>
  );
}

function View({
  view,
  onNavigate,
  refresh,
  overview,
  logout,
}: {
  view: ViewKey;
  onNavigate: (v: ViewKey) => void;
  refresh: () => void;
  overview: Overview | null;
  logout: () => void;
}) {
  switch (view) {
    case "overview": return <OverviewView overview={overview} onNavigate={onNavigate} refresh={refresh} />;
    case "chat": return <ChatView overview={overview} onNavigate={onNavigate} />;
    case "decisions": return <DecisionsView />;
    case "tasks": return <TasksView />;
    case "projects": return <ProjectsView />;
    case "pipeline": return <PipelineView />;
    case "finance": return <FinanceView />;
    case "products": return <ProductsView />;
    case "engineering": return <EngineeringView />;
    case "marketing": return <MarketingView />;
    case "memory": return <MemoryView />;
    case "doctrine": return <DoctrineView />;
    case "approvals": return <ApprovalsView />;
    case "activity": return <ActivityView />;
    case "calendar": return <CalendarView />;
    case "notes": return <NotesView />;
    case "settings": return <SettingsView logout={logout} />;
    default: return null;
  }
}

/** Small hook to refresh data when a view mounts / after actions. */
export function useRefresh(deps: unknown[] = []): [number, () => void] {
  const [tick, setTick] = useState(0);
  const bump = () => setTick((t) => t + 1);
  useEffect(() => {
    // re-fetch when deps change (mount + after actions)
  }, deps);
  return [tick, bump];
}

/** Preserve chat session across view switches. */
const chatSession = { current: "" };

export function getChatSession(): string {
  return chatSession.current;
}

export function setChatSession(sessionId: string): void {
  chatSession.current = sessionId;
}

export { chatSession };
