/**
 * Audit & observability (spec §25).
 *
 * Every agent run and every important action is observable. We record
 * run_id, session, model, tools called, tokens, latency, cost, decision,
 * approval, execution, errors, and outcomes — and never expose hidden
 * chain-of-thought; we store concise rationales and evidence references.
 */

import { newId, nowIso } from "../../ids";
import type { AuditEvent, Source } from "../../types";

export interface AuditInput {
  actor: string;
  action: string;
  entityType?: string;
  entityId?: string;
  detail?: unknown;
  verdict?: string;
  ip?: string;
  source?: Source;
}

export function auditEvent(input: AuditInput): AuditEvent {
  const now = nowIso();
  return {
    id: newId("aud"),
    created_at: now,
    updated_at: now,
    source: input.source ?? "system",
    actor: input.actor,
    action: input.action,
    entity_type: input.entityType,
    entity_id: input.entityId,
    detail_json: input.detail !== undefined ? JSON.stringify(input.detail) : undefined,
    verdict: input.verdict,
    ip: input.ip,
  };
}

/** Compose the human-readable rationale for a decision (no hidden CoT). */
export function decisionRationale(input: {
  problem: string;
  lensAssessments: Record<string, string>;
  recommendedAction: string;
  confidence: number;
  uncertainties: string[];
}): string {
  const lines = [
    `Problem: ${input.problem}`,
    `CEO: ${input.lensAssessments.ceo ?? "—"}`,
    `CFO: ${input.lensAssessments.cfo ?? "—"}`,
    `CTO: ${input.lensAssessments.cto ?? "—"}`,
    `Risk: ${input.lensAssessments.risk ?? "—"}`,
    `Recommended: ${input.recommendedAction} (confidence ${Math.round(input.confidence * 100)}%)`,
  ];
  if (input.uncertainties.length > 0) {
    lines.push(`Uncertainties: ${input.uncertainties.join("; ")}`);
  }
  return lines.join("\n");
}
