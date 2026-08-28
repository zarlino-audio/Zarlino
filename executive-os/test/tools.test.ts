import { describe, expect, it } from "vitest";
import { z } from "zod";
import { ToolRegistry } from "../src/core/tools/registry";
import type { ToolRecord } from "../src/core/tools/registry";
import type { Tool, ToolContext } from "../src/core/tools/types";
import { createApprovalRequest } from "../src/core/approvals/queue";

function makeContext(overrides: Partial<ToolContext["authority"]> = {}): ToolContext {
  const ctx: ToolContext = {
    authority: {
      actor: "executive",
      config: { spendApprovalThreshold: 100, maxAutonomousSpend: 25, autonomyMode: "CONTROLLED", delegateMode: false, fullAccess: false },
      emergency: { paused: false, spendingDisabled: false, deploymentsDisabled: false },
      ...overrides,
    },
    env: {},
    requestApproval: (input) =>
      createApprovalRequest({
        action_label: input.action_label,
        reason: input.reason,
        expected_upside: input.expected_upside,
        max_downside: input.max_downside,
        budget_available: input.budget_available,
        requested_by: "executive",
        source: "executive",
      }),
    audit: () => {},
  };
  return ctx;
}

const okTool: Tool<{ id: string }, { found: boolean }> = {
  name: "test.echo",
  description: "Echo an id",
  inputSchema: z.object({ id: z.string().min(1) }),
  outputSchema: z.object({ found: z.boolean() }),
  requiredPermission: "execute",
  riskLevel: "LOW",
  reversible: true,
  auditRequired: true,
  run: async (_ctx, input) => ({ found: input.id === "abc" }),
};

describe("tool system (spec §13, §24)", () => {
  it("converts registered tools to OpenAI function schemas for the model", () => {
    const reg = new ToolRegistry({ recordExecution: () => {} });
    reg.register(okTool);
    const schemas = reg.functionSchemas();
    expect(schemas.length).toBe(1);
    expect(schemas[0]?.type).toBe("function");
    // DeepSeek rejects dots in function names → sanitized to underscores.
    expect(schemas[0]?.function.name).toBe("test_echo");
    expect(schemas[0]?.function.description).toBe("Echo an id");
    expect(schemas[0]?.function.parameters).toBeTruthy();
    // The model returns the sanitized name; resolveName maps it back.
    expect(reg.resolveName("test_echo")).toBe("test.echo");
    expect(reg.resolveName("test.echo")).toBe("test.echo");
    expect(reg.resolveName("nope")).toBeUndefined();
  });

  it("rejects unregistered tools — the model cannot call arbitrary APIs", async () => {
    const reg = new ToolRegistry({ recordExecution: () => {} });
    const result = await reg.execute(makeContext(), "github.anything", {});
    expect(result.status).toBe("error");
    if (result.status === "error") expect(result.message).toMatch(/Unknown tool/);
  });

  it("validates input against the typed schema", async () => {
    const reg = new ToolRegistry({ recordExecution: () => {} });
    reg.register(okTool);
    const bad = await reg.execute(makeContext(), "test.echo", { id: 123 });
    expect(bad.status).toBe("error");
    if (bad.status === "error") expect(bad.message).toMatch(/Invalid input/);
  });

  it("executes successfully and records the run", async () => {
    const records: ToolRecord[] = [];
    const reg = new ToolRegistry({ recordExecution: (r) => records.push(r) });
    reg.register(okTool);
    const result = await reg.execute(makeContext(), "test.echo", { id: "abc" });
    expect(result.status).toBe("success");
    expect(records.length).toBeGreaterThan(0);
    expect(records.at(-1)?.verdict).toBe("ALLOW");
    expect(records.at(-1)?.status).toBe("SUCCESS");
  });

  it("denies tools whose permission the authority rejects", async () => {
    const spendTool: Tool<{ amount: number }, { ok: boolean }> = {
      name: "test.spend",
      description: "Spend money",
      inputSchema: z.object({ amount: z.number() }),
      requiredPermission: "spend",
      riskLevel: "MEDIUM",
      costEstimate: 500,
      reversible: false,
      auditRequired: true,
      run: async () => ({ ok: true }),
    };
    const reg = new ToolRegistry({ recordExecution: () => {} });
    reg.register(spendTool);
    const result = await reg.execute(makeContext(), "test.spend", { amount: 500 });
    expect(result.status).toBe("approval_required");
    if (result.status === "approval_required") {
      expect(result.approval.action_label).toContain("test.spend");
    }
  });

  it("denies when emergency pause is active", async () => {
    const reg = new ToolRegistry({ recordExecution: () => {} });
    reg.register(okTool);
    const ctx = makeContext({ emergency: { paused: true, spendingDisabled: false, deploymentsDisabled: false } });
    const result = await reg.execute(ctx, "test.echo", { id: "abc" });
    expect(result.status).toBe("denied");
  });

  it("reports tool failures as errors without crashing", async () => {
    const failTool: Tool = {
      name: "test.fail",
      description: "Always fails",
      inputSchema: z.object({}),
      requiredPermission: "execute",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async () => {
        throw new Error("boom");
      },
    };
    const reg = new ToolRegistry({ recordExecution: () => {} });
    reg.register(failTool);
    const result = await reg.execute(makeContext(), "test.fail", {});
    expect(result.status).toBe("error");
    if (result.status === "error") expect(result.message).toMatch(/boom/);
  });

  it("exposes the registered tool manifest (name, description, permission, risk)", () => {
    const reg = new ToolRegistry({ recordExecution: () => {} });
    reg.register(okTool);
    const list = reg.list();
    expect(list[0]).toMatchObject({
      name: "test.echo",
      description: "Echo an id",
      requiredPermission: "execute",
      riskLevel: "LOW",
      reversible: true,
    });
  });
});
