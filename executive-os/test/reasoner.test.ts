import { describe, expect, it } from "vitest";
import { GatewayReasoner, isCommandLike } from "../src/agent/reasoner";
import { createMockGateway } from "./helpers";

describe("reasoner classification — natural chat (not mock/canned)", () => {
  it("recognizes real command keywords (leading intent only)", () => {
    expect(isCommandLike("STATUS please")).toBe(true);
    expect(isCommandLike("risks now")).toBe(true);
    expect(isCommandLike("Run a brief")).toBe(true);
    expect(isCommandLike("pause")).toBe(true);
    expect(isCommandLike("hello")).toBe(false);
    expect(isCommandLike("What did I just ask?")).toBe(false);
    expect(isCommandLike("Can we afford another employee?")).toBe(false);
    // "status" in prose must NOT hijack natural chat into the overview.
    expect(isCommandLike("check the website status")).toBe(false);
    expect(isCommandLike("Use your tools to check the website status and reply")).toBe(false);
  });

  it("downgrades model-suggested 'command' to 'query' when no keyword is present", async () => {
    const { gateway } = createMockGateway([
      // The model (mock) over-eagerly labels a greeting as "command".
      { match: /Founder request: hello/, respond: '{"intent":"command","category":"OPERATIONAL","title":"t"}' },
      // A real command stays a command.
      { match: /Founder request: STATUS/, respond: '{"intent":"command","category":"OPERATIONAL","title":"t"}' },
    ]);
    const reasoner = new GatewayReasoner({ gateway });

    const hello = await reasoner.classify("hello");
    expect(hello.intent).toBe("query"); // guarded: greeting → natural model reply

    const status = await reasoner.classify("STATUS");
    expect(status.intent).toBe("command"); // real keyword → deterministic command
  });

  it("falls back to 'query' when the model returns invalid JSON", async () => {
    const { gateway } = createMockGateway([
      { match: /Founder request: nope/, respond: "not json at all" },
    ]);
    const reasoner = new GatewayReasoner({ gateway });
    const c = await reasoner.classify("nope");
    expect(c.intent).toBe("query");
  });
});
