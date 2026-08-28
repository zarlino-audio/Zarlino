import { describe, expect, it } from "vitest";
import {
  approveDoctrineChange,
  assertDoctrineImmutable,
  proposeDoctrineChange,
  rejectDoctrineChange,
} from "../src/core/memory/doctrine";
import type { DoctrineRule } from "../src/types";

function activeRule(code: string, version: number): DoctrineRule {
  return {
    id: `doc_${code}_${version}`,
    created_at: new Date().toISOString(),
    updated_at: new Date().toISOString(),
    source: "seed",
    code,
    principle: `Principle ${code}`,
    category: "GENERAL",
    status: "ACTIVE",
    version,
  };
}

describe("doctrine protection (spec §5, §21, §41)", () => {
  it("creates a PROPOSED rule — never touches active doctrine", () => {
    const active = [activeRule("D-001", 1)];
    const proposal = proposeDoctrineChange(
      { code: "D-001", principle: "New principle", rationale: "Founder requested" },
      "executive",
    );
    expect(proposal.rule.status).toBe("PROPOSED");
    expect(proposal.rule.version).toBe(1);
    expect(active[0]?.status).toBe("ACTIVE");
    expect(active[0]?.principle).toBe("Principle D-001"); // unchanged
  });

  it("approving a change supersedes the old version and bumps the version", () => {
    const active = [activeRule("D-001", 1), activeRule("D-002", 1)];
    const proposal = proposeDoctrineChange({ code: "D-001", principle: "New principle" }, "executive");
    const { active: newActive, superseded } = approveDoctrineChange(proposal.rule, active, "founder");
    expect(newActive.status).toBe("ACTIVE");
    expect(newActive.version).toBe(2);
    expect(newActive.approved_by).toBe("founder");
    expect(superseded.map((r) => r.id)).toEqual(["doc_D-001_1"]);
    expect(superseded[0]?.status).toBe("SUPERSEDED");
  });

  it("rejects proposals", () => {
    const proposal = proposeDoctrineChange({ code: "D-001", principle: "X" }, "executive");
    const rejected = rejectDoctrineChange(proposal.rule);
    expect(rejected.status).toBe("REJECTED");
  });

  it("flags any attempt to silently change active doctrine", () => {
    const active = [activeRule("D-001", 1)];
    const violation = assertDoctrineImmutable(active, { code: "D-001", principle: "Different principle" });
    expect(violation).toContain("D-001");
    const same = assertDoctrineImmutable(active, { code: "D-001", principle: "Principle D-001" });
    expect(same).toBeNull();
  });
});
