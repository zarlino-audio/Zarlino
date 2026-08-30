// Typed client for the Executive OS API. The token is the Founder auth token
// (from .dev.vars / wrangler secret); login exchanges it for a cookie-less
// session. We keep it simple: store the token in localStorage and send it as
// Authorization: Bearer on every request.

export const TOKEN_KEY = "exec_os_token";

export function getToken(): string {
  return localStorage.getItem(TOKEN_KEY) ?? "";
}

export function setToken(token: string): void {
  localStorage.setItem(TOKEN_KEY, token);
}

export function clearToken(): void {
  localStorage.removeItem(TOKEN_KEY);
}

async function request<T>(path: string, init: RequestInit = {}): Promise<T> {
  const token = getToken();
  const headers: Record<string, string> = {
    ...(init.headers as Record<string, string> | undefined),
  };
  if (token) headers.Authorization = `Bearer ${token}`;
  if (init.body !== undefined && !headers["Content-Type"]) {
    headers["Content-Type"] = "application/json";
  }
  const res = await fetch(path, { ...init, headers });
  if (!res.ok) {
    let msg = `HTTP ${res.status}`;
    try {
      const body = (await res.json()) as { error?: string };
      if (body.error) msg = body.error;
    } catch {
      /* non-JSON error body */
    }
    throw new Error(msg);
  }
  return (await res.json()) as T;
}

export const api = {
  health: () => request<Health>("/api/health"),

  // Username/password login returns a session token, which is then sent as
  // Authorization: Bearer for every subsequent request.
  login: (username: string, password: string) =>
    request<{ ok: boolean; token: string; expiresAt: string }>("/api/auth/login", {
      method: "POST",
      body: JSON.stringify({ username, password }),
    }),
  logout: () =>
    request<{ ok: boolean }>("/api/auth/logout", {
      method: "POST",
      body: JSON.stringify({}),
    }),
  changePassword: (currentPassword: string, newPassword: string) =>
    request<{ ok: boolean; note: string }>("/api/auth/change-password", {
      method: "POST",
      body: JSON.stringify({ currentPassword, newPassword }),
    }),

  overview: () => request<Overview>("/api/executive/overview"),

  doctorRun: (autoFix: boolean) =>
    request<DoctorReport>("/api/doctor/run", {
      method: "POST",
      body: JSON.stringify({ autoFix, trigger: "console" }),
    }),

  doctorLogs: () => request<{ logs: DoctorLog[] }>("/api/doctor/logs"),

  maintenance: () =>
    request<Record<string, unknown>>("/api/maintenance/run", {
      method: "POST",
      body: JSON.stringify({}),
    }),

  autoTasks: () =>
    request<{ ok: boolean; started: number }>("/api/autotasks/run", {
      method: "POST",
      body: JSON.stringify({}),
    }),

  chat: (message: string, sessionId: string) =>
    request<ChatReply>("/api/chat", {
      method: "POST",
      body: JSON.stringify({ message, sessionId }),
    }),

  tasks: () => request<{ tasks: Task[] }>("/api/tasks"),
  createTask: (title: string, description?: string, priority?: string) =>
    request<{ ok: boolean; id: string }>("/api/tasks", {
      method: "POST",
      body: JSON.stringify({ title, description, priority }),
    }),

  approvals: () => request<{ approvals: Approval[] }>("/api/approvals"),
  decideApproval: (id: string, decision: string, note?: string) =>
    request<{ ok: boolean; status: string }>("/api/approvals/decide", {
      method: "POST",
      body: JSON.stringify({ id, decision, note }),
    }),

  settings: () => request<Settings>("/api/settings"),
  saveSettings: (overrides: BudgetOverrides) =>
    request<{ ok: boolean; budgetOverrides: BudgetOverrides }>("/api/settings", {
      method: "POST",
      body: JSON.stringify(overrides),
    }),

  productivityReport: () => request<ProductivityReport>("/api/productivity/report"),
  productivityBudget: () => request<{ budgets: BudgetStatusEntry[] }>("/api/productivity/budget"),
  productivityAssignments: () => request<{ assignments: Assignment[] }>("/api/productivity/assignments"),
  productivityAssign: (taskId: string, roleId?: string) =>
    request<{ ok: boolean; assignmentId: string }>("/api/productivity/assign", {
      method: "POST",
      body: JSON.stringify({ taskId, roleId }),
    }),
  productivityOptimize: () =>
    request<{ ok: boolean; events: OptimizationEvent[]; summary: { applied: number; recommended: number } }>("/api/productivity/optimize", {
      method: "POST",
      body: JSON.stringify({}),
    }),
  productivityContradictions: () =>
    request<{ contradictions: Contradiction[]; open: number }>("/api/productivity/contradictions"),
  productivityDetect: () =>
    request<{ ok: boolean; found: ContradictionDetection[]; created: number }>("/api/productivity/contradictions/detect", {
      method: "POST",
      body: JSON.stringify({}),
    }),
  productivityResolve: (id: string, resolution: string, status: string) =>
    request<{ ok: boolean; id: string; status: string }>(`/api/productivity/contradictions/${encodeURIComponent(id)}/resolve`, {
      method: "POST",
      body: JSON.stringify({ resolution, status }),
    }),
  productivityMetrics: () => request<{ metrics: ProductivityMetric[] }>("/api/productivity/metrics"),

  exportSnapshot: () => request<{ ok: boolean; snapshot: unknown }>("/api/export"),
};

// ---- types ----------------------------------------------------------------

export interface Health {
  ok: boolean;
  service: string;
  time: string;
  schemaVersion: unknown;
  doctorLastRun: string | null;
  doctorHealthy: boolean | null;
}

export interface Overview {
  state: { status: string; mode: string };
  doctrine: unknown;
  counters: Record<string, number>;
  finance: unknown;
  products: Array<{ id: string; name: string; status: string; version: string }>;
  recentAudit: Array<{ id: string; actor: string; action: string; created_at: string }>;
  links: Record<string, string>;
}

export interface DoctorReport {
  generatedAt: string;
  trigger: string;
  autoFix: boolean;
  checks: Array<{
    id: string;
    label: string;
    severity: string;
    status: "OK" | "WARN" | "FAIL";
    detail: string;
    fixed?: string;
    action?: string;
  }>;
  summary: { total: number; ok: number; warn: number; fail: number; fixed: number };
  healthy: boolean;
}

export interface DoctorLog {
  id: string;
  trigger: string;
  auto_fix: number;
  run_at: string;
  summary_json: string;
  healthy: number;
}

export interface ChatReply {
  ok: boolean;
  reply: string;
  runId?: string;
}

export interface Task {
  id: string;
  title: string;
  status: string;
  priority: string;
  project_id: string | null;
  created_at: string;
}

export interface Approval {
  id: string;
  title: string;
  action_label: string;
  status: string;
  priority: string;
  detail: string;
  created_at: string;
  decided_by: string | null;
  decision_note: string | null;
}

export interface BudgetOverrides {
  aiDailyBudget?: number;
  aiMonthlyBudget?: number;
  aiPerTaskBudget?: number;
  aiPerRunBudget?: number;
}

export interface Settings {
  budgetOverrides: BudgetOverrides;
  budget: BudgetOverrides;
}

export interface BudgetStatusEntry {
  scope: string;
  scopeId?: string;
  dailyBudget?: number;
  monthlyBudget?: number;
  tokensUsedToday: number;
  tokensUsedMonth: number;
  costToday: number;
  costMonth: number;
  usagePctMonth: number;
  status: "OK" | "WARN" | "OVER";
  modelTier?: string;
}

export interface Assignment {
  id: string;
  task_id: string;
  role_id?: string;
  assignee?: string;
  status: string;
  priority?: string;
  skill_matched: number;
  model_tier?: string;
  token_budget?: number;
  tokens_used: number;
  cost_estimate: number;
  cost_actual: number;
  created_at: string;
}

export interface OptimizationEvent {
  id: string;
  kind: string;
  scope: string;
  scope_id?: string;
  reason: string;
  impact_json?: string;
  applied: number;
  created_at: string;
}

export interface Contradiction {
  id: string;
  kind: string;
  subject_a?: string;
  subject_b?: string;
  claim_a?: string;
  claim_b?: string;
  status: string;
  resolution?: string;
  resolved_by?: string;
  resolved_at?: string;
  created_at: string;
}

export interface ContradictionDetection {
  kind: string;
  subjectA: string;
  subjectB: string;
  claimA: string;
  claimB: string;
}

export interface ProductivityMetric {
  id: string;
  day: string;
  tasks_done: number;
  tokens_used: number;
  cost: number;
  tasks_per_dollar: number;
  avg_tokens_per_task: number;
}

export interface ProductivityReport {
  generatedAt: string;
  budget: BudgetStatusEntry[];
  assignments: {
    total: number;
    byStatus: Record<string, number>;
    open: number;
  };
  contradictions: { open: number; resolved: number };
  optimizationEvents: { total: number; applied: number; recent: number };
  metrics: Array<{ day: string; tasksDone: number; tokensUsed: number; cost: number; tasksPerDollar: number; avgTokensPerTask: number }>;
  efficiency: Array<{ roleId: string; roleName: string; done: number; cost: number; costPerTask: number }>;
  today: { tokens: number; cost: number };
}
