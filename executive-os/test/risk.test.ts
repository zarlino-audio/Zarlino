import { describe, expect, it } from "vitest";
import { classifyRisk } from "../src/core/decision/risk";

describe("risk engine (spec §23)", () => {
  it("classifies low-impact reversible actions as LOW", () => {
    const r = classifyRisk({ financialImpact: 0, reversible: true });
    expect(r.level).toBe("LOW");
  });

  it("classifies irreversible medium spend as MEDIUM", () => {
    const r = classifyRisk({ financialImpact: 50, reversible: false });
    expect(r.level).toBe("MEDIUM");
  });

  it("classifies high spend as HIGH", () => {
    const r = classifyRisk({ financialImpact: 2000, reversible: false });
    expect(r.level).toBe("HIGH");
  });

  it("classifies legal or security exposure as CRITICAL", () => {
    expect(classifyRisk({ reversible: false, legal: true }).level).toBe("CRITICAL");
    expect(classifyRisk({ reversible: false, securityImpact: true }).level).toBe("CRITICAL");
  });

  it("classifies production + customer impact as HIGH", () => {
    const r = classifyRisk({ reversible: false, productionImpact: true, customerImpact: true });
    expect(r.level).toBe("HIGH");
  });

  it("never returns CRITICAL for a small reversible action", () => {
    expect(classifyRisk({ financialImpact: 5, reversible: true }).level).not.toBe("CRITICAL");
  });

  it("reports factors for traceability", () => {
    const r = classifyRisk({ financialImpact: 250, reversible: false });
    expect(r.factors.length).toBeGreaterThan(0);
    expect(r.score).toBeGreaterThanOrEqual(3);
  });
});
