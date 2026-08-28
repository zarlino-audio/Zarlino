import { describe, expect, it } from "vitest";
import { resolveTarget, tierFor, parseProviderModel, type TierTable } from "../src/core/model/routing";

const TABLE: TierTable = {
  cheap: "deepseek:deepseek-chat",
  medium: "deepseek:deepseek-chat",
  strong: "deepseek:deepseek-reasoner",
  strongest: "anthropic:claude-opus-4-1",
};

describe("model routing (spec §3, §35)", () => {
  it("maps routine classification to the cheap tier", () => {
    const t = resolveTarget("classification", TABLE);
    expect(t.tier).toBe("cheap");
    expect(t.model).toBe("deepseek-chat");
  });

  it("maps summarization to the cheap tier", () => {
    expect(tierFor("summarization")).toBe("cheap");
  });

  it("maps financial/technical reasoning to the strong tier", () => {
    expect(tierFor("financial_reasoning")).toBe("strong");
    expect(tierFor("technical_reasoning")).toBe("strong");
  });

  it("maps strategic decisions and final synthesis to the strongest tier", () => {
    expect(tierFor("strategic_reasoning")).toBe("strongest");
    expect(tierFor("executive_synthesis")).toBe("strongest");
    const t = resolveTarget("executive_synthesis", TABLE);
    expect(t.provider).toBe("anthropic");
    expect(t.model).toBe("claude-opus-4-1");
  });

  it("maps chat and briefings to the medium tier", () => {
    expect(tierFor("chat")).toBe("medium");
    expect(tierFor("briefing")).toBe("medium");
  });

  it("parses provider:model targets and defaults provider when bare", () => {
    expect(parseProviderModel("deepseek:deepseek-chat")).toEqual({ provider: "deepseek", model: "deepseek-chat" });
    expect(parseProviderModel("some-model")).toEqual({ provider: "deepseek", model: "some-model" });
  });

  it("supports explicit tier override", () => {
    const t = resolveTarget("chat", TABLE, "strong");
    expect(t.tier).toBe("strong");
    expect(t.model).toBe("deepseek-reasoner");
  });

  it("never hard-codes provider names in business logic (all config-driven)", () => {
    const custom: TierTable = { cheap: "openai:gpt-4o-mini", medium: "openai:gpt-4o", strong: "anthropic:claude-opus", strongest: "openai:o3" };
    expect(resolveTarget("strategic_reasoning", custom).provider).toBe("openai");
    expect(resolveTarget("financial_reasoning", custom).provider).toBe("anthropic");
  });
});
