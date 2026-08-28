/**
 * Approval queue (spec §12).
 *
 * The Founder approves/rejects from the web console. Every approval is
 * logged. Transitions are validated in a pure state machine so they can be
 * unit-tested and enforced in backend code.
 */

import { newId, nowIso } from "../../ids";
import type { ApprovalRequest, ApprovalStatus, Source } from "../../types";

export type ApprovalAction = "approve" | "reject" | "cancel" | "modify";

export interface ApprovalInput {
  action_label: string;
  reason: string;
  expected_upside?: string;
  max_downside?: string;
  budget_available?: string;
  recommendation?: "APPROVE" | "REJECT" | "MODIFY";
  decision_id?: string;
  requested_by?: string;
  source?: Source;
}

export interface TransitionResult {
  ok: boolean;
  request?: ApprovalRequest;
  error?: string;
}

const ALLOWED_TRANSITIONS: Record<ApprovalStatus, ApprovalAction[]> = {
  PENDING: ["approve", "reject", "cancel", "modify"],
  APPROVED: [],
  REJECTED: [],
  MODIFIED: ["approve", "reject", "cancel"],
  CANCELLED: [],
};

/** Validate a status transition; returns error message when invalid. */
export function canTransition(current: ApprovalStatus, action: ApprovalAction): string | null {
  if (action === "modify" && current !== "PENDING") {
    return `Cannot modify a ${current} approval request`;
  }
  if (action !== "modify" && !ALLOWED_TRANSITIONS[current].includes(action)) {
    return `Cannot ${action} a ${current} approval request`;
  }
  return null;
}

export function createApprovalRequest(input: ApprovalInput): ApprovalRequest {
  const now = nowIso();
  return {
    id: newId("apr"),
    created_at: now,
    updated_at: now,
    source: input.source ?? "executive",
    decision_id: input.decision_id,
    action_label: input.action_label,
    reason: input.reason,
    expected_upside: input.expected_upside,
    max_downside: input.max_downside,
    budget_available: input.budget_available,
    recommendation: input.recommendation ?? "APPROVE",
    status: "PENDING",
    requested_by: input.requested_by ?? "executive",
  };
}

/**
 * Transition an approval request. Returns the updated request on success, or
 * an error result when the transition is invalid.
 */
export function transitionApproval(
  request: ApprovalRequest,
  action: ApprovalAction,
  by: string,
  note?: string,
): TransitionResult {
  const invalid = canTransition(request.status, action);
  if (invalid) return { ok: false, error: invalid };

  const next: ApprovalRequest = { ...request, updated_at: nowIso() };
  switch (action) {
    case "approve":
      next.status = "APPROVED";
      break;
    case "reject":
      next.status = "REJECTED";
      break;
    case "cancel":
      next.status = "CANCELLED";
      break;
    case "modify":
      next.status = "MODIFIED";
      break;
  }
  next.decided_by = by;
  next.decision_note = note;
  next.decided_at = nowIso();
  return { ok: true, request: next };
}
