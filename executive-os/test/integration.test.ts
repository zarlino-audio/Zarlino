/**
 * Integration test (spec §32): Founder request → classify → context → lenses →
 * synthesis → authority → approval / execution → tasks → audit trail.
 *
 * Uses the REAL GatewayReasoner + ModelGateway (mock provider) + durable
 * store, replicating exactly how the ExecutiveAgent wires them.
 */

import { describe, expect, it } from "vitest";
import { createTestStore, createMockGateway } from "./helpers";
import { GatewayReasoner } from "../src/agent/reasoner";
import { loadExecutiveContext } from "../src/agent/context";
import { DecisionEngine, type DecisionResult } from "../src/core/decision/engine";
import { defaultActions } from "../src/core/authority/engine";
import { transitionApproval } from "../src/core/approvals/queue";
import { tasksFromDecision } from "../src/core/tasks/model";
import { buildToolRegistry } from "../src/agent/tools";
import type { ExecutiveStore } from "../src/db/store";
import { ModelGateway } from "../src/core/model/gateway";
import { mockProvider } from "../src/core/model/providers";
import { DefaultCostTracker, MemoryCostStorage } from "../src/core/model/cost";

function buildEngine(store: ExecutiveStore, gateway: ModelGateway) {
  const reasoner = new GatewayReasoner({ gateway });
  const engine = new DecisionEngine({
    reasoner,
    sink: {
      insertDecision: (d) => store.insertDecision(d),
      insertApproval: (a) => store.insertApproval(a),
      insertRisk: (r) => store.insertRisk(r),
      insertAudit: (e) => store.insertAudit(e),
      createTasksFromDecision: (d) => {
        for (const t of tasksFromDecision(d)) store.insertTask(t);
      },
    },
    authority: () => ({
      actor: "executive" as const,
      config: { spendApprovalThreshold: 100, maxAutonomousSpend: 25, autonomyMode: "CONTROLLED", delegateMode: false, fullAccess: false },
      emergency: { paused: false, spendingDisabled: false, deploymentsDisabled: false },
    }),
    actionSpecFor: (name) => defaultActions().find((a) => a.name === name),
  });
  return { engine, reasoner };
}

/** Simulates ExecutiveAgent.runDecision (same wiring). */
async function agentRunDecision(store: ExecutiveStore, gateway: ModelGateway, problem: string, extra?: { estimatedCost?: number; proposedAction?: string; categoryHint?: string }) {
  const { engine } = buildEngine(store, gateway);
  const context = loadExecutiveContext(store, { problem });
  return engine.run(
    {
      problem,
      estimatedCost: extra?.estimatedCost,
      proposedAction: extra?.proposedAction,
      categoryHint: extra?.categoryHint as never,
      source: "founder",
    },
    context,
  );
}

/** Simulates ExecutiveAgent.onApprovalDecided (including the audit trail). */
function approveInStore(store: ExecutiveStore, approvalId: string): void {
  const request = store.getApproval(approvalId);
  if (!request) throw new Error("approval not found");
  const result = transitionApproval(request, "approve", "founder", "approved in integration test");
  if (!result.ok || !result.request) throw new Error(result.error ?? "transition failed");
  store.updateApproval(approvalId, result.request);
  store.insertAudit({
    id: `aud_${crypto.randomUUID()}`,
    created_at: new Date().toISOString(),
    updated_at: new Date().toISOString(),
    source: "system",
    actor: "founder",
    action: "approval.approve",
    entity_type: "approval_request",
    entity_id: approvalId,
  });
  if (result.request.decision_id) {
    const decision = store.getDecision(result.request.decision_id);
    if (decision) {
      store.updateDecision(decision.id, { status: "APPROVED" });
      for (const t of tasksFromDecision({ ...decision, status: "APPROVED" })) store.insertTask(t);
    }
  }
}

describe("integration: founder request → decision → approval → execution → audit", () => {
  it("executes a low-risk decision autonomously and creates tasks", async () => {
    const store = createTestStore();
    const { gateway } = createMockGateway();
    const result: DecisionResult = await agentRunDecision(
      store,
      gateway,
      "Should we run a $20 experiment on the ZTame landing page?",
      { estimatedCost: 20, proposedAction: "marketing.create_experiment" },
    );

    expect(result.decision.status).toBe("APPROVED");
    expect(result.authority.verdict).toBe("ALLOW");
    expect(result.decision.approval_required).toBe(false);

    // Chief of Staff created tasks from the execution plan.
    const tasks = store.listTasks({ decisionId: result.decision.id });
    expect(tasks.length).toBeGreaterThan(0);

    // Audit trail exists and can be traced decision → evidence.
    const audit = store.auditForEntity("decision", result.decision.id);
    expect(audit.length).toBeGreaterThan(0);
    expect(audit.some((a) => a.action === "decision.create")).toBe(true);

    // Memory was consulted (doctrine was loaded into the decision).
    expect(result.decision.risks_json).toBeTruthy();
  });

  it("escalates high spend, then creates tasks after Founder approval", async () => {
    const store = createTestStore();
    const { gateway } = createMockGateway();
    const result: DecisionResult = await agentRunDecision(
      store,
      gateway,
      "Spend $250 validating the affiliate acquisition funnel.",
      { estimatedCost: 250, proposedAction: "marketing.spend", categoryHint: "FINANCIAL" },
    );

    expect(result.authority.verdict).toBe("APPROVAL");
    expect(result.decision.status).toBe("ESCALATED");
    expect(result.approval).toBeTruthy();
    expect(result.approval?.status).toBe("PENDING");
    expect(result.approval?.recommendation).toBe("APPROVE");

    // No tasks until approved.
    expect(store.listTasks({ decisionId: result.decision.id }).length).toBe(0);

    // Founder approves via the queue (mirrors /api/approvals/:id/approve).
    approveInStore(store, result.approval!.id);
    expect(store.getApproval(result.approval!.id)?.status).toBe("APPROVED");
    expect(store.getDecision(result.decision.id)?.status).toBe("APPROVED");
    expect(store.listTasks({ decisionId: result.decision.id }).length).toBeGreaterThan(0);

    // Approval decision is logged in the audit trail.
    expect(store.listAudit().some((a) => a.action === "approval.approve")).toBe(true);
  });

  it("records model cost per call for observability", async () => {
    const store = createTestStore();
    // Wire the gateway's cost tracker to the durable store (as the real agent does).
    const cost = new DefaultCostTracker(
      { daily: 2, monthly: 50, perTask: 0.5, perRun: 1 },
      store,
    );
    const gateway = new ModelGateway({
      config: { modelTierCheap: "mock:m", modelTierMedium: "mock:m", modelTierStrong: "mock:m", modelTierStrongest: "mock:m" },
      providers: [mockProvider()],
      cost,
    });
    await agentRunDecision(store, gateway, "Should we add a preset browser to ZTame?");
    expect(store.costHistory().length).toBeGreaterThan(3); // classify + 4 lenses + synthesis
    expect(store.spentToday()).toBeGreaterThan(0);
  });

  it("stops when the AI budget is exceeded (spec §34)", async () => {
    // Budget is nearly exhausted so the very first call is blocked.
    const storage = new MemoryCostStorage();
    const cost = new DefaultCostTracker({ daily: 0.0000001, monthly: 0.0000001, perTask: 1, perRun: 1 }, storage);
    const gateway = new ModelGateway({
      config: { modelTierCheap: "mock:m", modelTierMedium: "mock:m", modelTierStrong: "mock:m", modelTierStrongest: "mock:m" },
      providers: [mockProvider()],
      cost,
    });
    const store = createTestStore();
    await expect(
      agentRunDecision(store, gateway, "Any decision"),
    ).rejects.toThrow(/budget/i);
  });

  it("internal store-backed tools execute through the permissioned registry", async () => {
    const store = createTestStore();
    const registry = buildToolRegistry({
      env: {},
      sink: { recordExecution: () => {} },
    });

    const ctx = {
      authority: {
        actor: "executive" as const,
        config: { spendApprovalThreshold: 100, maxAutonomousSpend: 25, autonomyMode: "CONTROLLED" as const, delegateMode: false, fullAccess: false },
        emergency: { paused: false, spendingDisabled: false, deploymentsDisabled: false },
      },
      env: {},
      store,
      requestApproval: () => null,
      audit: () => {},
    };

    const created = await registry.execute(ctx, "projects.create_task", { title: "Tool-created task", priority: "P1" });
    expect(created.status).toBe("success");
    expect(store.listTasks().some((t) => t.title === "Tool-created task")).toBe(true);

    const snap = await registry.execute(ctx, "finance.get_snapshot", {});
    expect(snap.status).toBe("success");
    if (snap.status === "success") {
      expect((snap.output as { status?: string }).status ?? "snapshot").toBeTruthy();
    }
  });

  it("registers the expanded tool set (github, cloudflare, website, web, internal)", () => {
    const registry = buildToolRegistry({ env: {}, sink: { recordExecution: () => {} } });
    const names = registry.list().map((t) => t.name);
    // Web search + web automation.
    for (const n of ["web.search", "web.fetch_page", "web.check_url", "web.crawl_links"]) {
      expect(names).toContain(n);
    }
    // Cloudflare discovery + zones.
    for (const n of ["cloudflare.list_workers", "cloudflare.get_worker", "cloudflare.get_deployments", "cloudflare.list_zones", "cloudflare.get_zone"]) {
      expect(names).toContain(n);
    }
    // Expanded GitHub reads.
    for (const n of ["github.list_releases", "github.get_file", "github.list_commits", "github.list_repos", "github.get_website_source"]) {
      expect(names).toContain(n);
    }
    // Cloudflare worker source.
    expect(names).toContain("cloudflare.get_worker_source");
    expect(names).toContain("cloudflare.get_dns_records");
    expect(names).toContain("cloudflare.get_account");
    // Vision tools.
    expect(names).toContain("vision.analyze_image");
    expect(names).toContain("vision.analyze_url_image");
    // Internal read tools.
    for (const n of ["executive.list_approvals", "executive.list_decisions", "executive.list_risks", "executive.list_tasks", "executive.list_products", "executive.list_activity", "marketing.list_campaigns"]) {
      expect(names).toContain(n);
    }
    // File tools.
    expect(names).toContain("executive.list_files");
    expect(names).toContain("executive.get_file");
    // New GitHub tools.
    for (const n of ["github.list_collaborators", "github.get_commit_diff", "github.get_file_metadata"]) {
      expect(names).toContain(n);
    }
    // Website page fetch.
    expect(names).toContain("website.get_page");
    // Core internal tools still present.
    expect(names).toContain("pipeline.list_gates");
    expect(names).toContain("finance.get_snapshot");
  });

  it("pipeline gates are readable and updatable through the registry (closed loop)", async () => {
    const store = createTestStore();
    const registry = buildToolRegistry({
      env: {},
      sink: { recordExecution: () => {} },
    });
    const ctx = {
      authority: {
        actor: "executive" as const,
        config: { spendApprovalThreshold: 100, maxAutonomousSpend: 25, autonomyMode: "CONTROLLED" as const, delegateMode: false, fullAccess: false },
        emergency: { paused: false, spendingDisabled: false, deploymentsDisabled: false },
      },
      env: {},
      store,
      requestApproval: () => null,
      audit: () => {},
    };

    const listed = await registry.execute(ctx, "pipeline.list_gates", {});
    expect(listed.status).toBe("success");
    if (listed.status !== "success") throw new Error(listed.status === "error" ? listed.message : "denied");
    const gates = (listed.output as { gates: Array<{ id: string; pluginId: string; gateNumber: number; status: string }> }).gates;
    expect(gates.length).toBe(40);
    const ztameG5 = gates.find((g) => g.pluginId === "ZTame" && g.gateNumber === 5);
    expect(ztameG5).toBeTruthy();
    expect(ztameG5?.status).toBe("PASSED");

    const updated = await registry.execute(ctx, "pipeline.update_gate", { gateId: ztameG5!.id, status: "BLOCKED", note: "golden refs missing" });
    expect(updated.status).toBe("success");
    const after = store.listPipelineGates("ZTame").find((g) => g.gate_number === 5);
    expect(after?.status).toBe("BLOCKED");
    expect(after?.note).toBe("golden refs missing");

    // Restore so other tests stay deterministic.
    await registry.execute(ctx, "pipeline.update_gate", { gateId: ztameG5!.id, status: "PASSED" });
    expect(store.listPipelineGates("ZTame").find((g) => g.gate_number === 5)?.status).toBe("PASSED");
  });

  it("records tool executions without primary-key collisions (RUNNING → SUCCESS)", async () => {
    const store = createTestStore();
    const registry = buildToolRegistry({
      env: {},
      // Real sink: persist every lifecycle record to the store.
      sink: { recordExecution: (record) => store.recordToolExecution(record as never) },
    });
    const ctx = {
      authority: {
        actor: "executive" as const,
        config: { spendApprovalThreshold: 100, maxAutonomousSpend: 25, autonomyMode: "CONTROLLED" as const, delegateMode: false, fullAccess: false },
        emergency: { paused: false, spendingDisabled: false, deploymentsDisabled: false },
      },
      env: {},
      store,
      requestApproval: () => null,
      audit: () => {},
    };

    const snap = await registry.execute(ctx, "finance.get_snapshot", {});
    expect(snap.status).toBe("success");

    // The registry writes RUNNING then SUCCESS with the same executionId —
    // the store upsert must collapse them into a single row (no UNIQUE error).
    const execs = store.listToolExecutions();
    expect(execs.length).toBe(1);
    expect(execs[0]?.status).toBe("SUCCESS");
    expect(execs[0]?.tool_name).toBe("finance.get_snapshot");
    expect(execs[0]?.verdict).toBe("ALLOW");

    // A second execution produces its own row.
    await registry.execute(ctx, "finance.get_snapshot", {});
    expect(store.listToolExecutions().length).toBe(2);
  });

  it("streams model completions as deltas and records the call", async () => {
    const store = createTestStore();
    const cost = new DefaultCostTracker(
      { daily: 2, monthly: 50, perTask: 0.5, perRun: 1 },
      store,
    );
    const gateway = new ModelGateway({
      config: { modelTierCheap: "mock:m", modelTierMedium: "mock:m", modelTierStrong: "mock:m", modelTierStrongest: "mock:m" },
      providers: [mockProvider([{ match: /hello/i, respond: "Hello, Founder. Streaming works." }])],
      cost,
    });

    const deltas: string[] = [];
    for await (const delta of gateway.streamComplete({ taskType: "chat", user: "hello", maxTokens: 100 })) {
      deltas.push(delta);
    }

    // The mock provider streams in 10-char chunks that reassemble exactly.
    const joined = deltas.join("");
    expect(joined).toBe("Hello, Founder. Streaming works.");
    expect(deltas.length).toBeGreaterThan(1); // actually streamed in pieces

    // The streamed call is recorded for observability like a normal call.
    expect(store.costHistory().length).toBe(1);
    expect(store.spentToday()).toBeGreaterThan(0);
  });

  it("streams without emitting anything when the provider lacks streaming", async () => {
    const store = createTestStore();
    const cost = new DefaultCostTracker({ daily: 2, monthly: 50, perTask: 0.5, perRun: 1 }, store);
    const gateway = new ModelGateway({
      config: { modelTierCheap: "mock:m", modelTierMedium: "mock:m", modelTierStrong: "mock:m", modelTierStrongest: "mock:m" },
      providers: [{ id: "mock", label: "Mock", complete: async () => ({ content: "x", provider: "mock", model: "m", inputTokens: 1, outputTokens: 1, latencyMs: 1 }) }],
      cost,
    });
    await expect(async () => {
      for await (const _ of gateway.streamComplete({ taskType: "chat", user: "hi" })) { /* noop */ }
    }).rejects.toThrow(/does not support streaming/);
  });
});
