import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { DatabaseSync } from "node:sqlite";
import { createNodeSql } from "../src/db/sql";
import { ExecutiveStore } from "../src/db/store";
import { createTask } from "../src/core/tasks/model";
import { createApprovalRequest } from "../src/core/approvals/queue";

describe("ExecutiveStore (node:sqlite)", () => {
  let db: DatabaseSync;
  let store: ExecutiveStore;

  beforeAll(() => {
    db = new DatabaseSync(":memory:");
    store = new ExecutiveStore(createNodeSql(db as never));
  });

  afterAll(() => {
    db.close();
  });

  it("runs all migrations, then is idempotent", () => {
    const first = store.migrate();
    expect(first.length).toBeGreaterThan(0);
    expect(first[first.length - 1]!).toBeGreaterThanOrEqual(17);
    const again = store.migrate();
    expect(again.length).toBe(0);
  });

  it("seeds products, pipeline gates, doctrine and baseline finance", () => {
    const products = store.listProducts();
    expect(products.length).toBeGreaterThanOrEqual(2);
    expect(products.some((p) => p.name === "ZScorch")).toBe(true);
    const gates = store.listPipelineGates();
    expect(gates.length).toBeGreaterThanOrEqual(20); // 2 plugins × 10 gates
    const doctrine = store.listActiveDoctrine();
    expect(doctrine.length).toBeGreaterThanOrEqual(4);
    expect(store.latestSnapshot()).not.toBeNull();
  });

  it("creates, updates, and lists tasks", () => {
    const task = createTask({ title: "Test task", priority: "P1", source: "test" });
    store.insertTask(task);
    expect(store.getTask(task.id)?.title).toBe("Test task");
    store.updateTask(task.id, { status: "IN_PROGRESS" } as never);
    expect(store.getTask(task.id)?.status).toBe("IN_PROGRESS");
    expect(store.countOpenTasks()).toBeGreaterThanOrEqual(1);
  });

  it("creates approval requests and lists pending", () => {
    const appr = createApprovalRequest({ action_label: "Approve test", reason: "testing", source: "test" });
    store.insertApproval(appr);
    expect(store.listApprovals({ status: "PENDING" }).some((a) => a.id === appr.id)).toBe(true);
  });

  it("maintenance reaps stale runs, expired leases, and expired sessions", () => {
    const now = new Date().toISOString();
    store.createAgentRun({
      id: "run_stale", prompt: "x", status: "RUNNING", iterations: 0, tools_called: 0,
      started_at: now, source: "test", created_at: now, updated_at: now,
      last_heartbeat: new Date(Date.now() - 60 * 60 * 1000).toISOString(),
    });
    const stale = store.reapStaleAgentRuns(1000);
    expect(stale).toContain("run_stale");

    store.createLocalTask({
      id: "lt_lease", prompt: "y", status: "CLAIMED", claimed_by: "ghost", source: "test",
      created_at: now, updated_at: now,
      lease_expires_at: new Date(Date.now() - 60 * 1000).toISOString(),
    });
    const reaped = store.reapExpiredLocalTasks();
    expect(reaped).toContain("lt_lease");
    expect(store.getLocalTask("lt_lease")?.status).toBe("PENDING");

    store.createSession({ token: "tok_exp", username: "founder", expires_at: new Date(Date.now() - 1000).toISOString() });
    // getSession reads the row regardless of expiry; the sweep deletes it.
    expect(store.getSession("tok_exp")).not.toBeNull();
    store.deleteExpiredSessions();
    expect(store.getSession("tok_exp")).toBeNull();
  });

  it("records tool executions idempotently (upsert on same id)", () => {
    const rec = {
      id: "tex_1", tool_name: "test.tool", input_json: "{}", permission: "read" as const,
      verdict: "APPROVED", status: "RUNNING" as const, reversible: 1, source: "test", created_at: new Date().toISOString(),
    };
    store.recordToolExecution(rec);
    store.recordToolExecution({ ...rec, status: "SUCCESS", output_json: "{}" });
    const list = store.listToolExecutions();
    expect(list.filter((t) => t.id === "tex_1")).toHaveLength(1);
    expect(list.find((t) => t.id === "tex_1")?.status).toBe("SUCCESS");
  });

  it("exports and re-imports a snapshot", () => {
    const snap = store.exportSnapshot();
    expect(Array.isArray(snap.tasks)).toBe(true);
    expect(Array.isArray(snap.pipeline_gates)).toBe(true);
    const result = store.importSnapshot(snap);
    expect(result.tables).toBeGreaterThan(0);
    expect(result.rows).toBeGreaterThan(0);
  });

  it("local task claim lease round-trips", () => {
    store.createLocalTask({
      id: "lt_claim", prompt: "do work", status: "PENDING", source: "test",
      created_at: new Date().toISOString(), updated_at: new Date().toISOString(),
    });
    // claimLocalTask claims the OLDEST pending task — lt_lease (re-queued by
    // the maintenance test) precedes lt_claim, so claim until lt_claim.
    const first = store.claimLocalTask("daemon-a");
    expect(first?.status).toBe("CLAIMED");
    const claimed = store.claimLocalTask("daemon-a");
    expect(claimed?.id).toBe("lt_claim");
    expect(claimed?.status).toBe("CLAIMED");
    // A third claim should grab nothing (no PENDING tasks left).
    expect(store.claimLocalTask("daemon-b")).toBeNull();
  });
});
