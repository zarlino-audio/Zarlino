/**
 * Tasks — the Chief of Staff's execution unit (spec §10).
 *
 * The Chief of Staff converts decisions into actions and tracks each task
 * through What / Why / Who / When / Dependency / Success condition / Status /
 * Next action. No important decision disappears after the conversation ends.
 */

import { newId, nowIso } from "../../ids";
import type { Decision, Task, TaskPriority, TaskStatus } from "../../types";

export interface TaskInput {
  title: string;
  description?: string;
  project_id?: string;
  decision_id?: string;
  assignee_person_id?: string;
  priority?: TaskPriority;
  due_date?: string;
  dependency_task_id?: string;
  success_condition?: string;
  source?: Task["source"];
  /** 1 = auto-execute without approval (default). */
  auto_run?: boolean;
}

export function createTask(input: TaskInput): Task {
  const now = nowIso();
  return {
    id: newId("task"),
    created_at: now,
    updated_at: now,
    source: input.source ?? "executive",
    title: input.title,
    description: input.description,
    project_id: input.project_id,
    decision_id: input.decision_id,
    assignee_person_id: input.assignee_person_id,
    status: "OPEN",
    priority: input.priority ?? "P2",
    due_date: input.due_date,
    dependency_task_id: input.dependency_task_id,
    success_condition: input.success_condition,
    auto_run: input.auto_run ?? true,
  };
}

/**
 * Convert an approved/executed decision's execution plan into tasks.
 * Each plan step becomes a task linked back to the decision.
 */
export function tasksFromDecision(decision: Decision, defaultPriority: TaskPriority = "P2"): Task[] {
  let plan: string[] = [];
  try {
    plan = decision.execution_plan_json ? (JSON.parse(decision.execution_plan_json) as string[]) : [];
  } catch {
    plan = [];
  }
  if (plan.length === 0) {
    plan = [decision.recommended_action ?? `Follow up on decision ${decision.id}`];
  }
  return plan.map((step, i) =>
    createTask({
      title: step.slice(0, 120),
      description: `From decision ${decision.id} (${decision.title}). Step ${i + 1}/${plan.length}.`,
      decision_id: decision.id,
      priority: i === 0 ? defaultPriority : "P2",
      source: decision.source,
      success_condition: decision.success_metric,
    }),
  );
}

/** Validate a task status transition. */
export function canTransitionTask(current: TaskStatus, next: TaskStatus): string | null {
  const allowed: Record<TaskStatus, TaskStatus[]> = {
    OPEN: ["IN_PROGRESS", "BLOCKED", "DONE", "CANCELLED"],
    IN_PROGRESS: ["BLOCKED", "DONE", "CANCELLED", "OPEN"],
    BLOCKED: ["IN_PROGRESS", "OPEN", "CANCELLED"],
    DONE: [],
    CANCELLED: [],
  };
  if (!allowed[current].includes(next)) {
    return `Cannot move task from ${current} to ${next}`;
  }
  return null;
}

/** Priority ordering: P0 first. */
export function comparePriority(a: Task, b: Task): number {
  const rank: Record<TaskPriority, number> = { P0: 0, P1: 1, P2: 2, P3: 3 };
  return rank[a.priority] - rank[b.priority];
}

/** Sort open tasks: priority, then due date, then created. */
export function sortOpenTasks(tasks: Task[]): Task[] {
  return [...tasks].sort((a, b) => {
    const pa = comparePriority(a, b);
    if (pa !== 0) return pa;
    const da = a.due_date ? new Date(a.due_date).getTime() : Number.POSITIVE_INFINITY;
    const db = b.due_date ? new Date(b.due_date).getTime() : Number.POSITIVE_INFINITY;
    return da - db;
  });
}

/** Detect blocked tasks (dependency not done, or marked BLOCKED). */
export function blockedTasks(tasks: Task[]): Task[] {
  const byId = new Map(tasks.map((t) => [t.id, t]));
  return tasks.filter((t) => {
    if (t.status === "BLOCKED") return true;
    if (t.dependency_task_id) {
      const dep = byId.get(t.dependency_task_id);
      if (dep && dep.status !== "DONE" && dep.status !== "CANCELLED") return true;
    }
    return false;
  });
}
