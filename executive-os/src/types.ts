/**
 * Shared entity and value types for the Zarlino Executive OS.
 *
 * Every structured entity carries: stable id, created_at, updated_at, source,
 * and (where applicable) confidence, status, and provenance. Critical
 * information is stored as structured data — never only as natural-language
 * blobs (spec §4).
 */

// ---------------------------------------------------------------------------
// Confidence / uncertainty taxonomy (spec §22)
// ---------------------------------------------------------------------------

export type Confidence = "FACT" | "INFERENCE" | "ASSUMPTION" | "ESTIMATE" | "UNKNOWN";

export interface Provenance {
  /** Where the record came from: founder, executive, system, a lens, a tool, manual. */
  source: Source;
  /** Optional reference, e.g. tool run id, upstream record id, prompt version. */
  ref?: string;
  /** Who/what created it. */
  actor?: string;
}

export type Source =
  | "founder"
  | "executive"
  | "system"
  | "ceo_lens"
  | "cfo_lens"
  | "cto_lens"
  | "tool"
  | "manual"
  | "seed";

// ---------------------------------------------------------------------------
// Core entities (spec §4)
// ---------------------------------------------------------------------------

export interface BaseEntity {
  id: string;
  created_at: string; // ISO-8601
  updated_at: string; // ISO-8601
  source: Source;
}

export interface FounderProfile extends BaseEntity {
  name: string;
  email?: string;
  role: string;
  authority_level: "FOUNDER";
}

export interface Organization extends BaseEntity {
  name: string;
  mission?: string;
}

export interface Objective extends BaseEntity {
  title: string;
  description?: string;
  status: "ACTIVE" | "COMPLETED" | "CANCELLED";
  priority: number; // lower = higher priority
  completed_at?: string;
}

export interface Project extends BaseEntity {
  name: string;
  description?: string;
  objective_id?: string;
  status: "PLANNED" | "ACTIVE" | "BLOCKED" | "COMPLETED" | "CANCELLED";
  priority: number;
  owner_person_id?: string;
  start_date?: string;
  target_date?: string;
  success_condition?: string;
  metadata_json?: string;
}

export type TaskStatus = "OPEN" | "IN_PROGRESS" | "BLOCKED" | "DONE" | "CANCELLED";
export type TaskPriority = "P0" | "P1" | "P2" | "P3";

export interface Task extends BaseEntity {
  title: string;
  description?: string;
  project_id?: string;
  decision_id?: string;
  assignee_person_id?: string;
  status: TaskStatus;
  priority: TaskPriority;
  due_date?: string;
  dependency_task_id?: string;
  success_condition?: string;
  metadata_json?: string;
  completed_at?: string;
  /** 1 = auto-execute without Founder approval (default); 0 = manual only. */
  auto_run?: boolean;
}

export type DecisionCategory =
  | "STRATEGIC"
  | "FINANCIAL"
  | "TECHNICAL"
  | "OPERATIONAL"
  | "MARKETING";

export type DecisionStatus =
  | "DRAFT"
  | "APPROVED"
  | "EXECUTED"
  | "ESCALATED"
  | "REJECTED"
  | "COMPLETED";

export interface DecisionOption {
  id: string;
  label: string;
  description?: string;
  cost?: number;
  benefit?: string;
  risk?: string;
  reversible: boolean;
}

export interface Decision extends BaseEntity {
  title: string;
  problem: string;
  objective_id?: string;
  status: DecisionStatus;
  category: DecisionCategory;
  confidence?: number; // 0..1
  known_facts_json?: string;
  unknowns_json?: string;
  constraints_json?: string;
  options_json?: string; // DecisionOption[]
  ceo_assessment?: string;
  cfo_assessment?: string;
  cto_assessment?: string;
  risk_assessment?: string;
  risks_json?: string; // Risk[]
  recommended_action?: string;
  expected_outcome?: string;
  authority_required?: string;
  approval_required: boolean;
  execution_plan_json?: string; // string[] steps
  success_metric?: string;
  review_date?: string;
  outcome_status?: "PENDING" | "SUCCESS" | "FAILURE" | "PARTIAL";
  outcome_actual?: string;
  outcome_variance?: string;
  lesson?: string;
  model_trace_json?: string;
}

export type ApprovalStatus = "PENDING" | "APPROVED" | "REJECTED" | "MODIFIED" | "CANCELLED";

export interface ApprovalRequest extends BaseEntity {
  decision_id?: string;
  action_label: string;
  reason: string;
  expected_upside?: string;
  max_downside?: string;
  budget_available?: string;
  recommendation: "APPROVE" | "REJECT" | "MODIFY";
  status: ApprovalStatus;
  requested_by: string;
  decided_by?: string;
  decision_note?: string;
  decided_at?: string;
}

export type RiskLevel = "LOW" | "MEDIUM" | "HIGH" | "CRITICAL";

export interface Risk extends BaseEntity {
  title: string;
  description?: string;
  level: RiskLevel;
  category: string;
  likelihood?: string;
  impact?: string;
  mitigation?: string;
  status: "OPEN" | "WATCH" | "MITIGATED" | "CLOSED";
  owner_person_id?: string;
}

export interface Metric extends BaseEntity {
  name: string;
  value: number;
  unit?: string;
  period?: string;
  confidence: Confidence;
  recorded_at: string;
}

export interface FinancialSnapshot extends BaseEntity {
  period: string; // e.g. "2026-08"
  cash?: number;
  revenue?: number;
  expenses?: number;
  runway_months?: number;
  gross_margin?: number;
  confidence: Confidence;
  notes?: string;
}

export type ProductStatus =
  | "CONCEPT"
  | "PLANNED"
  | "IN_DEVELOPMENT"
  | "BETA"
  | "SHIPPED"
  | "MAINTENANCE"
  | "SUNSET";

export interface Product extends BaseEntity {
  name: string;
  category?: string;
  status: ProductStatus;
  version?: string;
  description?: string;
  priority: number;
  price?: number;
  metadata_json?: string; // flexible metadata (spec §31: JSON only for flexible metadata)
}

export interface ProductStatusRecord extends BaseEntity {
  product_id: string;
  status: string;
  gate?: string; // pipeline gate (absorbs /orchestrator pipeline)
  note?: string;
  recorded_at: string;
}

export interface EngineeringIssue extends BaseEntity {
  title: string;
  description?: string;
  product_id?: string;
  severity: "LOW" | "MEDIUM" | "HIGH" | "CRITICAL";
  status: "OPEN" | "IN_PROGRESS" | "RESOLVED" | "WONT_FIX";
  assignee_person_id?: string;
}

export interface MarketingCampaign extends BaseEntity {
  name: string;
  status: "PLANNED" | "RUNNING" | "PAUSED" | "COMPLETED" | "CANCELLED";
  channel?: string;
  budget?: number;
  spent?: number;
  objective?: string;
  metrics_json?: string;
}

export interface Person extends BaseEntity {
  name: string;
  role?: string;
  email?: string;
}

export interface Delegation extends BaseEntity {
  delegate_person_id: string;
  role: string;
  scope: string;
  status: "ACTIVE" | "REVOKED";
}

export type ToolVerdict = "ALLOW" | "APPROVAL" | "DENY";
export type ToolRunStatus = "PENDING" | "RUNNING" | "SUCCESS" | "FAILED" | "BLOCKED" | "CANCELLED";

export interface ToolExecution extends BaseEntity {
  tool_name: string;
  input_json: string;
  output_json?: string;
  permission: string;
  verdict: ToolVerdict;
  status: ToolRunStatus;
  cost_estimate?: number;
  reversible: boolean;
  run_id?: string;
  error?: string;
  started_at?: string;
  finished_at?: string;
}

export interface AgentRun extends BaseEntity {
  session_id?: string;
  model?: string;
  model_version?: string;
  prompt_version?: string;
  tools_called_json?: string;
  tokens_json?: string;
  latency_ms?: number;
  cost?: number;
  decision_id?: string;
  approval_id?: string;
  error?: string;
  outcome?: string;
}

export type MemoryLayer = "L0" | "L1" | "L2" | "L3";
export type MemoryKind = "FACT" | "INFERENCE" | "ASSUMPTION" | "ESTIMATE" | "UNKNOWN" | "LESSON" | "POLICY";

export interface Memory extends BaseEntity {
  layer: MemoryLayer;
  kind: MemoryKind;
  title: string;
  body: string;
  tags_json?: string;
  entity_id?: string;
  importance: number; // 0..1
  confidence?: number;
  status: "ACTIVE" | "ARCHIVED";
  expires_at?: string;
}

export interface DoctrineRule extends BaseEntity {
  code: string; // unique, e.g. "D-001"
  principle: string;
  rationale?: string;
  category: string;
  status: "ACTIVE" | "PROPOSED" | "REJECTED" | "SUPERSEDED";
  version: number;
  proposed_by?: string;
  approved_by?: string;
}

export interface Policy extends BaseEntity {
  code: string;
  title: string;
  body: string;
  category: string;
  status: "ACTIVE" | "DRAFT" | "RETIRED";
  version: number;
}

export interface AuditEvent extends BaseEntity {
  actor: string; // founder | executive | system | ceo_lens | ...
  action: string;
  entity_type?: string;
  entity_id?: string;
  detail_json?: string;
  verdict?: string;
  ip?: string;
}

export interface ChatMessage extends BaseEntity {
  session_id: string;
  role: "founder" | "executive";
  content: string;
  model?: string;
}

export interface ModelCost extends BaseEntity {
  provider: string;
  model: string;
  task_type: string;
  input_tokens: number;
  output_tokens: number;
  cost: number;
  session_id?: string;
}

export interface ExecutiveStateRow {
  id: string;
  overall_status: "GREEN" | "YELLOW" | "RED";
  current_objective_id?: string;
  top_priorities_json: string;
  paused: number;
  pause_reason?: string;
  autonomy_mode: "CONTROLLED" | "APPROVAL_ONLY" | "PAUSED";
  spending_disabled: number;
  deployments_disabled: number;
  delegate_mode: number;
  full_access: number;
  updated_at: string;
}

export type AgentRunStatus = "RUNNING" | "DONE" | "FAILED" | "STOPPED" | "TIMEOUT";

export interface AgentRun extends BaseEntity {
  task_id?: string;
  prompt: string;
  status: AgentRunStatus;
  iterations: number;
  tools_called: number;
  started_at: string;
  finished_at?: string;
  result?: string;
  error?: string;
  /** Durable tool-loop transcript snapshot (JSON) — lets a long-horizon run
   *  resume from where it stopped after a DO restart. Migration v12. */
  checkpoint_json?: string;
  /** Liveness — updated each iteration; stale RUNNING jobs get reaped. */
  last_heartbeat?: string;
}

export type LocalTaskStatus = "PENDING" | "CLAIMED" | "RUNNING" | "DONE" | "FAILED";

/**
 * A task queued for the LOCAL machine (migration v10). The live Executive or
 * the Founder (web console) queues it; the local daemon claims it, works with
 * local tools (repo files, plugin UI source, commands), and posts the result
 * back to the live worker.
 */
export interface LocalTask extends BaseEntity {
  task_id?: string;
  prompt: string;
  status: LocalTaskStatus;
  claimed_by?: string;
  started_at?: string;
  finished_at?: string;
  result?: string;
  error?: string;
  /** Claim lease expiry (ISO) — an offline machine's lease lapses and the task
   *  becomes re-claimable by another daemon. Migration v12. */
  lease_expires_at?: string;
  /** Structured brief (JSON): goal / context / acceptance criteria. */
  meta_json?: string;
}

/**
 * A file uploaded by the Founder from the web console (migration v11). The
 * Executive can inspect it: images via vision, text directly, anything else
 * by metadata. `data_base64` holds the file content (base64) bounded to a few MB.
 */
export interface UploadedFile extends BaseEntity {
  name: string;
  mime?: string;
  size_bytes: number;
  data_base64?: string;
  kind: "image" | "text" | "audio" | "other";
}

// ---------------------------------------------------------------------------
// Day-to-day planner entities (migration v14): calendar, notes, reminders,
// contacts — the "CEO day" records the agent works with.
// ---------------------------------------------------------------------------

export type CalendarEventStatus = "SCHEDULED" | "CANCELLED" | "DONE";

export interface CalendarEvent extends BaseEntity {
  title: string;
  description?: string;
  start_at: string; // ISO-8601
  end_at?: string; // ISO-8601
  all_day: boolean;
  location?: string;
  attendees_json?: string;
  status: CalendarEventStatus;
}

export interface Note extends BaseEntity {
  title: string;
  body?: string;
  tags_json?: string;
  pinned: boolean;
}

export interface Reminder extends BaseEntity {
  title: string;
  due_at: string; // ISO-8601
  done: boolean;
  done_at?: string;
}

export interface Contact extends BaseEntity {
  name: string;
  role?: string;
  company?: string;
  email?: string;
  phone?: string;
  tags_json?: string;
  notes?: string;
}

export interface Workstream extends BaseEntity {
  name: string;
  description?: string;
  kind: string;
  status: "ACTIVE" | "PAUSED" | "COMPLETED";
  config_json?: string;
}

export interface PipelineGate extends BaseEntity {
  workstream_id: string;
  plugin_id: string;
  gate_number: number;
  gate_name: string;
  status: "NOT_STARTED" | "IN_PROGRESS" | "PASSED" | "FAILED" | "BLOCKED" | "SHIPPED";
  owner_role: string;
  fail_count: number;
  note?: string;
}

// ---------------------------------------------------------------------------
// Agent-level state (persisted via Agent.setState, spec §5 L0)
// ---------------------------------------------------------------------------

export interface ExecutiveAgentState {
  initialized: boolean;
  overallStatus: "GREEN" | "YELLOW" | "RED";
  currentObjectiveId?: string;
  topPriorities: string[];
  paused: boolean;
  pauseReason?: string;
  autonomyMode: "CONTROLLED" | "APPROVAL_ONLY" | "PAUSED";
  spendingDisabled: boolean;
  deploymentsDisabled: boolean;
  delegateMode: boolean;
  fullAccess: boolean;
  lastDailyBriefAt?: string;
  lastWeeklyReviewAt?: string;
  lastActivity?: string;
}
