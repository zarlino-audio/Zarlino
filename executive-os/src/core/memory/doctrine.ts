/**
 * Founder Doctrine (spec §5 L3, §21, §41).
 *
 * Doctrine is the most protected layer. The model may NOT silently modify
 * doctrine — changes require Founder approval. Historical doctrine is
 * immutable: changing a rule creates a PROPOSED successor; on Founder
 * approval the old version is marked SUPERSEDED (never edited in place).
 */

import { newId, nowIso } from "../../ids";
import type { DoctrineRule, Source } from "../../types";

export interface DoctrineProposal {
  rule: DoctrineRule;
  supersedesCode: string;
  status: "PROPOSED";
}

/** Create a proposed doctrine change. Does NOT alter active doctrine. */
export function proposeDoctrineChange(
  input: { code: string; principle: string; rationale?: string; category?: string },
  by: string,
  source: Source = "executive",
): DoctrineProposal {
  const now = nowIso();
  const rule: DoctrineRule = {
    id: newId("doc"),
    created_at: now,
    updated_at: now,
    source,
    code: input.code,
    principle: input.principle,
    rationale: input.rationale,
    category: input.category ?? "GENERAL",
    status: "PROPOSED",
    version: 1, // bumped on approval
    proposed_by: by,
  };
  return { rule, supersedesCode: input.code, status: "PROPOSED" };
}

/**
 * Approve a doctrine proposal: marks all ACTIVE rules with the same code as
 * SUPERSEDED and promotes the proposal to ACTIVE with an incremented version.
 * Returns the superseded rules and the active rule.
 */
export function approveDoctrineChange(
  proposal: DoctrineRule,
  activeRules: DoctrineRule[],
  approvedBy: string,
): { active: DoctrineRule; superseded: DoctrineRule[] } {
  const now = nowIso();
  const maxVersion = activeRules
    .filter((r) => r.code === proposal.code)
    .reduce((max, r) => Math.max(max, r.version), 0);

  const superseded = activeRules
    .filter((r) => r.code === proposal.code && r.status === "ACTIVE")
    .map((r): DoctrineRule => ({ ...r, status: "SUPERSEDED", updated_at: now }));

  const active: DoctrineRule = {
    ...proposal,
    version: maxVersion + 1,
    status: "ACTIVE",
    approved_by: approvedBy,
    updated_at: now,
  };

  return { active, superseded };
}

/** Reject a doctrine proposal. */
export function rejectDoctrineChange(proposal: DoctrineRule): DoctrineRule {
  return { ...proposal, status: "REJECTED", updated_at: nowIso() };
}

/** Verify the model cannot silently change doctrine. Only compares provided fields. */
export function assertDoctrineImmutable(active: DoctrineRule[], proposed: Partial<DoctrineRule>): string | null {
  const existing = active.find((r) => r.code === proposed.code);
  if (!existing) return null;
  if (
    (proposed.principle !== undefined && existing.principle !== proposed.principle) ||
    (proposed.category !== undefined && existing.category !== proposed.category)
  ) {
    return `Doctrine ${existing.code} differs from active rule — must go through Founder approval.`;
  }
  return null;
}
