import { describe, expect, it } from "vitest";
import {
  runwayMonths,
  netBurn,
  roi,
  expectedValue,
  cac,
  paybackMonths,
  assessFinancials,
} from "../src/core/finance/calc";

describe("financial calculations (spec §8)", () => {
  it("computes net burn and runway when data is known", () => {
    expect(netBurn({ monthlyRevenue: 500, monthlyExpenses: 1800 })).toBe(1300);
    expect(runwayMonths({ cash: 5200, monthlyRevenue: 500, monthlyExpenses: 1800 })).toBeCloseTo(4, 5);
  });

  it("returns UNKNOWN (null) when data is missing — never fabricates", () => {
    expect(netBurn({ monthlyRevenue: 500 })).toBeNull();
    expect(runwayMonths({ cash: 1000 })).toBeNull();
    expect(roi(undefined, 100)).toBeNull();
    expect(cac(100, 0)).toBeNull();
    expect(paybackMonths(100, undefined, 0.7)).toBeNull();
  });

  it("returns infinite runway when not burning cash", () => {
    expect(runwayMonths({ cash: 1000, monthlyRevenue: 2000, monthlyExpenses: 1000 })).toBe(Number.POSITIVE_INFINITY);
  });

  it("computes ROI and expected value", () => {
    expect(roi(100, 150)).toBeCloseTo(0.5, 5);
    expect(expectedValue(0.5, 200, 100)).toBe(50);
  });

  it("computes CAC and payback", () => {
    expect(cac(250, 5)).toBe(50);
    expect(paybackMonths(50, 20, 0.5)).toBe(5);
  });

  it("flags unknown dimensions in the assessment", () => {
    const a = assessFinancials({ cash: 4000, monthlyExpenses: 1800 });
    expect(a.unknown).toContain("net burn");
    expect(a.runwayMonths).toBeNull();
  });

  it("produces a complete assessment when fully specified", () => {
    const a = assessFinancials({
      cash: 10000,
      monthlyRevenue: 1000,
      monthlyExpenses: 2000,
      proposedCost: 250,
      expectedGain: 1000,
      probabilityOfSuccess: 0.3,
      marketingSpend: 250,
      newCustomers: 5,
      monthlyRevenuePerCustomer: 20,
      grossMarginRate: 0.7,
    });
    expect(a.runwayMonths).toBe(10);
    expect(a.cac).toBe(50);
    expect(a.roi).toBeCloseTo(3, 5);
    expect(a.expectedValue).toBeCloseTo(125, 5);
  });
});
