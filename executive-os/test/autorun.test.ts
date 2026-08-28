/**
 * Auto-run tests — "everything auto-completes without a Run click".
 *
 * Covers the migration (v15 tasks.auto_run), the eligibility sweep helpers,
 * the running-run guard, and the pending-approval skip rule. The full sweep
 * (agent-level autoRunEligibleTasks) is exercised via the maintenance /
 * task-creation triggers on the live worker.
 */
import { describe, expect, it } from "vitest";
import { createTestStore } from "./helpers";
import { nowIso } from "../src/ids";
import { createTask } from "../src/core/tasks/model";

describe("auto-run: migration v15", () => {
  it("adds auto_run to tasks (defaults to 1) and stays idempotent", () => {
    const store = createTestStore();
    store.migrate(); // re-running migrate is a no-op (versions tracked)
    const tasks = store.listTasks({ limit: 50 });
    expect(tasks.length).toBeGreaterThan(0);
    for (const t of tasks) {
      // auto_run is always present after v15 (0 = manual, 1 = auto).
      expect(t.auto_run === undefined).toBe(false);
    }
    // Normal implementation work is auto-eligible.
    expect(store.getTask("tsk_zscorch_signing")?.auto_run).toBeTruthy();
    expect(store.getTask("tsk_ztame_signing")?.auto_run).toBeTruthy();
  });

  it("founder-decision tasks are marked manual (never auto-run)", () => {
    const store = createTestStore();
    const gate10 = store.getTask("tsk_zscorch_gate10");
    const p8 = store.getTask("tsk_ztame_p8");
    // Seeded founder-only tasks must NOT auto-run.
    expect(gate10?.auto_run).toBeFalsy();
    expect(p8?.auto_run).toBeFalsy();
    // A normal implementation task stays eligible.
    const signing = store.getTask("tsk_zscorch_signing");
    expect(signing?.auto_run).toBeTruthy();
  });

  it("createTask defaults auto_run = true and honors auto_run = false", () => {
    expect(createTask({ title: "default" }).auto_run).toBe(true);
    expect(createTask({ title: "manual", auto_run: false }).auto_run).toBe(false);
  });
});

describe("auto-run: eligibility sweep (store)", () => {
  it("lists auto-runnable tasks by priority then oldest, skipping manual/done", () => {
    const store = createTestStore(false);
    store.insertTask(createTask({ title: "P2 old", priority: "P2", auto_run: true }));
    store.insertTask(createTask({ title: "P0 urgent", priority: "P0", auto_run: true }));
    store.insertTask(createTask({ title: "manual only", priority: "P0", auto_run: false }));
    const done = createTask({ title: "done task", priority: "P0", auto_run: true });
    store.insertTask(done);
    store.updateTask(done.id, { status: "DONE" });

    const eligible = store.listAutoRunnableTasks(50).map((t) => t.title);
    // My auto tasks are included and priority-ordered (P0 before P2)…
    expect(eligible.indexOf("P0 urgent")).toBeGreaterThanOrEqual(0);
    expect(eligible.indexOf("P2 old")).toBeGreaterThan(eligible.indexOf("P0 urgent"));
    // …manual-only and done tasks are excluded.
    expect(eligible).not.toContain("manual only");
    expect(eligible).not.toContain("done task");
    // Every eligible row is auto_run + not done.
    for (const t of store.listAutoRunnableTasks(50)) {
      expect(t.auto_run).toBeTruthy();
      expect(["OPEN", "IN_PROGRESS"]).toContain(t.status);
    }
  });

  it("hasRunningRunForTask / countRunningAgentRuns reflect RUNNING runs only", () => {
    const store = createTestStore(false);
    const now = nowIso();
    const task = createTask({ title: "t" });
    store.insertTask(task);
    expect(store.hasRunningRunForTask(task.id)).toBe(false);

    store.createAgentRun({
      id: "run_auto_1", created_at: now, updated_at: now, source: "executive",
      task_id: task.id, prompt: "p", status: "RUNNING", iterations: 0, tools_called: 0, started_at: now,
    });
    expect(store.hasRunningRunForTask(task.id)).toBe(true);
    expect(store.countRunningAgentRuns()).toBe(1);

    store.finishAgentRun("run_auto_1", "DONE", "ok");
    expect(store.hasRunningRunForTask(task.id)).toBe(false);
    expect(store.countRunningAgentRuns()).toBe(0);
  });

  it("pendingApprovalForTask is true only when the task's decision has a PENDING approval", () => {
    const store = createTestStore(false);
    const now = nowIso();

    const noDecision = createTask({ title: "no decision" });
    store.insertTask(noDecision);
    expect(store.pendingApprovalForTask(noDecision.id)).toBe(false);

    const withDecision = createTask({ title: "with decision", decision_id: "dec_auto_1" });
    store.insertTask(withDecision);
    expect(store.pendingApprovalForTask(withDecision.id)).toBe(false); // no approval yet

    store.insertApproval({
      id: "apr_auto_1", created_at: now, updated_at: now, source: "executive",
      action_label: "Spend $500", reason: "needs founder", recommendation: "APPROVE",
      status: "PENDING", requested_by: "executive", decision_id: "dec_auto_1",
    });
    expect(store.pendingApprovalForTask(withDecision.id)).toBe(true);

    // A different task (no decision link) is unaffected.
    expect(store.pendingApprovalForTask(noDecision.id)).toBe(false);
  });
});
