/**
 * Risk engine (spec §23).
 *
 * Classifies actions as LOW / MEDIUM / HIGH / CRITICAL by scoring seven
 * dimensions: financial impact, reversibility, legal, customer, production,
 * security, and reputational impact.
 *
 * High-risk actions require approval. Critical actions must never be
 * autonomous (enforced by the authority engine).
 */

import type { RiskLevel } from "../../types";

export interface RiskInput {
  /** Expected maximum financial downside in USD (0 if none). */
  financialImpact?: number;
  reversible: boolean;
  legal?: boolean;
  customerImpact?: boolean;
  productionImpact?: boolean;
  securityImpact?: boolean;
  reputationalImpact?: boolean;
}

export interface RiskClassification {
  level: RiskLevel;
  score: number;
  factors: string[];
}

export function classifyRisk(input: RiskInput): RiskClassification {
  let score = 0;
  const factors: string[] = [];

  const fin = input.financialImpact ?? 0;
  if (fin > 10_000) {
    score += 4;
    factors.push("financial downside > $10k");
  } else if (fin > 1_000) {
    score += 3;
    factors.push("financial downside $1k–$10k");
  } else if (fin > 100) {
    score += 2;
    factors.push("financial downside $100–$1k");
  } else if (fin > 0) {
    score += 1;
    factors.push("financial downside < $100");
  }

  if (!input.reversible) {
    score += 2;
    factors.push("irreversible");
  }

  if (input.legal) {
    score += 5;
    factors.push("legal exposure");
  }
  if (input.securityImpact) {
    score += 5;
    factors.push("security impact");
  }
  if (input.customerImpact) {
    score += 2;
    factors.push("customer impact");
  }
  if (input.productionImpact) {
    score += 2;
    factors.push("production impact");
  }
  if (input.reputationalImpact) {
    score += 1;
    factors.push("reputational impact");
  }

  let level: RiskLevel;
  if (score >= 7) level = "CRITICAL";
  else if (score >= 5) level = "HIGH";
  else if (score >= 3) level = "MEDIUM";
  else level = "LOW";

  return { level, score, factors };
}

/** Convenience: minimum risk for a given set of flags. */
export function riskOf(action: {
  reversible: boolean;
  costEstimate?: number;
  legal?: boolean;
  customerImpact?: boolean;
  productionImpact?: boolean;
  securityImpact?: boolean;
}): RiskLevel {
  return classifyRisk({
    financialImpact: action.costEstimate,
    reversible: action.reversible,
    legal: action.legal,
    customerImpact: action.customerImpact,
    productionImpact: action.productionImpact,
    securityImpact: action.securityImpact,
  }).level;
}
