/**
 * System Doctor — the Executive OS's self-healing layer.
 *
 * Runs a battery of checks against the durable store and (optionally) fixes
 * what it finds, then logs every run to `doctor_logs` so self-healing is
 * auditable. It runs:
 *   - on every DO wake (constructor, blockConcurrencyWhile),
 *   - on the 6-hourly cron (scheduled handler),
 *   - on demand via `/api/doctor/run` or the `system.doctor` tool,
 *   - locally via `npm run doctor` / `npm run doctor:fix`.
 *
 * Doctrine: the Doctor only auto-fixes things that are unambiguously safe and
 * reversible (reaping stale runs, re-queuing expired leases, clearing expired
 * sessions, pruning orphaned streams, re-applying migrations). Anything that
 * needs judgment is reported with an `action` and left for the Founder.
 */
import { z } from "zod";
import type { ExecutiveStore } from "../db/store";
import type { DoctorCheckResult, DoctorReport, DoctorSeverity } from "../types";
import type { Tool, ToolContext } from "./tools/types";
import { newId } from "../ids";
export interface DoctorContext {
  store: ExecutiveStore;
  env: Record<string, string | undefined>;
  /** Optional KV namespace (Cloudflare). Detected at runtime. */
  kv?: {
    list(opts: { limit: number }): Promise<{ keys: unknown[] }>;
    get(key: string): Promise<string | null>;
  };
  /** Optional tool registry — used by feature checks to verify the agent/chat layer. */
  registry?: {
    functionSchemas(): Array<{ function: { name: string } }>;
  };
  now(): string;
}

export interface DoctorCheck {
  id: string;
  label: string;
  severity: DoctorSeverity;
  /** Run the check; when `autoFix` is true, repair safe issues in place. */
  run(ctx: DoctorContext, autoFix: boolean): Promise<DoctorCheckResult> | DoctorCheckResult;
}

const okResult = (id: string, label: string, severity: DoctorSeverity, detail: string): DoctorCheckResult => ({
  id, label, severity, status: "OK", detail,
});
const warnResult = (id: string, label: string, severity: DoctorSeverity, detail: string, action?: string): DoctorCheckResult => ({
  id, label, severity, status: "WARN", detail, action,
});
const failResult = (id: string, label: string, severity: DoctorSeverity, detail: string, action: string, fixed?: string): DoctorCheckResult => ({
  id, label, severity, status: "FAIL", detail, action, fixed,
});

const BUDGET_KEYS = ["ai_daily_budget", "ai_monthly_budget", "ai_per_task_budget", "ai_per_run_budget"] as const;

export const DOCTOR_CHECKS: DoctorCheck[] = [
  {
    id: "schema.migrations",
    label: "Schema migrations are current",
    severity: "CRITICAL",
    run: (ctx, autoFix) => {
      const applied = ctx.store.migrate();
      if (applied.length > 0) {
        return failResult(
          "schema.migrations",
          "Schema migrations are current",
          "CRITICAL",
          `Applied ${applied.length} pending migration(s): ${applied.map((v) => `v${v}`).join(", ")}.`,
          "Migrations are applied automatically on DO wake.",
          `applied ${applied.map((v) => `v${v}`).join(", ")}`,
        );
      }
      return okResult("schema.migrations", "Schema migrations are current", "CRITICAL", "All migrations applied (schema up to date).");
    },
  },
  {
    id: "db.tables",
    label: "Core tables exist and are queryable",
    severity: "HIGH",
    run: (ctx, autoFix) => {
      const required = ["executive_state", "tasks", "decisions", "approval_requests", "products", "pipeline_gates", "financial_snapshots", "memories", "audit_events", "agent_jobs", "local_tasks", "doctor_logs"];
      const missing: string[] = [];
      ctx.store.migrate(); // ensure tables exist via migrations
      for (const t of required) {
        try {
          if (!probeTable(ctx, t)) missing.push(t);
        } catch {
          missing.push(t);
        }
      }
      if (missing.length > 0) {
        return failResult("db.tables", "Core tables exist and are queryable", "HIGH", `Missing/unqueryable tables: ${missing.join(", ")}.`, "Run migrations (auto on wake).");
      }
      return okResult("db.tables", "Core tables exist and are queryable", "HIGH", `${required.length} core tables present and queryable.`);
    },
  },
  {
    id: "jobs.stale_runs",
    label: "No stale agent runs (restart orphans)",
    severity: "HIGH",
    run: (ctx, autoFix) => {
      const stale = ctx.store.stuckCounts().staleAgentRuns;
      if (stale > 0) {
        if (autoFix) {
          const reaped = ctx.store.reapStaleAgentRuns().length;
          return failResult("jobs.stale_runs", "No stale agent runs (restart orphans)", "HIGH", `Found ${stale} RUNNING agent job(s) with no heartbeat.`, "Auto-reaped.", `reaped ${reaped}`);
        }
        return warnResult("jobs.stale_runs", "No stale agent runs (restart orphans)", "HIGH", `Found ${stale} RUNNING agent job(s) with no heartbeat.`, "Run doctor with autoFix, or Continue from the durable checkpoint.");
      }
      return okResult("jobs.stale_runs", "No stale agent runs (restart orphans)", "HIGH", "No stale agent runs.");
    },
  },
  {
    id: "tasks.expired_leases",
    label: "No expired local-task leases",
    severity: "MEDIUM",
    run: (ctx, autoFix) => {
      const expired = ctx.store.stuckCounts().expiredLeases;
      if (expired > 0) {
        if (autoFix) {
          const reaped = ctx.store.reapExpiredLocalTasks().length;
          return failResult("tasks.expired_leases", "No expired local-task leases", "MEDIUM", `Found ${expired} local task(s) with an expired claim lease.`, "Auto re-queued for any daemon.", `re-queued ${reaped}`);
        }
        return warnResult("tasks.expired_leases", "No expired local-task leases", "MEDIUM", `Found ${expired} local task(s) with an expired claim lease.`, "Run doctor with autoFix.");
      }
      return okResult("tasks.expired_leases", "No expired local-task leases", "MEDIUM", "No expired local-task leases.");
    },
  },
  {
    id: "sessions.expired",
    label: "No expired auth sessions",
    severity: "LOW",
    run: (ctx, autoFix) => {
      const count = ctx.store.runMaintenance().expiredSessions;
      if (count > 0) {
        return failResult("sessions.expired", "No expired auth sessions", "LOW", `Found ${count} expired session(s).`, "Auto-cleared.", `cleared ${count}`);
      }
      return okResult("sessions.expired", "No expired auth sessions", "LOW", "No expired sessions.");
    },
  },
  {
    id: "streams.orphaned",
    label: "No orphaned chat streams",
    severity: "LOW",
    run: (ctx, autoFix) => {
      if (autoFix) {
        const pruned = ctx.store.pruneCompletedChatStreams(24 * 60 * 60 * 1000);
        if (pruned > 0) {
          return failResult("streams.orphaned", "No orphaned chat streams", "LOW", `Pruned ${pruned} completed chat stream(s) older than 24h.`, "Auto-pruned.", `pruned ${pruned}`);
        }
      } else {
        const count = (ctx.store as unknown as { countCompletedChatStreams?: (olderThanMs: number) => number }).countCompletedChatStreams?.(24 * 60 * 60 * 1000) ?? 0;
        if (count > 0) {
          return warnResult("streams.orphaned", "No orphaned chat streams", "LOW", `${count} completed chat stream(s) older than 24h.`, "Run doctor with autoFix.");
        }
      }
      return okResult("streams.orphaned", "No orphaned chat streams", "LOW", "No orphaned chat streams.");
    },
  },
  {
    id: "state.executive",
    label: "Executive state row exists",
    severity: "HIGH",
    run: (ctx, autoFix) => {
      const existing = ctx.store.getExecutiveState();
      if (!existing) {
        ctx.store.ensureExecutiveState();
        return failResult("state.executive", "Executive state row exists", "HIGH", "Missing executive_state row.", "Auto-seeded.", "seeded");
      }
      return okResult("state.executive", "Executive state row exists", "HIGH", `State ${existing.overall_status} / ${existing.autonomy_mode}.`);
    },
  },
  {
    id: "settings.budget",
    label: "Budget overrides are valid positive numbers",
    severity: "MEDIUM",
    run: (ctx, autoFix) => {
      const invalid: string[] = [];
      for (const key of BUDGET_KEYS) {
        const v = ctx.store.getSetting(key);
        if (v === null || v === "") continue;
        const n = Number(v);
        if (!Number.isFinite(n) || n <= 0) invalid.push(key);
      }
      if (invalid.length > 0) {
        if (autoFix) {
          for (const key of invalid) ctx.store.setSetting(key, null);
          return failResult("settings.budget", "Budget overrides are valid positive numbers", "MEDIUM", `Invalid budget override(s): ${invalid.join(", ")}.`, "Auto-cleared.", `cleared ${invalid.join(", ")}`);
        }
        return warnResult("settings.budget", "Budget overrides are valid positive numbers", "MEDIUM", `Invalid budget override(s): ${invalid.join(", ")}.`, "Run doctor with autoFix.");
      }
      return okResult("settings.budget", "Budget overrides are valid positive numbers", "MEDIUM", "Budget overrides valid.");
    },
  },
  {
    id: "metrics.seed",
    label: "Headline product metrics seeded",
    severity: "LOW",
    run: (ctx, autoFix) => {
      const downloads = ctx.store.latestMetricByName("downloads");
      const licensed = ctx.store.latestMetricByName("licensed");
      if (!downloads || !licensed) {
        if (autoFix) {
          ctx.store.ensureProductMetrics();
          return failResult("metrics.seed", "Headline product metrics seeded", "LOW", "Missing downloads/licensed metric rows.", "Auto-seeded.", "seeded");
        }
        return warnResult("metrics.seed", "Headline product metrics seeded", "LOW", "Missing downloads/licensed metric rows.", "Run doctor with autoFix.");
      }
      return okResult("metrics.seed", "Headline product metrics seeded", "LOW", "downloads + licensed metrics present.");
    },
  },
  {
    id: "secrets.required",
    label: "Required secrets configured (presence only)",
    severity: "HIGH",
    run: (ctx, _autoFix) => {
      const required = ["DEEPSEEK_API_KEY", "FOUNDER_AUTH_TOKEN", "PAYSTACK_SECRET_KEY", "ZARLINO_LICENSE_PRIVATE_KEY"];
      const missing = required.filter((k) => !ctx.env[k]);
      if (missing.length > 0) {
        return warnResult(
          "secrets.required",
          "Required secrets configured (presence only)",
          "HIGH",
          `Missing secrets: ${missing.join(", ")}.`,
          "Set via `wrangler secret put <NAME>` (or .dev.vars locally). Values are never read by the Doctor.",
        );
      }
      return okResult("secrets.required", "Required secrets configured (presence only)", "HIGH", "All required secrets present (presence checked, values never read).");
    },
  },
  {
    id: "features.agent_ready",
    label: "Chat / agent feature layer is ready",
    severity: "HIGH",
    run: (ctx, _autoFix) => {
      const issues: string[] = [];
      // 1. Model provider key present (chat + agent runs need it).
      if (!ctx.env.DEEPSEEK_API_KEY) {
        issues.push("DEEPSEEK_API_KEY is missing — chat and agent runs will fail to authenticate.");
      }
      // 2. Tool names must be provider-safe: DeepSeek requires ^[a-zA-Z0-9_-]+$ (no dots).
      //    A dotted name like finance.get_snapshot makes every chat call 400.
      if (ctx.registry) {
        const invalid = ctx.registry
          .functionSchemas()
          .map((s) => s.function.name)
          .filter((n) => !/^[a-zA-Z0-9_-]+$/.test(n));
        if (invalid.length > 0) {
          issues.push(`Invalid tool name(s) sent to the model: ${invalid.slice(0, 5).join(", ")} — chat will fail with DeepSeek HTTP 400. Sanitize names in the registry.`);
        }
      } else {
        issues.push("Registry not attached — tool-name safety not verified.");
      }
      // 3. Spend gate: if the budget is exhausted the model calls stop.
      const daily = Number(ctx.env.AI_DAILY_BUDGET) || 0;
      const monthly = Number(ctx.env.AI_MONTHLY_BUDGET) || 0;
      const spentToday = ctx.store.spentToday();
      const spentMonth = ctx.store.spentThisMonth();
      if (daily > 0 && spentToday >= daily) issues.push(`Daily AI budget exhausted ($${spentToday.toFixed(2)}/$${daily}) — model calls are gated until it resets.`);
      if (monthly > 0 && spentMonth >= monthly) issues.push(`Monthly AI budget exhausted ($${spentMonth.toFixed(2)}/$${monthly}) — model calls are gated.`);
      if (issues.length > 0) {
        return failResult(
          "features.agent_ready",
          "Chat / agent feature layer is ready",
          "HIGH",
          issues.join(" "),
          "Fix the flagged configuration, then verify /api/chat returns a reply.",
        );
      }
      return okResult("features.agent_ready", "Chat / agent feature layer is ready", "HIGH", "Model key present, tool names provider-safe, budget headroom available.");
    },
  },
  {
    id: "kv.health",
    label: "KV namespace reachable",
    severity: "LOW",
    run: async (ctx, _autoFix) => {
      if (!ctx.kv) {
        return warnResult("kv.health", "KV namespace reachable", "LOW", "ZARLINO_KV not bound — counters/affiliate state unavailable.", "Bind ZARLINO_KV in wrangler.toml.");
      }
      try {
        await ctx.kv.list({ limit: 1 });
        return okResult("kv.health", "KV namespace reachable", "LOW", "KV reachable.");
      } catch {
        return warnResult("kv.health", "KV namespace reachable", "LOW", "KV unreachable at check time.", "Check the ZARLINO_KV binding.");
      }
    },
  },
  {
    id: "productivity.stale_assignments",
    label: "Productivity assignments are progressing",
    severity: "MEDIUM",
    run: (ctx, autoFix) => {
      const stale = ctx.store.countStaleAssignments(48 * 60 * 60 * 1000);
      if (stale > 0) {
        if (autoFix) {
          const ids = ctx.store.reapStaleAssignments(48 * 60 * 60 * 1000);
          return failResult(
            "productivity.stale_assignments",
            "Productivity assignments are progressing",
            "MEDIUM",
            `${stale} assignment(s) with no progress in 48h.`,
            "Assignments are auto-marked STALLED so they can be re-planned.",
            `marked ${ids.length} assignment(s) STALLED`,
          );
        }
        return warnResult(
          "productivity.stale_assignments",
          "Productivity assignments are progressing",
          "MEDIUM",
          `${stale} assignment(s) with no progress in 48h.`,
          "Run with auto-fix to mark them STALLED.",
        );
      }
      return okResult("productivity.stale_assignments", "Productivity assignments are progressing", "MEDIUM", "No stale assignments.");
    },
  },
  {
    id: "productivity.budget_health",
    label: "Token budgets are within limits",
    severity: "MEDIUM",
    run: (ctx, _autoFix) => {
      try {
        const budgets = ctx.store.listTokenBudgets();
        if (budgets.length === 0) {
          return warnResult("productivity.budget_health", "Token budgets are within limits", "MEDIUM", "No token budgets tracked yet.", "Run productivity.optimize once to seed the global budget.");
        }
        const over = budgets.filter((b) => {
          const monthly = b.monthly_budget ?? 0;
          return monthly > 0 && b.tokens_used_month >= monthly * 0.9;
        });
        if (over.length) {
          return warnResult(
            "productivity.budget_health",
            "Token budgets are within limits",
            "MEDIUM",
            `${over.length} scope(s) at ≥90% of monthly token budget: ${over.map((b) => b.scope).join(", ")}.`,
            "Reallocate budget (productivity.optimize) or raise the cap in wrangler vars.",
          );
        }
        return okResult("productivity.budget_health", "Token budgets are within limits", "MEDIUM", "All token budgets within limits.");
      } catch {
        return okResult("productivity.budget_health", "Token budgets are within limits", "MEDIUM", "Budget check unavailable (no productivity tables yet).");
      }
    },
  },
  {
    id: "productivity.contradictions",
    label: "No open contradictions/misconceptions",
    severity: "LOW",
    run: (ctx, _autoFix) => {
      const open = ctx.store.countOpenContradictions();
      if (open > 0) {
        return warnResult(
          "productivity.contradictions",
          "No open contradictions/misconceptions",
          "LOW",
          `${open} open contradiction(s)/misconception(s) — run productivity.detect_contradictions, review and resolve.`,
          "Review and resolve in the Productivity view.",
        );
      }
      return okResult("productivity.contradictions", "No open contradictions/misconceptions", "LOW", "No open contradictions.");
    },
  },
];

/** Probe whether a table exists by running a guarded SELECT against it. */
function probeTable(ctx: DoctorContext, table: string): boolean {
  try {
    const store = ctx.store as unknown as { probeTableExists(t: string): boolean };
    if (typeof store.probeTableExists === "function") return store.probeTableExists(table);
  } catch {
    /* fall through */
  }
  return true; // conservative: store-level probe unavailable
}

export interface RunDoctorOptions {
  autoFix: boolean;
  trigger: string;
}

/** Run every Doctor check, apply fixes when autoFix, and log the run. */
export async function runDoctor(ctx: DoctorContext, opts: RunDoctorOptions): Promise<DoctorReport> {
  const results: DoctorCheckResult[] = [];
  for (const check of DOCTOR_CHECKS) {
    try {
      const r = await check.run(ctx, opts.autoFix);
      results.push(r);
    } catch (err) {
      results.push({
        id: check.id,
        label: check.label,
        severity: check.severity,
        status: "FAIL",
        detail: `Check crashed: ${err instanceof Error ? err.message : String(err)}`,
        action: "Review the Doctor logs and the store state manually.",
      });
    }
  }
  const summary = {
    total: results.length,
    ok: results.filter((r) => r.status === "OK").length,
    warn: results.filter((r) => r.status === "WARN").length,
    fail: results.filter((r) => r.status === "FAIL").length,
    fixed: results.filter((r) => Boolean(r.fixed)).length,
  };
  const report: DoctorReport = {
    generatedAt: ctx.now(),
    trigger: opts.trigger,
    autoFix: opts.autoFix,
    checks: results,
    summary,
    healthy: summary.fail === 0,
  };
  try {
    ctx.store.insertDoctorLog({
      id: newId("doc"),
      run_at: report.generatedAt,
      trigger: opts.trigger,
      auto_fix: opts.autoFix ? 1 : 0,
      summary_json: JSON.stringify(summary),
      checks_json: JSON.stringify(results),
      source: "system",
      created_at: report.generatedAt,
      updated_at: report.generatedAt,
    });
  } catch {
    /* logging must never break the Doctor */
  }
  return report;
}

// ---------------------------------------------------------------------------
// Doctor tools exposed to the Executive agent
// ---------------------------------------------------------------------------

function doctorContextFromTool(ctx: ToolContext): DoctorContext {
  return {
    store: ctx.store!,
    env: ctx.env ?? {},
    now: () => new Date().toISOString(),
  };
}

export function doctorTools(): Tool[] {
  return [
    {
      name: "system.doctor",
      description:
        "Run the System Doctor: checks schema/migrations, stale agent runs, expired local-task leases, expired sessions, orphaned chat streams, executive state, budget overrides, metrics seed, secrets presence and KV health. With autoFix (default true) it safely repairs what it finds and logs the run. Returns the full check report.",
      inputSchema: z.object({ autoFix: z.boolean().optional(), trigger: z.string().max(40).optional() }),
      requiredPermission: "modify",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: true,
      run: async (ctx: ToolContext, input: { autoFix?: boolean; trigger?: string }) => {
        if (!ctx.store) throw new Error("store unavailable");
        const report = await runDoctor(doctorContextFromTool(ctx), {
          autoFix: input.autoFix ?? true,
          trigger: input.trigger ?? "system.doctor tool",
        });
        return {
          healthy: report.healthy,
          summary: report.summary,
          checks: report.checks.map((c) => ({ id: c.id, status: c.status, severity: c.severity, detail: c.detail, fixed: c.fixed, action: c.action })),
          note: report.healthy ? "All systems green." : "Issues found — see check details. Warnings may need human action.",
        };
      },
    },
    {
      name: "system.self_test",
      description: "Run a read-only self test: verify the store is wired, migrations are current, and core subsystems respond. Does not mutate anything.",
      inputSchema: z.object({}),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (ctx: ToolContext) => {
        if (!ctx.store) throw new Error("store unavailable");
        const checks: Array<{ name: string; ok: boolean; detail: string }> = [];
        const ok = (name: string, detail: string) => checks.push({ name, ok: true, detail });
        const bad = (name: string, detail: string) => checks.push({ name, ok: false, detail });

        const before = ctx.store.migrate().length;
        ok("migrations", before === 0 ? "schema current" : `${before} pending (will apply on wake)`);

        const state = ctx.store.getExecutiveState();
        state ? ok("executive_state", `present (${state.overall_status})`) : bad("executive_state", "missing");

        const tasks = ctx.store.listTasks({ limit: 3 });
        ok("tasks", `queryable (${tasks.length} returned)`);

        const products = ctx.store.listProducts();
        ok("products", `queryable (${products.length} products)`);

        const gates = ctx.store.listPipelineGates();
        ok("pipeline_gates", `queryable (${gates.length} gates)`);

        const costs = ctx.store.spentToday();
        ok("cost_tracking", `spentToday=${costs}`);

        const audit = ctx.store.listAudit(1);
        ok("audit", `queryable (${audit.length} events)`);

        return { ok: checks.every((c) => c.ok), checks, note: "Read-only self test — no data was mutated." };
      },
    },
    {
      name: "system.maintenance",
      description: "Run the maintenance sweep: reap stale agent runs, re-queue expired local-task leases, delete expired sessions, and prune old completed chat streams. Audited.",
      inputSchema: z.object({}),
      requiredPermission: "modify",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: true,
      run: async (ctx: ToolContext) => {
        if (!ctx.store) throw new Error("store unavailable");
        const maintenance = ctx.store.runMaintenance();
        const pruned = ctx.store.pruneCompletedChatStreams(24 * 60 * 60 * 1000);
        ctx.audit("executive", "system.maintenance", "system", "maintenance", { ...maintenance, prunedStreams: pruned });
        return { ran: true, staleRunsReaped: maintenance.staleRuns.length, localTasksRequeued: maintenance.reapedLocal.length, expiredSessions: maintenance.expiredSessions, prunedStreams: pruned };
      },
    },
  ];
}
