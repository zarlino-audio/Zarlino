/**
 * Central configuration and thresholds for the Executive OS.
 *
 * All autonomy thresholds are configurable (spec §41). Values here are
 * defaults; production overrides come from Worker vars / secrets. Never
 * hard-code financial assumptions in business logic — read from config or
 * state, and represent missing data as UNKNOWN.
 */

export interface ExecutiveConfig {
  // Autonomy policy (spec §41)
  spendApprovalThreshold: number; // USD — spending above this requires approval
  maxAutonomousSpend: number; // USD — max single autonomous spend
  autonomyMode: "CONTROLLED" | "APPROVAL_ONLY" | "PAUSED";

  // Cost controls (spec §34)
  aiDailyBudget: number;
  aiMonthlyBudget: number;
  aiPerTaskBudget: number;
  aiPerRunBudget: number;
  // Token budgets (displayed in the AI Fuel gauge; soft guides, cost budgets enforce).
  aiDailyTokenBudget: number;
  aiMonthlyTokenBudget: number;
  // When true, the daily token budget ALSO hard-stops (default soft/off — the
  // USD cost budgets are the primary enforcement).
  aiTokenBudgetEnforce: boolean;

  // Model routing (spec §3, §35) — tier → "provider:model"
  modelTierCheap: string;
  modelTierMedium: string;
  modelTierStrong: string;
  modelTierStrongest: string;
  defaultModelTier: ModelTier;

  // Long-horizon agentic work (effectively-unlimited tool calls, safely bounded)
  maxToolIterations: number; // per run — default 200 (no hard 8-cap); loop guard + budgets stop runaway
  maxAgentRunMs: number; // wall-clock bound for interactive chat tool loops (10 min)
  maxLongRunMs: number; // wall-clock bound for background long-horizon runs (30 min — they checkpoint + resume)

  // Misc
  defaultTimeZone: string;
  dailyBriefCron: string; // e.g. "0 8 * * *"
  weeklyReviewCron: string; // e.g. "0 9 * * 1"
  maxRetries: number;
  requestTimeoutMs: number;
}

export type ModelTier = "cheap" | "medium" | "strong" | "strongest";

export const MODEL_TIERS: readonly ModelTier[] = ["cheap", "medium", "strong", "strongest"] as const;

export function parseConfig(env: Record<string, string | undefined>): ExecutiveConfig {
  const num = (key: string, fallback: number): number => {
    const v = env[key];
    if (v === undefined || v === "") return fallback;
    const n = Number(v);
    return Number.isFinite(n) ? n : fallback;
  };

  const tier = (key: string, fallback: string): string => env[key] || fallback;

  const autonomyMode = (env.AUTONOMY_MODE as ExecutiveConfig["autonomyMode"]) || "CONTROLLED";

  return {
    spendApprovalThreshold: num("SPEND_APPROVAL_THRESHOLD", 100),
    maxAutonomousSpend: num("MAX_AUTONOMOUS_SPEND", 25),
    autonomyMode,
    aiDailyBudget: num("AI_DAILY_BUDGET", 8),
    aiMonthlyBudget: num("AI_MONTHLY_BUDGET", 150),
    aiPerTaskBudget: num("AI_PER_TASK_BUDGET", 1),
    aiPerRunBudget: num("AI_PER_RUN_BUDGET", 2),
    aiDailyTokenBudget: num("AI_DAILY_TOKEN_BUDGET", 30_000_000),
    aiMonthlyTokenBudget: num("AI_MONTHLY_TOKEN_BUDGET", 900_000_000),
    aiTokenBudgetEnforce: env.AI_TOKEN_BUDGET_ENFORCE === "true",
    modelTierCheap: tier("MODEL_TIER_CHEAP", "deepseek:deepseek-chat"),
    modelTierMedium: tier("MODEL_TIER_MEDIUM", "deepseek:deepseek-chat"),
    modelTierStrong: tier("MODEL_TIER_STRONG", "deepseek:deepseek-reasoner"),
    modelTierStrongest: tier("MODEL_TIER_STRONGEST", "deepseek:deepseek-reasoner"),
    defaultModelTier: (env.DEFAULT_MODEL_TIER as ModelTier) || "medium",
    defaultTimeZone: "UTC",
    dailyBriefCron: env.DAILY_BRIEF_CRON || "0 8 * * *",
    weeklyReviewCron: env.WEEKLY_REVIEW_CRON || "0 9 * * 1",
    maxRetries: num("MAX_RETRIES", 2),
    requestTimeoutMs: num("REQUEST_TIMEOUT_MS", 60_000),
    maxToolIterations: num("MAX_TOOL_ITERATIONS", 200),
    maxAgentRunMs: num("MAX_AGENT_RUN_MS", 600_000),
    maxLongRunMs: num("MAX_LONG_RUN_MS", 1_800_000),
  };
}

/** Default config used in tests and local dev when env is sparse. */
export const DEFAULT_CONFIG: ExecutiveConfig = parseConfig({});

/** Initial Founder Doctrine (spec §5 L3). These seed doctrine_rules. */
export const SEED_DOCTRINE: Array<{
  code: string;
  principle: string;
  rationale: string;
  category: string;
}> = [
  {
    code: "D-001",
    principle: "Prefer completion over unnecessary perfection.",
    rationale: "Ship the defined gate; polish only what the market validates.",
    category: "EXECUTION",
  },
  {
    code: "D-002",
    principle: "Do not optimize a system before measuring the bottleneck.",
    rationale: "Optimize against data, not guesses.",
    category: "EXECUTION",
  },
  {
    code: "D-003",
    principle: "Do not spend money without a measurable hypothesis.",
    rationale: "Every dollar needs a falsifiable expected outcome.",
    category: "FINANCE",
  },
  {
    code: "D-004",
    principle: "A project is complete when it meets its defined ship gate.",
    rationale: "Definition of done comes from the gate, not vibes.",
    category: "EXECUTION",
  },
  {
    code: "D-005",
    principle: "Avoid both overfitting and underfitting.",
    rationale: "Match complexity to evidence; neither gold-plate nor cut corners.",
    category: "GENERAL",
  },
  {
    code: "D-006",
    principle: "Use the minimum sufficient complexity.",
    rationale: "Simple, modular, observable, replaceable, testable, secure.",
    category: "TECHNICAL",
  },
  {
    code: "D-007",
    principle: "Protect cash.",
    rationale: "Runway is survival. Never trade it casually.",
    category: "FINANCE",
  },
  {
    code: "D-008",
    principle: "Prefer reversible experiments before irreversible commitments.",
    rationale: "Small, reversible steps before big commitments.",
    category: "STRATEGY",
  },
  {
    code: "D-009",
    principle: "Escalate decisions whose downside exceeds delegated authority.",
    rationale: "If the downside of being wrong is large, the Founder decides.",
    category: "GOVERNANCE",
  },
  {
    code: "D-010",
    principle: "Do NOT silently modify Founder Doctrine.",
    rationale: "Doctrine changes require Founder approval. This rule itself is immutable.",
    category: "GOVERNANCE",
  },
];
