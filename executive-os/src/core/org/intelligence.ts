/**
 * Collective intelligence measurement + organizational analyst (AOrgI §35,
 * §36) — the organization's self-observation mechanism.
 *
 * Computes the five system-level properties from REAL tables only:
 *   - convergence: negotiation rounds, capped debates, stalls,
 *   - scalability: messages per completed task (coordination overhead),
 *   - robustness: chaos drills run/passed, doctor health, agent liveness,
 *   - objective achievement: completed objectives with PostObjectiveRecords,
 *   - emergence: analyst-recorded observations (never auto-fabricated).
 *
 * The analyst produces observations + recommendations and stores a report
 * row; the Doctor enforces freshness. Chaos drills are guarded: production
 * instances are NEVER targeted (§35 — no production disruption).
 */

import type { ExecutiveStore } from "../../db/store";
import type { OrgAnalystReport } from "../../types";

export interface CollectiveMetrics {
  generatedAt: string;
  convergence: {
    negotiationsTotal: number;
    resolved: number;
    avgRoundsToResolve: number | null;
    cappedDebates: number;
    stalled: number;
  };
  scalability: {
    messagesTotal: number;
    completedTasks: number;
    completedTasksWithMessages: number;
    messagesPerCompletedTask: number | null;
  };
  robustness: {
    chaosTestsTotal: number;
    chaosTestsPassed: number;
    doctorFailCount: number;
    doctorWarnCount: number;
    registeredAgents: number;
    staleAgents: number;
  };
  objectiveAchievement: {
    objectivesTotal: number;
    completed: number;
    completedWithRecord: number;
    missingRecords: number;
    proxyDivergence: null; // honest: no per-objective proxy metrics defined yet
  };
  emergence: {
    observationsRecorded: number;
  };
}

function safeJsonCount(json: string | undefined | null): number {
  try {
    const v = JSON.parse(json ?? "[]");
    return Array.isArray(v) ? v.length : 0;
  } catch {
    return 0;
  }
}

export function collectiveMetrics(store: ExecutiveStore): CollectiveMetrics {
  const now = Date.now();

  const negotiations = store.listOrgNegotiations(500);
  const resolved = negotiations.filter((n) => n.status === "RESOLVED");
  const rounds = resolved.map((n) => n.round);
  const avgRounds = resolved.length > 0 ? rounds.reduce((a, b) => a + b, 0) / resolved.length : null;
  const capped = resolved.filter((n) => n.round >= n.max_rounds).length;
  const stalled = negotiations.filter((n) => n.status === "ARBITRATED" || n.status === "OPEN").length;

  const messages = store.listOrgMessages({ limit: 2000 });
  const tasks = store.listTasks({ limit: 1000 });
  const done = tasks.filter((t) => t.status === "DONE");
  const doneIds = new Set(done.map((t) => t.id));
  const completedWithMessages = done.filter((t) => messages.some((m) => m.task_id === t.id)).length;
  void doneIds; // reserved for future per-task coordination analysis

  const chaos = store.listChaosTests(500);
  const doctor = store.latestDoctorLog();
  let doctorFail = 0;
  let doctorWarn = 0;
  if (doctor?.summary_json) {
    try {
      const s = JSON.parse(doctor.summary_json) as { fail?: number; warn?: number };
      doctorFail = s.fail ?? 0;
      doctorWarn = s.warn ?? 0;
    } catch {
      /* doctor summary unreadable — report 0, the doctor itself will flag */
    }
  }
  const agents = store.listOrgAgents();
  const staleAgents = agents.filter((a) => !a.last_seen || Date.parse(a.last_seen) < now - 2 * 24 * 60 * 60 * 1000).length;

  const objectives = store.listObjectives();
  const completed = objectives.filter((o) => ["COMPLETED", "DONE", "ACHIEVED"].includes(String(o.status).toUpperCase()));
  const completedWithRecord = completed.filter((o) => store.hasPostObjectiveRecord(o.id)).length;

  const reports = store.listAnalystReports(20);
  const observationsRecorded = reports.reduce((n, r) => n + safeJsonCount(r.observations_json), 0);

  return {
    generatedAt: new Date().toISOString(),
    convergence: {
      negotiationsTotal: negotiations.length,
      resolved: resolved.length,
      avgRoundsToResolve: avgRounds,
      cappedDebates: capped,
      stalled,
    },
    scalability: {
      messagesTotal: messages.length,
      completedTasks: done.length,
      completedTasksWithMessages: completedWithMessages,
      messagesPerCompletedTask: done.length > 0 ? messages.length / done.length : null,
    },
    robustness: {
      chaosTestsTotal: chaos.length,
      chaosTestsPassed: chaos.filter((c) => c.status === "PASSED").length,
      doctorFailCount: doctorFail,
      doctorWarnCount: doctorWarn,
      registeredAgents: agents.length,
      staleAgents,
    },
    objectiveAchievement: {
      objectivesTotal: objectives.length,
      completed: completed.length,
      completedWithRecord,
      missingRecords: completed.length - completedWithRecord,
      proxyDivergence: null,
    },
    emergence: { observationsRecorded },
  };
}

/** Run the organizational analyst: measure, observe, recommend, record. */
export function runOrgAnalyst(store: ExecutiveStore, trigger: string): OrgAnalystReport {
  const metrics = collectiveMetrics(store);
  const observations: string[] = [];
  const recommendations: string[] = [];
  const c = metrics.convergence;
  const r = metrics.robustness;
  const o = metrics.objectiveAchievement;

  observations.push(
    c.negotiationsTotal === 0
      ? "Convergence: no negotiations recorded yet — nothing to converge on."
      : `Convergence: ${c.resolved}/${c.negotiationsTotal} negotiations resolved, avg ${c.avgRoundsToResolve?.toFixed(1) ?? "n/a"} rounds.`,
  );
  if (c.cappedDebates > 0) {
    observations.push(`Convergence: ${c.cappedDebates} debate(s) exhausted their round cap — arbitration was required (§22).`);
    recommendations.push("Review whether 3 rounds is too few for recurring disagreement classes.");
  }
  if (c.stalled > 0) {
    recommendations.push(`Arbitrate or close ${c.stalled} stalled negotiation(s) (§23).`);
  }
  observations.push(
    r.chaosTestsTotal === 0
      ? "Robustness: no chaos drills run yet — fallback behavior is unproven (§35)."
      : `Robustness: ${r.chaosTestsPassed}/${r.chaosTestsTotal} chaos drills passed.`,
  );
  if (r.chaosTestsTotal === 0) {
    recommendations.push("Run an agent_kill_sim drill (POST /api/org/chaos-tests/run) to verify fallback detection.");
  }
  if (r.staleAgents > 0) {
    recommendations.push(`${r.staleAgents} registered agent instance(s) stale — restart or deregister them.`);
  }
  if (o.missingRecords > 0) {
    recommendations.push(`${o.missingRecords} completed objective(s) lack PostObjectiveRecords — record them (§38).`);
  }
  observations.push(
    `Scalability: ${metrics.scalability.messagesPerCompletedTask === null ? "no completed tasks to measure" : metrics.scalability.messagesPerCompletedTask.toFixed(2)} messages per completed task.`,
  );
  observations.push(
    `Objective achievement: ${o.completedWithRecord}/${o.completed || 0} completed objectives recorded. Proxy divergence: not yet measurable (no per-objective proxy/true metric pairs defined — honest null, §35).`,
  );
  observations.push(`Emergence: ${metrics.emergence.observationsRecorded} observation(s) recorded across analyst reports.`);

  return store.createAnalystReport({
    generatedAt: metrics.generatedAt,
    trigger,
    metrics,
    observations,
    recommendations,
  });
}

// -- chaos drills (§35: robustness — staging/simulated only) ------------------

export type ChaosKind = "agent_kill_sim" | "grant_degrade";
export type ChaosOutcome = { status: "PASSED" | "FAILED" | "REFUSED"; detail: string };

const PRODUCTION_AGENTS = new Set(["executive_do_01", "local_daemon_01"]);

export function runChaosTest(store: ExecutiveStore, kind: string, target: string): ChaosOutcome {
  const record = store.createChaosTest({ kind, target });
  let outcome: ChaosOutcome;
  try {
    if (kind === "agent_kill_sim") {
      outcome = agentKillSim(store, target);
    } else if (kind === "grant_degrade") {
      outcome = grantDegrade(store, target);
    } else {
      outcome = { status: "FAILED", detail: `Unknown chaos kind '${kind}' (agent_kill_sim | grant_degrade).` };
    }
  } catch (err) {
    outcome = { status: "FAILED", detail: err instanceof Error ? err.message : String(err) };
  }
  store.finishChaosTest(record.id, outcome.status, outcome);
  return outcome;
}

function agentKillSim(store: ExecutiveStore, target: string): ChaosOutcome {
  const agent = store.getOrgAgent(target);
  if (!agent) return { status: "FAILED", detail: `Target agent '${target}' not found.` };
  if (PRODUCTION_AGENTS.has(target) || agent.source !== "chaos") {
    return {
      status: "REFUSED",
      detail: `Refusing to disrupt '${target}': production instances are never targeted; register a chaos probe (source='chaos') for drills (§35).`,
    };
  }
  // Kill simulation: heartbeat stops 3 days ago.
  const threeDaysAgo = new Date(Date.now() - 3 * 24 * 60 * 60 * 1000).toISOString();
  store.setOrgAgentLastSeen(target, threeDaysAgo);
  // Fallback behavior: the registry's liveness detection must flag the agent.
  const stale = store.countStaleOrgAgents(new Date(Date.now() - 2 * 24 * 60 * 60 * 1000).toISOString());
  // Restore: the probe is removed so the drill leaves no residue.
  store.deleteOrgAgent(target);
  if (stale >= 1) {
    return { status: "PASSED", detail: `Fallback behavior verified: '${target}' flagged stale by registry liveness detection. Probe cleaned up.` };
  }
  return { status: "FAILED", detail: `Liveness detection did not flag '${target}' as stale.` };
}

function grantDegrade(store: ExecutiveStore, target: string): ChaosOutcome {
  const agent = store.getOrgAgent(target);
  if (!agent) return { status: "FAILED", detail: `Target agent '${target}' not found.` };
  if (PRODUCTION_AGENTS.has(target) || agent.source !== "chaos") {
    return {
      status: "REFUSED",
      detail: `Refusing to degrade grants for '${target}': production instances are never targeted; register a chaos probe (source='chaos') for drills (§35).`,
    };
  }
  // Degrade: an L0 grant on a dedicated probe capability — fail-closed
  // enforcement must DENY. The probe capability never collides with a real
  // grant, and the grant is deleted in finally, so nothing real is touched.
  const PROBE_CAPABILITY = "__chaos_probe__";
  const grant = store.upsertCapabilityGrant({
    roleId: agent.role_id,
    capability: PROBE_CAPABILITY,
    autonomyLevel: "L0",
    allowedActions: ["*"],
  });
  try {
    const verdict = store.capabilityVerdictForAgent(target, PROBE_CAPABILITY, "probe_action");
    if (verdict.verdict === "DENY") {
      return { status: "PASSED", detail: "Fail-closed enforcement verified: L0 grant denied execution for the probe." };
    }
    return { status: "FAILED", detail: `L0 grant did not deny execution (got ${verdict.verdict}).` };
  } finally {
    store.deleteCapabilityGrant(grant.id);
  }
}
