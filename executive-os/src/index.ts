/**
 * Zarlino Executive OS — Worker entry.
 *
 * A Cloudflare Worker + SQLite-backed Durable Object that runs the business:
 *   - `ExecutiveAgent` Durable Object holds the durable store (ExecutiveStore).
 *   - On every wake it runs the System Doctor (auto-fix), starts auto-runnable
 *     tasks, and reschedules itself (constant maintenance).
 *   - A 6-hourly cron triggers the same maintenance via the `scheduled` handler.
 *   - REST API (token/session auth) exposes overview, chat, tools, files,
 *     approvals, tasks, settings, export/import, and the Doctor.
 *
 * Secrets (never in this file): DEEPSEEK_API_KEY, FOUNDER_AUTH_TOKEN,
 * PAYSTACK_SECRET_KEY, GITHUB_TOKEN, CF_API_TOKEN, ZARLINO_LICENSE_PRIVATE_KEY.
 */

import { ExecutiveStore } from "./db/store";
import { createDoSql, type Sql } from "./db/sql";
import { buildToolRegistry } from "./agent/tools";
import type { ToolRegistry } from "./core/tools/registry";
import { runDoctor } from "./core/doctor";
import { createTask } from "./core/tasks/model";
import { hashPassword, timingSafeEqual, verifyPassword } from "./core/auth/passwords";
import {
  assignTask,
  budgetStatus,
  dailyRollup,
  detectContradictions,
  optimize,
  productivityReport,
  recommendAssignment,
} from "./core/productivity/manager";
import { newId, nowIso } from "./ids";
import type { AgentRun, DoctorLog, Task } from "./types";
import type { ModelCallRecord as _M } from "./core/model/types";
type ModelCall = _M;

// ---------------------------------------------------------------------------
// Env
// ---------------------------------------------------------------------------

export interface Env {
  EXECUTIVE_AGENT: DurableObjectNamespace;
  ZARLINO_KV?: KVNamespace;
  ASSETS?: Fetcher;
  DEEPSEEK_API_KEY?: string;
  FOUNDER_AUTH_TOKEN?: string;
  GITHUB_TOKEN?: string;
  CF_API_TOKEN?: string;
  PAYSTACK_SECRET_KEY?: string;
  PAYSTACK_PUBLIC_KEY?: string;
  PAYSTACK_CURRENCY?: string;
  ZARLINO_LICENSE_PRIVATE_KEY?: string;
  WEBSITE_URL?: string;
  AI_DAILY_BUDGET?: string;
  AI_MONTHLY_BUDGET?: string;
  AI_PER_TASK_BUDGET?: string;
  AI_PER_RUN_BUDGET?: string;
  DOCTOR_AUTOFIX?: string;
  MAX_AUTO_JOBS?: string;
}

const INTERNAL_HEADER = "x-exec-internal";
const INTERNAL_SECRET = "zarlino-internal-scheduled";
const DEEPSEEK_URL = "https://api.deepseek.com/chat/completions";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function json(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      "Content-Type": "application/json; charset=utf-8",
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Allow-Methods": "GET, POST, OPTIONS, DELETE",
      "Access-Control-Allow-Headers": "Content-Type, Authorization",
    },
  });
}

function readBody<T>(request: Request): Promise<T> {
  return request.json() as Promise<T>;
}

/** Detect uploaded-file kind from MIME type. */
function kindFromMime(mime: string): "image" | "text" | "audio" | "other" {
  if (/^image\//i.test(mime)) return "image";
  if (/^text\//i.test(mime) || /json|xml|yaml|markdown|csv|javascript/.test(mime)) return "text";
  if (/^audio\//i.test(mime) || /wav|mp3|ogg|flac|m4a/.test(mime)) return "audio";
  return "other";
}

function parseBool(v: string | undefined, def: boolean): boolean {
  if (v === undefined || v === "") return def;
  return !/^(0|false|no|off)$/i.test(v);
}

// ---------------------------------------------------------------------------
// Auth
// ---------------------------------------------------------------------------

function authorized(request: Request, env: Env, store: ExecutiveStore): boolean {
  const header = request.headers.get("Authorization") || "";
  const token = env.FOUNDER_AUTH_TOKEN;
  if (token) {
    if (header.startsWith("Bearer ") && timingSafeEqual(header.slice(7), token)) return true;
    const url = new URL(request.url);
    const q = url.searchParams.get("token");
    if (q && timingSafeEqual(q, token)) return true;
  }
  if (header.startsWith("Bearer ")) {
    const session = store.getSession(header.slice(7));
    if (session && session.expires_at > new Date().toISOString()) return true;
  }
  return false;
}

function internal(request: Request): boolean {
  return request.headers.get(INTERNAL_HEADER) === INTERNAL_SECRET;
}

// ---------------------------------------------------------------------------
// DeepSeek chat + agent loop
// ---------------------------------------------------------------------------

interface ChatMsg {
  role: "system" | "user" | "assistant" | "tool";
  content?: string;
  tool_calls?: Array<{ id: string; type: string; function: { name: string; arguments: string } }>;
  tool_call_id?: string;
  name?: string;
}

async function callDeepSeek(
  apiKey: string,
  messages: ChatMsg[],
  registry: ToolRegistry,
  recordCost: (c: ModelCall) => void,
  model = "deepseek-chat",
): Promise<ChatMsg> {
  // Tool names are sanitized for the wire (DeepSeek forbids dots in names).
  const tools = registry.functionSchemas();
  const started = Date.now();
  const res = await fetch(DEEPSEEK_URL, {
    method: "POST",
    headers: { Authorization: `Bearer ${apiKey}`, "Content-Type": "application/json" },
    body: JSON.stringify({
      model,
      messages,
      tools,
      tool_choice: "auto",
      max_tokens: 2048,
      stream: false,
    }),
  });
  const latencyMs = Date.now() - started;
  if (!res.ok) {
    const body = await res.text().catch(() => "");
    throw new Error(`DeepSeek HTTP ${res.status}: ${body.slice(0, 300)}`);
  }
  const data = (await res.json()) as {
    choices?: Array<{ message?: ChatMsg }>;
    usage?: { prompt_tokens?: number; completion_tokens?: number };
  };
  const message = data.choices?.[0]?.message;
  if (!message) throw new Error("Empty model response");
  const inputTokens = data.usage?.prompt_tokens ?? 0;
  const outputTokens = data.usage?.completion_tokens ?? 0;
  recordCost({
    provider: "deepseek",
    model,
    taskType: "chat",
    inputTokens,
    outputTokens,
    cost: (inputTokens / 1_000_000) * 0.27 + (outputTokens / 1_000_000) * 1.1,
    latencyMs,
    timestamp: new Date().toISOString(),
  });
  return message;
}

const SYSTEM_PROMPT = `You are the Zarlino Executive OS — the autonomous operator of Zarlino Audio, a small audio-plugins company (ZTame, ZScorch). You run the business through tools and a durable store.

Working rules:
1. Use tools to gather facts before asserting them. Never fabricate numbers (doctrine: HONEST_NUMBERS) — say UNKNOWN instead.
2. HIGH-risk / irreversible actions (spending money, deploys, cache purges) are gated behind Founder approval — never execute them yourself; request an approval or flag the Founder.
3. Audit-worthy actions are recorded automatically by the tool registry.
4. Keep replies concise and actionable. Use markdown for structure.
5. When the Founder asks, run the System Doctor (system.doctor) to check and repair the system; run maintenance (system.maintenance) when housekeeping is needed.
6. You can queue work for the LOCAL machine via executive.request_local_task / executive.delegate_idea and poll with executive.get_local_task.`;

function buildSystemContext(env: Env): ChatMsg {
  return {
    role: "system",
    content: `${SYSTEM_PROMPT}\n\nWebsite: ${env.WEBSITE_URL ?? "https://zarlinoaudio.com"}\nCurrency: ${env.PAYSTACK_CURRENCY ?? "GHS"}\nDaily AI budget: ${env.AI_DAILY_BUDGET ?? "5"}$\nMonthly AI budget: ${env.AI_MONTHLY_BUDGET ?? "80"}$`,
  };
}

/**
 * Run one agent turn (chat or task-driven) with the tool loop. Bounded by
 * maxIterations. Records messages + cost. Returns the final assistant text.
 */
async function agentLoop(opts: {
  store: ExecutiveStore;
  registry: ToolRegistry;
  env: Env;
  sessionId?: string;
  runId?: string;
  messages: ChatMsg[];
  onChunk?: (text: string) => void;
  maxIterations?: number;
}): Promise<{ text: string; iterations: number; toolsCalled: number }> {
  const apiKey = opts.env.DEEPSEEK_API_KEY;
  if (!apiKey) throw new Error("DEEPSEEK_API_KEY not configured — set it as a Worker secret.");
  const store = opts.store;
  const maxIter = opts.maxIterations ?? 8;
  const messages = [...opts.messages];
  const recordCost = (c: ModelCall) => {
    store.record({ ...c, sessionId: opts.sessionId, runId: opts.runId });
  };
  let iterations = 0;
  let toolsCalled = 0;

  for (let i = 0; i < maxIter; i++) {
    iterations++;
    const reply = await callDeepSeek(apiKey, messages, opts.registry, recordCost);
    messages.push(reply);
    const toolCalls = reply.tool_calls;
    if (toolCalls && toolCalls.length > 0) {
      for (const call of toolCalls) {
        toolsCalled++;
        // Map the provider-sanitized name back to the real tool before running.
        const realName = opts.registry.resolveName(call.function.name);
        const result = await opts.registry.run(realName, safeParseArgs(call.function.arguments), {
          env: opts.env as unknown as Record<string, string | undefined>,
          store,
          audit: (actor, action, entityType, entityId, detail) => {
            store.insertAudit({
              id: newId("aud"),
              actor,
              action,
              entity_type: entityType,
              entity_id: entityId,
              detail_json: detail ? JSON.stringify(detail) : undefined,
              source: "executive",
              created_at: nowIso(),
              updated_at: nowIso(),
            });
          },
          recordCost,
          runId: opts.runId,
          sessionId: opts.sessionId,
        });
        messages.push({
          role: "tool",
          tool_call_id: call.id,
          content: result.ok ? truncate(JSON.stringify(result.output), 6000) : `ERROR: ${truncate(result.error, 1000)}`,
        });
      }
      continue;
    }
    const text = String(reply.content ?? "");
    opts.onChunk?.(text);
    return { text, iterations, toolsCalled };
  }
  const text = "Reached the tool-call limit for this turn. Narrow the request or ask to continue.";
  opts.onChunk?.(text);
  return { text, iterations, toolsCalled };
}

function safeParseArgs(raw: string): Record<string, unknown> {
  try {
    const v = JSON.parse(raw || "{}");
    return v && typeof v === "object" ? v : {};
  } catch {
    return {};
  }
}

function truncate(s: string, n: number): string {
  return s.length > n ? s.slice(0, n) + "\n…(truncated)" : s;
}

// ---------------------------------------------------------------------------
// Auto-run sweep (automate fixing): start + execute auto-runnable tasks.
// ---------------------------------------------------------------------------

function startAutoRunnable(store: ExecutiveStore, maxJobs: number): { started: string[] } {
  const running = store.countRunningAgentRuns();
  const budget = Math.max(0, maxJobs - running);
  if (budget <= 0) return { started: [] };
  const tasks = store.listAutoRunnableTasks(budget);
  const started: string[] = [];
  for (const t of tasks) {
    if (store.pendingApprovalForTask(t.id)) continue;
    if (store.hasRunningRunForTask(t.id)) continue;
    const now = nowIso();
    store.createAgentRun({
      id: newId("run"),
      task_id: t.id,
      prompt: `AUTO-RUN task: ${t.title}\n\n${t.description ?? ""}\n\nWork through PLAN → IMPLEMENT → VERIFY → REPORT using your tools, then mark the task DONE if the work is complete, or report what is blocking.`,
      status: "RUNNING",
      iterations: 0,
      tools_called: 0,
      started_at: now,
      source: "system",
      created_at: now,
      updated_at: now,
      last_heartbeat: now,
    });
    store.updateTask(t.id, { status: "IN_PROGRESS" } as never);
    started.push(t.id);
  }
  return { started };
}

async function executeAgentRun(store: ExecutiveStore, registry: ToolRegistry, env: Env, run: AgentRun): Promise<void> {
  const maxIter = Math.max(1, Math.min(8, Number(env.AI_PER_RUN_BUDGET) || 8));
  try {
    const result = await agentLoop({
      store,
      registry,
      env,
      runId: run.id,
      sessionId: `run_${run.id}`,
      messages: [buildSystemContext(env), { role: "user", content: run.prompt }],
      maxIterations: maxIter,
    });
    store.finishAgentRun(run.id, "DONE", result.text);
    if (run.task_id) store.updateTask(run.task_id, { status: "DONE", completed_at: nowIso() } as never);
  } catch (err) {
    store.finishAgentRun(run.id, "FAILED", undefined, err instanceof Error ? err.message : String(err));
  }
}

// ---------------------------------------------------------------------------
// Overview
// ---------------------------------------------------------------------------

function buildOverview(store: ExecutiveStore, env: Env): Record<string, unknown> {
  const state = store.ensureExecutiveState();
  const doctor: DoctorLog | null = store.latestDoctorLog();
  const stuck = store.stuckCounts();
  const overrides = store.getBudgetOverrides();
  return {
    generatedAt: new Date().toISOString(),
    state: {
      overallStatus: state.overall_status,
      autonomyMode: state.autonomy_mode,
      paused: state.paused === 1,
      delegateMode: state.delegate_mode === 1,
      fullAccess: state.full_access === 1,
      spendingDisabled: state.spending_disabled === 1,
      deploymentsDisabled: state.deployments_disabled === 1,
    },
    doctor: doctor
      ? {
          lastRunAt: doctor.run_at,
          trigger: doctor.trigger,
          autoFix: doctor.auto_fix === 1,
          summary: safeJson(doctor.summary_json, null),
        }
      : { lastRunAt: null, note: "Doctor has not run yet — it runs on DO wake." },
    maintenance: { staleAgentRuns: stuck.staleAgentRuns, expiredLocalLeases: stuck.expiredLeases },
    budget: {
      daily: Number(env.AI_DAILY_BUDGET) || 0,
      monthly: Number(env.AI_MONTHLY_BUDGET) || 0,
      spentToday: store.spentToday(),
      spentThisMonth: store.spentThisMonth(),
      tokensToday: store.tokensToday(),
      tokensThisMonth: store.tokensThisMonth(),
      overrides,
    },
    counters: {
      openTasks: store.countOpenTasks(),
      pendingApprovals: store.listApprovals({ status: "PENDING" }).length,
      openRisks: store.openRisks().length,
      products: store.listProducts().length,
      pipelineGates: store.listPipelineGates().length,
      localTasksPending: store.listLocalTasks(100).filter((t) => t.status === "PENDING").length,
      agentRunsRunning: store.listAgentRuns(100).filter((r) => r.status === "RUNNING").length,
      remindersOverdue: store.listReminders({ overdueOnly: true }).length,
      notes: store.listNotes(1000).length,
      events: store.listCalendarEvents(undefined, undefined, 1000).length,
      assignmentsOpen: store.listAssignments({ limit: 200 }).filter((a) => a.status === "ASSIGNED" || a.status === "IN_PROGRESS").length,
      contradictionsOpen: store.countOpenContradictions(),
    },
    finance: store.latestSnapshot(),
    products: store.listProducts().map((p) => ({ id: p.id, name: p.name, status: p.status, version: p.version })),
    recentAudit: store.listAudit(8).map((e) => ({ id: e.id, actor: e.actor, action: e.action, entity_type: e.entity_type, entity_id: e.entity_id, created_at: e.created_at })),
    links: {
      health: "/api/health",
      doctor: "/api/doctor/run",
      productivity: "/api/productivity/report",
      overview: "/api/executive/overview",
    },
  };
}

function safeJson(raw: string | null | undefined, fallback: unknown): unknown {
  if (!raw) return fallback;
  try {
    return JSON.parse(raw);
  } catch {
    return fallback;
  }
}

// ---------------------------------------------------------------------------
// Durable Object
// ---------------------------------------------------------------------------

const ALARM_INTERVAL_MS = 6 * 60 * 60 * 1000; // 6 hours

export class ExecutiveAgent {
  private readonly sql: Sql;
  private readonly store: ExecutiveStore;

  constructor(
    private readonly state: DurableObjectState,
    private readonly env: Env,
  ) {
    this.sql = createDoSql(state.storage.sql);
    this.store = new ExecutiveStore(this.sql);
    this.state.blockConcurrencyWhile(async () => {
      this.store.migrate();
      // Fast, local maintenance on wake (no model calls): doctor + start
      // auto-runnable jobs + schedule the next alarm. Auto-job *execution*
      // happens on alarm/cron so the constructor never blocks on the network.
      await this.runWakeMaintenance("wake", { runAutoJobs: false });
    });
  }

  // -- maintenance ----------------------------------------------------------

  private doctorContext() {
    return {
      store: this.store,
      env: this.env as unknown as Record<string, string | undefined>,
      kv: this.env.ZARLINO_KV,
      registry: this.buildRegistry(),
      now: () => new Date().toISOString(),
    };
  }

  private async runWakeMaintenance(trigger: string, opts: { runAutoJobs: boolean }): Promise<void> {
    const autoFix = parseBool(this.env.DOCTOR_AUTOFIX, true);
    try {
      await runDoctor(this.doctorContext(), { autoFix, trigger });
    } catch {
      /* doctor must never break wake */
    }
    // Productivity maintenance (safe, local, no model calls): daily rollup +
    // safe optimizations (stale assignments, budget recalibration/realloc).
    try {
      dailyRollup(this.store);
      optimize(this.store, this.env as unknown as Record<string, string | undefined>);
    } catch {
      /* productivity maintenance must never break wake */
    }
    // Start auto-runnable tasks (bounded, "automate fixing").
    const maxJobs = Math.max(0, Math.min(3, Number(this.env.MAX_AUTO_JOBS) || 2));
    try {
      const { started } = startAutoRunnable(this.store, maxJobs);
      if (opts.runAutoJobs) {
        for (const taskId of started) {
          const run = this.store.listAgentRuns(50).find((r) => r.task_id === taskId && r.status === "RUNNING");
          if (run) {
            await executeAgentRun(this.store, this.buildRegistry(), this.env, run);
          }
        }
      }
    } catch {
      /* auto-run must never break wake */
    }
    // Reschedule: first alarm sooner so fresh deploys start work quickly.
    try {
      const next = Date.now() + (trigger === "wake" ? 5 * 60 * 1000 : ALARM_INTERVAL_MS);
      await this.state.storage.setAlarm(next);
    } catch {
      /* alarm reschedule is best-effort */
    }
  }

  private registryCache: ToolRegistry | null = null;
  private buildRegistry(): ToolRegistry {
    if (this.registryCache) return this.registryCache;
    this.registryCache = buildToolRegistry({
      env: this.env as unknown as Record<string, string | undefined>,
      sink: { recordExecution: (record) => this.store.recordToolExecution(record as never) },
    });
    return this.registryCache;
  }

  /** One bounded bundle of all console-facing data (cheap: every list is capped). */
  private buildDataBundle(): Record<string, unknown> {
    const s = this.store;
    return {
      generatedAt: nowIso(),
      state: s.getExecutiveState(),
      counters: {
        openTasks: s.countOpenTasks(),
        pendingApprovals: s.listApprovals({ status: "PENDING" }).length,
        openRisks: s.openRisks().length,
        products: s.listProducts().length,
        gates: s.listPipelineGates().length,
        localPending: s.listLocalTasks(100).filter((t) => t.status === "PENDING").length,
        runsRunning: s.listAgentRuns(100).filter((r) => r.status === "RUNNING").length,
        overdueReminders: s.listReminders({ overdueOnly: true }).length,
        notes: s.listNotes(1000).length,
        events: s.listCalendarEvents(undefined, undefined, 1000).length,
        assignmentsOpen: s.listAssignments({ limit: 200 }).filter((a) => a.status === "ASSIGNED" || a.status === "IN_PROGRESS").length,
        contradictionsOpen: s.countOpenContradictions(),
      },
      products: s.listProducts().map((p) => ({ id: p.id, name: p.name, category: p.category, status: p.status, version: p.version, price: p.price })),
      productStatus: s.listProducts().flatMap((p) => s.listProductStatus(p.id).slice(0, 5).map((st) => ({ product: p.name, ...st }))),
      projects: s.listProjects(),
      objectives: s.listObjectives(),
      tasks: s.listTasks({ limit: 200 }),
      decisions: s.listDecisions({ limit: 100 }),
      risks: s.openRisks(),
      engineering: s.listEngineeringIssues(),
      marketing: s.listCampaigns().slice(0, 50),
      doctrine: s.listActiveDoctrine(),
      policies: s.listPolicies(),
      memories: s.listMemories().slice(0, 50),
      pipeline: s.listPipelineGates(),
      persons: s.listPersons(),
      finance: s.latestSnapshot(),
      financialSnapshots: s.listSnapshots(6),
      calendar: s.listCalendarEvents(undefined, undefined, 200),
      notes: s.listNotes(200),
      reminders: s.listReminders({ limit: 100 }),
      contacts: s.listContacts(),
      audit: s.listAudit(40),
      files: s.listUploadedFiles(50).map((f) => ({ id: f.id, name: f.name, kind: f.kind, mime: f.mime, sizeBytes: f.size_bytes, created_at: f.created_at })),
      localTasks: s.listLocalTasks(50),
      runs: s.listAgentRuns(50),
      toolExecutions: s.listToolExecutions(40),
      metrics: s.listMetrics(10),
      workstreams: s.listWorkstreams(),
      budget: {
        daily: Number(this.env.AI_DAILY_BUDGET) || 0,
        monthly: Number(this.env.AI_MONTHLY_BUDGET) || 0,
        spentToday: s.spentToday(),
        spentThisMonth: s.spentThisMonth(),
        tokensToday: s.tokensToday(),
        tokensThisMonth: s.tokensThisMonth(),
        overrides: s.getBudgetOverrides(),
      },
      productivity: {
        assignments: s.listAssignments({ limit: 100 }),
        optimizationEvents: s.listOptimizationEvents(20),
        contradictions: s.listContradictions({ limit: 100 }),
        budgets: s.listTokenBudgets(),
      },
      doctorLastRun: (() => {
        const d = s.latestDoctorLog();
        return d ? { runAt: d.run_at, trigger: d.trigger, summary: safeJson(d.summary_json, null) } : null;
      })(),
      localAgent: (() => {
        const st = s.getLocalAgentStatus();
        const ago = s.localAgentSeenAgoMs();
        return st
          ? {
              host: st.host ?? null,
              version: st.version ?? null,
              pid: st.pid ?? null,
              lastSeen: st.last_seen ?? null,
              seenAgoMs: ago,
              online: ago !== null && ago < 5 * 60 * 1000,
              lastClaimAt: st.last_claim_at ?? null,
              pending: s.listLocalTasks(100).filter((t) => t.status === "PENDING").length,
            }
          : { host: null, online: false, seenAgoMs: null, pending: s.listLocalTasks(100).filter((t) => t.status === "PENDING").length };
      })(),
      links: {
        overview: "/api/executive/overview",
        doctor: "/api/doctor/run",
        productivity: "/api/productivity/report",
      },
    };
  }

  // -- alarm ----------------------------------------------------------------

  async alarm(): Promise<void> {
    await this.runWakeMaintenance("alarm", { runAutoJobs: true });
  }

  // -- fetch routing ---------------------------------------------------------

  async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);
    const path = url.pathname;
    const method = request.method;

    if (method === "OPTIONS") return json({ ok: true });

    // Internal scheduled kick (from the Worker scheduled handler).
    if (internal(request) && path === "/api/_scheduled") {
      await this.runWakeMaintenance("cron", { runAutoJobs: true });
      return json({ ok: true, trigger: "cron" });
    }

    // Public health (no auth) — minimal info for uptime monitors.
    if (method === "GET" && path === "/api/health") {
      const doctor = this.store.latestDoctorLog();
      return json({
        ok: true,
        service: "zarlino-executive-os",
        time: new Date().toISOString(),
        schemaVersion: doctor ? safeJson(doctor.summary_json, null) : null,
        doctorLastRun: doctor?.run_at ?? null,
        doctorHealthy: doctor ? (safeJson(doctor.summary_json, null) as { fail?: number } | null)?.fail === 0 : null,
      });
    }

    // Auth (login is public; logout/status/change-password need a session/token).
    if (path === "/api/auth/login" && method === "POST") return this.handleLogin(request);
    if (!authorized(request, this.env, this.store)) {
      return json({ error: "Unauthorized" }, 401);
    }
    if (path === "/api/auth/logout" && method === "POST") return this.handleLogout(request);
    if (path === "/api/auth/status" && method === "GET") return json({ ok: true, authenticated: true });
    if (path === "/api/auth/bootstrap" && method === "POST") return this.handleBootstrap(request);
    if (path === "/api/auth/change-password" && method === "POST") return this.handleChangePassword(request);

    // Overview
    if (path === "/api/executive/overview" && method === "GET") {
      return json(buildOverview(this.store, this.env));
    }

    // Executive control — the Founder's kill-switches + autonomy dials.
    if (path === "/api/executive/control" && method === "POST") {
      const body = (await readBody<{
        paused?: boolean;
        spendingDisabled?: boolean;
        deploymentsDisabled?: boolean;
        delegateMode?: boolean;
        fullAccess?: boolean;
        autonomyMode?: string;
      }>(request).catch(() => ({}))) as {
        paused?: boolean;
        spendingDisabled?: boolean;
        deploymentsDisabled?: boolean;
        delegateMode?: boolean;
        fullAccess?: boolean;
        autonomyMode?: string;
      };
      const patch: Record<string, unknown> = {};
      const boolKeys: Array<[string, boolean | undefined]> = [
        ["paused", body.paused],
        ["spending_disabled", body.spendingDisabled],
        ["deployments_disabled", body.deploymentsDisabled],
        ["delegate_mode", body.delegateMode],
        ["full_access", body.fullAccess],
      ];
      for (const [col, val] of boolKeys) {
        if (typeof val === "boolean") patch[col] = val ? 1 : 0;
      }
      if (body.autonomyMode) {
        const mode = String(body.autonomyMode).toUpperCase();
        if (!["CONTROLLED", "DELEGATED", "FULL"].includes(mode)) return json({ error: "autonomyMode must be CONTROLLED | DELEGATED | FULL" }, 400);
        patch.autonomy_mode = mode;
        // Enabling delegation/full access implies a more permissive autonomy mode.
        if (mode === "DELEGATED" && patch.delegate_mode === undefined) patch.delegate_mode = 1;
        if (mode === "FULL" && patch.full_access === undefined) patch.full_access = 1;
      }
      if (Object.keys(patch).length === 0) return json({ error: "no control fields provided" }, 400);
      this.store.ensureExecutiveState();
      this.store.updateExecutiveState(patch);
      this.store.insertAudit({
        id: newId("aud"),
        actor: "founder",
        action: "executive.control",
        entity_type: "executive_state",
        entity_id: "zarlino",
        detail_json: JSON.stringify(patch),
        source: "api",
        created_at: nowIso(),
        updated_at: nowIso(),
      });
      return json({ ok: true, state: this.store.getExecutiveState() });
    }

    // System Doctor
    if (path === "/api/doctor/run" && method === "POST") {
      const body = (await readBody<{ autoFix?: boolean; trigger?: string }>(request).catch(() => ({}))) as { autoFix?: boolean; trigger?: string };
      const report = await runDoctor(this.doctorContext(), {
        autoFix: body.autoFix ?? true,
        trigger: body.trigger ?? "api",
      });
      return json(report);
    }
    if (path === "/api/doctor/logs" && method === "GET") {
      return json({ logs: this.store.listDoctorLogs(20) });
    }

    // Maintenance
    if (path === "/api/maintenance/run" && method === "POST") {
      const maintenance = this.store.runMaintenance();
      const pruned = this.store.pruneCompletedChatStreams(24 * 60 * 60 * 1000);
      this.store.insertAudit({
        id: newId("aud"),
        actor: "founder",
        action: "maintenance.run",
        entity_type: "system",
        entity_id: "maintenance",
        detail_json: JSON.stringify({ ...maintenance, prunedStreams: pruned }),
        source: "api",
        created_at: nowIso(),
        updated_at: nowIso(),
      });
      return json({ ok: true, ...maintenance, prunedStreams: pruned });
    }

    // Auto-run
    if (path === "/api/autotasks/run" && method === "POST") {
      const { started } = startAutoRunnable(this.store, 2);
      for (const taskId of started) {
        const run = this.store.listAgentRuns(50).find((r) => r.task_id === taskId && r.status === "RUNNING");
        if (run) await executeAgentRun(this.store, this.buildRegistry(), this.env, run);
      }
      return json({ ok: true, started: started.length });
    }

    // Chat
    if (path === "/api/chat" && method === "POST") return this.handleChat(request);
    if (path === "/api/chat/sessions" && method === "GET") {
      return json({ sessions: this.store.listChatSessions(50) });
    }
    if (path === "/api/chat/messages" && method === "GET") {
      const sessionId = url.searchParams.get("sessionId") ?? "default";
      return json({ messages: this.store.listChatMessages(sessionId, 200) });
    }
    if (path === "/api/chat/stream" && method === "GET") {
      const id = url.searchParams.get("streamId") ?? url.searchParams.get("id") ?? "";
      const after = Math.max(0, Number(url.searchParams.get("after")) || 0);
      return json(this.store.readChatStream(id, after));
    }
    if (path === "/api/chat/clear" && method === "POST") {
      const body = (await readBody<{ sessionId?: string }>(request).catch(() => ({}))) as { sessionId?: string };
      this.store.clearChat(body.sessionId ?? "default");
      return json({ ok: true });
    }

    // Files
    if (path === "/api/files" && method === "POST") return this.handleFileUpload(request);
    if (path === "/api/files" && method === "GET") {
      return json({ files: this.store.listUploadedFiles(50).map((f) => ({ fileId: f.id, name: f.name, kind: f.kind, mime: f.mime, sizeBytes: f.size_bytes, created_at: f.created_at })) });
    }
    const fileMatch = /^\/api\/files\/([^/]+)$/.exec(path);
    if (fileMatch) {
      const id = decodeURIComponent(fileMatch[1]!);
      if (method === "GET") {
        const f = this.store.getUploadedFile(id);
        return f ? json(f) : json({ error: "Not found" }, 404);
      }
      if (method === "DELETE") {
        return json({ deleted: this.store.deleteUploadedFile(id) });
      }
    }

    // Tasks
    if (path === "/api/tasks" && method === "GET") {
      return json({ tasks: this.store.listTasks({ limit: 100 }).map((t) => ({ id: t.id, title: t.title, status: t.status, priority: t.priority, project_id: t.project_id, created_at: t.created_at })) });
    }
    if (path === "/api/tasks" && method === "POST") {
      const body = (await readBody<{ title?: string; description?: string; priority?: string }>(request).catch(() => ({}))) as { title?: string; description?: string; priority?: string };
      if (!body.title || !body.title.trim()) return json({ error: "title required" }, 400);
      const task = createTask({ title: body.title, description: body.description, priority: body.priority as never, source: "founder" });
      this.store.insertTask(task);
      return json({ ok: true, id: task.id });
    }
    const taskPatch = /^\/api\/tasks\/([^/]+)$/.exec(path);
    if (taskPatch && method === "PATCH") {
      const id = decodeURIComponent(taskPatch[1]!);
      const task = this.store.getTask(id);
      if (!task) return json({ error: "Task not found" }, 404);
      const body = (await readBody<{ status?: string; priority?: string }>(request).catch(() => ({}))) as { status?: string; priority?: string };
      const patch: Partial<Task> = {};
      if (body.status) {
        if (!["OPEN", "IN_PROGRESS", "BLOCKED", "DONE", "CANCELLED"].includes(body.status)) return json({ error: "Invalid status" }, 400);
        patch.status = body.status as never;
        if (body.status === "DONE") patch.completed_at = nowIso();
      }
      if (body.priority && ["P0", "P1", "P2", "P3"].includes(body.priority)) patch.priority = body.priority as never;
      this.store.updateTask(id, patch);
      return json({ ok: true });
    }

    // Approvals
    if (path === "/api/approvals" && method === "GET") {
      return json({ approvals: this.store.listApprovals({ limit: 50 }) });
    }
    if (path === "/api/approvals/decide" && method === "POST") {
      const body = (await readBody<{ id?: string; decision?: string; note?: string }>(request).catch(() => ({}))) as { id?: string; decision?: string; note?: string };
      const approval = this.store.getApproval(body.id ?? "");
      if (!approval) return json({ error: "Approval not found" }, 404);
      const decision = (body.decision ?? "").toUpperCase();
      if (decision !== "APPROVED" && decision !== "REJECTED" && decision !== "CANCELLED") {
        return json({ error: "decision must be APPROVED | REJECTED | CANCELLED" }, 400);
      }
      this.store.updateApproval(approval.id, {
        status: decision,
        decided_by: "founder",
        decision_note: body.note,
        decided_at: nowIso(),
      } as never);
      this.store.insertAudit({
        id: newId("aud"),
        actor: "founder",
        action: `approval.${decision.toLowerCase()}`,
        entity_type: "approval_requests",
        entity_id: approval.id,
        detail_json: JSON.stringify({ action_label: approval.action_label }),
        source: "api",
        created_at: nowIso(),
        updated_at: nowIso(),
      });
      return json({ ok: true, id: approval.id, status: decision });
    }

    // Tool runner (executes any registered tool; HIGH-risk gating happens here)
    if (path === "/api/tools/run" && method === "POST") {
      const body = (await readBody<{ name?: string; input?: Record<string, unknown>; confirm?: boolean }>(request).catch(() => ({}))) as { name?: string; input?: Record<string, unknown>; confirm?: boolean };
      const registry = this.buildRegistry();
      const tool = registry.getTool(body.name ?? "");
      if (!tool) return json({ error: `Unknown tool: ${body.name}` }, 404);
      if ((tool.riskLevel === "HIGH" || tool.riskLevel === "CRITICAL") && body.confirm !== true) {
        return json({ error: `Tool ${body.name} is ${tool.riskLevel} risk — pass confirm:true to execute.`, riskLevel: tool.riskLevel }, 403);
      }
      const result = await registry.run(tool.name, body.input ?? {}, {
        env: this.env as unknown as Record<string, string | undefined>,
        store: this.store,
        audit: (actor, action, entityType, entityId, detail) => {
          this.store.insertAudit({
            id: newId("aud"),
            actor,
            action,
            entity_type: entityType,
            entity_id: entityId,
            detail_json: detail ? JSON.stringify(detail) : undefined,
            source: "api",
            created_at: nowIso(),
            updated_at: nowIso(),
          });
        },
        recordCost: (c) => this.store.record(c),
      });
      return json(result);
    }

    // Export / import
    if (path === "/api/export" && method === "GET") {
      return json({ ok: true, exportedAt: nowIso(), snapshot: this.store.exportSnapshot() });
    }
    if (path === "/api/import" && method === "POST") {
      const body = (await readBody<{ snapshot?: Record<string, unknown[]> }>(request).catch(() => ({}))) as { snapshot?: Record<string, unknown[]> };
      if (!body.snapshot) return json({ error: "snapshot required" }, 400);
      const result = this.store.importSnapshot(body.snapshot);
      return json({ ok: true, ...result });
    }

    // Settings (budget overrides)
    if (path === "/api/settings" && method === "GET") {
      return json({ budgetOverrides: this.store.getBudgetOverrides(), budget: this.store.getBudgetOverrides() });
    }
    if (path === "/api/settings" && method === "POST") {
      const body = (await readBody<Record<string, unknown>>(request).catch(() => ({}))) as Record<string, unknown>;
      this.store.setBudgetOverrides({
        aiDailyBudget: numOrUndef(body.aiDailyBudget),
        aiMonthlyBudget: numOrUndef(body.aiMonthlyBudget),
        aiPerTaskBudget: numOrUndef(body.aiPerTaskBudget),
        aiPerRunBudget: numOrUndef(body.aiPerRunBudget),
      });
      return json({ ok: true, budgetOverrides: this.store.getBudgetOverrides() });
    }

    // Productivity Manager (budgets, assignments, optimization, contradictions)
    if (path === "/api/productivity/report" && method === "GET") {
      return json(productivityReport(this.store));
    }
    if (path === "/api/productivity/budget" && method === "GET") {
      return json({ budgets: budgetStatus(this.store) });
    }
    if (path === "/api/productivity/assignments" && method === "GET") {
      return json({ assignments: this.store.listAssignments({ limit: 200 }) });
    }
    if (path === "/api/productivity/assign" && method === "POST") {
      const body = (await readBody<{ taskId?: string; roleId?: string; assignee?: string; modelTier?: string; tokenBudget?: number; note?: string }>(request).catch(() => ({}))) as { taskId?: string; roleId?: string; assignee?: string; modelTier?: string; tokenBudget?: number; note?: string };
      if (!body.taskId) return json({ error: "taskId required" }, 400);
      try {
        const id = assignTask(this.store, body.taskId, {
          roleId: body.roleId,
          assignee: body.assignee,
          modelTier: (body.modelTier as never) ?? undefined,
          tokenBudget: body.tokenBudget,
          note: body.note,
        });
        this.store.insertAudit({
          id: newId("aud"),
          actor: "founder",
          action: "productivity.assign",
          entity_type: "productivity_assignments",
          entity_id: id,
          detail_json: JSON.stringify({ taskId: body.taskId, roleId: body.roleId }),
          source: "api",
          created_at: nowIso(),
          updated_at: nowIso(),
        });
        return json({ ok: true, assignmentId: id });
      } catch (err) {
        return json({ error: err instanceof Error ? err.message : String(err) }, 400);
      }
    }
    if (path === "/api/productivity/optimize" && method === "POST") {
      const result = optimize(this.store, this.env as unknown as Record<string, string | undefined>);
      this.store.insertAudit({
        id: newId("aud"),
        actor: "founder",
        action: "productivity.optimize",
        entity_type: "optimization_events",
        entity_id: "productivity",
        detail_json: JSON.stringify(result.summary),
        source: "api",
        created_at: nowIso(),
        updated_at: nowIso(),
      });
      return json({ ok: true, ...result });
    }
    if (path === "/api/productivity/contradictions" && method === "GET") {
      const status = url.searchParams.get("status") ?? undefined;
      return json({ contradictions: this.store.listContradictions({ status: status as never, limit: 200 }), open: this.store.countOpenContradictions() });
    }
    if (path === "/api/productivity/contradictions/detect" && method === "POST") {
      const result = detectContradictions(this.store);
      return json({ ok: true, ...result });
    }
    const contradictionMatch = /^\/api\/productivity\/contradictions\/([^/]+)\/resolve$/.exec(path);
    if (contradictionMatch && method === "POST") {
      const id = decodeURIComponent(contradictionMatch[1]!);
      const body = (await readBody<{ status?: string; resolution?: string }>(request).catch(() => ({}))) as { status?: string; resolution?: string };
      if (!body.resolution) return json({ error: "resolution required" }, 400);
      const existing = this.store.getContradiction(id);
      if (!existing) return json({ error: "Contradiction not found" }, 404);
      const status = body.status === "DISMISSED" ? "DISMISSED" : "RESOLVED";
      this.store.resolveContradiction(id, status, body.resolution, "founder");
      this.store.insertAudit({
        id: newId("aud"),
        actor: "founder",
        action: `productivity.contradiction.${status.toLowerCase()}`,
        entity_type: "contradictions",
        entity_id: id,
        detail_json: JSON.stringify({ resolution: body.resolution }),
        source: "api",
        created_at: nowIso(),
        updated_at: nowIso(),
      });
      return json({ ok: true, id, status });
    }
    if (path === "/api/productivity/metrics" && method === "GET") {
      dailyRollup(this.store);
      return json({ metrics: this.store.listProductivityMetrics(14) });
    }

    // ---------------------------------------------------------------------
    // Full data bundle + domain mutations (powers the console's deep views).
    // One bounded request so the console can render all views without
    // hammering the DO (keeps the SQLite row budget happy).
    // ---------------------------------------------------------------------
    if (path === "/api/data/all" && method === "GET") {
      return json(this.buildDataBundle());
    }

    // Pipeline gates (PATCH) — founder/agent updates a single gate.
    const gateMatch = /^\/api\/pipeline\/gates\/([^/]+)$/.exec(path);
    if (gateMatch && method === "PATCH") {
      const body = (await readBody<{ status?: string; note?: string }>(request).catch(() => ({}))) as { status?: string; note?: string };
      const id = decodeURIComponent(gateMatch[1]!);
      const gate = this.store.listPipelineGates().find((g) => g.id === id);
      if (!gate) return json({ error: "Gate not found" }, 404);
      if (body.status && !["NOT_STARTED", "IN_PROGRESS", "PASSED", "FAILED", "BLOCKED", "SHIPPED"].includes(body.status)) {
        return json({ error: `Invalid gate status: ${body.status}` }, 400);
      }
      this.store.updatePipelineGate(id, { status: (body.status as never) ?? gate.status });
      this.store.insertAudit({
        id: newId("aud"),
        actor: "founder",
        action: "pipeline.gate_update",
        entity_type: "pipeline_gates",
        entity_id: id,
        detail_json: JSON.stringify({ status: body.status, note: body.note }),
        source: "api",
        created_at: nowIso(),
        updated_at: nowIso(),
      });
      return json({ ok: true, gate: this.store.listPipelineGates().find((g) => g.id === id) });
    }

    // Calendar
    if (path === "/api/calendar" && method === "GET") {
      return json({ events: this.store.listCalendarEvents(undefined, undefined, 200) });
    }
    if (path === "/api/calendar" && method === "POST") {
      const body = (await readBody<{ title?: string; startAt?: string; endAt?: string; allDay?: boolean; location?: string }>(request).catch(() => ({}))) as { title?: string; startAt?: string; endAt?: string; allDay?: boolean; location?: string };
      if (!body.title || !body.startAt) return json({ error: "title and startAt required" }, 400);
      const now = nowIso();
      this.store.createCalendarEvent({
        id: newId("cal"),
        title: body.title,
        start_at: body.startAt,
        end_at: body.endAt ?? body.startAt,
        all_day: body.allDay ? 1 : 0,
        location: body.location,
        attendees_json: "[]",
        status: "SCHEDULED",
        source: "founder",
        created_at: now,
        updated_at: now,
      });
      return json({ ok: true });
    }
    const calDelete = /^\/api\/calendar\/([^/]+)$/.exec(path);
    if (calDelete && method === "DELETE") {
      return json({ deleted: this.store.deleteCalendarEvent(decodeURIComponent(calDelete[1]!)) });
    }

    // Notes
    if (path === "/api/notes" && method === "GET") return json({ notes: this.store.listNotes(200) });
    if (path === "/api/notes" && method === "POST") {
      const body = (await readBody<{ title?: string; body?: string; pinned?: boolean }>(request).catch(() => ({}))) as { title?: string; body?: string; pinned?: boolean };
      if (!body.title) return json({ error: "title required" }, 400);
      const now = nowIso();
      this.store.createNote({
        id: newId("note"),
        title: body.title,
        body: body.body ?? "",
        tags_json: "[]",
        pinned: body.pinned ? 1 : 0,
        source: "founder",
        created_at: now,
        updated_at: now,
      });
      return json({ ok: true });
    }
    if (path === "/api/notes/:id" && method === "DELETE") {
      return json({ deleted: this.store.deleteNote(path.split("/")[3]!) });
    }

    // Reminders
    if (path === "/api/reminders" && method === "GET") return json({ reminders: this.store.listReminders({ limit: 100 }) });
    if (path === "/api/reminders" && method === "POST") {
      const body = (await readBody<{ title?: string; dueAt?: string }>(request).catch(() => ({}))) as { title?: string; dueAt?: string };
      if (!body.title || !body.dueAt) return json({ error: "title and dueAt required" }, 400);
      const now = nowIso();
      this.store.createReminder({
        id: newId("rem"),
        title: body.title,
        due_at: body.dueAt,
        done: 0,
        source: "founder",
        created_at: now,
        updated_at: now,
      });
      return json({ ok: true });
    }
    if (path === "/api/reminders/:id/done" && method === "POST") {
      this.store.updateReminder(decodeURIComponent(path.split("/")[3]!), { done: 1, done_at: nowIso() });
      return json({ ok: true });
    }
    if (path === "/api/reminders/:id" && method === "DELETE") {
      return json({ deleted: this.store.deleteReminder(path.split("/")[3]!) });
    }

    // Contacts
    if (path === "/api/contacts" && method === "GET") return json({ contacts: this.store.listContacts() });
    if (path === "/api/contacts" && method === "POST") {
      const body = (await readBody<{ name?: string; role?: string; company?: string; email?: string; phone?: string }>(request).catch(() => ({}))) as { name?: string; role?: string; company?: string; email?: string; phone?: string };
      if (!body.name) return json({ error: "name required" }, 400);
      const now = nowIso();
      this.store.createContact({
        id: newId("ctc"),
        name: body.name,
        role: body.role ?? "",
        company: body.company ?? "",
        email: body.email ?? "",
        phone: body.phone ?? "",
        tags_json: "[]",
        notes: "",
        source: "founder",
        created_at: now,
        updated_at: now,
      });
      return json({ ok: true });
    }

    // Agent runs (long-horizon)
    if (path === "/api/agents/runs" && method === "GET") {
      return json({ runs: this.store.listAgentRuns(50) });
    }
    const runContinue = /^\/api\/agents\/runs\/([^/]+)\/continue$/.exec(path);
    if (runContinue && method === "POST") {
      const id = decodeURIComponent(runContinue[1]!);
      const run = this.store.listAgentRuns(100).find((r) => r.id === id);
      if (!run) return json({ error: "Run not found" }, 404);
      this.store.resetAgentRun(id);
      return json({ ok: true, id });
    }

    // Local agent health beacon (daemon pings every ~30s).
    if (path === "/api/local-agent/heartbeat" && method === "POST") {
      const body = (await readBody<{ host?: string; version?: string; pid?: string; claimed?: boolean }>(request).catch(() => ({}))) as { host?: string; version?: string; pid?: string; claimed?: boolean };
      this.store.upsertLocalAgentHeartbeat({ host: body.host, version: body.version, pid: body.pid, claimed: body.claimed });
      return json({ ok: true, lastSeen: nowIso() });
    }

    // Local machine daemon bridge
    if (path === "/api/local-tasks" && method === "GET") {
      return json({ tasks: this.store.listLocalTasks(50) });
    }
    if (path === "/api/local-tasks/claim" && method === "POST") {
      const body = (await readBody<{ claimer?: string }>(request).catch(() => ({}))) as { claimer?: string };
      const task = this.store.claimLocalTask(body.claimer ?? "api");
      return task ? json({ ok: true, task }) : json({ ok: true, task: null });
    }
    if (path === "/api/local-tasks/heartbeat" && method === "POST") {
      const body = (await readBody<{ id?: string; status?: string }>(request).catch(() => ({}))) as { id?: string; status?: string };
      this.store.heartbeatLocalTask(body.id ?? "", (body.status ?? "RUNNING") as never);
      return json({ ok: true });
    }
    if (path === "/api/local-tasks/finish" && method === "POST") {
      const body = (await readBody<{ id?: string; status?: string; result?: string; error?: string }>(request).catch(() => ({}))) as { id?: string; status?: string; result?: string; error?: string };
      this.store.finishLocalTask(body.id ?? "", (body.status === "FAILED" ? "FAILED" : "DONE") as never, body.result, body.error);
      return json({ ok: true });
    }

    return json({ error: "Not found", path }, 404);
  }

  // -- chat ------------------------------------------------------------------

  private async handleChat(request: Request): Promise<Response> {
    const body = (await readBody<{ message?: string; sessionId?: string; stream?: boolean }>(request).catch(() => ({}))) as { message?: string; sessionId?: string; stream?: boolean };
    const message = (body.message ?? "").trim();
    if (!message) return json({ error: "message required" }, 400);
    const sessionId = body.sessionId ?? "default";
    const registry = this.buildRegistry();

    // Record the user message.
    const now = nowIso();
    this.store.insertChatMessage({ id: newId("msg"), session_id: sessionId, role: "user", content: message, source: "founder", created_at: now, updated_at: now });

    // Build history (system + last N messages).
    const history = this.store.listChatMessages(sessionId, 40);
    const messages: ChatMsg[] = [buildSystemContext(this.env)];
    for (const m of history) {
      if (m.role === "user") messages.push({ role: "user", content: m.content });
      else if (m.role === "assistant") messages.push({ role: "assistant", content: m.content });
    }

    if (body.stream === true) {
      // Kick off async streaming into a chat_streams row.
      const streamId = newId("str");
      this.store.openChatStream(streamId, sessionId, nowIso());
      const promise = (async () => {
        try {
          const result = await agentLoop({
            store: this.store,
            registry,
            env: this.env,
            sessionId,
            messages,
            onChunk: (text) => this.store.appendChatStreamChunk(streamId, text),
            maxIterations: 8,
          });
          const now2 = nowIso();
          this.store.insertChatMessage({ id: newId("msg"), session_id: sessionId, role: "assistant", content: result.text, source: "executive", created_at: now2, updated_at: now2 });
          this.store.completeChatStream(streamId);
        } catch (err) {
          this.store.completeChatStream(streamId, err instanceof Error ? err.message : String(err));
        }
      })();
      // Best-effort: keep the work alive past the response where the runtime
      // exposes waitUntil on the DO state.
      (this.state as unknown as { waitUntil?: (p: Promise<unknown>) => void }).waitUntil?.(promise);
      return json({ ok: true, streamId, sessionId, note: "Poll /api/chat/stream?id=… to read the reply." });
    }

    try {
      const result = await agentLoop({ store: this.store, registry, env: this.env, sessionId, messages, maxIterations: 8 });
      const now2 = nowIso();
      this.store.insertChatMessage({ id: newId("msg"), session_id: sessionId, role: "assistant", content: result.text, source: "executive", created_at: now2, updated_at: now2 });
      return json({ ok: true, reply: result.text, iterations: result.iterations, toolsCalled: result.toolsCalled });
    } catch (err) {
      return json({ error: err instanceof Error ? err.message : String(err) }, 502);
    }
  }

  private async handleFileUpload(request: Request): Promise<Response> {
    const body = (await readBody<{ name?: string; mime?: string; dataBase64?: string }>(request).catch(() => ({}))) as { name?: string; mime?: string; dataBase64?: string };
    const name = (body.name ?? "").trim();
    if (!name) return json({ error: "name required" }, 400);
    if (!body.dataBase64) return json({ error: "dataBase64 required" }, 400);
    if (body.dataBase64.length > 15_000_000) return json({ error: "file too large (max ~11 MB decoded)" }, 413);
    const mime = body.mime ?? "application/octet-stream";
    const now = nowIso();
    this.store.createUploadedFile({
      id: newId("up"),
      name,
      mime,
      size_bytes: Math.round((body.dataBase64.length * 3) / 4),
      data_base64: body.dataBase64,
      kind: kindFromMime(mime),
      source: "founder",
      created_at: now,
      updated_at: now,
    });
    const f = this.store.listUploadedFiles(1)[0];
    return json({ ok: true, fileId: f!.id, kind: f!.kind, sizeBytes: f!.size_bytes });
  }

  // -- auth handlers ----------------------------------------------------------

  private async handleLogin(request: Request): Promise<Response> {
    const body = (await readBody<{ username?: string; password?: string }>(request).catch(() => ({}))) as { username?: string; password?: string };
    const creds = this.store.getFounderCredentials();
    if (!creds) {
      return json({ error: "No founder account configured. Use POST /api/auth/bootstrap (with the FOUNDER_AUTH_TOKEN) to create one." }, 401);
    }
    const now = new Date().toISOString();
    if (creds.locked_until && creds.locked_until > now) {
      return json({ error: "Account temporarily locked after repeated failures. Try again later." }, 423);
    }
    if (creds.username === (body.username ?? "") && (await verifyPassword(body.password ?? "", creds.password_hash))) {
      this.store.updateFounderCredentials({ failed_attempts: 0, locked_until: null });
      const token = crypto.randomUUID();
      const expires = new Date(Date.now() + 30 * 24 * 60 * 60 * 1000).toISOString();
      this.store.createSession({ token, username: creds.username, expires_at: expires });
      return json({ ok: true, token, expiresAt: expires });
    }
    const attempts = (creds.failed_attempts ?? 0) + 1;
    this.store.updateFounderCredentials({
      failed_attempts: attempts,
      locked_until: attempts >= 5 ? new Date(Date.now() + 15 * 60 * 1000).toISOString() : undefined,
    });
    return json({ error: "Invalid credentials" }, 401);
  }

  private async handleBootstrap(request: Request): Promise<Response> {
    const body = (await readBody<{ username?: string; password?: string }>(request).catch(() => ({}))) as { username?: string; password?: string };
    const username = (body.username ?? "").trim();
    const password = body.password ?? "";
    if (username.length < 3) return json({ error: "username must be at least 3 characters" }, 400);
    if (password.length < 8) return json({ error: "password must be at least 8 characters" }, 400);
    this.store.setFounderCredentials({ username, password_hash: await hashPassword(password) });
    return json({ ok: true, note: "Founder credentials configured. Log in via /api/auth/login." });
  }

  /**
   * Change the Founder password. Requires an authenticated session/token so
   * only the Founder (or someone with API access) can rotate it. The current
   * password must be verified; the new password replaces it and any other
   * active sessions are invalidated so a stolen session dies immediately.
   */
  private async handleChangePassword(request: Request): Promise<Response> {
    const body = (await readBody<{ currentPassword?: string; newPassword?: string }>(request).catch(() => ({}))) as { currentPassword?: string; newPassword?: string };
    const creds = this.store.getFounderCredentials();
    if (!creds) return json({ error: "No founder account configured." }, 401);
    const newPassword = body.newPassword ?? "";
    if (newPassword.length < 4) return json({ error: "new password must be at least 4 characters" }, 400);
    if (!(await verifyPassword(body.currentPassword ?? "", creds.password_hash))) {
      return json({ error: "Current password is incorrect" }, 401);
    }
    this.store.setFounderCredentials({ username: creds.username, password_hash: await hashPassword(newPassword) });
    // Invalidate every session except the caller's so other logged-in devices
    // must re-authenticate with the new password.
    const caller = request.headers.get("Authorization") || "";
    const callerToken = caller.startsWith("Bearer ") ? caller.slice(7) : null;
    this.store.deleteSessionsExcept(callerToken);
    this.store.insertAudit({
      id: newId("aud"),
      actor: "founder",
      action: "auth.password_changed",
      entity_type: "founder_credentials",
      entity_id: creds.id,
      detail_json: JSON.stringify({ note: "Founder rotated password; other sessions invalidated." }),
      source: "api",
      created_at: nowIso(),
      updated_at: nowIso(),
    });
    return json({ ok: true, note: "Password changed. Other sessions were signed out." });
  }

  private async handleLogout(request: Request): Promise<Response> {
    const header = request.headers.get("Authorization") || "";
    if (header.startsWith("Bearer ")) this.store.deleteSession(header.slice(7));
    return json({ ok: true });
  }
}

// ---------------------------------------------------------------------------
// Chat SSE relay (worker-level): forwards the DO-buffered agent reply to the
// browser as Server-Sent Events. The DO runs the agent loop and buffers
// deltas into chat_streams; we poll it and push `data:` events, ending with
// `event: done`. The caller's auth header is forwarded for each poll.
// ---------------------------------------------------------------------------

async function handleChatSse(request: Request, env: Env): Promise<Response> {
  const url = new URL(request.url);
  const streamId = url.searchParams.get("streamId") ?? "";
  const auth = request.headers.get("Authorization") ?? "";
  if (!streamId) return json({ error: "streamId required" }, 400);

  const id = env.EXECUTIVE_AGENT.idFromName("zarlino");
  const stub = env.EXECUTIVE_AGENT.get(id);
  const encoder = new TextEncoder();

  const body = new ReadableStream<Uint8Array>({
    async start(controller) {
      let after = 0;
      const timeout = 5 * 60 * 1000;
      const started = Date.now();
      try {
        // Poll the DO's buffered stream until it completes.
        // eslint-disable-next-line no-constant-condition
        while (true) {
          if (Date.now() - started > timeout) {
            controller.enqueue(encoder.encode(`event: error\ndata: ${JSON.stringify({ error: "stream timeout" })}\n\n`));
            break;
          }
          const res = await stub.fetch(
            new Request(`https://executive.internal/api/chat/stream?streamId=${encodeURIComponent(streamId)}&after=${after}`, {
              headers: { Authorization: auth },
            }),
          );
          if (!res.ok) {
            controller.enqueue(encoder.encode(`event: error\ndata: ${JSON.stringify({ error: `HTTP ${res.status}` })}\n\n`));
            break;
          }
          const data = (await res.json()) as { chunks: string[]; nextIndex: number; complete: boolean; error?: string };
          for (const chunk of data.chunks ?? []) {
            controller.enqueue(encoder.encode(`data: ${JSON.stringify({ text: chunk })}\n\n`));
          }
          after = data.nextIndex ?? after;
          if (data.complete) {
            controller.enqueue(encoder.encode(`event: done\ndata: ${JSON.stringify({ error: data.error ?? null })}\n\n`));
            break;
          }
          await new Promise((r) => setTimeout(r, 250));
        }
      } catch (err) {
        controller.enqueue(encoder.encode(`event: error\ndata: ${JSON.stringify({ error: err instanceof Error ? err.message : String(err) })}\n\n`));
      } finally {
        try {
          controller.close();
        } catch {
          /* already closed */
        }
      }
    },
    cancel() {
      /* client disconnected — stop polling */
    },
  });

  return new Response(body, {
    headers: {
      "Content-Type": "text/event-stream",
      "Cache-Control": "no-cache, no-transform",
      Connection: "keep-alive",
      "X-Accel-Buffering": "no",
    },
  });
}

function numOrUndef(v: unknown): number | undefined {
  const n = Number(v);
  return Number.isFinite(n) && n > 0 ? n : undefined;
}

// ---------------------------------------------------------------------------
// Worker entry
// ---------------------------------------------------------------------------

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);
    const path = url.pathname;
    if (path.startsWith("/api/")) {
      // Chat SSE: stream the DO-buffered reply to the browser as real
      // Server-Sent Events (the DO runs the agent loop; we relay chunks).
      if (path === "/api/chat/sse" && request.method === "GET") {
        return handleChatSse(request, env);
      }
      const id = env.EXECUTIVE_AGENT.idFromName("zarlino");
      const stub = env.EXECUTIVE_AGENT.get(id);
      return stub.fetch(request);
    }
    // Serve the Founder web console from worker static assets.
    if (env.ASSETS) {
      return env.ASSETS.fetch(request);
    }
    return json({ error: "Executive OS API only — build the console (npm run build:console) to enable assets." }, 404);
  },

  async scheduled(controller: ScheduledController, env: Env): Promise<void> {
    const id = env.EXECUTIVE_AGENT.idFromName("zarlino");
    const stub = env.EXECUTIVE_AGENT.get(id);
    // Internal, auth-free kick that runs doctor + auto-run + reschedules.
    await stub.fetch(
      new Request("https://executive.internal/api/_scheduled", {
        method: "POST",
        headers: { [INTERNAL_HEADER]: INTERNAL_SECRET },
      }),
    );
    controller.noRetry();
  },
};
