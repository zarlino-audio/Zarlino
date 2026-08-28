import { describe, expect, it } from "vitest";
import { createTestStore } from "./helpers";
import { createTask } from "../src/core/tasks/model";
import { createApprovalRequest } from "../src/core/approvals/queue";
import { newId, nowIso } from "../src/ids";

describe("ExecutiveStore (spec §4, §31)", () => {
  it("migrates and seeds doctrine, catalog, and the pipeline workstream", () => {
    const store = createTestStore();
    expect(store.listActiveDoctrine().length).toBeGreaterThanOrEqual(10);
    expect(store.listProducts().length).toBe(24);
    expect(store.listWorkstreams().length).toBe(1);
    expect(store.listPipelineGates().length).toBe(40); // 4 plugins × 10 gates
    expect(store.getExecutiveState()).toBeTruthy();
  });

  it("onboards the technical roles so tasks can be assigned (closed loop)", () => {
    const store = createTestStore();
    const persons = store.listPersons();
    // 12 technical roles from the orchestrator roster.
    expect(persons.length).toBe(12);
    expect(persons.some((p) => p.id === "person_role_dsp_engineer" && p.name === "DSP Engineer")).toBe(true);
    expect(persons.some((p) => p.id === "person_role_release_engineer")).toBe(true);
    expect(persons.some((p) => p.id === "person_role_qa_engineer")).toBe(true);
    expect(persons.some((p) => p.id === "person_role_security_licensing_engineer")).toBe(true);
    // Assigning a task to a technical role works end-to-end.
    store.insertTask({
      id: "tsk_assign_check", created_at: nowIso(), updated_at: nowIso(), source: "founder",
      title: "Audit thread safety", status: "OPEN", priority: "P1", assignee_person_id: "person_role_code_reviewer",
    });
    expect(store.getTask("tsk_assign_check")?.assignee_person_id).toBe("person_role_code_reviewer");
  });

  it("reflects the real ZScorch/ZTame gate state and seeds pipeline facts (migration v3+v7)", () => {
    const store = createTestStore();
    const gates = store.listPipelineGates();
    for (const plugin of ["ZScorch", "ZTame"]) {
      const pg = gates.filter((g) => g.plugin_id === plugin);
      expect(pg.filter((g) => g.gate_number <= 9 && g.status === "PASSED").length).toBe(9);
      // Both are live on the website → Gate 10 SHIPPED (migration v7).
      expect(pg.find((g) => g.gate_number === 10)?.status).toBe("SHIPPED");
    }
    // Products reflect the shipped reality.
    expect(store.getProduct("prod_zscorch")?.status).toBe("SHIPPED");
    expect(store.getProduct("prod_ztame")?.status).toBe("SHIPPED");
    expect(store.getProduct("prod_ztame")?.version).toBe("1.0.0");
    // ZVocals / PresenceField remain untouched.
    for (const plugin of ["ZVocals", "PresenceField"]) {
      expect(store.listPipelineGates(plugin).every((g) => g.status === "NOT_STARTED")).toBe(true);
    }
    // Verified open tasks / risks / approval.
    expect(store.getTask("tsk_ztame_p8")?.status).toBe("BLOCKED");
    expect(store.getTask("tsk_ztame_p8")?.priority).toBe("P0");
    expect(store.getTask("tsk_zscorch_gate10")).toBeTruthy();
    expect(store.getTask("tsk_ztame_signing")).toBeTruthy();
    expect(store.openRisks().some((r) => r.id === "risk_ztame_p8" && r.level === "HIGH")).toBe(true);
    expect(store.listApprovals({ status: "PENDING" }).some((a) => a.id === "appr_zscorch_gate10")).toBe(true);
    // Idempotent: re-running migrations must not duplicate rows.
    store.migrate();
    store.migrate();
    expect(store.listPipelineGates().length).toBe(40);
    expect(store.listTasks().filter((t) => ["tsk_ztame_p8", "tsk_ztame_signing", "tsk_zscorch_gate10", "tsk_zscorch_signing"].includes(t.id)).length).toBe(4);
    expect(store.openRisks().length).toBe(4);
    expect(store.listApprovals({ status: "PENDING" }).length).toBe(1);
  });

  it("round-trips tasks with JSON metadata", () => {
    const store = createTestStore();
    const before = store.countOpenTasks();
    const task = createTask({ title: "Ship gate 3", priority: "P1", source: "founder" });
    store.insertTask(task);
    const got = store.getTask(task.id);
    expect(got?.title).toBe("Ship gate 3");
    expect(got?.priority).toBe("P1");
    expect(store.countOpenTasks()).toBe(before + 1);
    store.updateTask(task.id, { status: "DONE", completed_at: nowIso() });
    expect(store.getTask(task.id)?.status).toBe("DONE");
    expect(store.countOpenTasks()).toBe(before);
  });

  it("round-trips decisions and approvals", () => {
    const store = createTestStore();
    const now = nowIso();
    store.insertDecision({
      id: newId("dec"), created_at: now, updated_at: now, source: "founder",
      title: "T", problem: "P", status: "APPROVED", category: "OPERATIONAL", approval_required: false,
    });
    expect(store.listDecisions({ status: "APPROVED" }).length).toBe(1);
    const approval = createApprovalRequest({ action_label: "Spend $250", reason: "Test funnel" });
    store.insertApproval(approval);
    // Seed provides one pending approval (ZScorch Gate 10); ours is added on top.
    expect(store.listApprovals({ status: "PENDING" }).some((a) => a.id === approval.id)).toBe(true);
  });

  it("stores and retrieves memory with confidence and provenance", () => {
    const store = createTestStore();
    const now = nowIso();
    store.insertMemory({
      id: newId("mem"), created_at: now, updated_at: now, source: "founder",
      layer: "L1", kind: "FACT", title: "Cash", body: "Cash is 4000", importance: 0.8, status: "ACTIVE",
    });
    const found = store.retrieveMemories("how much cash do we have", { limit: 5 });
    expect(found[0]?.title).toBe("Cash");
  });

  it("records model costs and reports daily/monthly spend", () => {
    const store = createTestStore();
    store.record({ provider: "deepseek", model: "deepseek-chat", taskType: "chat", inputTokens: 1000, outputTokens: 500, cost: 0.005, latencyMs: 10, timestamp: nowIso() });
    expect(store.spentToday()).toBeCloseTo(0.005, 5);
    expect(store.spentThisMonth()).toBeCloseTo(0.005, 5);
    expect(store.costHistory().length).toBe(1);
  });

  it("aggregates token usage today and this month", () => {
    const store = createTestStore();
    const now = nowIso();
    store.record({ provider: "deepseek", model: "deepseek-chat", taskType: "chat", inputTokens: 1000, outputTokens: 500, cost: 0.005, latencyMs: 10, timestamp: now });
    store.record({ provider: "deepseek", model: "deepseek-reasoner", taskType: "lens_analysis", inputTokens: 3000, outputTokens: 2000, cost: 0.02, latencyMs: 20, timestamp: now });
    // 1000+500 + 3000+2000 = 6500 tokens total.
    expect(store.tokensToday()).toBe(6500);
    expect(store.tokensThisMonth()).toBe(6500);
    // Cost still tracked independently.
    expect(store.spentToday()).toBeCloseTo(0.025, 5);
  });

  it("records audit events and supports entity tracing", () => {
    const store = createTestStore();
    const now = nowIso();
    store.insertAudit({ id: newId("aud"), created_at: now, updated_at: now, source: "system", actor: "executive", action: "decision.create", entity_type: "decision", entity_id: "dec_x" });
    expect(store.auditForEntity("decision", "dec_x").length).toBe(1);
  });

  it("supports doctrine proposals and versioned approval", () => {
    const store = createTestStore();
    const now = nowIso();
    store.insertDoctrineRule({
      id: newId("doc"), created_at: now, updated_at: now, source: "executive",
      code: "D-011", principle: "Proposed principle", category: "GENERAL", status: "PROPOSED", version: 2, proposed_by: "executive",
    });
    expect(store.listDoctrine({ status: "PROPOSED" }).length).toBe(1);
    // Active doctrine still has no D-011.
    expect(store.listActiveDoctrine().find((r) => r.code === "D-011")).toBeUndefined();
  });

  it("tracks pipeline gates per plugin", () => {
    const store = createTestStore();
    const gates = store.listPipelineGates("ZScorch");
    expect(gates.length).toBe(10);
    expect(gates[0]?.gate_number).toBe(1);
    // ZScorch is shipped — gates 1-9 PASSED, Gate 10 SHIPPED.
    expect(gates[0]?.status).toBe("PASSED");
    expect(gates.find((g) => g.gate_number === 10)?.status).toBe("SHIPPED");
  });

  it("ships a plugin: product SHIPPED, Gate 10 SHIPPED, history recorded", () => {
    const store = createTestStore();
    // Ship a not-yet-shipped plugin through the Ship action.
    store.shipProduct("prod_zvocals", { version: "0.9.0", note: "test ship" });
    expect(store.getProduct("prod_zvocals")?.status).toBe("SHIPPED");
    expect(store.getProduct("prod_zvocals")?.version).toBe("0.9.0");
    expect(store.listPipelineGates("ZVocals").find((g) => g.gate_number === 10)?.status).toBe("SHIPPED");
    expect(store.listProductStatus("prod_zvocals").some((h) => h.status === "SHIPPED" && h.gate === "Gate 10")).toBe(true);
  });

  it("tracks long-horizon agent runs (migration v9)", () => {
    const store = createTestStore();
    const now = nowIso();
    store.createAgentRun({
      id: "run_t1", created_at: now, updated_at: now, source: "founder",
      prompt: "Do the thing", status: "RUNNING", iterations: 0, tools_called: 0, started_at: now,
    });
    store.heartbeatAgentRun("run_t1", 3, 7);
    store.finishAgentRun("run_t1", "DONE", "finished", undefined);
    const run = store.getAgentRun("run_t1");
    expect(run?.status).toBe("DONE");
    expect(run?.iterations).toBe(3);
    expect(run?.tools_called).toBe(7);
    expect(run?.result).toBe("finished");
    expect(run?.finished_at).toBeTruthy();
    expect(store.listAgentRuns().some((r) => r.id === "run_t1")).toBe(true);

    // A stopped run can be resumed (status back to RUNNING, counters kept).
    store.finishAgentRun("run_t1", "STOPPED", undefined, "bound");
    expect(store.getAgentRun("run_t1")?.status).toBe("STOPPED");
    // Continue reopens the same row (no duplicate id) and keeps history.
    store.resetAgentRun("run_t1");
    const resumed = store.getAgentRun("run_t1");
    expect(resumed?.status).toBe("RUNNING");
    expect(resumed?.iterations).toBe(3);
    expect(resumed?.tools_called).toBe(7);
    expect(resumed?.error).toBeNull();
    expect(store.listAgentRuns().filter((r) => r.id === "run_t1").length).toBe(1);
  });

  it("persists executive state and emergency flags", () => {
    const store = createTestStore();
    store.updateExecutiveState({ paused: 1, autonomy_mode: "APPROVAL_ONLY" });
    const state = store.getExecutiveState();
    expect(state?.paused).toBe(1);
    expect(state?.autonomy_mode).toBe("APPROVAL_ONLY");
  });

  it("tracks local machine tasks (migration v10: queue → claim → run → done)", () => {
    const store = createTestStore();
    const now = nowIso();
    store.createLocalTask({
      id: "ltk_1", created_at: now, updated_at: now, source: "founder",
      prompt: "Inspect the ZScorch plugin UI locally and report on it",
      status: "PENDING",
    });
    store.createLocalTask({
      id: "ltk_2", created_at: now, updated_at: now, source: "executive",
      prompt: "List local plugin folders",
      status: "PENDING",
    });

    // Claim returns the oldest PENDING task and marks it CLAIMED.
    const claimed = store.claimLocalTask("local-daemon");
    expect(claimed?.id).toBe("ltk_1");
    expect(claimed?.status).toBe("CLAIMED");
    expect(claimed?.claimed_by).toBe("local-daemon");

    // A second claim picks the next pending one.
    const claimed2 = store.claimLocalTask("local-daemon");
    expect(claimed2?.id).toBe("ltk_2");

    // No more pending.
    expect(store.claimLocalTask("local-daemon")).toBeNull();

    // Heartbeat → RUNNING, then finish with a result.
    store.heartbeatLocalTask("ltk_1", "RUNNING");
    store.finishLocalTask("ltk_1", "DONE", "The UI has a gain knob and a mix control.");
    const done = store.getLocalTask("ltk_1");
    expect(done?.status).toBe("DONE");
    expect(done?.result).toContain("gain knob");
    expect(done?.finished_at).toBeTruthy();

    // Failure path.
    store.finishLocalTask("ltk_2", "FAILED", undefined, "daemon crashed");
    expect(store.getLocalTask("ltk_2")?.status).toBe("FAILED");
    expect(store.getLocalTask("ltk_2")?.error).toBe("daemon crashed");

    expect(store.listLocalTasks().map((t) => t.id).sort()).toEqual(["ltk_1", "ltk_2"]);
  });

  it("tracks uploaded files (migration v11: create → get → list → delete)", () => {
    const store = createTestStore();
    const now = nowIso();
    store.createUploadedFile({
      id: "upf_1", created_at: now, updated_at: now, source: "founder",
      name: "zscorch-ui.png", mime: "image/png", size_bytes: 1024, data_base64: "aGVsbG8=", kind: "image",
    });
    store.createUploadedFile({
      id: "upf_2", created_at: now, updated_at: now, source: "founder",
      name: "spec.md", mime: "text/markdown", size_bytes: 512, data_base64: "aGVsbG8gd29ybGQ=", kind: "text",
    });

    const file = store.getUploadedFile("upf_1");
    expect(file?.name).toBe("zscorch-ui.png");
    expect(file?.kind).toBe("image");
    expect(file?.data_base64).toBe("aGVsbG8=");

    expect(store.listUploadedFiles().map((f) => f.id).sort()).toEqual(["upf_1", "upf_2"]);

    expect(store.deleteUploadedFile("upf_2")).toBe(true);
    expect(store.getUploadedFile("upf_2")).toBeNull();
    expect(store.deleteUploadedFile("upf_2")).toBe(false);
  });

  it("migration v5 adds delegate_mode and it defaults off", () => {
    const store = createTestStore();
    // After migrate + seed the column exists and is OFF by default.
    expect(store.getExecutiveState()?.delegate_mode).toBe(0);
    // Founder can enable it (persisted).
    store.updateExecutiveState({ delegate_mode: 1 });
    expect(store.getExecutiveState()?.delegate_mode).toBe(1);
  });

  it("buffers chat stream chunks for polled SSE delivery (migration v6)", () => {
    const store = createTestStore();
    const now = nowIso();
    store.openChatStream("str_test", "sess_1", now);

    store.appendChatStreamChunk("str_test", "Hello ");
    store.appendChatStreamChunk("str_test", "world");
    store.appendChatStreamChunk("str_test", "!");

    // Incremental read: nothing after the last seen index.
    const r1 = store.readChatStream("str_test", -1);
    expect(r1.chunks.join("")).toBe("Hello world!");
    expect(r1.complete).toBe(false);
    expect(r1.nextIndex).toBe(3); // count of chunks now buffered

    // Incremental read resumes from an index.
    const r2 = store.readChatStream("str_test", 1);
    expect(r2.chunks).toEqual(["world", "!"]);

    store.completeChatStream("str_test");
    expect(store.readChatStream("str_test", 2).complete).toBe(true);

    // Errors are surfaced, missing streams report complete to stop polling.
    store.completeChatStream("str_test", "boom");
    expect(store.readChatStream("str_test", 2).error).toBe("boom");
    expect(store.readChatStream("nope", 0).complete).toBe(true);
  });

  it("persists chat history and lists recent sessions", () => {
    const store = createTestStore();
    const now = nowIso();
    const msg = (sessionId: string, role: "founder" | "executive", content: string, at: string) =>
      store.insertChatMessage({ id: newId("msg"), created_at: at, updated_at: at, source: "founder", session_id: sessionId, role, content });

    msg("sess_a", "founder", "What blocks launch?", "2026-08-24T10:00:00.000Z");
    msg("sess_a", "executive", "ZTame P8 needs your ears", "2026-08-24T10:00:01.000Z");
    msg("sess_b", "founder", "Status?", "2026-08-24T11:00:00.000Z");

    const sessions = store.listChatSessions();
    expect(sessions.length).toBe(2);
    expect(sessions[0]?.session_id).toBe("sess_b"); // most recent first
    expect(sessions[0]?.message_count).toBe(1);
    expect(sessions[1]?.session_id).toBe("sess_a");
    expect(sessions[1]?.message_count).toBe(2);
    expect(sessions[1]?.last_message).toBe("ZTame P8 needs your ears");

    const transcript = store.listChatMessages("sess_a");
    expect(transcript.length).toBe(2);
    expect(transcript[0]?.role).toBe("founder");
    expect(transcript[1]?.content).toBe("ZTame P8 needs your ears");

    store.clearChat("sess_a");
    expect(store.listChatMessages("sess_a").length).toBe(0);
    expect(store.listChatSessions().length).toBe(1);
  });
});
