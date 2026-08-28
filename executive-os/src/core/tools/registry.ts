/**
 * Tool registry — the only way tools are invoked (spec §13, §24).
 *
 * execute() flow:
 *   1. Find the tool (unknown tools are never reachable).
 *   2. Validate input against the tool's zod schema.
 *   3. Evaluate the authority engine for the tool's permission/risk/cost.
 *   4. DENY  → record + audit, return denied.
 *      APPROVAL → create approval request, record BLOCKED, return approval_required.
 *      ALLOW → run, record + audit, return success.
 *   5. Every execution is recorded for observability (spec §25).
 */

import { newId, nowIso } from "../../ids";
import { z } from "zod";
import { evaluateAction } from "../authority/engine";
import type { AuthorityContext } from "../authority/engine";
import { toolApprovalRequired, toolDenied, toolError, success } from "./types";
import type { Tool, ToolContext, ToolResult } from "./types";
import type { ModelToolFunction } from "../model/types";

export interface ToolRecord {
  id: string;
  tool_name: string;
  input_json: string;
  output_json?: string;
  permission: string;
  verdict: string;
  status: "PENDING" | "RUNNING" | "SUCCESS" | "FAILED" | "BLOCKED" | "CANCELLED";
  cost_estimate?: number;
  reversible: boolean;
  error?: string;
  created_at: string;
}

export interface ToolExecutionSink {
  recordExecution(record: ToolRecord): void;
}

export class ToolRegistry {
  private tools = new Map<string, Tool>();

  constructor(private readonly sink: ToolExecutionSink) {}

  register(tool: Tool): void {
    if (this.tools.has(tool.name)) {
      throw new Error(`Tool "${tool.name}" already registered`);
    }
    this.tools.set(tool.name, tool);
  }

  get(name: string): Tool | undefined {
    return this.tools.get(name);
  }

  list(): Tool[] {
    return [...this.tools.values()];
  }

  /**
   * OpenAI-compatible function schemas for model function calling. Converts
   * each registered tool's zod input schema via zod 4's built-in JSON schema
   * conversion (z.toJSONSchema), so the model can invoke ANY registered tool
   * through the same authority-gated path.
   */
  /** Sanitized function-calling name for a tool (DeepSeek rejects dots). */
  schemaName(name: string): string {
    return name.replace(/\./g, "_");
  }

  /** Resolve a (possibly sanitized) model-returned name back to the real tool. */
  resolveName(name: string): string | undefined {
    if (this.tools.has(name)) return name;
    const match = [...this.tools.values()].find((t) => this.schemaName(t.name) === name);
    return match?.name;
  }

  functionSchemas(): Array<{ type: "function"; function: ModelToolFunction }> {
    return [...this.tools.values()].map((t) => ({
      type: "function",
      function: {
        name: this.schemaName(t.name),
        description: t.description,
        parameters: z.toJSONSchema(t.inputSchema),
      },
    }));
  }

  has(name: string): boolean {
    return this.tools.has(name);
  }

  async execute(ctx: ToolContext, name: string, rawInput: unknown): Promise<ToolResult> {
    const tool = this.tools.get(name);
    if (!tool) {
      return toolError(`Unknown tool "${name}" — the model cannot call unregistered tools.`);
    }

    const executionId = newId("tex");

    // 1. Input validation (spec §24: tool schema validation).
    const parsed = tool.inputSchema.safeParse(rawInput);
    if (!parsed.success) {
      this.record(ctx, tool, name, executionId, "PENDING", "DENY", rawInput, undefined, parsed.error.message);
      ctx.audit("executive", "tool.invalid_input", "tool_execution", executionId, { tool: name, error: parsed.error.message });
      return toolError(`Invalid input for "${name}": ${parsed.error.message}`);
    }
    const input = parsed.data;

    // 2. Authority check.
    const authorityCtx: AuthorityContext = {
      actor: ctx.authority.actor,
      config: ctx.authority.config,
      emergency: ctx.authority.emergency,
    };
    const decision = evaluateAction(
      {
        name: tool.name,
        permission: tool.requiredPermission,
        description: tool.description,
        riskLevel: tool.riskLevel,
        costEstimate: tool.costEstimate,
        reversible: tool.reversible,
        auditRequired: tool.auditRequired,
      },
      authorityCtx,
    );

    if (decision.verdict === "DENY") {
      this.record(ctx, tool, name, executionId, "BLOCKED", "DENY", input, undefined, decision.reasons.join("; "));
      ctx.audit("executive", "tool.denied", "tool_execution", executionId, { tool: name, reasons: decision.reasons }, "DENY");
      return toolDenied(decision.reasons.join("; "));
    }

    if (decision.verdict === "APPROVAL") {
      const approval = ctx.requestApproval({
        action_label: `${name}: ${tool.description}`,
        reason: `Tool "${name}" requires approval under current authority policy.`,
        max_downside: tool.costEstimate !== undefined ? `$${tool.costEstimate}` : undefined,
        decision_id: undefined,
      });
      this.record(ctx, tool, name, executionId, "BLOCKED", "APPROVAL", input, undefined, "Awaiting Founder approval");
      ctx.audit("executive", "tool.approval_required", "tool_execution", executionId, { tool: name }, "APPROVAL");
      return approval ? toolApprovalRequired(approval) : toolDenied("Approval required but approval queue unavailable");
    }

    // 3. Execute (ALLOW).
    try {
      this.record(ctx, tool, name, executionId, "RUNNING", "ALLOW", input);
      const output = await tool.run(ctx, input);
      this.record(ctx, tool, name, executionId, "SUCCESS", "ALLOW", input, output);
      if (tool.auditRequired) {
        ctx.audit("executive", `tool.${name}`, "tool_execution", executionId, { tool: name }, "ALLOW");
      }
      return success(output, executionId);
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      this.record(ctx, tool, name, executionId, "FAILED", "ALLOW", input, undefined, message);
      ctx.audit("executive", "tool.failed", "tool_execution", executionId, { tool: name, error: message });
      return toolError(`Tool "${name}" failed: ${message}`);
    }
  }

  private record(
    ctx: ToolContext,
    tool: Tool,
    name: string,
    executionId: string,
    status: ToolRecord["status"],
    verdict: string,
    input: unknown,
    output?: unknown,
    error?: string,
  ): void {
    const now = nowIso();
    this.sink.recordExecution({
      id: executionId,
      tool_name: name,
      input_json: safeJson(input),
      output_json: output === undefined ? undefined : safeJson(output),
      permission: tool.requiredPermission,
      verdict,
      status,
      cost_estimate: tool.costEstimate,
      reversible: tool.reversible,
      error,
      created_at: now,
    });
  }
}

function safeJson(value: unknown): string {
  try {
    return JSON.stringify(value);
  } catch {
    return '"<unserializable>"';
  }
}
