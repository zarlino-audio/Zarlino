import { describe, expect, it } from "vitest";
import { DecisionEngine, type Classification, type DecisionReasoner, type ExecutiveContext, type Lens, type Synthesis } from "../src/core/decision/engine";
import { tasksFromDecision } from "../src/core/tasks/model";
import { createTestStore } from "./helpers";
import type { DecisionOption } from "../src/types";
import { defaultActions } from "../src/core/authority/engine";

/** Deterministic reasoner for engine tests. */
class FakeReasoner implements DecisionReasoner {
  constructor(private readonly opts: { escalation?: boolean; action?: string } = {}) {}
  async classify(): Promise<Classification> {
    return { intent: "decision", category: "OPERATIONAL", title: "Test decision" };
  }
  async analyzeLens(lens: Lens): Promise<string> {
    return `${lens.toUpperCase()} assessment`;
  }
  async generateOptions(): Promise<DecisionOption[]> {
    return [
      { id: "opt1", label: "Proceed", reversible: true },
      { id: "opt2", label: "Experiment first", reversible: true },
    ];
  }
  async synthesize(): Promise<Synthesis> {
    return {
      recommendedAction: this.opts.action ?? "Run a small reversible experiment first.",
      confidence: 0.8,
      expectedOutcome: "Measured outcome against success metric.",
      executionPlan: ["Define metric", "Run experiment", "Measure"],
      successMetric: "At least one paying customer",
      uncertainties: ["Conversion rate unknown"],
    };
  }
}

function sinkFor(store: ReturnType<typeof createTestStore>) {
  return {
    insertDecision: (d: never) => store.insertDecision(d as never),
    insertApproval: (a: never) => store.insertApproval(a as never),
    insertRisk: (r: never) => store.insertRisk(r as never),
    insertAudit: (e: never) => store.insertAudit(e as never),
    createTasksFromDecision: (d: never) => {
      for (const t of tasksFromDecision(d as never)) store.insertTask(t);
    },
  };
}

function engineFor(store: ReturnType<typeof createTestStore>, opts: { escalation?: boolean } = {}) {
  const reasoner = new FakeReasoner(opts) as unknown as DecisionReasoner;
  return new DecisionEngine({
    reasoner,
    sink: sinkFor(store),
    authority: () => ({
      actor: "executive" as const,
      config: { spendApprovalThreshold: 100, maxAutonomousSpend: 25, autonomyMode: "CONTROLLED", delegateMode: false, fullAccess: false },
      emergency: { paused: false, spendingDisabled: false, deploymentsDisabled: false },
    }),
    actionSpecFor: (name) => defaultActions().find((a) => a.name === name),
  });
}

describe("decision engine (spec §6)", () => {
  it("runs the full pipeline and produces a structured decision", async () => {
    const store = createTestStore();
    const engine = engineFor(store);
    const result = await engine.run({
      problem: "Should we run a $20 experiment on ZTame landing page?",
      estimatedCost: 20,
      proposedAction: "marketing.create_experiment",
    });

    const d = result.decision;
    expect(d.id).toBeTruthy();
    expect(d.category).toBe("OPERATIONAL");
    expect(d.problem).toContain("experiment");
    expect(d.ceo_assessment).toBe("CEO assessment");
    expect(d.cfo_assessment).toBe("CFO assessment");
    expect(d.cto_assessment).toBe("CTO assessment");
    expect(d.risk_assessment).toBe("RISK assessment");
    expect(d.recommended_action).toContain("experiment");
    expect(d.confidence).toBe(0.8);
    expect(d.approval_required).toBe(false);
    expect(d.status).toBe("APPROVED");
    expect(d.outcome_status).toBe("PENDING");

    // Persisted + audit trail + tasks created from the execution plan.
    expect(store.getDecision(d.id)).toBeTruthy();
    expect(store.listAudit().some((a) => a.action === "decision.create")).toBe(true);
    const tasks = store.listTasks({ decisionId: d.id });
    expect(tasks.length).toBeGreaterThanOrEqual(3);
  });

  it("escalates and creates an approval request when authority requires it", async () => {
    const store = createTestStore();
    const engine = engineFor(store);
    const result = await engine.run({
      problem: "Spend $250 validating the affiliate acquisition funnel.",
      estimatedCost: 250,
      proposedAction: "marketing.spend",
      categoryHint: "FINANCIAL",
    });

    expect(result.authority.verdict).toBe("APPROVAL");
    expect(result.decision.status).toBe("ESCALATED");
    expect(result.approval).toBeTruthy();
    expect(result.approval?.status).toBe("PENDING");
    expect(result.approval?.max_downside).toContain("250");
    // No tasks created while pending approval.
    expect(store.listTasks({ decisionId: result.decision.id }).length).toBe(0);
    // Approval recorded.
    expect(store.getApproval(result.approval!.id)).toBeTruthy();
  });

  it("refuses delete-type actions (never autonomous)", async () => {
    const store = createTestStore();
    const engine = engineFor(store);
    const result = await engine.run({
      problem: "Delete a license record from production.",
      proposedAction: "executive.delete_data",
    });
    expect(result.authority.verdict).toBe("DENY");
    expect(result.decision.status).toBe("REJECTED"); // never executed, never escalated
    expect(result.approval).toBeUndefined();
  });

  it("produces one consolidated recommendation with risk + uncertainty", async () => {
    const store = createTestStore();
    const engine = engineFor(store);
    const result = await engine.run({ problem: "Should we hire a developer now?" });
    const trace = JSON.parse(result.decision.model_trace_json ?? "{}");
    expect(result.decision.recommended_action).toBeTruthy();
    expect(trace.uncertainties).toContain("Conversion rate unknown");
    expect(result.decision.risks_json).toBeTruthy();
  });

  it("uses provided options when given", async () => {
    const store = createTestStore();
    const engine = engineFor(store);
    const result = await engine.run({
      problem: "Pick a path",
      options: [
        { id: "a", label: "Path A", reversible: true },
        { id: "b", label: "Path B", reversible: false },
      ],
    });
    const options = JSON.parse(result.decision.options_json ?? "[]") as DecisionOption[];
    expect(options).toHaveLength(2);
    expect(options[0]?.id).toBe("a");
  });

  it("loads an externally-provided executive context into the record", async () => {
    const store = createTestStore();
    const engine = engineFor(store);
    const context: ExecutiveContext = {
      objectiveId: "obj_1",
      objectiveTitle: "Ship ZScorch",
      stateFacts: [
        { key: "cash", value: 4000, confidence: "FACT" },
        { key: "runway_months", value: null, confidence: "UNKNOWN" },
      ],
      memory: [],
      doctrine: [],
      availableBudget: 4000,
    };
    const result = await engine.run({ problem: "Ship ZScorch" }, context);
    expect(result.decision.objective_id).toBe("obj_1");
    const known = JSON.parse(result.decision.known_facts_json ?? "[]") as string[];
    expect(known.some((k) => k.includes("cash"))).toBe(true);
    const unknowns = JSON.parse(result.decision.unknowns_json ?? "[]") as string[];
    expect(unknowns).toContain("runway_months");
  });
});
