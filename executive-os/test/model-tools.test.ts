import { describe, expect, it } from "vitest";
import { mockProvider } from "../src/core/model/providers";
import { ModelGateway } from "../src/core/model/gateway";
import { DefaultCostTracker, MemoryCostStorage } from "../src/core/model/cost";
import type { CompleteOptions } from "../src/core/model/types";

function testGateway() {
  const storage = new MemoryCostStorage();
  const cost = new DefaultCostTracker({ daily: 2, monthly: 50, perTask: 0.5, perRun: 1 }, storage);
  return new ModelGateway({
    config: {
      modelTierCheap: "mock:mock-model",
      modelTierMedium: "mock:mock-model",
      modelTierStrong: "mock:mock-model",
      modelTierStrongest: "mock:mock-model",
    },
    providers: [mockProvider()],
    cost,
  });
}

const REPO_TOOL: CompleteOptions["tools"] = [
  { type: "function", function: { name: "github.read_repo", description: "Read a repo", parameters: { type: "object", properties: {} } } },
];

describe("model function calling — tool execution in chat (not printed)", () => {
  it("returns a structured tool call when the user names a tool", async () => {
    const gw = testGateway();
    const res = await gw.complete({
      taskType: "chat",
      messages: [
        { role: "system", content: "sys" },
        { role: "user", content: "please tool:github.read_repo and summarize" },
      ],
      tools: REPO_TOOL,
      toolChoice: "auto",
      maxTokens: 100,
    });
    expect(res.toolCalls).toBeDefined();
    expect(res.toolCalls?.[0]?.name).toBe("github.read_repo");
    expect(res.toolCalls?.[0]?.arguments).toBe("{}");
  });

  it("returns plain text when no tool is requested (natural chat)", async () => {
    const gw = testGateway();
    const res = await gw.complete({
      taskType: "chat",
      messages: [{ role: "user", content: "hello" }],
      tools: REPO_TOOL,
      toolChoice: "auto",
      maxTokens: 100,
    });
    expect(res.toolCalls).toBeUndefined();
    expect(res.content).toContain("Mock response");
  });
});
