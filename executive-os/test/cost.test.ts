import { describe, expect, it } from "vitest";
import { estimateCost, checkBudget, unitPrices } from "../src/core/model/cost";

describe("model cost tracking (spec §34)", () => {
  it("estimates cost from token usage", () => {
    const cost = estimateCost("deepseek", "deepseek-chat", 1_000_000, 0);
    expect(cost).toBeCloseTo(0.27, 5);
  });

  it("falls back to a sane estimate for unknown models", () => {
    const c = unitPrices("unknown", "some-model");
    expect(c.inputPerM).toBeGreaterThan(0);
    expect(c.outputPerM).toBeGreaterThan(0);
  });

  it("allows calls within budget", () => {
    const d = checkBudget(
      { daily: 2, monthly: 50, perTask: 0.5, perRun: 1 },
      { today: 0, month: 0, task: 0, run: 0 },
      0.001,
    );
    expect(d.allowed).toBe(true);
  });

  it("STOPS when the per-task budget is exceeded", () => {
    const d = checkBudget(
      { daily: 2, monthly: 50, perTask: 0.5, perRun: 1 },
      { today: 0, month: 0, task: 0, run: 0 },
      0.6,
    );
    expect(d.allowed).toBe(false);
    expect(d.reason).toMatch(/per-task/i);
  });

  it("STOPS when the daily budget is exceeded", () => {
    const d = checkBudget(
      { daily: 2, monthly: 50, perTask: 0.5, perRun: 1 },
      { today: 1.99, month: 5, task: 0, run: 0 },
      0.02,
    );
    expect(d.allowed).toBe(false);
    expect(d.reason).toMatch(/daily/i);
  });

  it("STOPS when the monthly budget is exceeded", () => {
    const d = checkBudget(
      { daily: 2, monthly: 50, perTask: 0.5, perRun: 1 },
      { today: 0, month: 49.99, task: 0, run: 0 },
      0.02,
    );
    expect(d.allowed).toBe(false);
    expect(d.reason).toMatch(/monthly/i);
  });

  it("never silently continues on budget exceed — returns a reason", () => {
    const d = checkBudget(
      { daily: 1, monthly: 1, perTask: 0.1, perRun: 0.1 },
      { today: 0.99, month: 0.99, task: 0.05, run: 0.05 },
      0.02,
    );
    expect(d.allowed).toBe(false);
    expect(d.reason).toBeTruthy();
  });
});
