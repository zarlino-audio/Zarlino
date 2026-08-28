import { describe, expect, it } from "vitest";
import { retrieve, scoreMemory } from "../src/core/memory/retrieval";
import type { Memory } from "../src/types";

function mem(partial: Partial<Memory> & { title: string; body: string }): Memory {
  return {
    id: "m1",
    created_at: new Date().toISOString(),
    updated_at: new Date().toISOString(),
    source: "executive",
    layer: "L2",
    kind: "FACT",
    importance: 0.5,
    status: "ACTIVE",
    ...partial,
  };
}

describe("memory retrieval (spec §27)", () => {
  it("ranks relevant entries above irrelevant ones", () => {
    const relevant = mem({ id: "a", title: "Affiliate acquisition funnel", body: "Affiliate conversion rate is not yet established. Test with small budgets." });
    const irrelevant = mem({ id: "b", title: "PresenceField width algorithm", body: "Haas effect delay coefficients for stereo widening." });
    const result = retrieve([relevant, irrelevant], { query: "should we increase affiliate acquisition?", limit: 5 });
    expect(result[0]?.id).toBe("a");
  });

  it("filters by layer", () => {
    const l1 = mem({ id: "x", title: "Cash", body: "Cash is 4000", layer: "L1" });
    const l2 = mem({ id: "y", title: "Brand", body: "Brand standards", layer: "L2" });
    const result = retrieve([l1, l2], { layers: ["L2"] });
    expect(result.map((m) => m.id)).toEqual(["y"]);
  });

  it("excludes archived and expired entries", () => {
    const archived = mem({ id: "a1", title: "Old", body: "Old memory", status: "ARCHIVED" });
    const expired = mem({ id: "a2", title: "Expired", body: "Expired memory", expires_at: new Date(Date.now() - 1000).toISOString() });
    const active = mem({ id: "a3", title: "Active", body: "Active memory" });
    const result = retrieve([archived, expired, active], {});
    expect(result.map((m) => m.id)).toEqual(["a3"]);
  });

  it("boosts lessons and policies for decision relevance", () => {
    const lesson = mem({ id: "l1", title: "Lesson", body: "Creative generated traffic but failed conversion", kind: "LESSON", importance: 0.5 });
    const fact = mem({ id: "f1", title: "Traffic", body: "Traffic numbers from ads", kind: "FACT", importance: 0.5 });
    const s1 = scoreMemory(lesson, "conversion");
    const s2 = scoreMemory(fact, "conversion");
    expect(s1).toBeGreaterThan(s2);
  });

  it("applies a limit and minimum score", () => {
    const entries = [1, 2, 3, 4, 5].map((i) => mem({ id: `m${i}`, title: `Entry ${i}`, body: "body" }));
    expect(retrieve(entries, { limit: 2 }).length).toBe(2);
    expect(retrieve(entries, { minScore: 100 }).length).toBe(0);
  });

  it("renders a compact context block for prompts", async () => {
    const { renderMemoryContext } = await import("../src/core/memory/retrieval");
    const text = renderMemoryContext([mem({ id: "a", title: "Cash", body: "Cash is 4000", layer: "L1" })]);
    expect(text).toContain("L1/FACT");
  });
});
