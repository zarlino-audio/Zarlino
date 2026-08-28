/**
 * Scheduled briefings (spec §18, §19).
 *
 * Daily Executive Brief and Weekly Executive Review. Data gathering and
 * rendering are deterministic; missing numbers are explicitly labeled UNKNOWN
 * — never fabricated. An optional LLM call produces the "recommended focus"
 * line from real state.
 */

import type { ExecutiveStore } from "../db/store";
import type { CompleteOptions, ModelResponse } from "../core/model/types";

export interface BriefingDeps {
  store: ExecutiveStore;
  /** Optional gateway call for the "recommended focus" synthesis. */
  complete?: (opts: CompleteOptions) => Promise<ModelResponse>;
}

// ---------------------------------------------------------------------------
// Daily brief (spec §18)
// ---------------------------------------------------------------------------

export interface DailyBriefData {
  date: string;
  companyStatus: string;
  cash: number | null;
  revenue: number | null;
  expenses: number | null;
  currentObjective: string | null;
  topPriorities: string[];
  criticalRisks: Array<{ title: string; level: string }>;
  blockedTasks: number;
  openTasks: number;
  pendingApprovals: number;
  recentDecisions: Array<{ id: string; title: string; status: string }>;
  yesterdayChanges: number; // audit events in the last 24h
  agenda: Array<{ title: string; startAt: string; status: string }>; // today's calendar
  overdueReminders: Array<{ title: string; dueAt: string }>;
  recommendedFocus: string;
}

export function gatherDailyBriefData(store: ExecutiveStore): DailyBriefData {
  const snap = store.latestSnapshot();
  const state = store.getExecutiveState() ?? store.ensureExecutiveState();
  const objective = state.current_objective_id
    ? store.getObjective(state.current_objective_id)
    : store.listObjectives("ACTIVE")[0] ?? null;
  const risks = store.openRisks();
  const tasks = store.listTasks();
  const approvals = store.listApprovals({ status: "PENDING" });
  const decisions = store.listDecisions({ limit: 5 });
  const dayAgo = new Date(Date.now() - 86_400_000).toISOString();
  const yesterday = store.listAudit(1000).filter((a) => a.created_at >= dayAgo);
  const dayStart = new Date();
  dayStart.setUTCHours(0, 0, 0, 0);
  const dayEnd = new Date(dayStart.getTime() + 86_400_000);
  const agenda = store.listCalendarEvents(dayStart.toISOString(), dayEnd.toISOString()).map((e) => ({ title: e.title, startAt: e.start_at, status: e.status }));
  const overdueReminders = store.listReminders({ overdueOnly: true, limit: 10 }).map((r) => ({ title: r.title, dueAt: r.due_at }));

  return {
    date: new Date().toISOString().slice(0, 10),
    companyStatus: state.overall_status,
    cash: snap?.cash ?? null,
    revenue: snap?.revenue ?? null,
    expenses: snap?.expenses ?? null,
    currentObjective: objective?.title ?? null,
    topPriorities: safeJsonArray(state.top_priorities_json),
    criticalRisks: risks
      .filter((r) => r.level === "CRITICAL" || r.level === "HIGH")
      .slice(0, 5)
      .map((r) => ({ title: r.title, level: r.level })),
    blockedTasks: tasks.filter((t) => t.status === "BLOCKED").length,
    openTasks: tasks.filter((t) => ["OPEN", "IN_PROGRESS", "BLOCKED"].includes(t.status)).length,
    pendingApprovals: approvals.length,
    recentDecisions: decisions.map((d) => ({ id: d.id, title: d.title, status: d.status })),
    yesterdayChanges: yesterday.length,
    agenda,
    overdueReminders,
    recommendedFocus: "(pending synthesis)",
  };
}

export function renderDailyBrief(data: DailyBriefData): string {
  const num = (v: number | null, label: string) => (v === null ? `${label}: UNKNOWN` : `${label}: $${v.toFixed(2)}`);
  const lines = [
    "ZARLINO EXECUTIVE BRIEF",
    `Date: ${data.date}`,
    `Company status: ${data.companyStatus}`,
    "",
    "1. CURRENT COMPANY STATE",
    `  Current objective: ${data.currentObjective ?? "UNKNOWN"}`,
    `  Top priorities: ${data.topPriorities.length ? data.topPriorities.join(", ") : "none set"}`,
    `  ${num(data.cash, "Cash")}`,
    `  ${num(data.revenue, "Revenue (mo)")}`,
    `  ${num(data.expenses, "Expenses (mo)")}`,
    "",
    `2. YESTERDAY'S CHANGES: ${data.yesterdayChanges} recorded activity events`,
    "",
    "3. CRITICAL METRICS",
    `  Open tasks: ${data.openTasks} | Blocked: ${data.blockedTasks} | Pending approvals: ${data.pendingApprovals}`,
    "",
    "4. CASH POSITION",
    `  ${num(data.cash, "Cash")}`,
    "",
    "5. HIGHEST-PRIORITY TASKS",
    `  ${data.openTasks} open tasks (${data.blockedTasks} blocked)`,
    "",
    "6. BLOCKERS",
    `  ${data.blockedTasks} blocked tasks`,
    "",
    "7. RISKS",
    ...(data.criticalRisks.length ? data.criticalRisks.map((r) => `  [${r.level}] ${r.title}`) : ["  No critical risks currently tracked"]),
    "",
    "8. DECISIONS REQUIRED FROM FOUNDER",
    `  ${data.pendingApprovals} approval(s) pending`,
    ...data.recentDecisions.map((d) => `  - ${d.title} (${d.status})`),
    "",
    "9. TODAY'S AGENDA",
    ...(data.agenda.length ? data.agenda.map((e) => `  ${e.startAt.slice(11, 16) || "all day"} ${e.title} [${e.status}]`) : ["  No events scheduled today"]),
    ...(data.overdueReminders.length ? ["", "  ⏰ OVERDUE REMINDERS", ...data.overdueReminders.map((r) => `  - ${r.title} (due ${r.dueAt.slice(0, 16)})`)] : []),
    "",
    "10. RECOMMENDED FOCUS FOR TODAY",
    `  ${data.recommendedFocus}`,
  ];
  return lines.join("\n");
}

export async function generateDailyBrief(deps: BriefingDeps): Promise<{ data: DailyBriefData; markdown: string }> {
  const data = gatherDailyBriefData(deps.store);
  if (deps.complete) {
    try {
      const res = await deps.complete({
        taskType: "briefing",
        system: "You are the Chief of Staff of Zarlino Audio. Recommend the single most important focus for today based ONLY on the provided real state. Be concise (1-3 sentences).",
        user: `Current objective: ${data.currentObjective ?? "none"}\nTop priorities: ${data.topPriorities.join(", ") || "none"}\nOpen tasks: ${data.openTasks}\nBlocked: ${data.blockedTasks}\nPending approvals: ${data.pendingApprovals}\nCritical risks: ${data.criticalRisks.map((r) => r.title).join("; ") || "none"}`,
        maxTokens: 200,
      });
      data.recommendedFocus = res.content.trim();
    } catch {
      data.recommendedFocus = "Resolve the highest-priority blocked task and clear pending Founder approvals.";
    }
  } else {
    data.recommendedFocus = "Resolve the highest-priority blocked task and clear pending Founder approvals.";
  }
  return { data, markdown: renderDailyBrief(data) };
}

// ---------------------------------------------------------------------------
// Weekly review (spec §19)
// ---------------------------------------------------------------------------

export interface WeeklyReviewData {
  weekEnding: string;
  revenue: number | null;
  expenses: number | null;
  cash: number | null;
  cashMovement: number | null;
  products: Array<{ name: string; status: string }>;
  engineering: { issuesOpen: number; issuesCritical: number };
  marketing: Array<{ name: string; status: string; spent: number | null }>;
  majorDecisions: number;
  objectivesCompleted: number;
  objectivesActive: number;
  unexpectedEvents: number;
  topRisks: Array<{ title: string; level: string }>;
  nextWeekPriorities: string[];
  founderDecisionsRequired: number;
}

export function gatherWeeklyReviewData(store: ExecutiveStore): WeeklyReviewData {
  const snap = store.latestSnapshot();
  const prev = store.listSnapshots(2)[1] ?? null;
  const products = store.listProducts();
  const issues = store.listEngineeringIssues();
  const campaigns = store.listCampaigns();
  const decisions = store.listDecisions({ limit: 100 });
  const objectives = store.listObjectives();
  const risks = store.openRisks();
  const approvals = store.listApprovals({ status: "PENDING" });
  const state = store.getExecutiveState() ?? store.ensureExecutiveState();

  return {
    weekEnding: new Date().toISOString().slice(0, 10),
    revenue: snap?.revenue ?? null,
    expenses: snap?.expenses ?? null,
    cash: snap?.cash ?? null,
    cashMovement:
      snap?.cash !== null && snap?.cash !== undefined && prev?.cash !== null && prev?.cash !== undefined
        ? snap.cash - (prev.cash ?? 0)
        : null,
    products: products.map((p) => ({ name: p.name, status: p.status })),
    engineering: {
      issuesOpen: issues.filter((i) => i.status === "OPEN" || i.status === "IN_PROGRESS").length,
      issuesCritical: issues.filter((i) => i.severity === "CRITICAL" && i.status !== "RESOLVED").length,
    },
    marketing: campaigns.map((c) => ({ name: c.name, status: c.status, spent: c.spent ?? null })),
    majorDecisions: decisions.filter((d) => d.status === "EXECUTED" || d.status === "COMPLETED" || d.status === "APPROVED").length,
    objectivesCompleted: objectives.filter((o) => o.status === "COMPLETED").length,
    objectivesActive: objectives.filter((o) => o.status === "ACTIVE").length,
    unexpectedEvents: store.listAudit(1000).filter((a) => a.action.includes("failed") || a.action.includes("error")).length,
    topRisks: risks
      .filter((r) => r.level === "CRITICAL" || r.level === "HIGH")
      .slice(0, 5)
      .map((r) => ({ title: r.title, level: r.level })),
    nextWeekPriorities: safeJsonArray(state.top_priorities_json),
    founderDecisionsRequired: approvals.length,
  };
}

export function renderWeeklyReview(data: WeeklyReviewData): string {
  const num = (v: number | null, label: string) => (v === null ? `${label}: UNKNOWN` : `${label}: $${v.toFixed(2)}`);
  return [
    "WEEKLY EXECUTIVE REVIEW",
    `Week ending: ${data.weekEnding}`,
    "",
    num(data.revenue, "Revenue"),
    num(data.expenses, "Expenses"),
    num(data.cash, "Cash"),
    num(data.cashMovement, "Cash movement (vs prior snapshot)"),
    "",
    "PRODUCT PROGRESS",
    ...(data.products.length ? data.products.map((p) => `  - ${p.name}: ${p.status}`) : ["  (no products tracked)"]),
    "",
    "ENGINEERING PROGRESS",
    `  Open issues: ${data.engineering.issuesOpen} | Critical open: ${data.engineering.issuesCritical}`,
    "",
    "MARKETING PERFORMANCE",
    ...(data.marketing.length ? data.marketing.map((c) => `  - ${c.name}: ${c.status}${c.spent !== null ? ` (spent $${c.spent})` : ""}`) : ["  (no campaigns tracked)"]),
    "",
    "MAJOR DECISIONS THIS WEEK",
    `  ${data.majorDecisions} executed/approved`,
    "",
    "OBJECTIVES",
    `  Completed: ${data.objectivesCompleted} | Active: ${data.objectivesActive}`,
    "",
    "UNEXPECTED EVENTS / ERRORS",
    `  ${data.unexpectedEvents} failure/error events`,
    "",
    "TOP RISKS",
    ...(data.topRisks.length ? data.topRisks.map((r) => `  [${r.level}] ${r.title}`) : ["  (none)"]),
    "",
    "NEXT WEEK'S PRIORITIES",
    ...(data.nextWeekPriorities.length ? data.nextWeekPriorities.map((p) => `  - ${p}`) : ["  (not set)"]),
    "",
    "FOUNDER DECISIONS REQUIRED",
    `  ${data.founderDecisionsRequired} pending approval(s)`,
  ].join("\n");
}

export async function generateWeeklyReview(deps: BriefingDeps): Promise<{ data: WeeklyReviewData; markdown: string }> {
  const data = gatherWeeklyReviewData(deps.store);
  return { data, markdown: renderWeeklyReview(data) };
}

function safeJsonArray(value: string): string[] {
  try {
    const parsed = JSON.parse(value);
    return Array.isArray(parsed) ? parsed.map(String) : [];
  } catch {
    return [];
  }
}
