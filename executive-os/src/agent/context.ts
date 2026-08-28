/**
 * Executive context loader — assembles L0/L1/L2/L3 context for a decision or
 * query from the durable store (spec §26, §27).
 */

import type { ExecutiveContext } from "../core/decision/engine";
import type { ExecutiveStore } from "../db/store";
import type { Memory, Objective } from "../types";

export interface LoadContextOptions {
  problem?: string;
  objectiveId?: string;
  memoryLimit?: number;
}

/**
 * Load operational state facts (L1), retrieved memory (L2), and doctrine
 * (L3) for a given problem. Financial figures that are missing are reported
 * as UNKNOWN — never fabricated.
 */
export function loadExecutiveContext(store: ExecutiveStore, opts: LoadContextOptions = {}): ExecutiveContext {
  const snap = store.latestSnapshot();
  const objective: Objective | null = opts.objectiveId
    ? store.getObjective(opts.objectiveId)
    : store.listObjectives("ACTIVE")[0] ?? null;

  const openTasks = store.countOpenTasks();
  const openRisks = store.openRisks();
  const pendingApprovals = store.listApprovals({ status: "PENDING" });

  const stateFacts: ExecutiveContext["stateFacts"] = [
    { key: "cash", value: snap?.cash ?? null, confidence: snap?.confidence ?? "UNKNOWN" },
    { key: "monthly_revenue", value: snap?.revenue ?? null, confidence: snap?.confidence ?? "UNKNOWN" },
    { key: "monthly_expenses", value: snap?.expenses ?? null, confidence: snap?.confidence ?? "UNKNOWN" },
    { key: "runway_months", value: snap?.runway_months ?? null, confidence: snap?.confidence ?? "UNKNOWN" },
    { key: "open_tasks", value: openTasks, confidence: "FACT" },
    { key: "open_risks", value: openRisks.length, confidence: "FACT" },
    { key: "pending_approvals", value: pendingApprovals.length, confidence: "FACT" },
    { key: "products_shipped", value: store.listProducts().filter((p) => p.status === "SHIPPED").length, confidence: "FACT" },
  ];

  const memory = store.retrieveMemories(opts.problem ?? "", { limit: opts.memoryLimit ?? 8 });
  const doctrine = store.listActiveDoctrine();

  return {
    objectiveId: objective?.id,
    objectiveTitle: objective?.title,
    stateFacts,
    memory,
    doctrine,
    availableBudget: snap?.cash,
  };
}

/** Convenience for typed memory access. */
export function memoryOf(store: ExecutiveStore): Memory[] {
  return store.listMemories({ status: "ACTIVE" });
}
