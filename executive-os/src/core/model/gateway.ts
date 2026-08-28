/**
 * ModelGateway — the single entry point for all LLM access (spec §3).
 *
 * Responsibilities:
 *   1. Resolve the target tier from the task type (or an explicit tier).
 *   2. Resolve "provider:model" from config (never hard-coded).
 *   3. Enforce cost budgets (STOP on budget exceeded, notify, do not continue).
 *   4. Call the provider.
 *   5. Record cost + usage for observability.
 *   6. Return a normalized response.
 *
 * Optionally proxies through Cloudflare AI Gateway when configured.
 */

import { estimateCost } from "./cost";
import { resolveTarget, tierTableFromConfig } from "./routing";
import type {
  CompleteOptions,
  CostTracker,
  ModelCallRecord,
  ModelMessage,
  ModelProvider,
  ModelResponse,
  TaskType,
} from "./types";

export interface GatewayConfig {
  modelTierCheap: string;
  modelTierMedium: string;
  modelTierStrong: string;
  modelTierStrongest: string;
}

export interface GatewayDeps {
  config: GatewayConfig;
  providers: ModelProvider[];
  cost: CostTracker;
  /** Optional AI Gateway base URL override, e.g. https://gateway.ai.cloudflare.com/v1/{acct}/{name}. */
  gatewayBaseUrl?: string;
  /** Notify hook when a budget is exceeded (spec §34: STOP → notify Founder). */
  onBudgetExceeded?: (reason: string, call: ModelCallRecord) => void;
  /** Observability hook for every call (spec §25). */
  onCall?: (call: ModelCallRecord) => void;
  /** Optional soft token-budget enforcement: STOP when today's tokens would
   *  exceed the daily token budget. When unset (or disabled), token budgets
   *  are display-only guides and the USD cost budgets are what enforce. */
  enforceTokenBudget?: boolean;
  dailyTokenBudget?: number;
  tokensToday?: () => number;
}

export class ModelGateway {
  constructor(private readonly deps: GatewayDeps) {}

  /**
   * Optional daily token-budget guard. When enforcement is enabled, estimate
   * this call's tokens from the messages and refuse BEFORE spending if the
   * daily token budget would be exceeded. Throws with a clear reason (the
   * caller surfaces it to the Founder). Soft by default (cost budgets enforce).
   */
  private guardTokenBudget(messages: ModelMessage[], maxTokens?: number): void {
    if (!this.deps.enforceTokenBudget || !this.deps.tokensToday || !this.deps.dailyTokenBudget) return;
    const estimate = Math.max(1, Math.ceil(messages.reduce((n, m) => n + m.content.length, 0) / 4));
    const output = Math.max(1, maxTokens ?? 1024);
    const used = this.deps.tokensToday();
    if (used + estimate + output > this.deps.dailyTokenBudget) {
      const reason = `Daily AI token budget exceeded (${(used + estimate + output).toLocaleString()} est. > ${this.deps.dailyTokenBudget.toLocaleString()})`;
      this.deps.onBudgetExceeded?.(reason, {
        provider: "",
        model: "",
        taskType: "chat",
        inputTokens: estimate,
        outputTokens: output,
        cost: 0,
        latencyMs: 0,
        timestamp: new Date().toISOString(),
      });
      throw new Error(`AI budget exceeded: ${reason}`);
    }
  }

  /** Complete a model call, routing by task type. Throws on budget exceed. */
  async complete(opts: CompleteOptions): Promise<ModelResponse> {
    const target = resolveTarget(
      opts.taskType,
      tierTableFromConfig(this.deps.config),
      opts.tier,
    );

    const provider = this.deps.providers.find((p) => p.id === target.provider);
    if (!provider) {
      throw new Error(
        `No provider "${target.provider}" registered (target ${target.tier} tier → "${target.provider}:${target.model}")`,
      );
    }

    const messages = opts.messages ?? [
      ...(opts.system ? [{ role: "system" as const, content: opts.system }] : []),
      ...(opts.user ? [{ role: "user" as const, content: opts.user }] : []),
    ];
    if (messages.length === 0) throw new Error("ModelGateway.complete requires messages or user/system text");

    // Token-budget guard runs BEFORE the call so we never spend past it.
    this.guardTokenBudget(messages, opts.maxTokens);

    const baseUrl = this.deps.gatewayBaseUrl;

    const response = await provider.complete({
      model: target.model,
      messages,
      temperature: opts.temperature,
      maxTokens: opts.maxTokens,
      jsonMode: opts.jsonMode,
      tools: opts.tools,
      toolChoice: opts.toolChoice,
    });

    const record: ModelCallRecord = {
      provider: response.provider,
      model: response.model,
      taskType: opts.taskType,
      inputTokens: response.inputTokens,
      outputTokens: response.outputTokens,
      cost: estimateCost(response.provider, response.model, response.inputTokens, response.outputTokens),
      sessionId: undefined,
      runId: undefined,
      latencyMs: response.latencyMs,
      timestamp: new Date().toISOString(),
    };

    const budget = this.deps.cost.check(record);
    if (!budget.allowed) {
      this.deps.onBudgetExceeded?.(budget.reason ?? "budget exceeded", record);
      throw new Error(`AI budget exceeded: ${budget.reason}`);
    }

    this.deps.cost.record(record);
    this.deps.onCall?.(record);
    void baseUrl; // AI Gateway proxying is configured at the provider URL level
    return response;
  }

  /** Convenience: structured JSON output for a task. Caller parses/validates. */
  async completeJson<T>(opts: CompleteOptions): Promise<{ data: T; raw: string }> {
    const res = await this.complete({ ...opts, jsonMode: true });
    return { data: JSON.parse(res.content) as T, raw: res.content };
  }

  /**
   * Stream a completion as text deltas. Routes by task type like complete(),
   * but yields tokens as they arrive (for natural-feeling chat). The budget
   * is pre-checked with an estimate before streaming starts (STOP before
   * spending) and the actual call is recorded afterwards for observability.
   */
  async *streamComplete(opts: CompleteOptions): AsyncGenerator<string> {
    const target = resolveTarget(
      opts.taskType,
      tierTableFromConfig(this.deps.config),
      opts.tier,
    );

    const provider = this.deps.providers.find((p) => p.id === target.provider);
    if (!provider) {
      throw new Error(
        `No provider "${target.provider}" registered (target ${target.tier} tier → "${target.provider}:${target.model}")`,
      );
    }
    if (!provider.stream) {
      throw new Error(`Provider "${target.provider}" does not support streaming`);
    }

    const messages = opts.messages ?? [
      ...(opts.system ? [{ role: "system" as const, content: opts.system }] : []),
      ...(opts.user ? [{ role: "user" as const, content: opts.user }] : []),
    ];
    if (messages.length === 0) throw new Error("ModelGateway.streamComplete requires messages or user/system text");

    // Token-budget guard BEFORE streaming (STOP before spending).
    this.guardTokenBudget(messages, opts.maxTokens);

    const inputTokens = Math.max(1, Math.ceil(messages.reduce((n, m) => n + m.content.length, 0) / 4));
    const started = Date.now();
    const preRecord: ModelCallRecord = {
      provider: target.provider,
      model: target.model,
      taskType: opts.taskType,
      inputTokens,
      outputTokens: 0,
      cost: 0,
      sessionId: undefined,
      runId: undefined,
      latencyMs: 0,
      timestamp: new Date().toISOString(),
    };
    const budget = this.deps.cost.check(preRecord);
    if (!budget.allowed) {
      this.deps.onBudgetExceeded?.(budget.reason ?? "budget exceeded", preRecord);
      throw new Error(`AI budget exceeded: ${budget.reason}`);
    }

    let content = "";
    for await (const delta of provider.stream({
      model: target.model,
      messages,
      temperature: opts.temperature,
      maxTokens: opts.maxTokens,
      jsonMode: opts.jsonMode,
      tools: opts.tools,
      toolChoice: opts.toolChoice,
    })) {
      content += delta;
      yield delta;
    }

    const outputTokens = Math.max(1, Math.ceil(content.length / 4));
    const record: ModelCallRecord = {
      ...preRecord,
      outputTokens,
      cost: estimateCost(target.provider, target.model, inputTokens, outputTokens),
      latencyMs: Date.now() - started,
    };
    this.deps.cost.record(record);
    this.deps.onCall?.(record);
  }
}
