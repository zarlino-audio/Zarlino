import { describe, expect, it } from "vitest";
import { FOUNDER_BENCHMARK } from "../src/benchmark/cases";
import { evaluateBenchmarkRuleBased, evaluateCaseRuleBased } from "../src/benchmark/evaluate";
import type { SystemOutput } from "../src/benchmark/evaluate";

describe("founder decision benchmark (spec §33)", () => {
  it("has at least 10 documented cases with required fields", () => {
    expect(FOUNDER_BENCHMARK.length).toBeGreaterThanOrEqual(10);
    for (const c of FOUNDER_BENCHMARK) {
      expect(c.id).toBeTruthy();
      expect(c.situation).toBeTruthy();
      expect(c.founderDecision).toBeTruthy();
      expect(c.expectedAction.length).toBeGreaterThan(0);
      expect(c.forbiddenActions).toBeDefined();
      expect(typeof c.requiresEscalation).toBe("boolean");
    }
  });

  it("gives a perfect score to a model output that matches the benchmark", () => {
    const outputs: SystemOutput[] = FOUNDER_BENCHMARK.map((c) => ({
      caseId: c.id,
      recommendedAction: `${c.founderDecision} ${c.expectedAction.join(" ")}`,
      escalated: c.requiresEscalation,
      confidence: 0.9,
      category: c.category,
    }));
    const { overall, passedCount, totalCount } = evaluateBenchmarkRuleBased(outputs);
    expect(totalCount).toBe(FOUNDER_BENCHMARK.length);
    expect(passedCount).toBe(totalCount);
    expect(overall).toBeGreaterThan(0.8);
  });

  it("punishes forbidden actions and wrong escalation", () => {
    const bad: SystemOutput[] = FOUNDER_BENCHMARK.map((c) => ({
      caseId: c.id,
      recommendedAction: `${c.forbiddenActions[0] ?? "delete"} everything`,
      escalated: !c.requiresEscalation,
      confidence: 1,
      category: c.category,
    }));
    const { passedCount } = evaluateBenchmarkRuleBased(bad);
    expect(passedCount).toBe(0);
  });

  it("requires escalation on B-002 (spend above threshold) and B-003 (hiring)", () => {
    const b2 = FOUNDER_BENCHMARK.find((c) => c.id === "B-002")!;
    const b3 = FOUNDER_BENCHMARK.find((c) => c.id === "B-003")!;
    expect(b2.requiresEscalation).toBe(true);
    expect(b3.requiresEscalation).toBe(true);
  });

  it("never allows deletion on B-006", () => {
    const b6 = FOUNDER_BENCHMARK.find((c) => c.id === "B-006")!;
    const result = evaluateCaseRuleBased(b6, {
      caseId: "B-006",
      recommendedAction: "Refuse: data deletion is never autonomous.",
      escalated: false,
      confidence: 1,
      category: "OPERATIONAL",
    });
    expect(result.dimensions.action_selection).toBe(1);
    expect(result.dimensions.escalation_correctness).toBe(1);
    expect(result.passed).toBe(true);
  });
});
