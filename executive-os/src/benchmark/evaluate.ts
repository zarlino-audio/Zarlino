/**
 * Benchmark evaluator (spec §33).
 *
 * Scores the system's recommended action against each benchmark case on
 * DECISION SIMILARITY. A rule-based scorer runs without any model; an
 * optional LLM scorer (via the ModelGateway) adds nuanced judgment.
 */

import type { BenchmarkCase, BenchmarkCaseResult, BenchmarkDimension } from "./cases";
import { FOUNDER_BENCHMARK } from "./cases";

export interface SystemOutput {
  caseId: string;
  recommendedAction: string;
  escalated: boolean;
  confidence: number;
  category: string;
  riskLevel?: string;
}

function containsAll(text: string, needles: string[]): boolean {
  const lower = text.toLowerCase();
  return needles.every((n) => lower.includes(n.toLowerCase()));
}

function containsAny(text: string, needles: string[]): boolean {
  const lower = text.toLowerCase();
  return needles.some((n) => lower.includes(n.toLowerCase()));
}

/**
 * Rule-based evaluation — deterministic, no model required.
 * Each dimension is a 0..1 score.
 */
export function evaluateCaseRuleBased(c: BenchmarkCase, out: SystemOutput): BenchmarkCaseResult {
  const action = out.recommendedAction;
  const notes: string[] = [];
  const dims: Record<BenchmarkDimension, number> = {
    strategic_alignment: 0,
    financial_discipline: 0,
    technical_judgment: 0,
    risk_management: 0,
    doctrine_adherence: 0,
    action_selection: 0,
    escalation_correctness: 0,
  };

  // Action selection: expected actions present, forbidden absent.
  const expectedPresent = containsAll(action, c.expectedAction);
  const forbiddenPresent = containsAny(action, c.forbiddenActions);
  dims.action_selection = expectedPresent && !forbiddenPresent ? 1 : expectedPresent ? 0.5 : 0;
  if (expectedPresent) notes.push("expected action reflected");
  else notes.push("expected action missing");
  if (forbiddenPresent) notes.push("forbidden action present!");

  // Escalation correctness.
  dims.escalation_correctness = out.escalated === c.requiresEscalation ? 1 : 0;
  if (out.escalated === c.requiresEscalation) notes.push("escalation correct");
  else notes.push(out.escalated ? "escalated when not required" : "did not escalate when required");

  // Doctrine adherence: doctrine codes present or spirit honored.
  dims.doctrine_adherence = 1;
  if (c.category === "FINANCIAL" || c.doctrine.includes("D-003") || c.doctrine.includes("D-007")) {
    const hasFinanceLanguage = /budget|cash|downside|cost|runway|spend|hypothesis|unknown/i.test(action);
    dims.financial_discipline = hasFinanceLanguage ? 1 : 0.25;
    if (!hasFinanceLanguage) notes.push("financial discipline: missing financial language");
  } else {
    dims.financial_discipline = 1;
  }

  if (c.category === "TECHNICAL" || c.doctrine.includes("D-006")) {
    dims.technical_judgment = /measure|bottleneck|simple|complex|reversib|scope|defer/i.test(action) ? 1 : 0.5;
  } else {
    dims.technical_judgment = 1;
  }

  if (c.doctrine.includes("D-008") || c.category === "MARKETING") {
    dims.risk_management = /experiment|reversib|small|limit|measure/i.test(action) ? 1 : 0.5;
  } else if (c.requiresEscalation) {
    dims.risk_management = out.escalated ? 1 : 0.25;
  } else {
    dims.risk_management = 1;
  }

  if (c.category === "STRATEGIC") {
    dims.strategic_alignment = /objective|priority|important|distraction|reversib/i.test(action) ? 1 : 0.5;
  } else {
    dims.strategic_alignment = 1;
  }

  const total = Object.values(dims).reduce((s, v) => s + v, 0) / Object.keys(dims).length;
  const passed = dims.action_selection === 1 && dims.escalation_correctness === 1 && total >= 0.6;

  return { caseId: c.id, title: c.title, dimensions: dims, total, passed, notes };
}

export function evaluateBenchmarkRuleBased(outputs: SystemOutput[]): {
  results: BenchmarkCaseResult[];
  overall: number;
  passedCount: number;
  totalCount: number;
} {
  const results = FOUNDER_BENCHMARK.map((c) => {
    const out = outputs.find((o) => o.caseId === c.id);
    if (!out) {
      return {
        caseId: c.id,
        title: c.title,
        dimensions: {
          strategic_alignment: 0,
          financial_discipline: 0,
          technical_judgment: 0,
          risk_management: 0,
          doctrine_adherence: 0,
          action_selection: 0,
          escalation_correctness: 0,
        },
        total: 0,
        passed: false,
        notes: ["no system output for this case"],
      } as BenchmarkCaseResult;
    }
    return evaluateCaseRuleBased(c, out);
  });
  const overall = results.reduce((s, r) => s + r.total, 0) / results.length;
  return {
    results,
    overall,
    passedCount: results.filter((r) => r.passed).length,
    totalCount: results.length,
  };
}

/**
 * Optional LLM evaluation. Returns per-dimension comments plus a 0-1 overall
 * similarity score. Falls back to the rule-based result when no model is
 * available or on failure.
 */
export async function evaluateCaseWithLlm(
  c: BenchmarkCase,
  out: SystemOutput,
  complete?: (opts: { taskType: string; system: string; user: string; maxTokens?: number }) => Promise<{ content: string }>,
): Promise<BenchmarkCaseResult> {
  if (!complete) return evaluateCaseRuleBased(c, out);
  try {
    const res = await complete({
      taskType: "executive_synthesis",
      system:
        "You are evaluating an AI executive against a Founder decision benchmark. Score decision similarity (not wording) 0-1 per dimension and return JSON: { \"strategic_alignment\":0-1, \"financial_discipline\":0-1, \"technical_judgment\":0-1, \"risk_management\":0-1, \"doctrine_adherence\":0-1, \"action_selection\":0-1, \"escalation_correctness\":0-1, \"notes\": [\"...\"] }. Be strict: forbidden actions = 0 for action_selection.",
      user: `CASE: ${c.title}\nSituation: ${c.situation}\nFounder decision: ${c.founderDecision}\nExpected: ${c.expectedAction.join(", ")}\nForbidden: ${c.forbiddenActions.join(", ")}\nRequires escalation: ${c.requiresEscalation}\n\nSYSTEM OUTPUT:\nRecommended: ${out.recommendedAction}\nEscalated: ${out.escalated}\nCategory: ${out.category}\nRisk: ${out.riskLevel ?? "unknown"}`,
      maxTokens: 400,
    });
    const parsed = JSON.parse(extractJson(res.content)) as Partial<Record<BenchmarkDimension, number>> & { notes?: string[] };
    const dims: Record<BenchmarkDimension, number> = {
      strategic_alignment: clamp(parsed.strategic_alignment),
      financial_discipline: clamp(parsed.financial_discipline),
      technical_judgment: clamp(parsed.technical_judgment),
      risk_management: clamp(parsed.risk_management),
      doctrine_adherence: clamp(parsed.doctrine_adherence),
      action_selection: clamp(parsed.action_selection),
      escalation_correctness: clamp(parsed.escalation_correctness),
    };
    const total = Object.values(dims).reduce((s, v) => s + v, 0) / 7;
    return {
      caseId: c.id,
      title: c.title,
      dimensions: dims,
      total,
      passed: dims.action_selection >= 0.9 && dims.escalation_correctness >= 0.9 && total >= 0.6,
      notes: parsed.notes ?? [],
    };
  } catch {
    return evaluateCaseRuleBased(c, out);
  }
}

function clamp(v: unknown): number {
  const n = typeof v === "number" ? v : Number(v);
  if (!Number.isFinite(n)) return 0;
  return Math.max(0, Math.min(1, n));
}

function extractJson(content: string): string {
  const start = content.indexOf("{");
  const end = content.lastIndexOf("}");
  if (start >= 0 && end > start) return content.slice(start, end + 1);
  return content;
}

export { FOUNDER_BENCHMARK };
