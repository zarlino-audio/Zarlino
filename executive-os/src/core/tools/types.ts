/**
 * Tool system (spec §13).
 *
 * Every tool exposes a strict typed interface:
 *   name, description, input_schema, output_schema, required_permission,
 *   risk_level, cost_estimate, reversible, audit_required.
 *
 * The model never gets unrestricted arbitrary API access — it can only call
 * tools registered here, and every call passes through the authority engine.
 */

import type { z } from "zod";
import type { Permission, RiskLevel, AuthorityConfig } from "../authority/engine";
import type { ApprovalRequest } from "../../types";
import type { ExecutiveStore } from "../../db/store";

export interface ToolContext {
  /** Authority context (actor, thresholds, emergency switches). */
  authority: {
    actor: "founder" | "executive" | "system";
    config: AuthorityConfig;
    emergency: { paused: boolean; spendingDisabled: boolean; deploymentsDisabled: boolean };
  };
  env: Record<string, string | undefined>;
  /** Durable store access for internal tools (finance, projects, marketing…). */
  store?: ExecutiveStore;
  /** Create an approval request when a call needs escalation. Returns null if not applicable. */
  requestApproval(input: {
    action_label: string;
    reason: string;
    expected_upside?: string;
    max_downside?: string;
    budget_available?: string;
    decision_id?: string;
  }): ApprovalRequest | null;
  /** Append an audit event. */
  audit(actor: string, action: string, entityType?: string, entityId?: string, detail?: unknown, verdict?: string): void;
  /** Called after a tool creates a task so it can start executing autonomously. */
  onTaskCreated?: () => void;
}

export interface Tool<I = any, O = any> {
  name: string;
  description: string;
  inputSchema: z.ZodType<I>;
  outputSchema?: z.ZodType<O>;
  requiredPermission: Permission;
  riskLevel: RiskLevel;
  costEstimate?: number;
  reversible: boolean;
  auditRequired: boolean;
  run(ctx: ToolContext, input: I): Promise<O>;
}

export type ToolResult =
  | { status: "success"; output: unknown; executionId: string }
  | { status: "denied"; reason: string; authorityVerdict: "DENY" }
  | { status: "approval_required"; approval: ApprovalRequest; authorityVerdict: "APPROVAL" }
  | { status: "error"; message: string };

export function success(output: unknown, executionId: string): ToolResult {
  return { status: "success", output, executionId };
}

export function toolDenied(reason: string): ToolResult {
  return { status: "denied", reason, authorityVerdict: "DENY" };
}

export function toolApprovalRequired(approval: ApprovalRequest): ToolResult {
  return { status: "approval_required", approval, authorityVerdict: "APPROVAL" };
}

export function toolError(message: string): ToolResult {
  return { status: "error", message };
}
