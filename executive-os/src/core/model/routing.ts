/**
 * Task-based model routing (spec §3, §35).
 *
 * Task type → tier → "provider:model". Provider names are never hard-coded
 * into business logic; the mapping lives in config (wrangler vars) and this
 * module only decides which TIER a task needs.
 */

import type { ModelTier, TaskType } from "./types";

/** Default task → tier mapping per spec §3. */
export const TASK_TIER: Record<TaskType, ModelTier> = {
  classification: "cheap",
  summarization: "cheap",
  routine_operational: "medium",
  chat: "medium",
  briefing: "medium",
  financial_reasoning: "strong",
  technical_reasoning: "strong",
  lens_analysis: "strong",
  strategic_reasoning: "strongest",
  executive_synthesis: "strongest",
};

export function tierFor(taskType: TaskType): ModelTier {
  return TASK_TIER[taskType];
}

export interface TierTable {
  cheap: string; // "provider:model"
  medium: string;
  strong: string;
  strongest: string;
}

export function tierTableFromConfig(cfg: {
  modelTierCheap: string;
  modelTierMedium: string;
  modelTierStrong: string;
  modelTierStrongest: string;
}): TierTable {
  return {
    cheap: cfg.modelTierCheap,
    medium: cfg.modelTierMedium,
    strong: cfg.modelTierStrong,
    strongest: cfg.modelTierStrongest,
  };
}

export function parseProviderModel(value: string): { provider: string; model: string } {
  const idx = value.indexOf(":");
  if (idx <= 0) return { provider: "deepseek", model: value };
  return { provider: value.slice(0, idx), model: value.slice(idx + 1) };
}

/** Deterministic routing test helper: task type + explicit tier table → target. */
export function resolveTarget(
  taskType: TaskType,
  table: TierTable,
  tierOverride?: ModelTier,
): { tier: ModelTier; provider: string; model: string } {
  const tier = tierOverride ?? tierFor(taskType);
  const target = table[tier];
  const { provider, model } = parseProviderModel(target);
  return { tier, provider, model };
}
