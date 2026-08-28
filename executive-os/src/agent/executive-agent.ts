/**
 * ExecutiveAgent — the single Executive Agent (spec §1, §2).
 *
 * ONE agent, four reasoning lenses (CEO/CFO/CTO + Chief of Staff). It holds
 * durable executive state (Agent.setState, SQLite-backed), runs the decision
 * pipeline, retrieves memory, applies Founder Doctrine, enforces authority in
 * backend code, converts decisions into tasks, tracks approvals, and
 * schedules the daily brief and weekly review.
 *
 * The worker calls its methods via Durable Object RPC (getAgentByName).
 */

import { Agent } from "agents";
import type { FiberRecoveryContext } from "agents";
import { parseConfig, type ExecutiveConfig } from "../config";
import { newId, newSessionId, nowIso } from "../ids";
import { fromDoSql } from "../db/sql";
import { ExecutiveStore } from "../db/store";
import { ModelGateway } from "../core/model/gateway";
import { buildProviders, mockProvider } from "../core/model/providers";
import { DefaultCostTracker, type BudgetConfig, type CostStorage } from "../core/model/cost";
import { fetchDeepSeekBalance, type ProviderBalance } from "../integrations/deepseek";
import { DecisionEngine, type DecisionRequest, type DecisionResult, type EngineSink, type ExecutiveContext } from "../core/decision/engine";
import { GatewayReasoner } from "./reasoner";
import { buildToolRegistry } from "./tools";
import { loadExecutiveContext } from "./context";
import { generateDailyBrief, generateWeeklyReview } from "./briefings";
import { tasksFromDecision, createTask, canTransitionTask } from "../core/tasks/model";
import { transitionApproval, createApprovalRequest, canTransition } from "../core/approvals/queue";
import { defaultActions, type AuthorityConfig } from "../core/authority/engine";
import {
  generateToken,
  hashPassword,
  isExpired,
  sessionExpiry,
  validatePassword,
  validateUsername,
  verifyPassword,
} from "../core/auth/passwords";
import { seedIfEmpty } from "../seed/seed";
import { basePolicy, authorityBlock, doctrineBlock, memoryBlock, objectiveBlock, stateBlock, toolPolicyBlock } from "./prompts";
import type { ModelMessage } from "../core/model/types";
import type { ToolResult } from "../core/tools/types";
import type {
  AgentRun,
  ApprovalRequest,
  AuditEvent,
  ChatMessage,
  Decision,
  DoctrineRule,
  ExecutiveAgentState,
  FinancialSnapshot,
  LocalTask,
  Memory,
  Metric,
  Person,
  PipelineGate,
  Product,
  Task,
  ToolExecution,
  UploadedFile,
} from "../types";

export interface ExecutiveEnv {
  ExecutiveAgent: DurableObjectNamespace<ExecutiveAgent>;
  ASSETS?: Fetcher;
  DEEPSEEK_API_KEY?: string;
  DEEPSEEK_VISION_MODEL?: string;
  OPENAI_API_KEY?: string;
  ANTHROPIC_API_KEY?: string;
  CF_API_TOKEN?: string;
  CF_ACCOUNT_ID?: string;
  GITHUB_TOKEN?: string;
  GITHUB_OWNER?: string;
  GITHUB_REPO?: string;
  WEBSITE_URL?: string;
  FOUNDER_AUTH_TOKEN?: string;
  ENVIRONMENT?: string;
  AI_GATEWAY_ENABLED?: string;
  AI_GATEWAY_ACCOUNT_ID?: string;
  AI_GATEWAY_NAME?: string;
  [key: string]: unknown;
}

export interface AskResult {
  reply: string;
  sessionId: string;
  intent: "decision" | "query" | "command";
  decision?: DecisionResult;
  runId: string;
}

/** Cap on a single tool result fed back to the model (keeps context bounded). */
const MAX_TOOL_RESULT_CHARS = 8_000;

/** Convert a registry execution result into a readable tool result for the model. */
function summarizeToolResult(name: string, result: ToolResult): unknown {
  if (result.status === "success") {
    return { tool: name, ok: true, output: truncateForModel(name, result.output) };
  }
  if (result.status === "approval_required") {
    return {
      tool: name,
      ok: false,
      approvalRequired: true,
      approvalId: result.approval.id,
      note: "This action needs the Founder's approval — tell the Founder to review it in the Approvals console.",
    };
  }
  if (result.status === "denied") {
    return { tool: name, ok: false, denied: true, reason: result.reason };
  }
  return { tool: name, ok: false, error: result.message };
}

/** Serialize + truncate arbitrary tool output so long payloads never blow context. */
function truncateForModel(name: string, output: unknown): unknown {
  try {
    const text = typeof output === "string" ? output : JSON.stringify(output);
    if (text.length <= MAX_TOOL_RESULT_CHARS) return output;
    const isFileRead = /get_file|get_website_source|read_file|plugin_ui/.test(name);
    const hint = isFileRead
      ? " — this was a file read that is longer than the model context allows. Re-call the same tool with an offset= parameter to read the next chunk (the tool reports totalChars + the next offset when hasMore is true)."
      : " — result truncated to fit the model context; request a narrower view if you need the rest.";
    return `${text.slice(0, MAX_TOOL_RESULT_CHARS)}\n…[truncated ${text.length - MAX_TOOL_RESULT_CHARS} chars${hint}]`;
  } catch {
    return output;
  }
}

export class ExecutiveAgent extends Agent<ExecutiveEnv, ExecutiveAgentState> {
  override initialState: ExecutiveAgentState = {
    initialized: false,
    overallStatus: "GREEN",
    topPriorities: [],
    paused: false,
    autonomyMode: "CONTROLLED",
    spendingDisabled: false,
    deploymentsDisabled: false,
    delegateMode: false,
    fullAccess: false,
  };

  private _store?: ExecutiveStore;
  private _config?: ExecutiveConfig;
  private _gateway?: ModelGateway;
  private _reasoner?: GatewayReasoner;
  private _registry?: ReturnType<typeof buildToolRegistry>;
  private _runId?: string;

  // -------------------------------------------------------------------------
  // Lazy dependencies
  // -------------------------------------------------------------------------

  private get store(): ExecutiveStore {
    if (!this._store) {
      // `this.sql` relies on `this` (the Agent instance), so wrap it in an
      // arrow that keeps the binding — a bare reference would lose it.
      this._store = new ExecutiveStore(
        fromDoSql((strings, ...values) => this.sql(strings, ...values)),
      );
    }
    return this._store;
  }

  private get config(): ExecutiveConfig {
    if (!this._config) this._config = parseConfig(stringVars(this.env));
    return this._config;
  }

  /**
   * Effective AI cost budgets = persisted Founder overrides (settings table)
   * layered over the env defaults. Lets the Founder extend the budget from the
   * UI; the cost tracker re-reads this on every check so it applies instantly.
   */
  private effectiveBudgets(): BudgetConfig {
    const o = this.store.getBudgetOverrides();
    return {
      daily: o.aiDailyBudget ?? this.config.aiDailyBudget,
      monthly: o.aiMonthlyBudget ?? this.config.aiMonthlyBudget,
      perTask: o.aiPerTaskBudget ?? this.config.aiPerTaskBudget,
      perRun: o.aiPerRunBudget ?? this.config.aiPerRunBudget,
    };
  }

  /** Public view of effective budgets + overrides (for the console). */
  effectiveBudgetView(): {
    daily: number;
    monthly: number;
    perTask: number;
    perRun: number;
    overrides: { aiDailyBudget?: number; aiMonthlyBudget?: number; aiPerTaskBudget?: number; aiPerRunBudget?: number };
  } {
    const b = this.effectiveBudgets();
    return { daily: b.daily, monthly: b.monthly, perTask: b.perTask, perRun: b.perRun, overrides: this.store.getBudgetOverrides() };
  }

  /** Budget snapshot for the console: effective budgets + current spend/tokens. */
  aiBudgetSnapshot(): {
    daily: number;
    monthly: number;
    perTask: number;
    perRun: number;
    overrides: { aiDailyBudget?: number; aiMonthlyBudget?: number; aiPerTaskBudget?: number; aiPerRunBudget?: number };
    spendToday: number;
    spendMonth: number;
    tokensToday: number;
    tokensMonth: number;
  } {
    const view = this.effectiveBudgetView();
    return {
      ...view,
      spendToday: this.store.spentToday(),
      spendMonth: this.store.spentThisMonth(),
      tokensToday: this.store.tokensToday(),
      tokensMonth: this.store.tokensThisMonth(),
    };
  }

  /**
   * Persist AI budget overrides (Founder extends the budget from the console).
   * Only positive finite numbers are accepted; undefined keeps the env default.
   * Returns the new effective view so the UI can update instantly.
   */
  setAiBudget(o: { aiDailyBudget?: number; aiMonthlyBudget?: number; aiPerTaskBudget?: number; aiPerRunBudget?: number }): { ok: boolean; view: ReturnType<ExecutiveAgent["effectiveBudgetView"]>; error?: string } {
    const clamp = (k: "aiDailyBudget" | "aiMonthlyBudget" | "aiPerTaskBudget" | "aiPerRunBudget", v: number | undefined): number | undefined => {
      if (v === undefined) return undefined;
      if (!Number.isFinite(v) || v <= 0) throw new Error(`Invalid ${k}: must be a positive number`);
      return v;
    };
    try {
      this.store.setBudgetOverrides({
        aiDailyBudget: clamp("aiDailyBudget", o.aiDailyBudget),
        aiMonthlyBudget: clamp("aiMonthlyBudget", o.aiMonthlyBudget),
        aiPerTaskBudget: clamp("aiPerTaskBudget", o.aiPerTaskBudget),
        aiPerRunBudget: clamp("aiPerRunBudget", o.aiPerRunBudget),
      });
      const view = this.effectiveBudgetView();
      this.audit("executive", "ai.budget_updated", "settings", undefined, {
        daily: view.daily,
        monthly: view.monthly,
        perTask: view.perTask,
        perRun: view.perRun,
        by: "founder",
      });
      return { ok: true, view };
    } catch (err) {
      return { ok: false, view: this.effectiveBudgetView(), error: err instanceof Error ? err.message : String(err) };
    }
  }

  /**
   * AI-provider account balance (DeepSeek /user/balance). Cached in the
   * settings table for a short TTL so overview renders don't hammer the
   * provider; `force` bypasses the cache. Falls back to the cache when the
   * live fetch fails (better a stale number than nothing).
   */
  async providerBalance(force = false): Promise<ProviderBalance & { cached: boolean }> {
    const env = stringVars(this.env);
    const apiKey = env.DEEPSEEK_API_KEY;
    if (!apiKey) {
      return {
        provider: "deepseek",
        isAvailable: false,
        fetchedAt: new Date().toISOString(),
        balances: [],
        totalUsdEstimate: null,
        cached: false,
        error: "No DEEPSEEK_API_KEY configured — add it as a Worker secret.",
      };
    }
    const cache = this.store.getProviderBalanceCache();
    const cacheTtlMs = 60_000; // re-check the provider at most once a minute
    const cacheAge = cache.updatedAt ? Date.now() - Date.parse(cache.updatedAt) : Number.POSITIVE_INFINITY;
    if (!force && cache.json && cacheAge < cacheTtlMs) {
      try {
        return { ...(JSON.parse(cache.json) as ProviderBalance), cached: true };
      } catch {
        /* corrupt cache — fall through and refetch */
      }
    }
    try {
      const balance = await fetchDeepSeekBalance(apiKey, { timeoutMs: 15_000, cnyToUsd: Number(env.DEEPSEEK_CNY_TO_USD ?? 0.14) || 0.14 });
      this.store.setProviderBalanceCache(JSON.stringify(balance));
      this.audit("executive", "ai.provider_balance", "settings", undefined, {
        isAvailable: balance.isAvailable,
        totalUsdEstimate: balance.totalUsdEstimate,
        balances: balance.balances,
      });
      return { ...balance, cached: false };
    } catch (err) {
      // Stale-but-usable cache beats an error if we have one.
      if (cache.json) {
        try {
          return { ...(JSON.parse(cache.json) as ProviderBalance), cached: true, error: `Live fetch failed (${err instanceof Error ? err.message : String(err)}); showing cached balance.` };
        } catch {
          /* ignore */
        }
      }
      return {
        provider: "deepseek",
        isAvailable: false,
        fetchedAt: new Date().toISOString(),
        balances: [],
        totalUsdEstimate: null,
        cached: false,
        error: err instanceof Error ? err.message : String(err),
      };
    }
  }

  // -------------------------------------------------------------------------
  // Planner (calendar / notes / reminders / contacts — migration v14)
  // -------------------------------------------------------------------------

  listCalendarEvents(opts: { from?: string; to?: string; limit?: number } = {}) {
    return this.store.listCalendarEvents(opts.from, opts.to, opts.limit ?? 100).map((e) => ({
      id: e.id,
      title: e.title,
      description: e.description ?? null,
      startAt: e.start_at,
      endAt: e.end_at ?? null,
      allDay: Boolean(e.all_day),
      location: e.location ?? null,
      status: e.status,
    }));
  }
  createCalendarEvent(input: { title: string; startAt: string; endAt?: string; allDay?: boolean; location?: string; description?: string }) {
    const start = validIso(input.startAt);
    if (!start) throw new Error(`Invalid startAt (expected ISO-8601): ${input.startAt}`);
    const end = input.endAt ? validIso(input.endAt) : undefined;
    if (input.endAt && !end) throw new Error(`Invalid endAt: ${input.endAt}`);
    if (end && end <= start) throw new Error("endAt must be after startAt");
    const now = nowIso();
    const ev = {
      id: newId("evt"),
      title: input.title.trim(),
      description: input.description?.trim() ?? null,
      start_at: start,
      end_at: end ?? null,
      all_day: input.allDay ? 1 : 0,
      location: input.location?.trim() ?? null,
      attendees_json: "[]",
      status: "SCHEDULED" as const,
      source: "executive" as const,
      created_at: now,
      updated_at: now,
    };
    this.store.createCalendarEvent(ev as never);
    this.audit("executive", "calendar.event_created", "calendar_events", ev.id, { title: ev.title, startAt: start });
    return { id: ev.id, title: ev.title, startAt: start, endAt: end ?? null, status: "SCHEDULED" };
  }
  updateCalendarEvent(id: string, patch: { title?: string; startAt?: string; endAt?: string; status?: string; location?: string }) {
    const p: Record<string, string | number> = {};
    if (patch.title !== undefined) p.title = patch.title.trim();
    if (patch.startAt !== undefined) {
      const s = validIso(patch.startAt);
      if (!s) throw new Error(`Invalid startAt: ${patch.startAt}`);
      p.start_at = s;
    }
    if (patch.endAt !== undefined) {
      const e = validIso(patch.endAt);
      if (!e) throw new Error(`Invalid endAt: ${patch.endAt}`);
      p.end_at = e;
    }
    if (patch.status !== undefined) p.status = patch.status;
    if (patch.location !== undefined) p.location = patch.location.trim();
    const updated = this.store.updateCalendarEvent(id, p as never);
    if (!updated) throw new Error(`Event ${id} not found`);
    this.audit("executive", "calendar.event_updated", "calendar_events", id, { title: updated.title, status: updated.status });
    return { id, title: updated.title, startAt: updated.start_at, status: updated.status };
  }
  deleteCalendarEvent(id: string) {
    const deleted = this.store.deleteCalendarEvent(id);
    this.audit("executive", "calendar.event_deleted", "calendar_events", id, { deleted });
    return { deleted };
  }

  listNotes(limit = 100) {
    return this.store.listNotes(limit).map((n) => ({
      id: n.id,
      title: n.title,
      body: n.body ?? null,
      tags: safeJsonArray(n.tags_json),
      pinned: Boolean(n.pinned),
      updatedAt: n.updated_at,
    }));
  }
  createNote(input: { title: string; body?: string; tags?: string[]; pinned?: boolean }) {
    const now = nowIso();
    const note = {
      id: newId("nt"),
      title: input.title.trim(),
      body: input.body?.trim() ?? null,
      tags_json: JSON.stringify((input.tags ?? []).slice(0, 10)),
      pinned: input.pinned ? 1 : 0,
      source: "executive" as const,
      created_at: now,
      updated_at: now,
    };
    this.store.createNote(note as never);
    this.audit("executive", "notes.note_created", "notes", note.id, { title: note.title });
    return { id: note.id, title: note.title, pinned: Boolean(input.pinned) };
  }
  deleteNote(id: string) {
    const deleted = this.store.deleteNote(id);
    this.audit("executive", "notes.note_deleted", "notes", id, { deleted });
    return { deleted };
  }

  listReminders(opts: { overdueOnly?: boolean; limit?: number } = {}) {
    const now = new Date().toISOString();
    return this.store.listReminders({ overdueOnly: opts.overdueOnly, limit: opts.limit ?? 100 }).map((r) => ({
      id: r.id,
      title: r.title,
      dueAt: r.due_at,
      done: Boolean(r.done),
      overdue: !r.done && r.due_at < now,
    }));
  }
  createReminder(input: { title: string; dueAt: string }) {
    const due = validIso(input.dueAt);
    if (!due) throw new Error(`Invalid dueAt (expected ISO-8601): ${input.dueAt}`);
    const now = nowIso();
    const rem = {
      id: newId("rem"),
      title: input.title.trim(),
      due_at: due,
      done: 0,
      done_at: null,
      source: "executive" as const,
      created_at: now,
      updated_at: now,
    };
    this.store.createReminder(rem as never);
    this.audit("executive", "reminders.reminder_created", "reminders", rem.id, { title: rem.title, dueAt: due });
    return { id: rem.id, title: rem.title, dueAt: due, overdue: due < now };
  }
  markReminderDone(id: string) {
    const updated = this.store.markReminderDone(id);
    if (!updated) throw new Error(`Reminder ${id} not found`);
    this.audit("executive", "reminders.reminder_done", "reminders", id, { title: updated.title });
    return { id, title: updated.title, done: true };
  }
  deleteReminder(id: string) {
    const deleted = this.store.deleteReminder(id);
    this.audit("executive", "reminders.reminder_deleted", "reminders", id, { deleted });
    return { deleted };
  }

  listContacts(limit = 200) {
    return this.store.listContacts(limit).map((c) => ({
      id: c.id,
      name: c.name,
      role: c.role ?? null,
      company: c.company ?? null,
      email: c.email ?? null,
      phone: c.phone ?? null,
      tags: safeJsonArray(c.tags_json),
    }));
  }
  addContact(input: { name: string; role?: string; company?: string; email?: string; phone?: string; tags?: string[]; notes?: string }) {
    const now = nowIso();
    const contact = {
      id: newId("ct"),
      name: input.name.trim(),
      role: input.role?.trim() ?? null,
      company: input.company?.trim() ?? null,
      email: input.email?.trim() ?? null,
      phone: input.phone?.trim() ?? null,
      tags_json: JSON.stringify((input.tags ?? []).slice(0, 10)),
      notes: input.notes?.trim() ?? null,
      source: "executive" as const,
      created_at: now,
      updated_at: now,
    };
    this.store.createContact(contact as never);
    this.audit("executive", "contacts.contact_added", "contacts", contact.id, { name: contact.name, company: contact.company ?? null });
    return { id: contact.id, name: contact.name, company: contact.company ?? null };
  }
  deleteContact(id: string) {
    const deleted = this.store.deleteContact(id);
    this.audit("executive", "contacts.contact_deleted", "contacts", id, { deleted });
    return { deleted };
  }

  private get gateway(): ModelGateway {
    if (!this._gateway) {
      const env = stringVars(this.env);
      const hasAnyKey = Boolean(env.DEEPSEEK_API_KEY || env.OPENAI_API_KEY || env.ANTHROPIC_API_KEY || env.CF_API_TOKEN);
      const providers = hasAnyKey
        ? buildProviders(env, false, { maxRetries: this.config.maxRetries, timeoutMs: this.config.requestTimeoutMs })
        : [mockProvider()];
      // Budgets are resolved dynamically so Founder overrides (settings table)
      // take effect immediately — no rebuild, no redeploy needed.
      const costTracker = new DefaultCostTracker(
        () => this.effectiveBudgets(),
        this.store as CostStorage,
        this._runId,
      );
      this._gateway = new ModelGateway({
        config: {
          modelTierCheap: this.config.modelTierCheap,
          modelTierMedium: this.config.modelTierMedium,
          modelTierStrong: this.config.modelTierStrong,
          modelTierStrongest: this.config.modelTierStrongest,
        },
        providers,
        cost: costTracker,
        gatewayBaseUrl: env.AI_GATEWAY_ENABLED === "true" && env.AI_GATEWAY_ACCOUNT_ID && env.AI_GATEWAY_NAME
          ? `https://gateway.ai.cloudflare.com/v1/${env.AI_GATEWAY_ACCOUNT_ID}/${env.AI_GATEWAY_NAME}`
          : undefined,
        onBudgetExceeded: (reason) => {
          this.audit("executive", "ai.budget_exceeded", undefined, undefined, { reason });
        },
        onCall: (call) => {
          this.audit("executive", "ai.call", "model_cost", undefined, {
            provider: call.provider,
            model: call.model,
            taskType: call.taskType,
            cost: call.cost,
          });
        },
        enforceTokenBudget: this.config.aiTokenBudgetEnforce,
        dailyTokenBudget: this.config.aiDailyTokenBudget,
        tokensToday: () => this.store.tokensToday(),
      });
    }
    return this._gateway;
  }

  private get reasoner(): GatewayReasoner {
    if (!this._reasoner) {
      this._reasoner = new GatewayReasoner({
        gateway: this.gateway,
        getObjective: () => {
          const obj = this.store.listObjectives("ACTIVE")[0];
          return obj ? { id: obj.id, title: obj.title } : undefined;
        },
      });
    }
    return this._reasoner;
  }

  private get registry(): ReturnType<typeof buildToolRegistry> {
    if (!this._registry) {
      this._registry = buildToolRegistry({
        env: stringVars(this.env),
        sink: {
          recordExecution: (record) => {
            this.store.recordToolExecution(record as unknown as ToolExecution);
          },
        },
      });
    }
    return this._registry;
  }

  // -------------------------------------------------------------------------
  // Lifecycle
  // -------------------------------------------------------------------------

  override async onStart(): Promise<void> {
    // 1. Migrations (idempotent).
    this.store.migrate();

    // 1b. Self-heal after any restart: reap crash-interrupted runs, re-queue
    // expired local-task leases, delete expired sessions. This is what makes
    // the OS survive a machine/DO going offline mid-task.
    try {
      this.runMaintenance();
    } catch (err) {
      this.audit("system", "maintenance.error", undefined, undefined, { error: err instanceof Error ? err.message : String(err) });
    }

    // 1c. Autonomous task execution: sweep for tasks that don't need approval
    // and start them without any Founder click.
    try {
      this.autoRunEligibleTasks();
    } catch (err) {
      this.audit("system", "task.autostart.error", undefined, undefined, { error: err instanceof Error ? err.message : String(err) });
    }

    // 1c. Surface overdue reminders so a fresh wake always knows what to follow up on.
    try {
      const overdue = this.store.listReminders({ overdueOnly: true, limit: 20 });
      if (overdue.length > 0) {
        this.audit("system", "reminders.overdue", "reminders", undefined, {
          count: overdue.length,
          items: overdue.map((r) => `${r.title} (due ${r.due_at})`),
        });
      }
    } catch (err) {
      this.audit("system", "reminders.check_error", undefined, undefined, { error: err instanceof Error ? err.message : String(err) });
    }

    // 2. Seed doctrine, catalog, pipeline workstream if empty.
    if (!this.state.initialized) {
      const seeded = seedIfEmpty(this.store);
      const state = this.store.ensureExecutiveState();
      this.setState({
        ...this.state,
        initialized: true,
        overallStatus: state.overall_status,
        paused: state.paused === 1,
        autonomyMode: state.autonomy_mode,
        spendingDisabled: state.spending_disabled === 1,
        deploymentsDisabled: state.deployments_disabled === 1,
        delegateMode: state.delegate_mode === 1,
        fullAccess: state.full_access === 1,
      });
      if (seeded) {
        this.audit("system", "seed.completed", undefined, undefined, { note: "Initial doctrine, catalog, and pipeline workstream seeded" });
      }
    }

    // 3. Recurring schedules (idempotent — safe on every wake).
    await this.schedule(this.config.dailyBriefCron, "runDailyBrief", {}, { idempotent: true });
    await this.schedule(this.config.weeklyReviewCron, "runWeeklyReview", {}, { idempotent: true });
  }

  override async onRequest(request: Request): Promise<Response> {
    // The agent route is publicly reachable via /agents/* — protect it with
    // the same Founder token the REST API uses (spec §24).
    const token = this.env.FOUNDER_AUTH_TOKEN;
    if (token) {
      const header = request.headers.get("Authorization") ?? "";
      if (header !== `Bearer ${token}`) {
        return new Response(JSON.stringify({ error: "Unauthorized" }), {
          status: 401,
          headers: { "Content-Type": "application/json" },
        });
      }
    }
    const url = new URL(request.url);
    if (url.pathname === "/" || url.pathname === "/state") {
      return Response.json(this.overview());
    }
    return new Response("Zarlino Executive Agent", { status: 200 });
  }

  override async onFiberRecovered(ctx: FiberRecoveryContext): Promise<void> {
    this.audit("system", "fiber.recovered", "fiber", ctx.id, {
      name: ctx.name,
      snapshot: ctx.snapshot,
    });
  }

  // -------------------------------------------------------------------------
  // Founder interface (spec §17, §40)
  // -------------------------------------------------------------------------

  async ask(input: { message: string; sessionId?: string }): Promise<AskResult> {
    const sessionId = input.sessionId ?? newSessionId();
    const message = input.message.trim();
    this._runId = newId("run");
    const runId = this._runId;

    this.store.insertChatMessage({ id: newId("msg"), created_at: nowIso(), updated_at: nowIso(), source: "founder", session_id: sessionId, role: "founder", content: message });

    const classification = await this.reasoner.classify(message);
    this.audit("founder", "chat.request", "chat_session", sessionId, { intent: classification.intent, runId });

    let reply: string;
    let decision: DecisionResult | undefined;

    if (classification.intent === "command") {
      reply = await this.handleCommand(message);
    } else if (classification.intent === "decision") {
      decision = await this.runDecision({ problem: message, categoryHint: classification.category, source: "founder", sessionId, runId });
      reply = this.decisionReply(decision);
    } else {
      reply = await this.answerQuery(message, sessionId);
    }

    this.store.insertChatMessage({ id: newId("msg"), created_at: nowIso(), updated_at: nowIso(), source: "executive", session_id: sessionId, role: "executive", content: reply });

    return { reply, sessionId, intent: classification.intent, decision, runId };
  }

  /** Run the full decision pipeline (spec §6). */
  async runDecision(input: DecisionRequest & { runId?: string }): Promise<DecisionResult> {
    if (input.runId) this._runId = input.runId;
    const context = loadExecutiveContext(this.store, { problem: input.problem, objectiveId: input.objectiveId });
    const engine = new DecisionEngine({
      reasoner: this.reasoner,
      sink: this.sink(),
      authority: () => this.authorityContext(),
      actionSpecFor: (name) => defaultActions().find((a) => a.name === name),
    });
    const result = await engine.run(input, context);
    this.refreshStateFromStore();
    this.audit("executive", "decision.pipeline", "decision", result.decision.id, {
      verdict: result.authority.verdict,
      category: result.classification.category,
      approvalId: result.approval?.id,
    });
    return result;
  }

  private querySystemPrompt(message: string): string {
    const context = loadExecutiveContext(this.store, { problem: message });
    const objective = context.objectiveId ? this.store.getObjective(context.objectiveId) : undefined;
    return [
      basePolicy(),
      "--- FOUNDER DOCTRINE ---",
      doctrineBlock(context.doctrine),
      "--- CURRENT OBJECTIVE ---",
      objectiveBlock(objective ?? undefined),
      "--- CURRENT STATE ---",
      stateBlock(context.stateFacts),
      "--- RELEVANT MEMORY ---",
      memoryBlock(context.memory),
      "--- TOOL POLICY ---",
      toolPolicyBlock(this.registry.list()),
      "--- AUTHORITY POLICY ---",
      authorityBlock(this.authorityConfig()),
      "--- QUESTION CARDS (optional, end of reply) ---",
      [
        "You MAY end your reply with a short block of follow-up question cards when it genuinely helps the Founder continue (clarify a decision, offer the next step, or surface a choice). Format EXACTLY, as the very LAST thing in your reply:",
        "[QUESTION_CARDS]",
        '{"questions": [{"label": "Short button text", "prompt": "The full message to send when clicked"}]}',
        "[/QUESTION_CARDS]",
        "Rules: 1-3 cards max; labels <= 40 chars; prompts are complete, self-contained questions or commands; omit the block entirely when no follow-up is useful.",
      ].join("\n"),
    ].join("\n\n");
  }

  /**
   * Build the conversation turns for a session (chat history), ending with the
   * current founder message. The current message is inserted into chat_messages
   * before this runs, so we drop the trailing turn and re-append it explicitly
   * to avoid duplication. Capped to the last 24 turns to bound tokens.
   */
  private chatContext(sessionId: string, current: string): Array<{ role: "system" | "user" | "assistant"; content: string }> {
    const history = this.store.listChatMessages(sessionId, 60).slice(0, -1); // drop the just-inserted current msg
    const turns = history.map((m) => ({
      role: m.role === "founder" ? ("user" as const) : ("assistant" as const),
      content: m.content,
    }));
    turns.push({ role: "user", content: current });
    return turns.slice(-24);
  }

  /**
   * Answer a chat query with REAL tool execution. Uses OpenAI-compatible
   * function calling (the registry's typed tools as function schemas), runs a
   * model→tool→result loop, and only returns the final text answer — tool calls
   * are executed, never printed as prose. Approval/denial results are fed back
   * to the model so it can tell the Founder what needs their sign-off.
   */
  async answerQuery(message: string, sessionId: string): Promise<string> {
    return this.queryWithTools(message, sessionId);
  }

  private async queryWithTools(message: string, sessionId: string): Promise<string> {
    const messages: ModelMessage[] = [
      { role: "system", content: this.querySystemPrompt(message) },
      ...this.chatContext(sessionId, message),
    ];
    return this.runToolLoop(messages, this.config.maxToolIterations);
  }

  /**
   * The agentic tool loop. Iterations are effectively unlimited (configurable
   * max, default 200 — no hard 8-cap), but guarded so long-horizon work stays
   * SAFE:
   *   - no-progress guard: the same tool+args called repeatedly → stop,
   *   - wall-clock bound (maxAgentRunMs),
   *   - the AI cost/budget tracker throws and stops a runaway run,
   *   - the authority engine + emergency switches gate every tool call.
   */
  private async runToolLoop(messages: ModelMessage[], maxIterations: number): Promise<string> {
    const startedAt = Date.now();
    let lastCallKey = "";
    let repeats = 0;
    for (let i = 0; i < maxIterations; i++) {
      if (Date.now() - startedAt > this.config.maxAgentRunMs) {
        return "I stopped because this run exceeded the time bound — the task is long, so it was cut off. Tell me to continue and I'll resume.";
      }
      const res = await this.gateway.complete({
        taskType: "chat",
        messages,
        tools: this.registry.functionSchemas(),
        toolChoice: "auto",
        maxTokens: 1600,
      });
      if (res.toolCalls && res.toolCalls.length > 0) {
        messages.push({
          role: "assistant",
          content: res.content ?? "",
          tool_calls: res.toolCalls.map((c) => ({
            id: c.id,
            type: "function",
            function: { name: c.name, arguments: c.arguments },
          })),
        });
        for (const call of res.toolCalls) {
          const key = `${call.name}|${call.arguments}`;
          if (key === lastCallKey) {
            repeats += 1;
          } else {
            repeats = 0;
            lastCallKey = key;
          }
          if (repeats >= 5) {
            return "I stopped because I was repeating the same tool call without progress. Please rephrase or let me know what changed.";
          }
          const result = await this.executeChatTool(call.name, call.arguments);
          messages.push({ role: "tool", tool_call_id: call.id, content: JSON.stringify(result) });
        }
        continue;
      }
      return (res.content ?? "").trim();
    }
    return "I reached the iteration bound for this run. Tell me to continue and I'll keep going.";
  }

  /** Execute a model-requested tool call through the authority-gated registry. */
  private async executeChatTool(name: string, rawArgs: string): Promise<unknown> {
    const realName = this.registry.resolveName(name) ?? name;
    let input: unknown = {};
    try {
      input = JSON.parse(rawArgs || "{}");
    } catch {
      input = {};
    }
    const result = await this.registry.execute(this.toolContext(), realName, input);
    return summarizeToolResult(realName, result);
  }

  /**
   * Open a streaming chat session. Returns immediately with a streamId; a
   * background task generates the reply and appends text deltas to a buffered
   * chat_streams row that the Worker polls (async generators don't survive DO
   * RPC, so we stream via a durable buffer + readStream()).
   */
  async openStream(input: { message: string; sessionId?: string }): Promise<{ streamId: string; sessionId: string }> {
    const sessionId = input.sessionId ?? newSessionId();
    const streamId = newId("str");
    this.store.openChatStream(streamId, sessionId, nowIso());
    this.keepBackgroundAlive(this.runStreamTask(streamId, sessionId, input.message.trim()));
    return { streamId, sessionId };
  }

  /** Read any buffered deltas after the given index (polled by the SSE endpoint). */
  async readStream(streamId: string, afterIndex: number): Promise<{ chunks: string[]; nextIndex: number; complete: boolean; error?: string }> {
    return this.store.readChatStream(streamId, afterIndex);
  }

  // -------------------------------------------------------------------------
  // Chat history (persisted transcripts, spec §30)
  // -------------------------------------------------------------------------

  listChatSessions(limit = 20): Array<{ session_id: string; message_count: number; last_at: string; last_message: string }> {
    return this.store.listChatSessions(limit);
  }

  listChatMessages(sessionId: string, limit = 200): ChatMessage[] {
    return this.store.listChatMessages(sessionId, limit);
  }

  clearChat(sessionId: string): { cleared: boolean } {
    this.store.clearChat(sessionId);
    this.audit("founder", "chat.clear", "chat_session", sessionId);
    return { cleared: true };
  }

  // -------------------------------------------------------------------------
  // Long-horizon agent runs (persistent multi-step work, safely bounded)
  // -------------------------------------------------------------------------

  /**
   * Keep background work alive after the calling context returns. Runs started
   * as bare fire-and-forget (e.g. from onStart) get dropped when the DO wake
   * completes; ctx.waitUntil (DurableObjectState) extends the object's life
   * until the promise settles. Falls back to void if waitUntil is unavailable.
   */
  private keepBackgroundAlive<T>(promise: Promise<T>): void {
    const ctx = this.ctx as { waitUntil?: (p: Promise<unknown>) => void } | undefined;
    try {
      if (ctx?.waitUntil) ctx.waitUntil(promise as unknown as Promise<unknown>);
      else void promise;
    } catch {
      void promise;
    }
  }

  /**
   * Start a persistent agent run: the Executive embarks on a long-horizon task
   * in the background, executing tools across many iterations. Bounded by
   * maxToolIterations + maxAgentRunMs + the no-progress guard + AI budgets, and
   * resumable via continueAgentRun. Progress is visible in agent_runs and the
   * tool_executions log.
   */
  async startAgentRun(input: { prompt: string; taskId?: string; maxIterations?: number; source?: "founder" | "executive" | "system" }): Promise<{ runId: string; status: string }> {
    const runId = newId("run");
    const now = nowIso();
    const source = input.source ?? "founder";
    this.store.createAgentRun({
      id: runId,
      created_at: now,
      updated_at: now,
      source,
      task_id: input.taskId,
      prompt: input.prompt,
      status: "RUNNING",
      iterations: 0,
      tools_called: 0,
      started_at: now,
    });
    this.keepBackgroundAlive(this.runAgentJob(runId, input.prompt, input.maxIterations ?? this.config.maxToolIterations));
    this.audit(source, "agent.run.start", "agent_run", runId, { taskId: input.taskId, source });
    return { runId, status: "RUNNING" };
  }

  /**
   * Max tasks the Executive may execute autonomously at the same time. Kept
   * small so auto-run stays bounded (runs also share the daily AI budget).
   */
  private readonly autoRunConcurrency = 2;

  /**
   * AUTONOMOUS TASK EXECUTION — the "no clicks needed" sweep.
   *
   * Finds OPEN/IN_PROGRESS tasks that don't need Founder approval (auto_run=1,
   * no pending approval on their decision, no run already going) and starts a
   * bounded long-horizon run for each, up to the concurrency cap. Runs are
   * marked IN_PROGRESS on start and DONE on completion (onRunCompleted).
   * Called on task creation, DO wake, maintenance, and after approvals.
   */
  autoRunEligibleTasks(): { started: string[]; skipped: string[]; capped: boolean } {
    const store = this.store;
    const running = store.countRunningAgentRuns();
    const budget = Math.max(0, this.autoRunConcurrency - running);
    const started: string[] = [];
    const skipped: string[] = [];
    if (budget <= 0) return { started, skipped, capped: true };
    for (const task of store.listAutoRunnableTasks(20)) {
      if (started.length >= budget) break;
      if (store.hasRunningRunForTask(task.id)) { skipped.push(task.id); continue; }
      if (store.pendingApprovalForTask(task.id)) { skipped.push(task.id); continue; }
      store.updateTask(task.id, { status: "IN_PROGRESS" });
      const prompt =
        `Work on this task end-to-end using your tools until it is genuinely complete, ` +
        `then write a concise final report of exactly what you did and the outcome. ` +
        `If any step needs Founder approval, stop and say so instead of forcing it.\n\n` +
        `TASK: ${task.title}\n${task.description ?? ""}` +
        (task.success_condition ? `\n\nSUCCESS CONDITION: ${task.success_condition}` : "");
      void this.startAgentRun({ prompt, taskId: task.id, source: "executive" });
      started.push(task.id);
    }
    if (started.length > 0) {
      this.audit("executive", "task.autostart", "task", undefined, { started });
    }
    return { started, skipped, capped: budget <= 0 };
  }

  private async runAgentJob(runId: string, prompt: string, maxIterations: number): Promise<void> {
    // Prevent DO eviction for the WHOLE run (alarm-heartbeat keepAlive). Without
    // this, runs started from onStart/scheduled contexts stall at iteration 1:
    // the DO goes idle after the wake, the first model fetch is abandoned, and
    // the run sits RUNNING forever until the stale reaper flags it.
    const dispose = await this.keepAlive().catch(() => () => {});
    const store = this.store;
    const run = store.getAgentRun(runId);
    let messages: ModelMessage[];
    const startedAt = Date.now();
    let iterations = 0;
    let toolsCalled = 0;
    // If a durable checkpoint exists (e.g. resuming after a restart), start
    // from the saved transcript instead of re-doing all prior work.
    const priorCheckpoint = run?.checkpoint_json;
    try {
      if (priorCheckpoint) {
        const parsed = JSON.parse(priorCheckpoint) as {
          messages?: ModelMessage[];
          iterations?: number;
          toolsCalled?: number;
        };
        if (Array.isArray(parsed.messages) && parsed.messages.length > 0) {
          messages = parsed.messages;
          iterations = parsed.iterations ?? 0;
          toolsCalled = parsed.toolsCalled ?? 0;
          messages.push({
            role: "user",
            content: "\n[Run resumed after a restart. Continue from exactly where you left off and complete the task.]",
          });
        } else {
          messages = this.baseRunMessages(prompt);
        }
      } else {
        messages = this.baseRunMessages(prompt);
      }
    } catch {
      messages = this.baseRunMessages(prompt);
    }

    let lastCallKey = "";
    let repeats = 0;
    try {
      // Loop from the (checkpoint) iteration count, granting a FULL fresh
      // budget on every run/resume — a run that already burned 150 iterations
      // still gets maxIterations more when continued (not the leftover).
      for (let i = iterations; i < iterations + maxIterations; i++) {
        iterations = i + 1;
        if (Date.now() - startedAt > this.config.maxLongRunMs) {
          store.finishAgentRun(runId, "TIMEOUT", undefined, "Wall-clock bound exceeded — tell me to continue and I'll resume.");
          return;
        }
        store.heartbeatAgentRun(runId, iterations, toolsCalled);
        const res = await this.gateway.complete({
          taskType: "chat",
          messages,
          tools: this.registry.functionSchemas(),
          toolChoice: "auto",
          maxTokens: 1600,
        });
        if (res.toolCalls && res.toolCalls.length > 0) {
          messages.push({
            role: "assistant",
            content: res.content ?? "",
            tool_calls: res.toolCalls.map((c) => ({
              id: c.id,
              type: "function",
              function: { name: c.name, arguments: c.arguments },
            })),
          });
          for (const call of res.toolCalls) {
            // Smarter no-progress guard: both exact-call repeats AND repeated
            // tool names with different args (oscillation between two tools)
            // are stopped so the run fails fast instead of burning budget.
            const key = `${call.name}|${call.arguments}`;
            if (key === lastCallKey) {
              repeats += 1;
            } else {
              repeats = Math.max(0, repeats);
              lastCallKey = key;
            }
            const nameRepeats = this.countNameRepeats(messages, call.name);
            if (repeats >= 5 || nameRepeats >= 7) {
              store.finishAgentRun(runId, "STOPPED", undefined, "No progress — repeating the same tool call without progress. Tell me to continue and I'll resume.");
              return;
            }
            const result = await this.executeChatTool(call.name, call.arguments);
            toolsCalled += 1;
            messages.push({ role: "tool", tool_call_id: call.id, content: JSON.stringify(result) });
            // Durable checkpoint after every tool call — a DO restart can now
            // resume from this exact transcript.
            store.checkpointAgentRun(runId, JSON.stringify({ messages, iterations, toolsCalled }));
          }
          continue;
        }
        store.finishAgentRun(runId, "DONE", (res.content ?? "").trim());
        this.onRunCompleted(runId, store);
        return;
      }
      store.finishAgentRun(runId, "STOPPED", undefined, "Iteration bound reached — tell me to continue and I'll resume.");
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      store.finishAgentRun(runId, "FAILED", undefined, message);
      this.audit("executive", "agent.run.failed", "agent_run", runId, { error: message });
    } finally {
      dispose();
    }
  }

  /** The opening system+user messages for a long-horizon run. */
  private baseRunMessages(prompt: string): ModelMessage[] {
    return [
      {
        role: "system",
        content:
          this.querySystemPrompt(prompt) +
          "\n\nYou are on a LONG-HORIZON autonomous task. Keep working through every step using your tools until the task is genuinely complete, then write a concise final report of exactly what you did and the outcome. Do not stop early.",
      },
      { role: "user", content: prompt },
    ];
  }

  /** Count how many times a tool name has been called recently (oscillation guard). */
  private countNameRepeats(messages: ModelMessage[], name: string): number {
    const recent = messages.slice(-12);
    let n = 0;
    for (const m of recent) {
      if (m.role === "assistant" && m.tool_calls) {
        for (const c of m.tool_calls) {
          if (c.function?.name === name) n += 1;
        }
      }
    }
    return n;
  }

  /** On successful completion: mark the linked task DONE and store a lesson. */
  private onRunCompleted(runId: string, store: ExecutiveStore): void {
    const run = store.getAgentRun(runId);
    if (!run) return;
    if (run.task_id) {
      const task = store.getTask(run.task_id);
      if (task && task.status !== "DONE") {
        store.updateTask(run.task_id, { status: "DONE" });
        this.audit("executive", "agent.run.task_done", "task", run.task_id, { runId });
      }
    }
    if (run.result && run.result.length > 0) {
      store.insertMemory({
        id: newId("mem"),
        created_at: nowIso(),
        updated_at: nowIso(),
        source: "executive",
        layer: "L1",
        kind: "LESSON",
        title: `Agent run ${runId.slice(0, 8)} completed`,
        body: `Long-horizon run completed in ${run.iterations} iterations / ${run.tools_called} tool calls. Summary: ${run.result.slice(0, 1400)}`,
        importance: 0.5,
        status: "ACTIVE",
      });
    }
    // The run may have created follow-up tasks — keep the auto-complete loop going.
    try {
      this.autoRunEligibleTasks();
    } catch {
      /* non-fatal */
    }
  }

  listAgentRuns(limit = 20): AgentRun[] {
    return this.store.listAgentRuns(limit);
  }

  getAgentRun(id: string): AgentRun | null {
    return this.store.getAgentRun(id);
  }

  /** Resume a stopped / timed-out / failed / crash-interrupted run. */
  async continueAgentRun(runId: string): Promise<{ runId: string; status: string; error?: string }> {
    const run = this.store.getAgentRun(runId);
    if (!run) return { runId, status: "NOT_FOUND", error: "Run not found" };
    if (run.status === "RUNNING") return { runId, status: "RUNNING", error: "Already running" };
    // Reopen the existing row (same id) rather than inserting a duplicate.
    this.store.resetAgentRun(runId);
    this.store.heartbeatAgentRun(runId, run.iterations, run.tools_called);
    // If a durable transcript checkpoint exists, resume from it (the runner
    // reads checkpoint_json and continues the exact conversation); otherwise
    // append a text continuation to the original prompt.
    const prompt = run.checkpoint_json
      ? run.prompt
      : run.prompt + `\n\nPRIOR ATTEMPT (${run.status}):\n${run.result ?? run.error ?? "no prior output"}\n\nContinue from where you left off and complete the task.`;
    this.keepBackgroundAlive(this.runAgentJob(runId, prompt, this.config.maxToolIterations));
    this.audit("founder", "agent.run.continue", "agent_run", runId, { from: run.status, fromCheckpoint: Boolean(run.checkpoint_json) });
    return { runId, status: "RUNNING" };
  }

  /**
   * Run maintenance on every DO wake so the system self-heals after restarts:
   * reaps crash-interrupted agent runs (marks FAILED → resumable via Continue),
   * re-queues expired local-task leases, deletes expired sessions, and starts
   * any task that became auto-runnable.
   */
  runMaintenance(): { staleRuns: string[]; reapedLocal: string[]; expiredSessions: number; autoStarted: string[] } {
    const result = this.store.runMaintenance();
    if (result.staleRuns.length > 0) {
      this.audit("system", "maintenance.reaped_runs", "agent_run", undefined, { runs: result.staleRuns });
    }
    if (result.reapedLocal.length > 0) {
      this.audit("system", "maintenance.reaped_local", "local_task", undefined, { tasks: result.reapedLocal });
    }
    const auto = this.autoRunEligibleTasks();
    return { ...result, autoStarted: auto.started };
  }

  /** Counts of stuck work — used by the deep health endpoint. */
  stuckCounts(): { staleAgentRuns: number; expiredLeases: number } {
    return this.store.stuckCounts();
  }

  // -------------------------------------------------------------------------
  // Local machine tasks (live web → local machine bridge, migration v10)
  // -------------------------------------------------------------------------

  /**
   * Queue a task for the LOCAL machine. The live Executive (or the Founder via
   * the web console) drops a task into local_tasks; a local daemon on the
   * machine claims it, works on it with local tools (repo files, plugin UI
   * source, commands, builds), and posts the result back. Returns the task id
   * so the caller can poll for the result.
   */
  requestLocalTask(input: { prompt: string; taskId?: string; source?: string; meta?: { goal?: string; context?: string; acceptance?: string; deliverable?: string } }): { taskId: string; status: string } {
    const id = newId("ltk");
    const now = nowIso();
    // Structured brief: a CEO-style idea is converted into an implementation
    // brief with explicit context + acceptance criteria so the local machine
    // knows exactly what "done" means (reduces idea→implementation drift).
    const meta = input.meta;
    const brief = meta && (meta.goal || meta.context || meta.acceptance)
      ? {
          goal: meta.goal ?? "",
          context: meta.context ?? "",
          acceptance: meta.acceptance ?? "",
          deliverable: meta.deliverable ?? "",
        }
      : undefined;
    this.store.createLocalTask({
      id,
      created_at: now,
      updated_at: now,
      source: (input.source === "founder" ? "founder" : "executive") as LocalTask["source"],
      task_id: input.taskId,
      prompt: input.prompt,
      status: "PENDING",
      meta_json: brief ? JSON.stringify(brief) : undefined,
    });
    this.audit(input.source ?? "founder", "local.task.requested", "local_task", id, { taskId: input.taskId, hasBrief: Boolean(brief) });
    return { taskId: id, status: "PENDING" };
  }

  listLocalTasks(limit = 25): LocalTask[] {
    return this.store.listLocalTasks(limit);
  }

  getLocalTask(id: string): LocalTask | null {
    return this.store.getLocalTask(id);
  }

  /** Claim the next PENDING local task (called by the local daemon). */
  claimLocalTask(claimer: string): LocalTask | null {
    // First re-queue tasks whose lease expired (a previous daemon's machine
    // went offline mid-task) so no work is ever lost.
    const reaped = this.store.reapExpiredLocalTasks();
    if (reaped.length > 0) {
      this.audit("system", "local.task.reap", "local_task", undefined, { tasks: reaped });
    }
    const task = this.store.claimLocalTask(claimer);
    if (task) {
      this.audit("system", "local.task.claimed", "local_task", task.id, { claimer });
    }
    return task;
  }

  /** Mark a claimed local task as RUNNING (heartbeat from the daemon). */
  heartbeatLocalTask(id: string): LocalTask | null {
    const task = this.store.getLocalTask(id);
    if (!task) return null;
    this.store.heartbeatLocalTask(id, "RUNNING");
    return this.store.getLocalTask(id);
  }

  /** Post a finished result back from the local daemon. */
  submitLocalResult(
    id: string,
    input: { status: "DONE" | "FAILED"; result?: string; error?: string },
  ): { taskId: string; status: string } | { error: string } {
    const task = this.store.getLocalTask(id);
    if (!task) return { error: "Local task not found" };
    this.store.finishLocalTask(id, input.status, input.result, input.error);
    this.audit("system", "local.task.finished", "local_task", id, {
      status: input.status,
      taskId: task.task_id,
    });
    if (input.status === "DONE" && task.task_id) {
      // Close the loop: mark the linked Task DONE and record a memory lesson
      // so future decisions benefit from what the local machine did.
      const linked = this.store.getTask(task.task_id);
      if (linked && linked.status !== "DONE") {
        this.store.updateTask(task.task_id, { status: "DONE" });
        this.audit("system", "local.task.task_done", "task", task.task_id, { localTask: id });
      }
      if (input.result && input.result.length > 0) {
        this.store.insertMemory({
          id: newId("mem"),
          created_at: nowIso(),
          updated_at: nowIso(),
          source: "executive",
          layer: "L1",
          kind: "LESSON",
          title: `Local task ${id.slice(0, 8)} completed`,
          body: `Local machine work finished. Summary: ${input.result.slice(0, 1400)}`,
          importance: 0.5,
          status: "ACTIVE",
        });
      }
    }
    return { taskId: id, status: input.status };
  }

  // -------------------------------------------------------------------------
  // Uploaded files (live web → file uploads, migration v11)
  // -------------------------------------------------------------------------

  /** Classify a file by mime/name so the Executive knows how to inspect it. */
  private fileKind(name: string, mime?: string): "image" | "text" | "audio" | "other" {
    const n = name.toLowerCase();
    const m = (mime ?? "").toLowerCase();
    if (m.startsWith("image/") || /\.(png|jpe?g|gif|webp|bmp|svg)$/.test(n)) return "image";
    if (m.startsWith("text/") || m.includes("json") || m.includes("javascript") || m.includes("xml") || /\.(txt|md|json|csv|h|cpp|ts|tsx|js|py|toml|yaml|yml|log|html|css|ast)$/.test(n)) return "text";
    if (m.startsWith("audio/") || /\.(wav|mp3|flac|aiff?|ogg)$/.test(n)) return "audio";
    return "other";
  }

  uploadFile(input: { name: string; mime?: string; dataBase64: string }): { fileId: string; kind: string; sizeBytes: number } {
    const id = newId("upf");
    const now = nowIso();
    const sizeBytes = Math.floor((input.dataBase64.length * 3) / 4); // base64 → bytes approx
    if (sizeBytes > 5_000_000) throw new Error("File too large — max 5 MB");
    this.store.createUploadedFile({
      id,
      created_at: now,
      updated_at: now,
      source: "founder",
      name: input.name.slice(0, 255),
      mime: input.mime,
      size_bytes: sizeBytes,
      data_base64: input.dataBase64,
      kind: this.fileKind(input.name, input.mime),
    });
    this.audit("founder", "file.uploaded", "uploaded_file", id, { name: input.name, kind: this.fileKind(input.name, input.mime), sizeBytes });
    return { fileId: id, kind: this.fileKind(input.name, input.mime), sizeBytes };
  }

  listUploadedFiles(limit = 50): Array<Pick<UploadedFile, "id" | "name" | "mime" | "size_bytes" | "kind" | "created_at">> {
    return this.store.listUploadedFiles(limit).map((f) => ({
      id: f.id,
      name: f.name,
      mime: f.mime,
      size_bytes: f.size_bytes,
      kind: f.kind,
      created_at: f.created_at,
    }));
  }

  getUploadedFile(id: string): UploadedFile | null {
    return this.store.getUploadedFile(id);
  }

  deleteUploadedFile(id: string): { deleted: boolean } {
    const deleted = this.store.deleteUploadedFile(id);
    if (deleted) this.audit("founder", "file.deleted", "uploaded_file", id);
    return { deleted };
  }

  private async runStreamTask(streamId: string, sessionId: string, message: string): Promise<void> {
    this._runId = newId("run");
    const runId = this._runId;
    const push = (text: string) => {
      if (text) this.store.appendChatStreamChunk(streamId, text);
    };
    try {
      this.store.insertChatMessage({ id: newId("msg"), created_at: nowIso(), updated_at: nowIso(), source: "founder", session_id: sessionId, role: "founder", content: message });

      const classification = await this.reasoner.classify(message);
      this.audit("founder", "chat.request", "chat_session", sessionId, { intent: classification.intent, runId });

      let reply: string;
      if (classification.intent === "command") {
        reply = await this.handleCommand(message);
        push(reply);
      } else if (classification.intent === "decision") {
        const decision = await this.runDecision({ problem: message, categoryHint: classification.category, source: "founder", sessionId, runId });
        reply = this.decisionReply(decision);
        push(reply);
      } else {
        // Execute tools (function-calling loop), then stream the final answer
        // in chunks so the console cursor still animates.
        const answer = await this.queryWithTools(message, sessionId);
        reply = answer;
        const step = 60;
        for (let i = 0; i < answer.length; i += step) {
          push(answer.slice(i, i + step));
        }
      }

      this.store.insertChatMessage({ id: newId("msg"), created_at: nowIso(), updated_at: nowIso(), source: "executive", session_id: sessionId, role: "executive", content: reply });
      this.store.completeChatStream(streamId);
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      this.store.completeChatStream(streamId, message);
    }
  }

  /** Deterministic Founder commands (spec §40). */
  async handleCommand(message: string): Promise<string> {
    const upper = message.toUpperCase();
    const store = this.store;

    if (upper.includes("BRIEF")) {
      return this.runDailyBrief();
    }
    if (upper.includes("STATUS") || upper.includes("OVERVIEW")) {
      return this.renderOverview();
    }
    if (upper.includes("PRIORITIES")) {
      const tasks = store.listTasks();
      const sorted = tasks.filter((t) => ["OPEN", "IN_PROGRESS", "BLOCKED"].includes(t.status)).sort((a, b) => (a.priority < b.priority ? -1 : 1));
      return `TOP PRIORITIES\n${sorted.slice(0, 10).map((t) => `[${t.priority}] ${t.title} (${t.status})`).join("\n") || "No open tasks."}`;
    }
    if (upper.includes("RISKS")) {
      const risks = store.openRisks();
      return `OPEN RISKS\n${risks.map((r) => `[${r.level}] ${r.title}`).join("\n") || "No open risks tracked."}`;
    }
    if (upper.includes("DECISIONS")) {
      const decisions = store.listDecisions({ limit: 10 });
      return `RECENT DECISIONS\n${decisions.map((d) => `${d.created_at.slice(0, 10)} ${d.title} (${d.status})`).join("\n") || "No decisions recorded."}`;
    }
    if (upper.includes("FINANCE")) {
      const snap = store.latestSnapshot();
      return `FINANCE\nCash: ${snap?.cash ?? "UNKNOWN"}\nRevenue (mo): ${snap?.revenue ?? "UNKNOWN"}\nExpenses (mo): ${snap?.expenses ?? "UNKNOWN"}\nRunway: ${snap?.runway_months ?? "UNKNOWN"} months`;
    }
    if (upper.includes("ENGINEERING")) {
      const issues = store.listEngineeringIssues();
      return `ENGINEERING\nOpen issues: ${issues.filter((i) => i.status === "OPEN" || i.status === "IN_PROGRESS").length}\n${issues.slice(0, 10).map((i) => `[${i.severity}] ${i.title}`).join("\n")}`;
    }
    if (upper.includes("PRODUCTS")) {
      const products = store.listProducts();
      return `PRODUCTS\n${products.slice(0, 12).map((p) => `${p.name}: ${p.status}`).join("\n")}`;
    }
    if (upper.includes("MARKETING")) {
      const campaigns = store.listCampaigns();
      return `MARKETING\n${campaigns.map((c) => `${c.name}: ${c.status}${c.spent !== undefined ? ` (spent $${c.spent})` : ""}`).join("\n") || "No campaigns tracked."}`;
    }
    if (upper.includes("TASKS")) {
      const tasks = store.listTasks();
      const open = tasks.filter((t) => ["OPEN", "IN_PROGRESS", "BLOCKED"].includes(t.status));
      return `TASKS\n${open.length} open\n${open.slice(0, 15).map((t) => `[${t.priority}] ${t.title} (${t.status})`).join("\n")}`;
    }
    if (upper.includes("APPROVALS")) {
      const approvals = store.listApprovals({ status: "PENDING" });
      return `PENDING APPROVALS\n${approvals.map((a) => `${a.action_label} — ${a.reason.slice(0, 100)}`).join("\n") || "None pending."}`;
    }
    if (upper.includes("PAUSE")) {
      this.pause("Paused by Founder command");
      return "Autonomous execution PAUSED.";
    }
    if (upper.includes("RESUME")) {
      this.resume();
      return "Autonomous execution RESUMED.";
    }
    if (upper.includes("AUDIT")) {
      const events = store.listAudit(15);
      return `RECENT ACTIVITY\n${events.map((e) => `${e.created_at.slice(0, 16)} ${e.actor} ${e.action}`).join("\n")}`;
    }

    return this.renderOverview();
  }

  // -------------------------------------------------------------------------
  // Overview / dashboard (spec §16, §42)
  // -------------------------------------------------------------------------

  /**
   * Short-TTL cache so the web console's 30–60s polls don't re-scan the whole
   * tasks table on every request (that was blowing past the DO SQLite free-tier
   * rows-read budget: ~5M rows/day). Overview is a snapshot — a few seconds of
   * staleness is invisible to the Founder.
   */
  private _overviewCache: { at: number; json: string } | null = null;

  overview(): Record<string, unknown> {
    const now = Date.now();
    if (this._overviewCache && now - this._overviewCache.at < 10_000) {
      return JSON.parse(this._overviewCache.json) as Record<string, unknown>;
    }
    const store = this.store;
    const state = store.ensureExecutiveState();
    const snap = store.latestSnapshot();
    const approvals = store.listApprovals({ status: "PENDING", limit: 50 });
    const risks = store.openRisks();
    const tasks = store.listTasks({ limit: 200 });
    const decisions = store.listDecisions({ limit: 6 });

    const result: Record<string, unknown> = {
      status: state.overall_status,
      currentObjective: state.current_objective_id ? store.getObjective(state.current_objective_id)?.title : store.listObjectives("ACTIVE")[0]?.title,
      topPriorities: safeJsonArray(state.top_priorities_json),
      cash: snap?.cash ?? null,
      revenue: snap?.revenue ?? null,
      expenses: snap?.expenses ?? null,
      runwayMonths: snap?.runway_months ?? null,
      financialConfidence: snap?.confidence ?? "UNKNOWN",
      criticalRisks: risks.filter((r) => r.level === "CRITICAL" || r.level === "HIGH").slice(0, 5),
      blockedTasks: tasks.filter((t) => t.status === "BLOCKED").length,
      openTasks: tasks.filter((t) => ["OPEN", "IN_PROGRESS", "BLOCKED"].includes(t.status)).length,
      pendingApprovals: approvals.length,
      recentDecisions: decisions.map((d) => ({ id: d.id, title: d.title, status: d.status, category: d.category })),
      shipped: store.listProducts().filter((p) => p.status === "SHIPPED").map((p) => ({ id: p.id, name: p.name, version: p.version })),
      shippedCount: store.listProducts().filter((p) => p.status === "SHIPPED").length,
      downloads: store.latestMetricByName("downloads")?.value ?? null,
      licensed: store.latestMetricByName("licensed")?.value ?? null,
      paused: state.paused === 1,
      autonomyMode: state.autonomy_mode,
      spendingDisabled: state.spending_disabled === 1,
      deploymentsDisabled: state.deployments_disabled === 1,
      delegateMode: state.delegate_mode === 1,
      fullAccess: state.full_access === 1,
      aiSpendToday: store.spentToday(),
      aiSpendMonth: store.spentThisMonth(),
      aiDailyBudget: this.effectiveBudgets().daily,
      aiMonthlyBudget: this.effectiveBudgets().monthly,
      aiTokensToday: store.tokensToday(),
      aiTokensMonth: store.tokensThisMonth(),
      aiDailyTokenBudget: this.config.aiDailyTokenBudget,
      aiMonthlyTokenBudget: this.config.aiMonthlyTokenBudget,
      aiProviderBalance: this.cachedProviderBalance(),
      lastActivity: this.state.lastActivity,
    };
    this._overviewCache = { at: now, json: JSON.stringify(result) };
    return result;
  }

  /** Sync read of the cached provider balance (no network). null = never fetched. */
  cachedProviderBalance(): (ProviderBalance & { cached: boolean }) | null {
    const cache = this.store.getProviderBalanceCache();
    if (!cache.json) return null;
    try {
      return { ...(JSON.parse(cache.json) as ProviderBalance), cached: true };
    } catch {
      return null;
    }
  }

  renderOverview(): string {
    const o = this.overview();
    return [
      "ZARLINO EXECUTIVE STATUS",
      `Overall: ${o.status}`,
      `Current Objective: ${o.currentObjective ?? "UNKNOWN"}`,
      `Top Priority: ${(o.topPriorities as string[])[0] ?? "UNKNOWN"}`,
      `Cash: ${o.cash ?? "UNKNOWN"}`,
      `Revenue: ${o.revenue ?? "UNKNOWN"}`,
      `Critical Risk: ${(o.criticalRisks as Array<{ title: string; level: string }>)[0]?.title ?? "none"}`,
      `Blocked: ${o.blockedTasks}`,
      `Pending Founder Decisions: ${o.pendingApprovals}`,
      `Shipped: ${o.shippedCount} (${(o.shipped as Array<{ name: string }>).map((p) => p.name).join(", ") || "none"})`,
    ].join("\n");
  }

  // -------------------------------------------------------------------------
  // Decisions / tasks / approvals
  // -------------------------------------------------------------------------

  listDecisions(opts?: { status?: string; category?: string; limit?: number }): Decision[] {
    return this.store.listDecisions(opts);
  }

  getDecision(id: string): Decision | null {
    return this.store.getDecision(id);
  }

  listTasks(opts?: { status?: string; projectId?: string; decisionId?: string }): Task[] {
    return this.store.listTasks(opts);
  }

  getTask(id: string): Task | null {
    return this.store.getTask(id);
  }

  createTask(input: { title: string; description?: string; priority?: "P0" | "P1" | "P2" | "P3"; projectId?: string; dueDate?: string; autoRun?: boolean }): Task {
    const task = createTask({ ...input, project_id: input.projectId, due_date: input.dueDate, source: "founder", auto_run: input.autoRun ?? true });
    this.store.insertTask(task);
    this.audit("founder", "task.create", "task", task.id, { title: task.title, autoRun: task.auto_run });
    // No approval needed? Start executing it autonomously right away.
    this.autoRunEligibleTasks();
    return task;
  }

  updateTask(id: string, patch: { status?: Task["status"]; priority?: Task["priority"]; assigneePersonId?: string; dueDate?: string; autoRun?: boolean }): Task | null {
    const existing = this.store.getTask(id);
    if (!existing) return null;
    if (patch.status) {
      const err = canTransitionTask(existing.status, patch.status);
      if (err) throw new Error(err);
    }
    const next: Partial<Task> = {};
    if (patch.status) next.status = patch.status;
    if (patch.priority) next.priority = patch.priority;
    if (patch.assigneePersonId) next.assignee_person_id = patch.assigneePersonId;
    if (patch.dueDate) next.due_date = patch.dueDate;
    if (patch.autoRun !== undefined) next.auto_run = patch.autoRun;
    if (patch.status === "DONE") next.completed_at = nowIso();
    this.store.updateTask(id, next);
    this.audit("founder", "task.update", "task", id, patch);
    // Re-enabled auto-run → give it a chance to start now.
    if (patch.autoRun === true) this.autoRunEligibleTasks();
    return this.store.getTask(id);
  }

  // -------------------------------------------------------------------------
  // 10-gate pipeline (closed loop: gates are updatable from the dashboard)
  // -------------------------------------------------------------------------

  listPipelineGates(pluginId?: string): PipelineGate[] {
    return this.store.listPipelineGates(pluginId);
  }

  updatePipelineGate(id: string, patch: { status?: PipelineGate["status"]; note?: string }): { ok: boolean; gate?: PipelineGate; error?: string } {
    const existing = this.store.listPipelineGates().find((g) => g.id === id);
    if (!existing) return { ok: false, error: `Gate ${id} not found` };
    const next: Partial<PipelineGate> = {};
    if (patch.status) next.status = patch.status;
    if (patch.note !== undefined) next.note = patch.note;
    this.store.updatePipelineGate(id, next);
    this.audit("founder", "pipeline.gate.update", "pipeline_gate", id, { plugin: existing.plugin_id, gate: existing.gate_number, ...patch });
    return { ok: true, gate: this.store.listPipelineGates().find((g) => g.id === id) };
  }

  listPersons(): Person[] {
    return this.store.listPersons();
  }

  listApprovals(status?: string): ApprovalRequest[] {
    return this.store.listApprovals(status ? { status } : undefined);
  }

  approveApproval(id: string, note?: string): { ok: boolean; request?: ApprovalRequest; error?: string } {
    const request = this.store.getApproval(id);
    if (!request) return { ok: false, error: `Approval ${id} not found` };
    const result = transitionApproval(request, "approve", "founder", note);
    if (!result.ok || !result.request) return { ok: false, error: result.error };
    this.store.updateApproval(id, result.request);
    this.onApprovalDecided(result.request);
    this.audit("founder", "approval.approve", "approval_request", id, { note });
    return { ok: true, request: result.request };
  }

  rejectApproval(id: string, note?: string): { ok: boolean; request?: ApprovalRequest; error?: string } {
    const request = this.store.getApproval(id);
    if (!request) return { ok: false, error: `Approval ${id} not found` };
    const result = transitionApproval(request, "reject", "founder", note);
    if (!result.ok || !result.request) return { ok: false, error: result.error };
    this.store.updateApproval(id, result.request);
    if (result.request.decision_id) {
      this.store.updateDecision(result.request.decision_id, { status: "REJECTED" });
    }
    this.audit("founder", "approval.reject", "approval_request", id, { note });
    return { ok: true, request: result.request };
  }

  private onApprovalDecided(request: ApprovalRequest): void {
    if (!request.decision_id) return;
    const decision = this.store.getDecision(request.decision_id);
    if (!decision) return;
    this.store.updateDecision(decision.id, { status: "APPROVED" });
    // Chief of Staff: convert the approved decision into tasks (spec §10).
    for (const task of tasksFromDecision({ ...decision, status: "APPROVED" })) {
      this.store.insertTask(task);
    }
    // Approved → these tasks no longer need approval: auto-run them.
    this.autoRunEligibleTasks();
  }

  // -------------------------------------------------------------------------
  // Memory & doctrine
  // -------------------------------------------------------------------------

  listMemory(opts?: { layer?: string; status?: string }): Memory[] {
    return this.store.listMemories(opts);
  }

  retrieveMemory(query: string, limit = 8): Memory[] {
    return this.store.retrieveMemories(query, { limit });
  }

  addMemory(input: { layer: Memory["layer"]; kind: Memory["kind"]; title: string; body: string; importance?: number; entityId?: string }): Memory {
    const now = nowIso();
    const memory: Memory = {
      id: newId("mem"),
      created_at: now,
      updated_at: now,
      source: "founder",
      layer: input.layer,
      kind: input.kind,
      title: input.title,
      body: input.body,
      importance: input.importance ?? 0.5,
      entity_id: input.entityId,
      status: "ACTIVE",
    };
    this.store.insertMemory(memory);
    this.audit("founder", "memory.add", "memory", memory.id, { layer: input.layer });
    return memory;
  }

  listDoctrine(status?: string): DoctrineRule[] {
    return this.store.listDoctrine(status ? { status } : undefined);
  }

  /**
   * Propose a doctrine change. NEVER applied silently — creates a PROPOSED
   * rule that only the Founder can approve (spec §5, §21).
   */
  proposeDoctrine(input: { code: string; principle: string; rationale?: string; category?: string }): DoctrineRule {
    const active = this.store.listActiveDoctrine();
    const existing = active.find((r) => r.code === input.code);
    const now = nowIso();
    const proposal: DoctrineRule = {
      id: newId("doc"),
      created_at: now,
      updated_at: now,
      source: "executive",
      code: input.code,
      principle: input.principle,
      rationale: input.rationale,
      category: input.category ?? existing?.category ?? "GENERAL",
      status: "PROPOSED",
      version: (existing?.version ?? 0) + 1,
      proposed_by: "executive",
    };
    this.store.insertDoctrineRule(proposal);
    this.audit("executive", "doctrine.propose", "doctrine_rule", proposal.id, { code: input.code });
    return proposal;
  }

  /** Founder approves a proposed doctrine change (versioned, immutable history). */
  approveDoctrine(proposalId: string): { ok: boolean; active?: DoctrineRule; error?: string } {
    const all = this.store.listDoctrine();
    const proposal = all.find((r) => r.id === proposalId && r.status === "PROPOSED");
    if (!proposal) return { ok: false, error: `No PROPOSED doctrine rule ${proposalId}` };
    const activeRules = all.filter((r) => r.code === proposal.code);
    const superseded = activeRules.filter((r) => r.status === "ACTIVE");
    for (const r of superseded) this.store.updateDoctrineRule(r.id, { status: "SUPERSEDED" });
    const approved: DoctrineRule = { ...proposal, status: "ACTIVE", approved_by: "founder", updated_at: nowIso() };
    this.store.updateDoctrineRule(proposal.id, approved);
    this.audit("founder", "doctrine.approve", "doctrine_rule", proposal.id, { code: proposal.code, version: approved.version });
    return { ok: true, active: approved };
  }

  rejectDoctrine(proposalId: string): { ok: boolean; error?: string } {
    const proposal = this.store.listDoctrine().find((r) => r.id === proposalId && r.status === "PROPOSED");
    if (!proposal) return { ok: false, error: `No PROPOSED doctrine rule ${proposalId}` };
    this.store.updateDoctrineRule(proposal.id, { status: "REJECTED" });
    this.audit("founder", "doctrine.reject", "doctrine_rule", proposalId, { code: proposal.code });
    return { ok: true };
  }

  // -------------------------------------------------------------------------
  // Finance / products / risks / marketing / engineering
  // -------------------------------------------------------------------------

  getFinance(): Record<string, unknown> {
    return {
      snapshots: this.store.listSnapshots(12),
      metrics: this.store.listMetrics(20),
      aiSpendToday: this.store.spentToday(),
      aiSpendMonth: this.store.spentThisMonth(),
      aiTokensToday: this.store.tokensToday(),
      aiTokensMonth: this.store.tokensThisMonth(),
    };
  }

  recordFinancialSnapshot(input: { cash?: number; revenue?: number; expenses?: number; period?: string; confidence?: string; notes?: string }): void {
    const now = nowIso();
    this.store.insertSnapshot({
      id: newId("fin"),
      created_at: now,
      updated_at: now,
      source: "founder",
      period: input.period ?? new Date().toISOString().slice(0, 7),
      cash: input.cash,
      revenue: input.revenue,
      expenses: input.expenses,
      confidence: (input.confidence as FinancialSnapshot["confidence"]) ?? "ESTIMATE",
      notes: input.notes,
    });
    this.audit("founder", "finance.snapshot", "financial_snapshot", undefined, { period: input.period });
  }

  /** Record or update a headline product metric (downloads / licensed seats). */
  recordProductMetric(input: { name: "downloads" | "licensed"; value: number; unit?: string }): Metric {
    const now = nowIso();
    const existing = this.store.latestMetricByName(input.name);
    const id = existing?.id ?? newId("met");
    const metric: Metric = {
      id,
      created_at: existing?.created_at ?? now,
      updated_at: now,
      source: "founder",
      name: input.name,
      value: input.value,
      unit: input.unit,
      period: new Date().toISOString().slice(0, 7),
      confidence: "FACT",
      recorded_at: now,
    };
    this.store.upsertMetric(metric);
    this.audit("founder", "product.metric.update", "metrics", id, { name: input.name, value: input.value });
    return metric;
  }

  /** Ship a plugin — the "Ship" action: SHIPPED product + Gate 10 SHIPPED + history. */
  shipPlugin(pluginId: string, opts: { version?: string; note?: string }): { ok: boolean; product?: Product; error?: string } {
    const product = this.store.getProduct(pluginId);
    if (!product) return { ok: false, error: `Product "${pluginId}" not found` };
    this.store.shipProduct(pluginId, opts);
    this.audit("founder", "product.ship", "product", pluginId, { name: product.name, version: opts.version, note: opts.note });
    return { ok: true, product: this.store.getProduct(pluginId) ?? undefined };
  }

  listToolExecutions(limit = 25): ToolExecution[] {
    return this.store.listToolExecutions(limit);
  }

  listProducts(): Array<Record<string, unknown>> {
    return this.store.listProducts().map((p) => ({
      id: p.id,
      name: p.name,
      category: p.category,
      status: p.status,
      priority: p.priority,
      version: p.version,
    }));
  }

  listRisks(): RiskLike[] {
    return this.store.openRisks();
  }

  listCampaigns(): MarketingCampaignLike[] {
    return this.store.listCampaigns();
  }

  listEngineeringIssues(): EngineeringIssueLike[] {
    return this.store.listEngineeringIssues();
  }

  listProjects(): unknown[] {
    return this.store.listProjects();
  }

  listCosts(limit = 50): unknown[] {
    return this.store.costHistory(limit);
  }

  /** Export a full portable JSON snapshot (backup / migration / restore). */
  exportSnapshot(): Record<string, unknown[]> {
    return this.store.exportSnapshot();
  }

  /** Restore a snapshot previously produced by exportSnapshot(). */
  importSnapshot(snapshot: Record<string, unknown[]>): { tables: number; rows: number } {
    return this.store.importSnapshot(snapshot);
  }

  listActivity(limit = 100): AuditEvent[] {
    return this.store.listAudit(limit);
  }

  // -------------------------------------------------------------------------
  // Founder auth (username + password, forced first-visit setup)
  // -------------------------------------------------------------------------

  getAuthStatus(): { setupRequired: boolean; username?: string } {
    const creds = this.store.getFounderCredentials();
    return { setupRequired: !creds, username: creds?.username };
  }

  /** First-visit setup: create the founder account (only allowed once). */
  async setupFounderCredentials(username: string, password: string): Promise<{ token: string; username: string }> {
    if (this.store.getFounderCredentials()) {
      throw new Error("Founder credentials are already set up. Sign in instead.");
    }
    const u = username.trim();
    const uErr = validateUsername(u);
    if (uErr) throw new Error(uErr);
    const pErr = validatePassword(password);
    if (pErr) throw new Error(pErr);
    const { encoded } = await hashPassword(password);
    this.store.setFounderCredentials({ username: u, password_hash: encoded });
    const token = generateToken();
    this.store.createSession({ token, username: u, expires_at: sessionExpiry() });
    this.audit("founder", "auth.setup", "founder_credentials", "founder", { username: u });
    return { token, username: u };
  }

  async login(username: string, password: string): Promise<{ token: string; username: string }> {
    const creds = this.store.getFounderCredentials();
    if (!creds) {
      throw new Error("No founder account set up yet. Complete first-visit setup.");
    }
    if (creds.locked_until && new Date(creds.locked_until).getTime() > Date.now()) {
      throw new Error("Account temporarily locked after too many failed attempts. Try again later.");
    }
    const valid = (await verifyPassword(password, creds.password_hash)) && creds.username === username.trim();
    if (!valid) {
      const attempts = creds.failed_attempts + 1;
      if (attempts >= 5) {
        this.store.updateFounderCredentials({ failed_attempts: attempts, locked_until: new Date(Date.now() + 15 * 60 * 1000).toISOString() });
        this.audit("founder", "auth.locked", "founder_credentials", "founder", { username });
        throw new Error("Too many failed attempts. Account locked for 15 minutes.");
      }
      this.store.updateFounderCredentials({ failed_attempts: attempts });
      this.audit("founder", "auth.login_failed", "founder_credentials", "founder", { username, attempts });
      throw new Error("Invalid username or password.");
    }
    this.store.updateFounderCredentials({ failed_attempts: 0, locked_until: null });
    const token = generateToken();
    this.store.createSession({ token, username: creds.username, expires_at: sessionExpiry() });
    this.audit("founder", "auth.login", "founder_credentials", "founder", { username: creds.username });
    return { token, username: creds.username };
  }

  /** Accepts either a live session token or the FOUNDER_AUTH_TOKEN env secret. */
  async isSessionValid(token: string): Promise<boolean> {
    if (!token) return false;
    const envToken = this.env.FOUNDER_AUTH_TOKEN;
    if (envToken && token === envToken) return true;
    this.store.deleteExpiredSessions();
    const session = this.store.getSession(token);
    return session ? !isExpired(session.expires_at) : false;
  }

  async logout(token: string): Promise<{ ok: boolean }> {
    this.store.deleteSession(token);
    this.audit("founder", "auth.logout");
    return { ok: true };
  }

  async changePassword(currentPassword: string, newPassword: string): Promise<{ ok: boolean }> {
    const creds = this.store.getFounderCredentials();
    if (!creds) throw new Error("No founder account set up yet.");
    if (!(await verifyPassword(currentPassword, creds.password_hash))) {
      throw new Error("Current password is incorrect.");
    }
    const pErr = validatePassword(newPassword);
    if (pErr) throw new Error(pErr);
    const { encoded } = await hashPassword(newPassword);
    this.store.updateFounderCredentials({ password_hash: encoded, failed_attempts: 0, locked_until: null });
    this.audit("founder", "auth.password_changed", "founder_credentials", "founder", { username: creds.username });
    return { ok: true };
  }

  // -------------------------------------------------------------------------
  // Emergency controls (spec §39) — enforced server-side
  // -------------------------------------------------------------------------

  pause(reason: string): Record<string, unknown> {
    this.store.updateExecutiveState({ paused: 1, pause_reason: reason });
    this.setState({ ...this.state, paused: true, pauseReason: reason });
    this.audit("founder", "executive.pause", undefined, undefined, { reason });
    return { paused: true, reason };
  }

  resume(): Record<string, unknown> {
    this.store.updateExecutiveState({ paused: 0, pause_reason: undefined });
    this.setState({ ...this.state, paused: false, pauseReason: undefined });
    this.audit("founder", "executive.resume");
    return { paused: false };
  }

  setSpendingDisabled(disabled: boolean): Record<string, unknown> {
    this.store.updateExecutiveState({ spending_disabled: disabled ? 1 : 0 });
    this.setState({ ...this.state, spendingDisabled: disabled });
    this.audit("founder", "executive.spending_toggle", undefined, undefined, { disabled });
    return { spendingDisabled: disabled };
  }

  setDeploymentsDisabled(disabled: boolean): Record<string, unknown> {
    this.store.updateExecutiveState({ deployments_disabled: disabled ? 1 : 0 });
    this.setState({ ...this.state, deploymentsDisabled: disabled });
    this.audit("founder", "executive.deployments_toggle", undefined, undefined, { disabled });
    return { deploymentsDisabled: disabled };
  }

  setAutonomyMode(mode: "CONTROLLED" | "APPROVAL_ONLY" | "PAUSED"): Record<string, unknown> {
    this.store.updateExecutiveState({ autonomy_mode: mode });
    this.setState({ ...this.state, autonomyMode: mode });
    this.audit("founder", "executive.autonomy_mode", undefined, undefined, { mode });
    return { autonomyMode: mode };
  }

  /**
   * Delegate mode: Founder grants the Executive the ability to trigger real
   * work autonomously (deployments + MEDIUM-risk reversible actions). HIGH/
   * CRITICAL risk, irreversible actions, and emergency switches still gate.
   */
  setDelegateMode(enabled: boolean): Record<string, unknown> {
    const patch: Record<string, number | string> = { delegate_mode: enabled ? 1 : 0 };
    // Granting access is a stronger signal than a stale APPROVAL_ONLY mode.
    if (enabled && this.store.ensureExecutiveState().autonomy_mode === "APPROVAL_ONLY") {
      patch.autonomy_mode = "CONTROLLED";
    }
    this.store.updateExecutiveState(patch);
    this.setState({ ...this.state, delegateMode: enabled });
    this.audit("founder", "executive.delegate_mode", undefined, undefined, { enabled });
    return { delegateMode: enabled };
  }

  /**
   * Full access: the Executive runs virtually everything autonomously
   * (deploy, HIGH-risk reversible actions, spending). Hard gates — delete,
   * ownership transfer, doctrine changes, CRITICAL risk, irreversible HIGH,
   * and every emergency switch — still hold.
   */
  setFullAccess(enabled: boolean): Record<string, unknown> {
    const state = this.store.ensureExecutiveState();
    this.store.updateExecutiveState({
      full_access: enabled ? 1 : 0,
      // Full access implies delegate mode.
      delegate_mode: enabled ? 1 : state.delegate_mode ?? 0,
      // Granting access is a stronger signal than a stale APPROVAL_ONLY mode.
      ...(enabled ? { autonomy_mode: "CONTROLLED" as const } : {}),
    });
    const updated = this.store.ensureExecutiveState();
    this.setState({ ...this.state, fullAccess: enabled, delegateMode: updated.delegate_mode === 1 });
    this.audit("founder", "executive.full_access", undefined, undefined, { enabled });
    return { fullAccess: enabled, delegateMode: updated.delegate_mode === 1 };
  }

  getEmergencyState(): Record<string, unknown> {
    const s = this.store.ensureExecutiveState();
    return {
      paused: s.paused === 1,
      pauseReason: s.pause_reason,
      autonomyMode: s.autonomy_mode,
      spendingDisabled: s.spending_disabled === 1,
      deploymentsDisabled: s.deployments_disabled === 1,
      delegateMode: s.delegate_mode === 1,
      fullAccess: s.full_access === 1,
    };
  }

  // -------------------------------------------------------------------------
  // Tools (spec §13) — typed + permissioned
  // -------------------------------------------------------------------------

  async executeTool(name: string, input: unknown): Promise<unknown> {
    const ctx = this.toolContext();
    const result = await this.registry.execute(ctx, name, input);
    if (result.status === "approval_required") {
      this.audit("executive", "tool.approval_required", "approval_request", result.approval.id, { tool: name });
    }
    return result;
  }

  toolContext(): ReturnType<typeof this.makeToolContext> {
    return this.makeToolContext();
  }

  private makeToolContext() {
    const store = this.store;
    const state = store.ensureExecutiveState();
    return {
      authority: this.authorityContext(),
      env: stringVars(this.env),
      store,
      requestApproval: (input: { action_label: string; reason: string; expected_upside?: string; max_downside?: string; budget_available?: string; decision_id?: string }) => {
        const request = createApprovalRequest({ ...input, requested_by: "executive", source: "executive" });
        store.insertApproval(request);
        return request;
      },
      audit: (actor: string, action: string, entityType?: string, entityId?: string, detail?: unknown, verdict?: string) => {
        this.audit(actor, action, entityType, entityId, detail, verdict);
      },
      // Called after a tool creates a task so it starts executing right away.
      onTaskCreated: () => {
        try {
          this.autoRunEligibleTasks();
        } catch {
          /* never let the sweep break a tool call */
        }
      },
    };
  }

  // -------------------------------------------------------------------------
  // Scheduled briefings (spec §18, §19) — durable
  // -------------------------------------------------------------------------

  async runDailyBrief(): Promise<string> {
    return this.runFiber("daily-brief", async (ctx) => {
      const { markdown, data } = await generateDailyBrief({
        store: this.store,
        complete: (opts) => this.gateway.complete(opts),
      });
      this.persistBrief("Daily Executive Brief", markdown);
      this.setState({ ...this.state, lastDailyBriefAt: nowIso(), lastActivity: nowIso() });
      ctx.stash({ date: data.date, length: markdown.length });
      return markdown;
    });
  }

  async runWeeklyReview(): Promise<string> {
    return this.runFiber("weekly-review", async (ctx) => {
      const { markdown, data } = await generateWeeklyReview({ store: this.store });
      this.persistBrief("Weekly Executive Review", markdown);
      this.setState({ ...this.state, lastWeeklyReviewAt: nowIso(), lastActivity: nowIso() });
      ctx.stash({ weekEnding: data.weekEnding, length: markdown.length });
      return markdown;
    });
  }

  getScheduleStatus(): Promise<unknown[]> {
    return this.listSchedules();
  }

  // -------------------------------------------------------------------------
  // Internals
  // -------------------------------------------------------------------------

  private sink(): EngineSink {
    const store = this.store;
    return {
      insertDecision: (d) => store.insertDecision(d),
      insertApproval: (a) => store.insertApproval(a),
      insertRisk: (r) => store.insertRisk(r),
      insertAudit: (e) => store.insertAudit(e),
      createTasksFromDecision: (d) => {
        for (const t of tasksFromDecision(d)) store.insertTask(t);
      },
    };
  }

  private authorityConfig(): AuthorityConfig {
    const state = this.store.ensureExecutiveState();
    return {
      spendApprovalThreshold: this.config.spendApprovalThreshold,
      maxAutonomousSpend: this.config.maxAutonomousSpend,
      autonomyMode: state.autonomy_mode,
      delegateMode: state.delegate_mode === 1,
      fullAccess: state.full_access === 1,
    };
  }

  private authorityContext() {
    const state = this.store.ensureExecutiveState();
    return {
      actor: "executive" as const,
      config: this.authorityConfig(),
      emergency: {
        paused: state.paused === 1,
        spendingDisabled: state.spending_disabled === 1,
        deploymentsDisabled: state.deployments_disabled === 1,
      },
    };
  }

  private decisionReply(result: DecisionResult): string {
    const d = result.decision;
    if (result.authority.verdict === "APPROVAL" && result.approval) {
      return [
        `DECISION RECORDED (${d.category}): ${d.title}`,
        `Recommended: ${d.recommended_action ?? "—"}`,
        `Confidence: ${d.confidence !== undefined ? Math.round(d.confidence * 100) + "%" : "UNKNOWN"}`,
        `This action requires your approval. Approval request: ${result.approval.id}`,
        `Review it in the Approvals console.`,
      ].join("\n");
    }
    return [
      `DECISION ${d.status} (${d.category}): ${d.title}`,
      `Recommended: ${d.recommended_action ?? "—"}`,
      `Confidence: ${d.confidence !== undefined ? Math.round(d.confidence * 100) + "%" : "UNKNOWN"}`,
      `Expected outcome: ${d.expected_outcome ?? "—"}`,
      `Success metric: ${d.success_metric ?? "—"}`,
      `The Chief of Staff has created follow-up tasks.`,
    ].join("\n");
  }

  private persistBrief(kind: string, markdown: string): void {
    const now = nowIso();
    this.store.insertMemory({
      id: newId("mem"),
      created_at: now,
      updated_at: now,
      source: "executive",
      layer: "L1",
      kind: "FACT",
      title: kind,
      body: markdown,
      importance: 0.7,
      status: "ACTIVE",
    });
    this.audit("executive", "briefing.generated", "memory", undefined, { kind });
  }

  private refreshStateFromStore(): void {
    const s = this.store.ensureExecutiveState();
    this.setState({
      ...this.state,
      overallStatus: s.overall_status,
      paused: s.paused === 1,
      autonomyMode: s.autonomy_mode,
      spendingDisabled: s.spending_disabled === 1,
      deploymentsDisabled: s.deployments_disabled === 1,
      lastActivity: nowIso(),
    });
  }

  private audit(actor: string, action: string, entityType?: string, entityId?: string, detail?: unknown, verdict?: string): void {
    this.store.insertAudit({
      id: newId("aud"),
      created_at: nowIso(),
      updated_at: nowIso(),
      source: "system",
      actor,
      action,
      entity_type: entityType,
      entity_id: entityId,
      detail_json: detail !== undefined ? JSON.stringify(detail) : undefined,
      verdict,
    });
  }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function stringVars(env: ExecutiveEnv): Record<string, string | undefined> {
  const out: Record<string, string | undefined> = {};
  for (const key of Object.keys(env)) {
    const value = env[key];
    if (typeof value === "string") out[key] = value;
  }
  return out;
}

function safeJsonArray(value: string | undefined | null): string[] {
  if (!value) return [];
  try {
    const parsed = JSON.parse(value);
    return Array.isArray(parsed) ? parsed.map(String) : [];
  } catch {
    return [];
  }
}

/** Parse + validate an ISO-8601 datetime string; returns normalized ISO or null. */
function validIso(v: string | undefined): string | null {
  if (!v) return null;
  const t = Date.parse(v);
  if (!Number.isFinite(t)) return null;
  return new Date(t).toISOString();
}

type RiskLike = { id: string; title: string; level: string; status: string; description?: string };
type MarketingCampaignLike = { id: string; name: string; status: string; budget?: number; spent?: number };
type EngineeringIssueLike = { id: string; title: string; severity: string; status: string };
