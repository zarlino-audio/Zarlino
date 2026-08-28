/**
 * Typed API client for the Executive OS REST API (spec §30).
 */

import { authHeaders } from "./auth";

const BASE = "/api";

/** Thrown when the API returns 401 — the console shows the login screen. */
export class AuthError extends Error {
  constructor() {
    super("Unauthorized — please sign in with your Founder token");
    this.name = "AuthError";
  }
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(`${BASE}${path}`, {
    headers: { "Content-Type": "application/json", ...authHeaders() },
    ...init,
  });
  if (res.status === 401) {
    throw new AuthError();
  }
  if (!res.ok) {
    let message = `HTTP ${res.status}`;
    try {
      const body = (await res.json()) as { error?: string };
      if (body.error) message = body.error;
    } catch {
      /* ignore */
    }
    throw new Error(message);
  }
  return (await res.json()) as T;
}

const get = <T,>(path: string) => request<T>(path);
const post = <T,>(path: string, body?: unknown) =>
  request<T>(path, { method: "POST", body: JSON.stringify(body ?? {}) });
const patch = <T,>(path: string, body?: unknown) =>
  request<T>(path, { method: "PATCH", body: JSON.stringify(body ?? {}) });
const del = <T,>(path: string) => request<T>(path, { method: "DELETE" });

export interface Overview {
  status: "GREEN" | "YELLOW" | "RED";
  currentObjective?: string;
  topPriorities: string[];
  cash: number | null;
  revenue: number | null;
  expenses: number | null;
  runwayMonths: number | null;
  financialConfidence: string;
  criticalRisks: Array<{ id: string; title: string; level: string }>;
  blockedTasks: number;
  openTasks: number;
  pendingApprovals: number;
  recentDecisions: Array<{ id: string; title: string; status: string; category: string }>;
  paused: boolean;
  autonomyMode: string;
  spendingDisabled: boolean;
  deploymentsDisabled: boolean;
  delegateMode: boolean;
  fullAccess: boolean;
  shipped: Array<{ id: string; name: string; version?: string }>;
  shippedCount: number;
  downloads?: number | null;
  licensed?: number | null;
  aiSpendToday?: number;
  aiSpendMonth?: number;
  aiDailyBudget?: number;
  aiMonthlyBudget?: number;
  aiTokensToday?: number;
  aiTokensMonth?: number;
  aiDailyTokenBudget?: number;
  aiMonthlyTokenBudget?: number;
  aiProviderBalance?: ProviderBalance | null;
  lastActivity?: string;
}

/** AI-provider account balance (DeepSeek /user/balance), cached server-side. */
export interface ProviderBalance {
  provider: "deepseek";
  isAvailable: boolean;
  fetchedAt: string;
  balances: Array<{ currency: string; total: string; granted: string; toppedUp: string }>;
  totalUsdEstimate: number | null;
  cached?: boolean;
  error?: string;
}

/** Effective AI cost budgets + current spend (Founder-extendable). */
export interface BudgetSnapshot {
  ok: boolean;
  daily: number;
  monthly: number;
  perTask: number;
  perRun: number;
  overrides: { aiDailyBudget?: number; aiMonthlyBudget?: number; aiPerTaskBudget?: number; aiPerRunBudget?: number };
  spendToday: number;
  spendMonth: number;
  tokensToday: number;
  tokensMonth: number;
}

export interface Decision {
  id: string;
  title: string;
  problem: string;
  status: string;
  category: string;
  confidence?: number;
  recommended_action?: string;
  expected_outcome?: string;
  approval_required: boolean;
  created_at: string;
  ceo_assessment?: string;
  cfo_assessment?: string;
  cto_assessment?: string;
  risk_assessment?: string;
  outcome_status?: string;
}

export interface Task {
  id: string;
  title: string;
  status: string;
  priority: string;
  description?: string;
  decision_id?: string;
  due_date?: string;
  assignee_person_id?: string;
  created_at: string;
  /** 1 = executes autonomously (no Founder click); 0 = manual. */
  auto_run?: boolean;
}

export interface Approval {
  id: string;
  action_label: string;
  reason: string;
  expected_upside?: string;
  max_downside?: string;
  budget_available?: string;
  recommendation: string;
  status: string;
  created_at: string;
  decision_note?: string;
  decided_at?: string;
}

export interface FinancialSnapshotView {
  period: string;
  cash: number | null;
  revenue: number | null;
  expenses: number | null;
  runway_months: number | null;
  confidence: string;
  notes?: string;
  created_at: string;
}

export interface ProjectView {
  id: string;
  name: string;
  status: string;
  priority: number;
  objective_id?: string;
}

export interface DoctrineRule {
  id: string;
  code: string;
  principle: string;
  category: string;
  status: string;
  version: number;
  rationale?: string;
}

export interface MemoryEntry {
  id: string;
  layer: string;
  kind: string;
  title: string;
  body: string;
  importance: number;
  status: string;
  created_at: string;
}

export interface AuditEvent {
  id: string;
  actor: string;
  action: string;
  entity_type?: string;
  entity_id?: string;
  created_at: string;
  verdict?: string;
}

export interface ChatResult {
  reply: string;
  sessionId: string;
  intent: string;
}

export interface ChatSession {
  session_id: string;
  message_count: number;
  last_at: string;
  last_message: string;
}

export interface ChatMessage {
  id: string;
  role: "founder" | "executive";
  content: string;
  created_at: string;
}

export interface ChatStreamEvent {
  kind: "meta" | "delta" | "done" | "error";
  sessionId?: string;
  intent?: string;
  runId?: string;
  text?: string;
  reply?: string;
  message?: string;
}

export interface ChatStreamHandlers {
  onMeta: (sessionId: string) => void;
  onDelta: (text: string) => void;
  onDone: () => void;
  onError: (message: string) => void;
}

export type GateStatus = "NOT_STARTED" | "IN_PROGRESS" | "PASSED" | "FAILED" | "BLOCKED" | "SHIPPED";

export interface ToolExecution {
  id: string;
  tool_name: string;
  status: string;
  verdict: string;
  error?: string;
  created_at: string;
}

export interface AgentRun {
  id: string;
  task_id?: string;
  prompt: string;
  status: "RUNNING" | "DONE" | "FAILED" | "STOPPED" | "TIMEOUT";
  iterations: number;
  tools_called: number;
  started_at: string;
  finished_at?: string;
  result?: string;
  error?: string;
  created_at: string;
}

export interface LocalTask {
  id: string;
  task_id?: string;
  prompt: string;
  status: "PENDING" | "CLAIMED" | "RUNNING" | "DONE" | "FAILED";
  claimed_by?: string;
  started_at?: string;
  finished_at?: string;
  result?: string;
  error?: string;
  lease_expires_at?: string;
  meta_json?: string;
  created_at: string;
}

export interface LocalTaskBrief {
  goal?: string;
  context?: string;
  acceptance?: string;
  deliverable?: string;
}

/** Calendar event (migration v14). */
export interface CalendarEvent {
  id: string;
  title: string;
  description?: string | null;
  startAt: string;
  endAt?: string | null;
  allDay: boolean;
  location?: string | null;
  status: "SCHEDULED" | "CANCELLED" | "DONE";
}

/** Captured note. */
export interface Note {
  id: string;
  title: string;
  body?: string | null;
  tags: string[];
  pinned: boolean;
  updatedAt: string;
}

/** Reminder. */
export interface Reminder {
  id: string;
  title: string;
  dueAt: string;
  done: boolean;
  overdue: boolean;
}

/** External contact (partners, vendors, testers…). */
export interface Contact {
  id: string;
  name: string;
  role?: string | null;
  company?: string | null;
  email?: string | null;
  phone?: string | null;
  tags: string[];
}

export interface UploadedFile {
  id: string;
  name: string;
  mime?: string;
  size_bytes: number;
  kind: "image" | "text" | "audio" | "other";
  data_base64?: string;
  created_at: string;
}

export interface PipelineGate {
  id: string;
  plugin_id: string;
  gate_number: number;
  gate_name: string;
  status: GateStatus;
  note?: string;
  owner_role?: string;
}

export interface Person {
  id: string;
  name: string;
  role?: string;
}

export interface AuthStatus {
  setupRequired: boolean;
  username?: string;
}

export interface AuthResult {
  token: string;
  username: string;
}

export const api = {
  // Auth (no prior session required)
  authStatus: () => get<AuthStatus>("/auth/status"),
  setupFounder: (username: string, password: string) => post<AuthResult>("/auth/setup", { username, password }),
  login: (username: string, password: string) => post<AuthResult>("/auth/login", { username, password }),
  logout: (token: string) => post<unknown>("/auth/logout", { token }),
  changePassword: (currentPassword: string, newPassword: string) =>
    post<unknown>("/auth/password", { currentPassword, newPassword }),

  overview: () => get<Overview>("/executive/overview"),
  state: () => get<{ state: Overview; emergency: unknown }>("/executive/state"),
  chat: (message: string, sessionId?: string) =>
    post<ChatResult>("/chat", { message, sessionId }),
  chatSessions: () => get<ChatSession[]>("/chat/sessions"),
  chatMessages: (sessionId: string) => get<ChatMessage[]>(`/chat/messages?sessionId=${encodeURIComponent(sessionId)}`),
  chatClear: (sessionId: string) => post<{ cleared: boolean }>("/chat/clear", { sessionId }),
  /** Stream a chat reply over SSE — the console renders it token-by-token. */
  chatStream: (message: string, sessionId: string | undefined, h: ChatStreamHandlers) =>
    new Promise<void>((resolve) => {
      const controller = new AbortController();
      const timeout = setTimeout(() => controller.abort(), 120_000);
      fetch(`${BASE}/chat/stream`, {
        method: "POST",
        headers: { "Content-Type": "application/json", ...authHeaders() },
        body: JSON.stringify({ message, sessionId }),
        signal: controller.signal,
      })
        .then(async (res) => {
          if (res.status === 401) throw new AuthError();
          if (!res.ok || !res.body) {
            const text = await res.text().catch(() => "");
            throw new Error(text || `HTTP ${res.status}`);
          }
          const reader = res.body.getReader();
          const decoder = new TextDecoder();
          let buf = "";
          const processLine = (line: string) => {
            if (!line.startsWith("data: ")) return;
            try {
              const ev = JSON.parse(line.slice(6)) as ChatStreamEvent;
              if (ev.kind === "meta" && ev.sessionId) h.onMeta(ev.sessionId);
              else if (ev.kind === "delta" && ev.text) h.onDelta(ev.text);
              else if (ev.kind === "done") h.onDone();
              else if (ev.kind === "error") h.onError(ev.message ?? "Stream error");
            } catch {
              /* ignore malformed frames */
            }
          };
          for (;;) {
            const { done, value } = await reader.read();
            if (done) break;
            buf += decoder.decode(value, { stream: true });
            let idx: number;
            while ((idx = buf.indexOf("\n\n")) !== -1) {
              const chunk = buf.slice(0, idx);
              buf = buf.slice(idx + 2);
              for (const line of chunk.split("\n")) processLine(line);
            }
          }
        })
        .catch((err) => {
          if (err instanceof AuthError) {
            h.onError(err.message);
          } else {
            h.onError(err instanceof Error ? err.message : String(err));
          }
        })
        .finally(() => {
          clearTimeout(timeout);
          resolve();
        });
    }),

  decisions: () => get<Decision[]>("/decisions"),
  decision: (id: string) => get<Decision>(`/decisions/${id}`),
  runDecision: (body: { problem: string; estimatedCost?: number; proposedAction?: string }) =>
    post<unknown>("/decisions", body),

  tasks: () => get<Task[]>("/tasks"),
  createTask: (body: { title: string; description?: string; priority?: string; autoRun?: boolean }) =>
    post<Task>("/tasks", body),
  updateTask: (id: string, body: { status?: string; priority?: string; assigneePersonId?: string; autoRun?: boolean }) =>
    patch<Task>(`/tasks/${id}`, body),

  projects: () => get<ProjectView[]>("/projects"),
  pipeline: () => get<{ gates: PipelineGate[] }>("/pipeline"),
  updateGate: (id: string, body: { status?: GateStatus; note?: string }) =>
    patch<PipelineGate>(`/pipeline/gates/${id}`, body),
  ship: (pluginId: string, version?: string, note?: string) =>
    post<{ status: string; version?: string }>("/pipeline/ship", { pluginId, version, note }),
  toolExecutions: () => get<ToolExecution[]>("/tools/executions"),
  agentRun: (prompt: string, taskId?: string, maxIterations?: number) =>
    post<{ runId: string; status: string }>("/agents/run", { prompt, taskId, maxIterations }),
  agentRuns: () => get<AgentRun[]>("/agents/runs"),
  agentRunDetail: (id: string) => get<AgentRun>(`/agents/runs/${id}`),
  agentRunContinue: (id: string) => post<{ runId: string; status: string }>(`/agents/runs/${id}/continue`, {}),
  /** Queue a task for the LOCAL machine (plugin UI inspection, repo work). */
  localTaskCreate: (prompt: string, taskId?: string, meta?: LocalTaskBrief) =>
    post<{ taskId: string; status: string }>("/local/tasks", { prompt, taskId, meta }),
  localTasks: () => get<LocalTask[]>("/local/tasks"),
  localTaskDetail: (id: string) => get<LocalTask>(`/local/tasks/${id}`),
  /** Upload a file (base64) so the Executive can inspect it (vision for images). */
  fileUpload: (name: string, mime: string, dataBase64: string) =>
    post<{ fileId: string; kind: string; sizeBytes: number }>("/files", { name, mime, dataBase64 }),
  files: () => get<UploadedFile[]>("/files"),
  fileDetail: (id: string) => get<UploadedFile>(`/files/${id}`),
  fileDelete: (id: string) => del<{ deleted: boolean }>(`/files/${id}`),
  persons: () => get<Person[]>("/persons"),
  finance: () => get<{ snapshots: FinancialSnapshotView[]; metrics: unknown[]; aiSpendToday: number; aiSpendMonth: number }>("/finance"),
  recordMetric: (name: "downloads" | "licensed", value: number) =>
    post<{ id: string; name: string; value: number }>("/finance/metrics", { name, value }),
  risks: () => get<Array<{ id: string; title: string; level: string; status: string }>>("/risks"),
  approvals: () => get<Approval[]>("/approvals"),
  approve: (id: string, note?: string) => post<Approval>(`/approvals/${id}/approve`, { note }),
  reject: (id: string, note?: string) => post<Approval>(`/approvals/${id}/reject`, { note }),
  memory: () => get<MemoryEntry[]>("/memory"),
  addMemory: (body: { layer: string; kind: string; title: string; body: string }) =>
    post<MemoryEntry>("/memory", body),
  doctrine: () => get<DoctrineRule[]>("/doctrine"),
  proposeDoctrine: (body: { code: string; principle: string; rationale?: string; category?: string }) =>
    post<DoctrineRule>("/doctrine?action=propose", body),
  approveDoctrine: (id: string) => post<DoctrineRule>(`/doctrine/${id}/approve`),
  rejectDoctrine: (id: string) => post<unknown>(`/doctrine/${id}/reject`),
  activity: () => get<AuditEvent[]>("/activity"),
  products: () => get<Array<{ id: string; name: string; status: string; category?: string }>>("/products"),
  engineering: () => get<Array<{ id: string; title: string; severity: string; status: string }>>("/engineering"),
  marketing: () => get<Array<{ id: string; name: string; status: string; spent?: number }>>("/marketing"),
  costs: () => get<unknown[]>("/costs"),
  tools: (name: string, input: unknown) => post<unknown>("/tools/execute", { name, input }),
  pause: (reason?: string) => post<unknown>("/executive/pause", { reason }),
  resume: () => post<unknown>("/executive/resume"),
  setSpending: (disabled: boolean) => post<unknown>("/executive/spending", { disabled }),
  setDeployments: (disabled: boolean) => post<unknown>("/executive/deployments", { disabled }),
  setAutonomy: (mode: string) => post<unknown>("/executive/autonomy", { mode }),
  setDelegate: (enabled: boolean) => post<{ delegateMode: boolean }>("/executive/delegate", { enabled }),
  setFullAccess: (enabled: boolean) => post<{ fullAccess: boolean; delegateMode: boolean }>("/executive/fullaccess", { enabled }),
  brief: () => post<{ brief: string }>("/executive/brief"),
  maintenance: () => post<{ staleRuns: string[]; reapedLocal: string[]; expiredSessions: number; autoStarted: string[] }>("/system/maintenance"),

  // ---- Planner (calendar / notes / reminders / contacts) --------------
  calendar: (from?: string, to?: string) =>
    get<CalendarEvent[]>(`/calendar${from || to ? `?from=${encodeURIComponent(from ?? "")}&to=${encodeURIComponent(to ?? "")}` : ""}`),
  calendarCreate: (body: { title: string; startAt: string; endAt?: string; allDay?: boolean; location?: string; description?: string }) =>
    post<CalendarEvent>("/calendar", body),
  calendarUpdate: (id: string, body: { title?: string; startAt?: string; endAt?: string; status?: string; location?: string }) =>
    patch<CalendarEvent>(`/calendar/${id}`, body),
  calendarDelete: (id: string) => del<{ deleted: boolean }>(`/calendar/${id}`),

  notes: (limit?: number) => get<Note[]>(`/notes${limit ? `?limit=${limit}` : ""}`),
  noteCreate: (body: { title: string; body?: string; tags?: string[]; pinned?: boolean }) => post<Note>("/notes", body),
  noteDelete: (id: string) => del<{ deleted: boolean }>(`/notes/${id}`),

  reminders: (overdueOnly?: boolean) => get<Reminder[]>(`/reminders${overdueOnly ? "?overdueOnly=1" : ""}`),
  reminderCreate: (body: { title: string; dueAt: string }) => post<Reminder>("/reminders", body),
  reminderDone: (id: string) => patch<Reminder>(`/reminders/${id}`, { done: true }),
  reminderDelete: (id: string) => del<{ deleted: boolean }>(`/reminders/${id}`),

  contacts: () => get<Contact[]>("/contacts"),
  contactAdd: (body: { name: string; role?: string; company?: string; email?: string; phone?: string; tags?: string[]; notes?: string }) =>
    post<Contact>("/contacts", body),
  contactDelete: (id: string) => del<{ deleted: boolean }>(`/contacts/${id}`),

  /** Effective AI budgets + spend/tokens (GET) and Founder overrides (POST). */
  budget: () => get<BudgetSnapshot>("/system/budget"),
  setBudget: (o: { aiDailyBudget?: number; aiMonthlyBudget?: number; aiPerTaskBudget?: number; aiPerRunBudget?: number }) =>
    post<{ ok: boolean; view: { daily: number; monthly: number; perTask: number; perRun: number; overrides: BudgetSnapshot["overrides"] } }>("/system/budget", o),
  /** AI-provider account balance (cached; refresh() hits the provider live). */
  providerBalance: () => get<ProviderBalance>("/system/balance"),
  providerBalanceRefresh: () => post<ProviderBalance>("/system/balance/refresh", {}),
  systemHealth: () => get<{
    ok: boolean;
    status: string;
    paused: boolean;
    stuck: { staleAgentRuns: number; expiredLeases: number };
    aiSpendToday: number;
    aiDailyBudget: number;
    aiTokensToday: number;
    aiDailyTokenBudget: number;
    openTasks: number;
    pendingApprovals: number;
  }>("/system/health"),
};
