/**
 * Cost tracking and budget enforcement (spec §34).
 *
 * Every model call records provider, model, tokens, task type, and estimated
 * cost. Budgets (daily / monthly / per-task / per-run) are configurable; when
 * a budget is exceeded the system STOPS and notifies the Founder — it never
 * silently continues.
 *
 * Pricing is a rough, explicit estimate table. Real per-provider pricing
 * changes; treat these as estimates, not billing.
 */

import type { CostTracker, ModelCallRecord } from "./types";

/** USD per 1M tokens, keyed "provider/model-prefix". Order matters (first match). */
const PRICING: Array<{ match: RegExp; input: number; output: number }> = [
  { match: /deepseek-reasoner/i, input: 0.55, output: 2.19 },
  { match: /deepseek-chat/i, input: 0.27, output: 1.10 },
  { match: /gpt-4o/i, input: 2.5, output: 10 },
  { match: /gpt-4/i, input: 30, output: 60 },
  { match: /gpt-4\.1/i, input: 2, output: 8 },
  { match: /gpt-4\.1-mini/i, input: 0.4, output: 1.6 },
  { match: /o[1-4]/i, input: 15, output: 60 },
  { match: /claude-sonnet/i, input: 3, output: 15 },
  { match: /claude-opus/i, input: 15, output: 75 },
  { match: /claude-haiku/i, input: 1, output: 5 },
  { match: /llama/i, input: 0.15, output: 0.6 },
  { match: /glm/i, input: 0.15, output: 0.6 },
];

/** Default fallback pricing when a model is unknown. */
const FALLBACK_INPUT = 1;
const FALLBACK_OUTPUT = 3;

export function unitPrices(provider: string, model: string): { inputPerM: number; outputPerM: number } {
  const key = `${provider}/${model}`;
  for (const p of PRICING) {
    if (p.match.test(key)) return { inputPerM: p.input, outputPerM: p.output };
  }
  return { inputPerM: FALLBACK_INPUT, outputPerM: FALLBACK_OUTPUT };
}

export function estimateCost(provider: string, model: string, inputTokens: number, outputTokens: number): number {
  const { inputPerM, outputPerM } = unitPrices(provider, model);
  return (inputTokens / 1_000_000) * inputPerM + (outputTokens / 1_000_000) * outputPerM;
}

export interface BudgetConfig {
  daily: number;
  monthly: number;
  perTask: number;
  perRun: number;
}

export interface BudgetTotals {
  today: number;
  month: number;
  task: number; // current task/run scope spend
  run: number;
}

/**
 * Pure budget check. Returns allowed=false with a reason when the call would
 * exceed a budget. STOPS on budget exceeded — no silent continuation.
 */
export function checkBudget(config: BudgetConfig, totals: BudgetTotals, cost: number): { allowed: boolean; reason?: string } {
  if (cost > config.perTask) {
    return { allowed: false, reason: `Cost ${cost.toFixed(4)} exceeds per-task budget ${config.perTask}` };
  }
  if (cost > config.perRun) {
    return { allowed: false, reason: `Cost ${cost.toFixed(4)} exceeds per-run budget ${config.perRun}` };
  }
  if (totals.today + cost > config.daily) {
    return { allowed: false, reason: `Daily AI budget exceeded (${(totals.today + cost).toFixed(4)} > ${config.daily})` };
  }
  if (totals.month + cost > config.monthly) {
    return { allowed: false, reason: `Monthly AI budget exceeded (${(totals.month + cost).toFixed(4)} > ${config.monthly})` };
  }
  return { allowed: true };
}

export interface CostStorage {
  /** Sum of costs recorded today (UTC). */
  spentToday(): number;
  /** Sum of costs recorded this calendar month (UTC). */
  spentThisMonth(): number;
  /** Sum of costs for a given run id. */
  spentThisRun(runId?: string): number;
  /** Sum of costs for a given session id. */
  spentThisSession(sessionId?: string): number;
  record(call: ModelCallRecord): void;
}

/** In-memory cost storage (tests / dev without DB). */
export class MemoryCostStorage implements CostStorage {
  private calls: ModelCallRecord[] = [];
  spentToday(): number {
    const day = new Date().toISOString().slice(0, 10);
    return this.calls.filter((c) => c.timestamp.startsWith(day)).reduce((s, c) => s + c.cost, 0);
  }
  spentThisMonth(): number {
    const month = new Date().toISOString().slice(0, 7);
    return this.calls.filter((c) => c.timestamp.startsWith(month)).reduce((s, c) => s + c.cost, 0);
  }
  spentThisRun(runId?: string): number {
    if (!runId) return 0;
    return this.calls.filter((c) => c.runId === runId).reduce((s, c) => s + c.cost, 0);
  }
  spentThisSession(sessionId?: string): number {
    if (!sessionId) return 0;
    return this.calls.filter((c) => c.sessionId === sessionId).reduce((s, c) => s + c.cost, 0);
  }
  record(call: ModelCallRecord): void {
    this.calls.push(call);
  }
}

/** Default tracker backed by a CostStorage. */
export class DefaultCostTracker implements CostTracker {
  /**
   * config may be a static BudgetConfig or a getter function. The function
   * form lets a caller (e.g. the Executive Agent) re-read persisted budget
   * overrides on every check, so a Founder raising the budget from the UI
   * takes effect immediately without rebuilding the tracker.
   */
  constructor(
    private readonly config: BudgetConfig | (() => BudgetConfig),
    private readonly storage: CostStorage,
    private readonly runId?: string,
    private readonly sessionId?: string,
  ) {}

  private resolveConfig(): BudgetConfig {
    return typeof this.config === "function" ? this.config() : this.config;
  }

  check(call: ModelCallRecord): { allowed: boolean; reason?: string; cost: number } {
    const cost = call.cost;
    const totals: BudgetTotals = {
      today: this.storage.spentToday(),
      month: this.storage.spentThisMonth(),
      task: this.storage.spentThisSession(this.sessionId),
      run: this.storage.spentThisRun(this.runId),
    };
    const decision = checkBudget(this.resolveConfig(), totals, cost);
    return { ...decision, cost };
  }

  record(call: ModelCallRecord): void {
    this.storage.record(call);
  }

  currentBudgetState(): { daily: number; monthly: number; perTask: number; perRun: number } {
    return {
      daily: this.storage.spentToday(),
      monthly: this.storage.spentThisMonth(),
      perTask: this.storage.spentThisSession(this.sessionId),
      perRun: this.storage.spentThisRun(this.runId),
    };
  }
}
