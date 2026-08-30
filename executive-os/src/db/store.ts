/**
 * ExecutiveStore — typed data access over the Sql abstraction.
 *
 * This is the durable brain of the Executive OS (spec §4). It runs unchanged
 * on:
 *   - the Durable Object's embedded SQLite (production),
 *   - node:sqlite (tests / local tooling).
 *
 * Every important record carries provenance. JSON is used only for flexible
 * metadata columns, never as the primary store (spec §31).
 */

import { migrate } from "./migrations";
import { PRODUCT_METRICS_SQL } from "./schema";
import { newId } from "../ids";
import type { Sql, SqlParam } from "./sql";
import type {
  AgentRun,
  ApprovalRequest,
  AuditEvent,
  CalendarEvent,
  ChatMessage,
  Contact,
  Contradiction,
  ContradictionStatus,
  Decision,
  DoctrineRule,
  DoctorLog,
  EngineeringIssue,
  ExecutiveStateRow,
  FinancialSnapshot,
  LocalTask,
  MarketingCampaign,
  Memory,
  Metric,
  Note,
  Objective,
  OptimizationEvent,
  Organization,
  Person,
  PipelineGate,
  Policy,
  Product,
  ProductivityAssignment,
  ProductivityMetric,
  ProductStatusRecord,
  Project,
  Reminder,
  Risk,
  RoleSkill,
  Task,
  TaskSkill,
  TokenBudget,
  ToolExecution,
  UploadedFile,
  Workstream,
} from "../types";
import { retrieve, scoreMemory } from "../core/memory/retrieval";
import type { ModelCallRecord } from "../core/model/types";
import type { CostStorage } from "../core/model/cost";

type Row = Record<string, unknown>;

export class ExecutiveStore implements CostStorage {
  constructor(private readonly sql: Sql) {}

  // -- migrations ---------------------------------------------------------
  migrate(): number[] {
    return migrate(this.sql).map((m) => m.version);
  }

  // -- generic helpers ----------------------------------------------------
  private insert(table: string, row: object): void {
    const rec = row as Record<string, unknown>;
    const keys = Object.keys(rec);
    const cols = keys.join(", ");
    const placeholders = keys.map(() => "?").join(", ");
    this.sql.run(
      `INSERT INTO ${table} (${cols}) VALUES (${placeholders})`,
      ...keys.map((k) => normalize(rec[k])),
    );
  }

  private update(table: string, id: string, patch: object): void {
    const rec = patch as Record<string, unknown>;
    const keys = Object.keys(rec);
    if (keys.length === 0) return;
    const sets = keys.map((k) => `${k} = ?`).join(", ");
    this.sql.run(`UPDATE ${table} SET ${sets}, updated_at = ? WHERE id = ?`, ...keys.map((k) => normalize(rec[k])), new Date().toISOString(), id);
  }

  // -- executive state ----------------------------------------------------
  getExecutiveState(): ExecutiveStateRow | null {
    const rows = this.sql.all<Row>("SELECT * FROM executive_state WHERE id = ?", "zarlino");
    if (rows.length === 0) return null;
    return rowTo<ExecutiveStateRow>(rows[0]!);
  }

  ensureExecutiveState(): ExecutiveStateRow {
    const existing = this.getExecutiveState();
    if (existing) return existing;
    const now = new Date().toISOString();
    const state: ExecutiveStateRow = {
      id: "zarlino",
      overall_status: "GREEN",
      current_objective_id: undefined,
      top_priorities_json: "[]",
      paused: 0,
      autonomy_mode: "CONTROLLED",
      spending_disabled: 0,
      deployments_disabled: 0,
      delegate_mode: 0,
      full_access: 0,
      updated_at: now,
    };
    this.insert("executive_state", state);
    return state;
  }

  updateExecutiveState(patch: Partial<Omit<ExecutiveStateRow, "id">>): void {
    const keys = Object.keys(patch);
    if (keys.length === 0) return;
    const sets = keys.map((k) => `${k} = ?`).join(", ");
    this.sql.run(`UPDATE executive_state SET ${sets}, updated_at = ? WHERE id = ?`, ...keys.map((k) => normalize(patch[k as keyof typeof patch])), new Date().toISOString(), "zarlino");
  }

  // -- founder profile / organization -------------------------------------
  upsertFounderProfile(profile: { id: string; name: string; email?: string; role: string }): void {
    const now = new Date().toISOString();
    const existing = this.sql.all<Row>("SELECT id FROM founder_profile WHERE id = ?", profile.id);
    if (existing.length > 0) {
      this.sql.run("UPDATE founder_profile SET name = ?, email = ?, role = ?, updated_at = ? WHERE id = ?", profile.name, profile.email ?? null, profile.role, now, profile.id);
    } else {
      this.insert("founder_profile", { id: profile.id, name: profile.name, email: profile.email ?? null, role: profile.role, authority_level: "FOUNDER", created_at: now, updated_at: now });
    }
  }

  insertOrganization(org: Organization): void {
    this.insert("organizations", org);
  }

  getOrganization(id: string): Organization | null {
    const rows = this.sql.all<Row>("SELECT * FROM organizations WHERE id = ?", id);
    return rows.length ? rowTo<Organization>(rows[0]) : null;
  }

  // -- objectives ---------------------------------------------------------
  insertObjective(o: Objective): void {
    this.insert("objectives", o);
  }
  listObjectives(status?: string): Objective[] {
    const rows = status
      ? this.sql.all<Row>("SELECT * FROM objectives WHERE status = ? ORDER BY priority ASC, created_at ASC", status)
      : this.sql.all<Row>("SELECT * FROM objectives ORDER BY priority ASC, created_at ASC");
    return rows.map((r) => rowTo<Objective>(r));
  }
  getObjective(id: string): Objective | null {
    const rows = this.sql.all<Row>("SELECT * FROM objectives WHERE id = ?", id);
    return rows.length ? rowTo<Objective>(rows[0]) : null;
  }

  // -- projects -----------------------------------------------------------
  insertProject(p: Project): void {
    this.insert("projects", p);
  }
  listProjects(): Project[] {
    return this.sql.all<Row>("SELECT * FROM projects ORDER BY priority ASC, created_at ASC").map((r) => rowTo<Project>(r));
  }
  getProject(id: string): Project | null {
    const rows = this.sql.all<Row>("SELECT * FROM projects WHERE id = ?", id);
    return rows.length ? rowTo<Project>(rows[0]) : null;
  }
  updateProject(id: string, patch: Partial<Project>): void {
    this.update("projects", id, patch as Row);
  }

  // -- tasks --------------------------------------------------------------
  insertTask(t: Task): void {
    this.insert("tasks", t);
  }
  listTasks(opts?: { status?: string; projectId?: string; decisionId?: string; limit?: number }): Task[] {
    let sql = "SELECT * FROM tasks WHERE 1=1";
    const params: SqlParam[] = [];
    if (opts?.status) { sql += " AND status = ?"; params.push(opts.status); }
    if (opts?.projectId) { sql += " AND project_id = ?"; params.push(opts.projectId); }
    if (opts?.decisionId) { sql += " AND decision_id = ?"; params.push(opts.decisionId); }
    sql += " ORDER BY created_at DESC";
    if (opts?.limit) { sql += " LIMIT ?"; params.push(opts.limit); }
    return this.sql.all<Row>(sql, ...params).map((r) => rowTo<Task>(r));
  }
  /**
   * Tasks eligible for AUTONOMOUS execution (auto-run): auto_run=1, not done,
   * ordered by priority then oldest-first. The Executive sweep runs these
   * without any Founder click (skipped only when a decision needs approval).
   */
  listAutoRunnableTasks(limit = 20): Task[] {
    const rows = this.sql.all<Row>(
      "SELECT * FROM tasks WHERE auto_run = 1 AND status IN ('OPEN','IN_PROGRESS') " +
        "ORDER BY CASE priority WHEN 'P0' THEN 0 WHEN 'P1' THEN 1 WHEN 'P2' THEN 2 ELSE 3 END, created_at ASC LIMIT ?",
      limit,
    );
    return rows.map((r) => rowTo<Task>(r));
  }
  /** True if an agent run is currently executing for this task. */
  hasRunningRunForTask(taskId: string): boolean {
    const rows = this.sql.all<Row>("SELECT id FROM agent_jobs WHERE task_id = ? AND status = 'RUNNING' LIMIT 1", taskId);
    return rows.length > 0;
  }
  /** Number of agent runs currently executing (auto-run concurrency guard). */
  countRunningAgentRuns(): number {
    const rows = this.sql.all<Row>("SELECT COUNT(*) AS c FROM agent_jobs WHERE status = 'RUNNING'");
    return Number(rows[0]?.c ?? 0);
  }
  /** True if the task's decision has a PENDING approval (needs the Founder). */
  pendingApprovalForTask(taskId: string): boolean {
    const task = this.getTask(taskId);
    if (!task?.decision_id) return false;
    const rows = this.sql.all<Row>(
      "SELECT id FROM approval_requests WHERE decision_id = ? AND status = 'PENDING' LIMIT 1",
      task.decision_id,
    );
    return rows.length > 0;
  }
  getTask(id: string): Task | null {
    const rows = this.sql.all<Row>("SELECT * FROM tasks WHERE id = ?", id);
    return rows.length ? rowTo<Task>(rows[0]) : null;
  }
  updateTask(id: string, patch: Partial<Task>): void {
    this.update("tasks", id, patch as Row);
  }
  countOpenTasks(): number {
    const rows = this.sql.all<Row>("SELECT COUNT(*) AS c FROM tasks WHERE status IN ('OPEN','IN_PROGRESS','BLOCKED')");
    return Number(rows[0]?.c ?? 0);
  }

  // -- decisions ----------------------------------------------------------
  insertDecision(d: Decision): void {
    this.insert("decisions", d);
  }
  listDecisions(opts?: { status?: string; category?: string; limit?: number }): Decision[] {
    let sql = "SELECT * FROM decisions WHERE 1=1";
    const params: SqlParam[] = [];
    if (opts?.status) { sql += " AND status = ?"; params.push(opts.status); }
    if (opts?.category) { sql += " AND category = ?"; params.push(opts.category); }
    sql += " ORDER BY created_at DESC";
    if (opts?.limit) sql += " LIMIT ?";
    if (opts?.limit) params.push(opts.limit);
    return this.sql.all<Row>(sql, ...params).map((r) => rowTo<Decision>(r));
  }
  getDecision(id: string): Decision | null {
    const rows = this.sql.all<Row>("SELECT * FROM decisions WHERE id = ?", id);
    return rows.length ? rowTo<Decision>(rows[0]) : null;
  }
  updateDecision(id: string, patch: Partial<Decision>): void {
    this.update("decisions", id, patch as Row);
  }

  // -- approvals ----------------------------------------------------------
  insertApproval(a: ApprovalRequest): void {
    this.insert("approval_requests", a);
  }
  listApprovals(opts?: { status?: string; limit?: number }): ApprovalRequest[] {
    const where = opts?.status ? "WHERE status = ?" : "";
    const params: SqlParam[] = opts?.status ? [opts.status] : [];
    let sql = `SELECT * FROM approval_requests ${where} ORDER BY created_at DESC`;
    if (opts?.limit) { sql += " LIMIT ?"; params.push(opts.limit); }
    return this.sql.all<Row>(sql, ...params).map((r) => rowTo<ApprovalRequest>(r));
  }
  getApproval(id: string): ApprovalRequest | null {
    const rows = this.sql.all<Row>("SELECT * FROM approval_requests WHERE id = ?", id);
    return rows.length ? rowTo<ApprovalRequest>(rows[0]) : null;
  }
  updateApproval(id: string, patch: Partial<ApprovalRequest>): void {
    this.update("approval_requests", id, patch as Row);
  }

  // -- risks --------------------------------------------------------------
  insertRisk(r: Risk): void {
    this.insert("risks", r);
  }
  listRisks(opts?: { status?: string; level?: string }): Risk[] {
    let sql = "SELECT * FROM risks WHERE 1=1";
    const params: SqlParam[] = [];
    if (opts?.status) { sql += " AND status = ?"; params.push(opts.status); }
    if (opts?.level) { sql += " AND level = ?"; params.push(opts.level); }
    sql += " ORDER BY CASE level WHEN 'CRITICAL' THEN 0 WHEN 'HIGH' THEN 1 WHEN 'MEDIUM' THEN 2 ELSE 3 END, created_at DESC";
    return this.sql.all<Row>(sql, ...params).map((r) => rowTo<Risk>(r));
  }
  openRisks(): Risk[] {
    return this.listRisks({ status: "OPEN" });
  }

  // -- metrics ------------------------------------------------------------
  insertMetric(m: Metric): void {
    this.insert("metrics", m);
  }
  listMetrics(limit = 50): Metric[] {
    return this.sql.all<Row>("SELECT * FROM metrics ORDER BY recorded_at DESC LIMIT ?", limit).map((r) => rowTo<Metric>(r));
  }
  /** Latest recorded metric with the given name (e.g. downloads, licensed). */
  latestMetricByName(name: string): Metric | null {
    const rows = this.sql.all<Row>("SELECT * FROM metrics WHERE name = ? ORDER BY recorded_at DESC LIMIT 1", name);
    return rows.length ? rowTo<Metric>(rows[0]) : null;
  }
  /** Insert a metric, or update in place if the id already exists. */
  upsertMetric(m: Metric): void {
    const rows = this.sql.all<Row>("SELECT id FROM metrics WHERE id = ?", m.id);
    if (rows.length > 0) this.update("metrics", m.id, m as unknown as Row);
    else this.insert("metrics", m);
  }

  // -- financial snapshots ------------------------------------------------
  insertSnapshot(s: FinancialSnapshot): void {
    this.insert("financial_snapshots", s);
  }
  latestSnapshot(): FinancialSnapshot | null {
    const rows = this.sql.all<Row>("SELECT * FROM financial_snapshots ORDER BY period DESC LIMIT 1");
    return rows.length ? rowTo<FinancialSnapshot>(rows[0]) : null;
  }
  listSnapshots(limit = 12): FinancialSnapshot[] {
    return this.sql.all<Row>("SELECT * FROM financial_snapshots ORDER BY period DESC LIMIT ?", limit).map((r) => rowTo<FinancialSnapshot>(r));
  }

  // -- products -----------------------------------------------------------
  insertProduct(p: Product): void {
    this.insert("products", p);
  }
  listProducts(): Product[] {
    return this.sql.all<Row>("SELECT * FROM products ORDER BY priority ASC, created_at ASC").map((r) => rowTo<Product>(r));
  }
  getProduct(id: string): Product | null {
    const rows = this.sql.all<Row>("SELECT * FROM products WHERE id = ?", id);
    return rows.length ? rowTo<Product>(rows[0]) : null;
  }
  updateProduct(id: string, patch: Partial<Product>): void {
    this.update("products", id, patch as Row);
  }
  insertProductStatus(ps: ProductStatusRecord): void {
    this.insert("product_status", ps);
  }
  listProductStatus(productId: string): ProductStatusRecord[] {
    return this.sql.all<Row>("SELECT * FROM product_status WHERE product_id = ? ORDER BY recorded_at DESC", productId).map((r) => rowTo<ProductStatusRecord>(r));
  }

  /**
   * Ship a plugin: mark the product SHIPPED (with optional version), record a
   * product_status history row, and set its Gate 10 to SHIPPED. This is the
   * "Ship" action — the single source of truth that a plugin is released.
   */
  shipProduct(pluginId: string, opts: { version?: string; note?: string }): void {
    const now = new Date().toISOString();
    this.updateProduct(pluginId, {
      status: "SHIPPED",
      ...(opts.version ? { version: opts.version } : {}),
    });
    const product = this.getProduct(pluginId);
    this.insertProductStatus({
      id: newId("ps"),
      created_at: now,
      source: "founder",
      product_id: pluginId,
      status: "SHIPPED",
      gate: "Gate 10",
      note: opts.note ?? `Shipped${opts.version ? ` v${opts.version}` : ""} (${product?.name ?? pluginId})`,
      recorded_at: now,
    });
    // pipeline_gates uses the product NAME as plugin_id (e.g. "ZScorch"),
    // not the product id ("prod_zscorch").
    this.sql.run(
      "UPDATE pipeline_gates SET status = 'SHIPPED', note = ?, updated_at = ? WHERE plugin_id = ? AND gate_number = 10",
      opts.note ?? `Shipped${opts.version ? ` v${opts.version}` : ""}`, now, product?.name ?? pluginId,
    );
  }

  // -- engineering issues -------------------------------------------------
  insertEngineeringIssue(e: EngineeringIssue): void {
    this.insert("engineering_issues", e);
  }
  listEngineeringIssues(opts?: { status?: string }): EngineeringIssue[] {
    const rows = opts?.status
      ? this.sql.all<Row>("SELECT * FROM engineering_issues WHERE status = ? ORDER BY created_at DESC", opts.status)
      : this.sql.all<Row>("SELECT * FROM engineering_issues ORDER BY created_at DESC");
    return rows.map((r) => rowTo<EngineeringIssue>(r));
  }

  // -- marketing ----------------------------------------------------------
  insertCampaign(c: MarketingCampaign): void {
    this.insert("marketing_campaigns", c);
  }
  listCampaigns(): MarketingCampaign[] {
    return this.sql.all<Row>("SELECT * FROM marketing_campaigns ORDER BY created_at DESC").map((r) => rowTo<MarketingCampaign>(r));
  }

  // -- persons ------------------------------------------------------------
  insertPerson(p: Person): void {
    this.insert("persons", p);
  }
  listPersons(): Person[] {
    return this.sql.all<Row>("SELECT * FROM persons ORDER BY created_at ASC").map((r) => rowTo<Person>(r));
  }

  // -- memory -------------------------------------------------------------
  insertMemory(m: Memory): void {
    this.insert("memories", m);
  }
  listMemories(opts?: { layer?: string; status?: string }): Memory[] {
    let sql = "SELECT * FROM memories WHERE 1=1";
    const params: SqlParam[] = [];
    if (opts?.layer) { sql += " AND layer = ?"; params.push(opts.layer); }
    if (opts?.status) { sql += " AND status = ?"; params.push(opts.status); }
    sql += " ORDER BY created_at DESC";
    return this.sql.all<Row>(sql, ...params).map((r) => rowTo<Memory>(r));
  }
  getMemory(id: string): Memory | null {
    const rows = this.sql.all<Row>("SELECT * FROM memories WHERE id = ?", id);
    return rows.length ? rowTo<Memory>(rows[0]) : null;
  }
  updateMemory(id: string, patch: Partial<Memory>): void {
    this.update("memories", id, patch as Row);
  }
  /** Semantic + recency + importance retrieval (spec §27). */
  retrieveMemories(query: string, opts: { limit?: number; layers?: Memory["layer"][] } = {}): Memory[] {
    const memories = this.listMemories({ status: "ACTIVE" });
    return retrieve(memories, { query, limit: opts.limit ?? 8, layers: opts.layers });
  }
  scoreMemories(query: string): Array<{ memory: Memory; score: number }> {
    return this.listMemories({ status: "ACTIVE" }).map((m) => ({ memory: m, score: scoreMemory(m, query) })).sort((a, b) => b.score - a.score);
  }

  // -- doctrine -----------------------------------------------------------
  insertDoctrineRule(r: DoctrineRule): void {
    // Do not duplicate an ACTIVE rule with the same code.
    const existing = this.sql.all<Row>("SELECT id FROM doctrine_rules WHERE code = ? AND status = 'ACTIVE'", r.code);
    if (r.status === "ACTIVE" && existing.length > 0) return;
    this.insert("doctrine_rules", r);
  }
  listDoctrine(opts?: { status?: string }): DoctrineRule[] {
    const rows = opts?.status
      ? this.sql.all<Row>("SELECT * FROM doctrine_rules WHERE status = ? ORDER BY code ASC", opts.status)
      : this.sql.all<Row>("SELECT * FROM doctrine_rules ORDER BY code ASC");
    return rows.map((r) => rowTo<DoctrineRule>(r));
  }
  listActiveDoctrine(): DoctrineRule[] {
    return this.listDoctrine({ status: "ACTIVE" });
  }
  listDoctrineByCode(code: string): DoctrineRule[] {
    return this.sql.all<Row>("SELECT * FROM doctrine_rules WHERE code = ? ORDER BY version ASC", code).map((r) => rowTo<DoctrineRule>(r));
  }
  updateDoctrineRule(id: string, patch: Partial<DoctrineRule>): void {
    this.update("doctrine_rules", id, patch as Row);
  }

  // -- policies -----------------------------------------------------------
  insertPolicy(p: Policy): void {
    this.insert("policies", p);
  }
  listPolicies(): Policy[] {
    return this.sql.all<Row>("SELECT * FROM policies ORDER BY code ASC").map((r) => rowTo<Policy>(r));
  }

  // -- audit --------------------------------------------------------------
  insertAudit(e: AuditEvent): void {
    this.insert("audit_events", e);
  }
  listAudit(limit = 100): AuditEvent[] {
    return this.sql.all<Row>("SELECT * FROM audit_events ORDER BY created_at DESC LIMIT ?", limit).map((r) => rowTo<AuditEvent>(r));
  }
  auditForEntity(entityType: string, entityId: string, limit = 20): AuditEvent[] {
    return this.sql.all<Row>("SELECT * FROM audit_events WHERE entity_type = ? AND entity_id = ? ORDER BY created_at DESC LIMIT ?", entityType, entityId, limit).map((r) => rowTo<AuditEvent>(r));
  }

  // -- chat ---------------------------------------------------------------
  insertChatMessage(m: ChatMessage): void {
    this.insert("chat_messages", m);
  }
  listChatMessages(sessionId: string, limit = 200): ChatMessage[] {
    return this.sql.all<Row>("SELECT * FROM chat_messages WHERE session_id = ? ORDER BY created_at ASC LIMIT ?", sessionId, limit).map((r) => rowTo<ChatMessage>(r));
  }

  /** Recent chat sessions (history), most recent first. */
  listChatSessions(limit = 20): Array<{ session_id: string; message_count: number; last_at: string; last_message: string }> {
    return this.sql
      .all<Row>(
        `SELECT session_id, COUNT(*) AS message_count, MAX(created_at) AS last_at,
           (SELECT content FROM chat_messages AS inner_m
             WHERE inner_m.session_id = outer_m.session_id
             ORDER BY inner_m.created_at DESC LIMIT 1) AS last_message
         FROM chat_messages AS outer_m
         GROUP BY session_id ORDER BY last_at DESC LIMIT ?`,
        limit,
      )
      .map((r) => ({
        session_id: String(r.session_id),
        message_count: Number(r.message_count),
        last_at: String(r.last_at),
        last_message: String(r.last_message ?? ""),
      }));
  }

  clearChat(sessionId: string): void {
    this.sql.run("DELETE FROM chat_messages WHERE session_id = ?", sessionId);
  }

  // -- chat streams (migration v6 — buffered SSE deltas) ------------------
  openChatStream(id: string, sessionId: string, createdAt: string): void {
    this.sql.run(
      "INSERT INTO chat_streams (id, session_id, chunks_json, complete, source, created_at, updated_at) VALUES (?, ?, '[]', 0, 'executive', ?, ?)",
      id, sessionId, createdAt, createdAt,
    );
  }
  appendChatStreamChunk(id: string, text: string): void {
    const rows = this.sql.all<Row>("SELECT chunks_json FROM chat_streams WHERE id = ?", id);
    const chunks = rows.length ? (JSON.parse(String(rows[0]!.chunks_json ?? "[]")) as string[]) : [];
    chunks.push(text);
    this.sql.run(
      "UPDATE chat_streams SET chunks_json = ?, updated_at = ? WHERE id = ?",
      JSON.stringify(chunks), new Date().toISOString(), id,
    );
  }
  completeChatStream(id: string, error?: string): void {
    this.sql.run(
      "UPDATE chat_streams SET complete = 1, error = ?, updated_at = ? WHERE id = ?",
      error ?? null, new Date().toISOString(), id,
    );
  }
  readChatStream(id: string, afterIndex: number): { chunks: string[]; nextIndex: number; complete: boolean; error?: string } {
    const rows = this.sql.all<Row>("SELECT * FROM chat_streams WHERE id = ?", id);
    if (!rows.length) return { chunks: [], nextIndex: afterIndex, complete: true, error: "Stream not found" };
    const all = JSON.parse(String(rows[0]!.chunks_json ?? "[]")) as string[];
    // afterIndex = number of chunks already consumed (-1 = none). We return
    // chunks from that count onward and report the total count as nextIndex so
    // the next poll resumes exactly after what was returned.
    const chunks = all.slice(Math.max(0, afterIndex));
    return {
      chunks,
      nextIndex: all.length,
      complete: rows[0]!.complete === 1,
      error: rows[0]!.error ? String(rows[0]!.error) : undefined,
    };
  }
  deleteChatStream(id: string): void {
    this.sql.run("DELETE FROM chat_streams WHERE id = ?", id);
  }

  // -- model costs (implements CostStorage) -------------------------------
  record(call: ModelCallRecord): void {
    const now = new Date().toISOString();
    this.insert("model_costs", {
      id: `mcr_${crypto.randomUUID()}`,
      provider: call.provider,
      model: call.model,
      task_type: call.taskType,
      input_tokens: call.inputTokens,
      output_tokens: call.outputTokens,
      cost: call.cost,
      session_id: call.sessionId ?? null,
      run_id: call.runId ?? null,
      latency_ms: call.latencyMs,
      source: "executive",
      created_at: now,
      updated_at: now,
    });
  }
  spentToday(): number {
    const day = new Date().toISOString().slice(0, 10);
    const rows = this.sql.all<Row>("SELECT COALESCE(SUM(cost),0) AS s FROM model_costs WHERE substr(created_at,1,10) = ?", day);
    return Number(rows[0]?.s ?? 0);
  }
  spentThisMonth(): number {
    const month = new Date().toISOString().slice(0, 7);
    const rows = this.sql.all<Row>("SELECT COALESCE(SUM(cost),0) AS s FROM model_costs WHERE substr(created_at,1,7) = ?", month);
    return Number(rows[0]?.s ?? 0);
  }
  /** Total tokens (input + output) consumed today (UTC). */
  tokensToday(): number {
    const day = new Date().toISOString().slice(0, 10);
    const rows = this.sql.all<Row>("SELECT COALESCE(SUM(input_tokens),0) + COALESCE(SUM(output_tokens),0) AS t FROM model_costs WHERE substr(created_at,1,10) = ?", day);
    return Number(rows[0]?.t ?? 0);
  }
  /** Total tokens (input + output) consumed this calendar month (UTC). */
  tokensThisMonth(): number {
    const month = new Date().toISOString().slice(0, 7);
    const rows = this.sql.all<Row>("SELECT COALESCE(SUM(input_tokens),0) + COALESCE(SUM(output_tokens),0) AS t FROM model_costs WHERE substr(created_at,1,7) = ?", month);
    return Number(rows[0]?.t ?? 0);
  }
  spentThisRun(runId?: string): number {
    if (!runId) return 0;
    const rows = this.sql.all<Row>("SELECT COALESCE(SUM(cost),0) AS s FROM model_costs WHERE run_id = ?", runId);
    return Number(rows[0]?.s ?? 0);
  }
  spentThisSession(sessionId?: string): number {
    if (!sessionId) return 0;
    const rows = this.sql.all<Row>("SELECT COALESCE(SUM(cost),0) AS s FROM model_costs WHERE session_id = ?", sessionId);
    return Number(rows[0]?.s ?? 0);
  }
  costHistory(limit = 100): ModelCallRecord[] {
    return this.sql.all<Row>("SELECT * FROM model_costs ORDER BY created_at DESC LIMIT ?", limit).map((r) => ({
      provider: String(r.provider),
      model: String(r.model),
      taskType: String(r.task_type) as ModelCallRecord["taskType"],
      inputTokens: Number(r.input_tokens),
      outputTokens: Number(r.output_tokens),
      cost: Number(r.cost),
      sessionId: r.session_id ? String(r.session_id) : undefined,
      runId: r.run_id ? String(r.run_id) : undefined,
      latencyMs: Number(r.latency_ms),
      timestamp: String(r.created_at),
    }));
  }

  // -- settings (migration v13) ------------------------------------------
  /**
   * Founder-editable key/value overrides (persisted across deploys). Used for
   * AI budget overrides and the cached provider balance.
   */
  getSetting(key: string): string | null {
    const rows = this.sql.all<Row>("SELECT value FROM settings WHERE key = ?", key);
    return rows[0]?.value ? String(rows[0].value) : null;
  }
  setSetting(key: string, value: string | null): void {
    const now = new Date().toISOString();
    if (value === null) {
      this.sql.run("DELETE FROM settings WHERE key = ?", key);
      return;
    }
    this.sql.run(
      `INSERT INTO settings (key, value, updated_at) VALUES (?, ?, ?)
       ON CONFLICT(key) DO UPDATE SET value = excluded.value, updated_at = excluded.updated_at`,
      key, value, now,
    );
  }
  getSettingUpdatedAt(key: string): string | null {
    const rows = this.sql.all<Row>("SELECT updated_at FROM settings WHERE key = ?", key);
    return rows[0]?.updated_at ? String(rows[0].updated_at) : null;
  }

  /** Persisted AI budget overrides (numbers or undefined = use env default). */
  getBudgetOverrides(): { aiDailyBudget?: number; aiMonthlyBudget?: number; aiPerTaskBudget?: number; aiPerRunBudget?: number } {
    const num = (k: string): number | undefined => {
      const v = this.getSetting(k);
      if (v === null || v === "") return undefined;
      const n = Number(v);
      return Number.isFinite(n) && n > 0 ? n : undefined;
    };
    return {
      aiDailyBudget: num("ai_daily_budget"),
      aiMonthlyBudget: num("ai_monthly_budget"),
      aiPerTaskBudget: num("ai_per_task_budget"),
      aiPerRunBudget: num("ai_per_run_budget"),
    };
  }
  /** Persist AI budget overrides. Pass undefined/omit to keep the env default. */
  setBudgetOverrides(o: { aiDailyBudget?: number; aiMonthlyBudget?: number; aiPerTaskBudget?: number; aiPerRunBudget?: number }): void {
    const set = (k: string, v: number | undefined) => {
      if (v !== undefined && Number.isFinite(v) && v > 0) this.setSetting(k, String(v));
    };
    set("ai_daily_budget", o.aiDailyBudget);
    set("ai_monthly_budget", o.aiMonthlyBudget);
    set("ai_per_task_budget", o.aiPerTaskBudget);
    set("ai_per_run_budget", o.aiPerRunBudget);
  }

  /** Cached AI-provider balance (JSON string + when it was fetched). */
  getProviderBalanceCache(): { json: string | null; updatedAt: string | null } {
    return { json: this.getSetting("provider_balance_json"), updatedAt: this.getSettingUpdatedAt("provider_balance_json") };
  }
  setProviderBalanceCache(json: string | null): void {
    this.setSetting("provider_balance_json", json);
  }

  // -- calendar (migration v14) ------------------------------------------
  listCalendarEvents(fromIso?: string, toIso?: string, limit = 100): CalendarEvent[] {
    if (fromIso && toIso) {
      return this.sql
        .all<Row>("SELECT * FROM calendar_events WHERE start_at >= ? AND start_at <= ? ORDER BY start_at ASC LIMIT ?", fromIso, toIso, limit)
        .map((r) => rowTo<CalendarEvent>(r));
    }
    return this.sql.all<Row>("SELECT * FROM calendar_events ORDER BY start_at ASC LIMIT ?", limit).map((r) => rowTo<CalendarEvent>(r));
  }
  getCalendarEvent(id: string): CalendarEvent | null {
    const rows = this.sql.all<Row>("SELECT * FROM calendar_events WHERE id = ?", id);
    return rows[0] ? rowTo<CalendarEvent>(rows[0]) : null;
  }
  createCalendarEvent(e: CalendarEvent): void {
    this.insert("calendar_events", e);
  }
  updateCalendarEvent(id: string, patch: Partial<CalendarEvent>): CalendarEvent | null {
    const existing = this.getCalendarEvent(id);
    if (!existing) return null;
    const next = { ...existing, ...patch, id, updated_at: new Date().toISOString() };
    this.update("calendar_events", id, next);
    return this.getCalendarEvent(id);
  }
  deleteCalendarEvent(id: string): boolean {
    const existing = this.getCalendarEvent(id);
    if (!existing) return false;
    this.sql.run("DELETE FROM calendar_events WHERE id = ?", id);
    return true;
  }

  // -- notes (migration v14) ---------------------------------------------
  listNotes(limit = 100): Note[] {
    return this.sql.all<Row>("SELECT * FROM notes ORDER BY pinned DESC, updated_at DESC LIMIT ?", limit).map((r) => rowTo<Note>(r));
  }
  getNote(id: string): Note | null {
    const rows = this.sql.all<Row>("SELECT * FROM notes WHERE id = ?", id);
    return rows[0] ? rowTo<Note>(rows[0]) : null;
  }
  createNote(n: Note): void {
    this.insert("notes", n);
  }
  updateNote(id: string, patch: Partial<Note>): Note | null {
    const existing = this.getNote(id);
    if (!existing) return null;
    const next = { ...existing, ...patch, id, updated_at: new Date().toISOString() };
    this.update("notes", id, next);
    return this.getNote(id);
  }
  deleteNote(id: string): boolean {
    const existing = this.getNote(id);
    if (!existing) return false;
    this.sql.run("DELETE FROM notes WHERE id = ?", id);
    return true;
  }

  // -- reminders (migration v14) -----------------------------------------
  /** Open reminders, soonest first. Optionally only overdue (due <= now). */
  listReminders(opts: { includeDone?: boolean; overdueOnly?: boolean; limit?: number } = {}): Reminder[] {
    const limit = opts.limit ?? 100;
    if (opts.overdueOnly) {
      return this.sql
        .all<Row>("SELECT * FROM reminders WHERE done = 0 AND due_at <= ? ORDER BY due_at ASC LIMIT ?", new Date().toISOString(), limit)
        .map((r) => rowTo<Reminder>(r));
    }
    if (opts.includeDone) {
      return this.sql.all<Row>("SELECT * FROM reminders ORDER BY due_at ASC LIMIT ?", limit).map((r) => rowTo<Reminder>(r));
    }
    return this.sql.all<Row>("SELECT * FROM reminders WHERE done = 0 ORDER BY due_at ASC LIMIT ?", limit).map((r) => rowTo<Reminder>(r));
  }
  getReminder(id: string): Reminder | null {
    const rows = this.sql.all<Row>("SELECT * FROM reminders WHERE id = ?", id);
    return rows[0] ? rowTo<Reminder>(rows[0]) : null;
  }
  createReminder(r: Reminder): void {
    this.insert("reminders", r);
  }
  markReminderDone(id: string): Reminder | null {
    const existing = this.getReminder(id);
    if (!existing) return null;
    return this.updateReminder(id, { done: 1, done_at: new Date().toISOString() });
  }
  updateReminder(id: string, patch: Partial<Reminder>): Reminder | null {
    const existing = this.getReminder(id);
    if (!existing) return null;
    const next = { ...existing, ...patch, id, updated_at: new Date().toISOString() };
    this.update("reminders", id, next);
    return this.getReminder(id);
  }
  deleteReminder(id: string): boolean {
    const existing = this.getReminder(id);
    if (!existing) return false;
    this.sql.run("DELETE FROM reminders WHERE id = ?", id);
    return true;
  }

  // -- contacts (migration v14) ------------------------------------------
  listContacts(limit = 200): Contact[] {
    return this.sql.all<Row>("SELECT * FROM contacts ORDER BY name ASC LIMIT ?", limit).map((r) => rowTo<Contact>(r));
  }
  getContact(id: string): Contact | null {
    const rows = this.sql.all<Row>("SELECT * FROM contacts WHERE id = ?", id);
    return rows[0] ? rowTo<Contact>(rows[0]) : null;
  }
  createContact(c: Contact): void {
    this.insert("contacts", c);
  }
  deleteContact(id: string): boolean {
    const existing = this.getContact(id);
    if (!existing) return false;
    this.sql.run("DELETE FROM contacts WHERE id = ?", id);
    return true;
  }

  // -- tool executions ----------------------------------------------------
  /**
   * Record (or update) a tool execution. The tool registry reuses the same
   * executionId across lifecycle records (RUNNING → SUCCESS/FAILED), so this
   * is an upsert keyed on id — a plain INSERT would violate the primary key.
   */
  recordToolExecution(t: ToolExecution): void {
    this.sql.run(
      `INSERT INTO tool_executions
         (id, tool_name, input_json, output_json, permission, verdict, status,
          cost_estimate, reversible, run_id, error, started_at, finished_at,
          source, created_at)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
       ON CONFLICT(id) DO UPDATE SET
         output_json = excluded.output_json,
         verdict = excluded.verdict,
         status = excluded.status,
         error = excluded.error,
         finished_at = excluded.finished_at`,
      t.id,
      t.tool_name,
      t.input_json,
      t.output_json ?? null,
      t.permission,
      t.verdict,
      t.status,
      t.cost_estimate ?? null,
      t.reversible ? 1 : 0,
      t.run_id ?? null,
      t.error ?? null,
      t.started_at ?? null,
      t.finished_at ?? null,
      t.source ?? "executive",
      t.created_at,
    );
  }
  listToolExecutions(limit = 50): ToolExecution[] {
    return this.sql.all<Row>("SELECT * FROM tool_executions ORDER BY created_at DESC LIMIT ?", limit).map((r) => rowTo<ToolExecution>(r));
  }

  // -- long-horizon agent runs (migration v9) ----------------------------
  createAgentRun(r: AgentRun): void {
    this.insert("agent_jobs", r);
  }
  heartbeatAgentRun(id: string, iterations: number, toolsCalled: number): void {
    const now = new Date().toISOString();
    this.sql.run(
      "UPDATE agent_jobs SET iterations = ?, tools_called = ?, last_heartbeat = ?, updated_at = ? WHERE id = ?",
      iterations, toolsCalled, now, now, id,
    );
  }
  /** Save a durable transcript snapshot so a run can resume after a restart. */
  checkpointAgentRun(id: string, checkpointJson: string): void {
    this.sql.run(
      "UPDATE agent_jobs SET checkpoint_json = ?, last_heartbeat = ?, updated_at = ? WHERE id = ?",
      checkpointJson, new Date().toISOString(), new Date().toISOString(), id,
    );
  }
  finishAgentRun(id: string, status: AgentRun["status"], result?: string, error?: string): void {
    this.sql.run(
      "UPDATE agent_jobs SET status = ?, result = ?, error = ?, finished_at = ?, updated_at = ? WHERE id = ?",
      status, result ?? null, error ?? null, new Date().toISOString(), new Date().toISOString(), id,
    );
  }
  getAgentRun(id: string): AgentRun | null {
    const rows = this.sql.all<Row>("SELECT * FROM agent_jobs WHERE id = ?", id);
    return rows.length ? rowTo<AgentRun>(rows[0]) : null;
  }
  /** Reopen a stopped/timed-out/failed run: back to RUNNING, history kept. */
  resetAgentRun(id: string): void {
    this.sql.run(
      "UPDATE agent_jobs SET status = 'RUNNING', result = NULL, error = NULL, finished_at = NULL, updated_at = ? WHERE id = ?",
      new Date().toISOString(), id,
    );
  }
  listAgentRuns(limit = 20): AgentRun[] {
    return this.sql.all<Row>("SELECT * FROM agent_jobs ORDER BY created_at DESC LIMIT ?", limit).map((r) => rowTo<AgentRun>(r));
  }
  /**
   * Reap long-horizon runs that were left RUNNING with no heartbeat for the
   * given stale window (e.g. the DO restarted mid-run). Marks them FAILED so
   * the Founder can Continue from the durable checkpoint. Returns the ids.
   */
  reapStaleAgentRuns(staleAfterMs = 15 * 60_000): string[] {
    const cutoff = new Date(Date.now() - staleAfterMs).toISOString();
    const rows = this.sql.all<Row>(
      "SELECT id FROM agent_jobs WHERE status = 'RUNNING' AND (last_heartbeat IS NULL OR last_heartbeat < ?)",
      cutoff,
    );
    const ids = rows.map((r) => String(r.id));
    for (const id of ids) {
      this.sql.run(
        "UPDATE agent_jobs SET status = 'FAILED', error = 'Interrupted by a restart — use Continue to resume from the checkpoint.', finished_at = ?, updated_at = ? WHERE id = ?",
        new Date().toISOString(), new Date().toISOString(), id,
      );
    }
    return ids;
  }

  // -- local machine tasks (migration v10) -------------------------------
  createLocalTask(r: LocalTask): void {
    this.insert("local_tasks", r);
  }
  /**
   * Atomically claim the oldest PENDING task for a local daemon, with a lease
   * (default 10 min, extended by heartbeats). If the daemon's machine goes
   * offline, the lease lapses and another daemon can re-claim the task.
   */
  claimLocalTask(claimer: string, leaseMs = 10 * 60_000): LocalTask | null {
    const rows = this.sql.all<Row>(
      "SELECT * FROM local_tasks WHERE status = 'PENDING' ORDER BY created_at ASC LIMIT 1",
    );
    const task = rows[0];
    if (!task) return null;
    const id = task.id as string;
    const now = new Date();
    this.sql.run(
      "UPDATE local_tasks SET status = 'CLAIMED', claimed_by = ?, started_at = ?, lease_expires_at = ?, updated_at = ? WHERE id = ? AND status = 'PENDING'",
      claimer, now.toISOString(), new Date(now.getTime() + leaseMs).toISOString(), now.toISOString(), id,
    );
    return this.getLocalTask(id);
  }
  heartbeatLocalTask(id: string, status: LocalTask["status"], leaseMs = 10 * 60_000): void {
    const now = new Date();
    this.sql.run(
      "UPDATE local_tasks SET status = ?, lease_expires_at = ?, updated_at = ? WHERE id = ?",
      status, new Date(now.getTime() + leaseMs).toISOString(), now.toISOString(), id,
    );
  }
  finishLocalTask(id: string, status: "DONE" | "FAILED", result?: string, error?: string): void {
    this.sql.run(
      "UPDATE local_tasks SET status = ?, result = ?, error = ?, finished_at = ?, lease_expires_at = NULL, updated_at = ? WHERE id = ?",
      status, result ?? null, error ?? null, new Date().toISOString(), new Date().toISOString(), id,
    );
  }
  getLocalTask(id: string): LocalTask | null {
    const rows = this.sql.all<Row>("SELECT * FROM local_tasks WHERE id = ?", id);
    return rows.length ? rowTo<LocalTask>(rows[0]) : null;
  }
  listLocalTasks(limit = 25): LocalTask[] {
    return this.sql.all<Row>("SELECT * FROM local_tasks ORDER BY created_at DESC LIMIT ?", limit).map((r) => rowTo<LocalTask>(r));
  }
  /**
   * Reap local tasks whose claim lease has expired (the machine went offline
   * mid-task). They return to PENDING so any daemon can re-claim them.
   * Returns the reaped ids.
   */
  reapExpiredLocalTasks(): string[] {
    const now = new Date().toISOString();
    const rows = this.sql.all<Row>(
      "SELECT id FROM local_tasks WHERE status IN ('CLAIMED','RUNNING') AND lease_expires_at IS NOT NULL AND lease_expires_at < ?",
      now,
    );
    const ids = rows.map((r) => String(r.id));
    for (const id of ids) {
      this.sql.run(
        "UPDATE local_tasks SET status = 'PENDING', claimed_by = NULL, started_at = NULL, lease_expires_at = NULL, error = 'Re-queued: the previous worker lease expired (machine likely offline).', updated_at = ? WHERE id = ?",
        now, id,
      );
    }
    return ids;
  }
  /**
   * Requeue completed/failed local tasks whose result or error mentions a
   * retryable condition (default: the tool-call limit). A task that "finished"
   * with the tool-call-limit message never actually completed — it returns to
   * PENDING so the daemon retries it with the extended limit. Tasks whose text
   * contains `exclude` (default "402" — insufficient balance, needs funding)
   * are skipped. Uses SQL LIKE so only matching rows are read (row-budget safe).
   * Returns the requeued ids.
   */
  requeueLocalTasks(opts: { match?: string; exclude?: string; limit?: number } = {}): string[] {
    const match = opts.match ?? "tool-call limit";
    const exclude = opts.exclude ?? "402";
    const limit = Math.max(1, Math.min(5000, opts.limit ?? 2000));
    const like = `%${match}%`;
    const rows = this.sql.all<Row>(
      "SELECT id, result, error FROM local_tasks WHERE status IN ('DONE','FAILED') AND (result LIKE ? OR error LIKE ?) ORDER BY updated_at ASC LIMIT ?",
      like, like, limit,
    );
    const now = new Date().toISOString();
    const ids: string[] = [];
    for (const r of rows) {
      const text = `${String(r.result ?? "")} ${String(r.error ?? "")}`;
      if (exclude && text.includes(exclude)) continue;
      this.sql.run(
        "UPDATE local_tasks SET status = 'PENDING', result = NULL, error = NULL, finished_at = NULL, claimed_by = NULL, started_at = NULL, lease_expires_at = NULL, updated_at = ? WHERE id = ?",
        now, String(r.id),
      );
      ids.push(String(r.id));
    }
    return ids;
  }

  // -- uploaded files (migration v11) -------------------------------------
  createUploadedFile(f: UploadedFile): void {
    this.insert("uploaded_files", f);
  }
  getUploadedFile(id: string): UploadedFile | null {
    const rows = this.sql.all<Row>("SELECT * FROM uploaded_files WHERE id = ?", id);
    return rows.length ? rowTo<UploadedFile>(rows[0]) : null;
  }
  listUploadedFiles(limit = 50): UploadedFile[] {
    return this.sql.all<Row>("SELECT * FROM uploaded_files ORDER BY created_at DESC LIMIT ?", limit).map((r) => rowTo<UploadedFile>(r));
  }
  deleteUploadedFile(id: string): boolean {
    const existing = this.getUploadedFile(id);
    if (!existing) return false;
    this.sql.run("DELETE FROM uploaded_files WHERE id = ?", id);
    return true;
  }

  // -- workstreams & pipeline gates ---------------------------------------
  insertWorkstream(w: Workstream): void {
    this.insert("workstreams", w);
  }  listWorkstreams(): Workstream[] {
    return this.sql.all<Row>("SELECT * FROM workstreams ORDER BY created_at ASC").map((r) => rowTo<Workstream>(r));
  }
  insertPipelineGate(g: PipelineGate): void {
    this.insert("pipeline_gates", g);
  }
  listPipelineGates(pluginId?: string): PipelineGate[] {
    const rows = pluginId
      ? this.sql.all<Row>("SELECT * FROM pipeline_gates WHERE plugin_id = ? ORDER BY gate_number ASC", pluginId)
      : this.sql.all<Row>("SELECT * FROM pipeline_gates ORDER BY plugin_id ASC, gate_number ASC");
    return rows.map((r) => rowTo<PipelineGate>(r));
  }
  updatePipelineGate(id: string, patch: Partial<PipelineGate>): void {
    this.update("pipeline_gates", id, patch as Row);
  }

  // -- local agent health beacon (migration v20) --------------------------
  getLocalAgentStatus(): { id: string; host?: string; version?: string; pid?: string; last_seen?: string; last_claim_at?: string; started_at?: string } | null {
    const rows = this.sql.all<Row>("SELECT * FROM local_agent_status WHERE id = 'local'");
    if (rows.length === 0) return null;
    const r = rows[0]!;
    return {
      id: String(r.id),
      host: r.host ? String(r.host) : undefined,
      version: r.version ? String(r.version) : undefined,
      pid: r.pid ? String(r.pid) : undefined,
      last_seen: r.last_seen ? String(r.last_seen) : undefined,
      last_claim_at: r.last_claim_at ? String(r.last_claim_at) : undefined,
      started_at: r.started_at ? String(r.started_at) : undefined,
    };
  }
  /** Record a heartbeat from the local daemon (idempotent single row). */
  upsertLocalAgentHeartbeat(input: { host?: string; version?: string; pid?: string; claimed?: boolean }): void {
    const now = new Date().toISOString();
    const existing = this.sql.all<Row>("SELECT id FROM local_agent_status WHERE id = 'local'");
    if (existing.length === 0) {
      this.insert("local_agent_status", {
        id: "local",
        host: input.host ?? null,
        version: input.version ?? null,
        pid: input.pid ?? null,
        last_seen: now,
        last_claim_at: input.claimed ? now : null,
        started_at: now,
        created_at: now,
        updated_at: now,
      });
      return;
    }
    const sets = ["last_seen = ?", "updated_at = ?"];
    const params: SqlParam[] = [now, now];
    if (input.host) { sets.push("host = ?"); params.push(input.host); }
    if (input.version) { sets.push("version = ?"); params.push(input.version); }
    if (input.pid) { sets.push("pid = ?"); params.push(input.pid); }
    if (input.claimed) { sets.push("last_claim_at = ?"); params.push(now); }
    params.push("local");
    this.sql.run(`UPDATE local_agent_status SET ${sets.join(", ")} WHERE id = ?`, ...params);
  }
  /** Age (ms) since the daemon's last heartbeat, or null if never seen. */
  localAgentSeenAgoMs(): number | null {
    const s = this.getLocalAgentStatus();
    if (!s?.last_seen) return null;
    const t = Date.parse(s.last_seen);
    if (Number.isNaN(t)) return null;
    return Date.now() - t;
  }
  /** Count PENDING local tasks older than the given age (ms). */
  countAgingPendingLocalTasks(olderThanMs: number): number {
    const cutoff = new Date(Date.now() - olderThanMs).toISOString();
    const rows = this.sql.all<Row>("SELECT COUNT(*) AS c FROM local_tasks WHERE status = 'PENDING' AND created_at < ?", cutoff);
    return Number(rows[0]?.c ?? 0);
  }

  // -- founder credentials & sessions --------------------------------------
  getFounderCredentials(): { id: string; username: string; password_hash: string; failed_attempts: number; locked_until?: string } | null {
    const rows = this.sql.all<Row>("SELECT * FROM founder_credentials WHERE id = ?", "founder");
    if (rows.length === 0) return null;
    const r = rows[0]!;
    return {
      id: String(r.id),
      username: String(r.username),
      password_hash: String(r.password_hash),
      failed_attempts: Number(r.failed_attempts ?? 0),
      locked_until: r.locked_until ? String(r.locked_until) : undefined,
    };
  }

  setFounderCredentials(input: { username: string; password_hash: string }): void {
    const now = new Date().toISOString();
    const existing = this.getFounderCredentials();
    if (existing) {
      this.sql.run(
        "UPDATE founder_credentials SET username = ?, password_hash = ?, failed_attempts = 0, locked_until = NULL, updated_at = ? WHERE id = ?",
        input.username,
        input.password_hash,
        now,
        "founder",
      );
    } else {
      this.insert("founder_credentials", {
        id: "founder",
        username: input.username,
        password_hash: input.password_hash,
        failed_attempts: 0,
        locked_until: null,
        created_at: now,
        updated_at: now,
      });
    }
  }

  updateFounderCredentials(patch: { password_hash?: string; failed_attempts?: number; locked_until?: string | null }): void {
    const sets: string[] = [];
    const params: SqlParam[] = [];
    if (patch.password_hash !== undefined) { sets.push("password_hash = ?"); params.push(patch.password_hash); }
    if (patch.failed_attempts !== undefined) { sets.push("failed_attempts = ?"); params.push(patch.failed_attempts); }
    if (patch.locked_until !== undefined) { sets.push("locked_until = ?"); params.push(patch.locked_until); }
    if (sets.length === 0) return;
    params.push(new Date().toISOString());
    this.sql.run(`UPDATE founder_credentials SET ${sets.join(", ")}, updated_at = ? WHERE id = 'founder'`, ...params);
  }

  createSession(input: { token: string; username: string; expires_at: string }): void {
    const now = new Date().toISOString();
    this.insert("auth_sessions", {
      id: `sess_${crypto.randomUUID()}`,
      token: input.token,
      username: input.username,
      created_at: now,
      expires_at: input.expires_at,
    });
  }

  getSession(token: string): { username: string; expires_at: string } | null {
    const rows = this.sql.all<Row>("SELECT * FROM auth_sessions WHERE token = ?", token);
    if (rows.length === 0) return null;
    return { username: String(rows[0]!.username), expires_at: String(rows[0]!.expires_at) };
  }

  deleteSession(token: string): void {
    this.sql.run("DELETE FROM auth_sessions WHERE token = ?", token);
  }

  /** Delete every session except the caller's (used after a password change). Pass null to delete all. */
  deleteSessionsExcept(keepToken: string | null): void {
    if (keepToken) {
      this.sql.run("DELETE FROM auth_sessions WHERE token <> ?", keepToken);
    } else {
      this.sql.run("DELETE FROM auth_sessions");
    }
  }

  deleteExpiredSessions(): void {
    this.sql.run("DELETE FROM auth_sessions WHERE expires_at < ?", new Date().toISOString());
  }
  /**
   * Full maintenance sweep — run on every DO wake + on a daily schedule so the
   * system self-heals after restarts: reaps stale agent runs, re-queues
   * expired local-task leases, and deletes expired sessions.
   */
  runMaintenance(opts: { staleRunMs?: number } = {}): { staleRuns: string[]; reapedLocal: string[]; expiredSessions: number } {
    const staleRuns = this.reapStaleAgentRuns(opts.staleRunMs);
    const reapedLocal = this.reapExpiredLocalTasks();
    const before = this.sql.all<Row>("SELECT COUNT(*) AS c FROM auth_sessions WHERE expires_at < ?", new Date().toISOString());
    const count = Number(before[0]?.c ?? 0);
    this.deleteExpiredSessions();
    return { staleRuns, reapedLocal, expiredSessions: count };
  }
  /** Counts of stuck work (for health checks / overview). */
  stuckCounts(): { staleAgentRuns: number; expiredLeases: number } {
    const now = new Date().toISOString();
    const staleAgentRuns = this.sql.all<Row>("SELECT COUNT(*) AS c FROM agent_jobs WHERE status = 'RUNNING' AND (last_heartbeat IS NULL OR last_heartbeat < ?)", new Date(Date.now() - 15 * 60_000).toISOString());
    const expiredLeases = this.sql.all<Row>("SELECT COUNT(*) AS c FROM local_tasks WHERE status IN ('CLAIMED','RUNNING') AND lease_expires_at IS NOT NULL AND lease_expires_at < ?", now);
    return {
      staleAgentRuns: Number(staleAgentRuns[0]?.c ?? 0),
      expiredLeases: Number(expiredLeases[0]?.c ?? 0),
    };
  }

  // -- Productivity Manager (migration v18) --------------------------------
  // Assignments ----------------------------------------------------------
  insertAssignment(a: ProductivityAssignment): void {
    this.insert("productivity_assignments", a);
  }
  getAssignment(id: string): ProductivityAssignment | null {
    const rows = this.sql.all<Row>("SELECT * FROM productivity_assignments WHERE id = ?", id);
    return rows.length ? rowTo<ProductivityAssignment>(rows[0]) : null;
  }
  listAssignments(opts: { status?: string; limit?: number } = {}): ProductivityAssignment[] {
    const limit = Math.max(1, Math.min(500, opts.limit ?? 200));
    if (opts.status) {
      return this.sql
        .all<Row>("SELECT * FROM productivity_assignments WHERE status = ? ORDER BY created_at DESC LIMIT ?", opts.status, limit)
        .map((r) => rowTo<ProductivityAssignment>(r));
    }
    return this.sql.all<Row>("SELECT * FROM productivity_assignments ORDER BY created_at DESC LIMIT ?", limit).map((r) => rowTo<ProductivityAssignment>(r));
  }
  updateAssignment(id: string, patch: Partial<ProductivityAssignment>): void {
    this.update("productivity_assignments", id, patch);
  }
  assignmentByTask(taskId: string): ProductivityAssignment | null {
    const rows = this.sql.all<Row>("SELECT * FROM productivity_assignments WHERE task_id = ? ORDER BY created_at DESC LIMIT 1", taskId);
    return rows.length ? rowTo<ProductivityAssignment>(rows[0]) : null;
  }
  /** Add token/cost usage to an assignment (task-scope actuals). */
  addAssignmentUsage(taskId: string, tokens: number, cost: number): void {
    this.sql.run(
      "UPDATE productivity_assignments SET tokens_used = tokens_used + ?, cost_actual = cost_actual + ?, updated_at = ? WHERE task_id = ?",
      tokens,
      cost,
      new Date().toISOString(),
      taskId,
    );
  }
  /** Count assignments stuck in ASSIGNED/IN_PROGRESS past the given age (ms). */
  countStaleAssignments(olderThanMs: number): number {
    const cutoff = new Date(Date.now() - olderThanMs).toISOString();
    const rows = this.sql.all<Row>(
      "SELECT COUNT(*) AS c FROM productivity_assignments WHERE status IN ('ASSIGNED','IN_PROGRESS') AND (started_at IS NULL OR started_at < ?)",
      cutoff,
    );
    return Number(rows[0]?.c ?? 0);
  }
  /** Mark stale assignments STALLED (Doctor auto-fix). Returns ids touched. */
  reapStaleAssignments(olderThanMs: number): string[] {
    const cutoff = new Date(Date.now() - olderThanMs).toISOString();
    const rows = this.sql.all<Row>(
      "SELECT id FROM productivity_assignments WHERE status IN ('ASSIGNED','IN_PROGRESS') AND (started_at IS NULL OR started_at < ?)",
      cutoff,
    );
    const ids = rows.map((r) => String(r.id));
    for (const id of ids) {
      this.sql.run(
        "UPDATE productivity_assignments SET status = 'STALLED', note = COALESCE(note, '') || 'Auto-marked STALLED by System Doctor (no progress within window).', updated_at = ? WHERE id = ?",
        new Date().toISOString(),
        id,
      );
    }
    return ids;
  }

  // Role skills -----------------------------------------------------------
  upsertRoleSkill(roleId: string, skill: string, proficiency: number, source = "manager"): void {
    const rows = this.sql.all<Row>("SELECT id FROM role_skills WHERE role_id = ? AND skill = ?", roleId, skill);
    const now = new Date().toISOString();
    if (rows.length) {
      this.sql.run("UPDATE role_skills SET proficiency = ?, updated_at = ? WHERE id = ?", proficiency, now, String(rows[0]!.id));
    } else {
      this.insert("role_skills", { id: newId("sk"), role_id: roleId, skill, proficiency, source, updated_at: now });
    }
  }
  listRoleSkills(roleId?: string): RoleSkill[] {
    if (roleId) {
      return this.sql.all<Row>("SELECT * FROM role_skills WHERE role_id = ? ORDER BY proficiency DESC", roleId).map((r) => rowTo<RoleSkill>(r));
    }
    return this.sql.all<Row>("SELECT * FROM role_skills ORDER BY role_id, proficiency DESC").map((r) => rowTo<RoleSkill>(r));
  }
  removeRoleSkill(roleId: string, skill: string): void {
    this.sql.run("DELETE FROM role_skills WHERE role_id = ? AND skill = ?", roleId, skill);
  }

  // Task skills -----------------------------------------------------------
  setTaskSkills(taskId: string, skills: Array<{ skill: string; weight: number }>): void {
    this.sql.run("DELETE FROM task_skills WHERE task_id = ?", taskId);
    const now = new Date().toISOString();
    for (const s of skills) {
      this.insert("task_skills", { id: newId("tsk"), task_id: taskId, skill: s.skill, weight: s.weight, updated_at: now });
    }
  }
  listTaskSkills(taskId: string): TaskSkill[] {
    return this.sql.all<Row>("SELECT * FROM task_skills WHERE task_id = ? ORDER BY weight DESC", taskId).map((r) => rowTo<TaskSkill>(r));
  }

  // Token budgets ---------------------------------------------------------
  getTokenBudget(scope: string, scopeId?: string): TokenBudget | null {
    const rows = this.sql.all<Row>("SELECT * FROM token_budgets WHERE scope = ? AND scope_id IS ?", scope, scopeId ?? null);
    return rows.length ? rowTo<TokenBudget>(rows[0]) : null;
  }
  listTokenBudgets(): TokenBudget[] {
    return this.sql.all<Row>("SELECT * FROM token_budgets ORDER BY scope, scope_id").map((r) => rowTo<TokenBudget>(r));
  }
  upsertTokenBudget(b: { scope: string; scopeId?: string; dailyBudget?: number; monthlyBudget?: number; modelTier?: string }): void {
    const existing = this.getTokenBudget(b.scope, b.scopeId);
    const now = new Date().toISOString();
    if (existing) {
      this.sql.run(
        "UPDATE token_budgets SET daily_budget = ?, monthly_budget = ?, model_tier = COALESCE(?, model_tier), updated_at = ? WHERE id = ?",
        b.dailyBudget ?? existing.daily_budget ?? null,
        b.monthlyBudget ?? existing.monthly_budget ?? null,
        b.modelTier ?? null,
        now,
        existing.id,
      );
    } else {
      this.insert("token_budgets", {
        id: newId("tb"),
        scope: b.scope,
        scope_id: b.scopeId ?? null,
        daily_budget: b.dailyBudget ?? null,
        monthly_budget: b.monthlyBudget ?? null,
        tokens_used_today: 0,
        tokens_used_month: 0,
        cost_used_today: 0,
        cost_used_month: 0,
        model_tier: b.modelTier ?? null,
        updated_at: now,
      });
    }
  }
  /** Add token/cost usage to a scope budget (and matching task assignment). */
  addTokenUse(scope: string, scopeId: string | undefined, tokens: number, cost: number): void {
    let budget = this.getTokenBudget(scope, scopeId);
    if (!budget) {
      this.upsertTokenBudget({ scope, scopeId });
      budget = this.getTokenBudget(scope, scopeId);
    }
    if (!budget) return;
    this.sql.run(
      "UPDATE token_budgets SET tokens_used_today = tokens_used_today + ?, tokens_used_month = tokens_used_month + ?, cost_used_today = cost_used_today + ?, cost_used_month = cost_used_month + ?, updated_at = ? WHERE id = ?",
      tokens,
      tokens,
      cost,
      cost,
      new Date().toISOString(),
      budget.id,
    );
    if (scope === "task" && scopeId) this.addAssignmentUsage(scopeId, tokens, cost);
  }
  /** Recalibrate the global budget from env defaults (idempotent, safe). */
  recalibrateGlobalBudget(envDaily: number, envMonthly: number): void {
    const g = this.getTokenBudget("global");
    if (!g) {
      this.upsertTokenBudget({ scope: "global", dailyBudget: envDaily, monthlyBudget: envMonthly });
      return;
    }
    this.sql.run("UPDATE token_budgets SET daily_budget = ?, monthly_budget = ?, updated_at = ? WHERE id = ?", envDaily, envMonthly, new Date().toISOString(), g.id);
  }

  // Optimization events ---------------------------------------------------
  insertOptimizationEvent(e: Omit<OptimizationEvent, "id" | "created_at" | "updated_at"> & { scopeId?: string }): void {
    const { scopeId, ...rest } = e;
    this.insert("optimization_events", {
      ...rest,
      scope_id: rest.scope_id ?? scopeId ?? null,
      id: newId("opt"),
      created_at: new Date().toISOString(),
      updated_at: new Date().toISOString(),
    });
  }
  listOptimizationEvents(limit = 50): OptimizationEvent[] {
    return this.sql.all<Row>("SELECT * FROM optimization_events ORDER BY created_at DESC LIMIT ?", limit).map((r) => rowTo<OptimizationEvent>(r));
  }

  // Contradictions --------------------------------------------------------
  /** Insert a contradiction; returns true if a new row was created (dedupes identical OPEN findings). */
  insertContradiction(c: Omit<Contradiction, "id" | "created_at" | "updated_at">): boolean {
    // Dedupe: skip if an identical OPEN contradiction already exists.
    const rows = this.sql.all<Row>(
      "SELECT id FROM contradictions WHERE kind = ? AND subject_a IS ? AND subject_b IS ? AND status = 'OPEN'",
      c.kind,
      c.subject_a ?? null,
      c.subject_b ?? null,
    );
    if (rows.length) return false;
    this.insert("contradictions", {
      ...c,
      id: newId("con"),
      status: c.status ?? "OPEN",
      created_at: new Date().toISOString(),
      updated_at: new Date().toISOString(),
    });
    return true;
  }
  listContradictions(opts: { status?: string; limit?: number } = {}): Contradiction[] {
    const limit = Math.max(1, Math.min(500, opts.limit ?? 200));
    if (opts.status) {
      return this.sql.all<Row>("SELECT * FROM contradictions WHERE status = ? ORDER BY created_at DESC LIMIT ?", opts.status, limit).map((r) => rowTo<Contradiction>(r));
    }
    return this.sql.all<Row>("SELECT * FROM contradictions ORDER BY created_at DESC LIMIT ?", limit).map((r) => rowTo<Contradiction>(r));
  }
  getContradiction(id: string): Contradiction | null {
    const rows = this.sql.all<Row>("SELECT * FROM contradictions WHERE id = ?", id);
    return rows.length ? rowTo<Contradiction>(rows[0]) : null;
  }
  countOpenContradictions(): number {
    const rows = this.sql.all<Row>("SELECT COUNT(*) AS c FROM contradictions WHERE status = 'OPEN'");
    return Number(rows[0]?.c ?? 0);
  }
  resolveContradiction(id: string, status: ContradictionStatus, resolution: string, by: string): void {
    this.sql.run(
      "UPDATE contradictions SET status = ?, resolution = ?, resolved_by = ?, resolved_at = ?, updated_at = ? WHERE id = ?",
      status,
      resolution,
      by,
      new Date().toISOString(),
      new Date().toISOString(),
      id,
    );
  }

  // Productivity metrics --------------------------------------------------
  getProductivityMetric(day: string): ProductivityMetric | null {
    const rows = this.sql.all<Row>("SELECT * FROM productivity_metrics WHERE day = ?", day);
    return rows.length ? rowTo<ProductivityMetric>(rows[0]) : null;
  }
  upsertProductivityMetric(day: string, m: { tasks_done: number; tokens_used: number; cost: number }): void {
    const existing = this.getProductivityMetric(day);
    const now = new Date().toISOString();
    const tasksPerDollar = m.cost > 0 ? Math.round((m.tasks_done / m.cost) * 100) / 100 : 0;
    const avgTokens = m.tasks_done > 0 ? Math.round(m.tokens_used / m.tasks_done) : 0;
    if (existing) {
      this.sql.run(
        "UPDATE productivity_metrics SET tasks_done = ?, tokens_used = ?, cost = ?, tasks_per_dollar = ?, avg_tokens_per_task = ?, updated_at = ? WHERE day = ?",
        m.tasks_done,
        m.tokens_used,
        m.cost,
        tasksPerDollar,
        avgTokens,
        now,
        day,
      );
    } else {
      this.insert("productivity_metrics", {
        id: newId("pm"),
        day,
        tasks_done: m.tasks_done,
        tokens_used: m.tokens_used,
        cost: m.cost,
        tasks_per_dollar: tasksPerDollar,
        avg_tokens_per_task: avgTokens,
        created_at: now,
        updated_at: now,
      });
    }
  }
  listProductivityMetrics(limit = 14): ProductivityMetric[] {
    return this.sql.all<Row>("SELECT * FROM productivity_metrics ORDER BY day DESC LIMIT ?", limit).map((r) => rowTo<ProductivityMetric>(r));
  }

  // -- System Doctor (migration v17) --------------------------------------
  /** Record a Doctor run into doctor_logs. */
  insertDoctorLog(log: DoctorLog): void {
    this.insert("doctor_logs", log);
  }
  listDoctorLogs(limit = 20): DoctorLog[] {
    return this.sql.all<Row>("SELECT * FROM doctor_logs ORDER BY run_at DESC LIMIT ?", limit).map((r) => rowTo<DoctorLog>(r));
  }
  latestDoctorLog(): DoctorLog | null {
    const rows = this.sql.all<Row>("SELECT * FROM doctor_logs ORDER BY run_at DESC LIMIT 1");
    return rows.length ? rowTo<DoctorLog>(rows[0]) : null;
  }
  /** Delete completed chat streams older than the given age (ms). Returns count pruned. */
  pruneCompletedChatStreams(olderThanMs: number): number {
    const cutoff = new Date(Date.now() - olderThanMs).toISOString();
    const rows = this.sql.all<Row>("SELECT id FROM chat_streams WHERE complete = 1 AND updated_at < ?", cutoff);
    const ids = rows.map((r) => String(r.id));
    for (const id of ids) this.sql.run("DELETE FROM chat_streams WHERE id = ?", id);
    return ids.length;
  }
  /** Count completed chat streams older than the given age (ms) — read-only. */
  countCompletedChatStreams(olderThanMs: number): number {
    const cutoff = new Date(Date.now() - olderThanMs).toISOString();
    const rows = this.sql.all<Row>("SELECT COUNT(*) AS c FROM chat_streams WHERE complete = 1 AND updated_at < ?", cutoff);
    return Number(rows[0]?.c ?? 0);
  }
  /** True if a table exists and can be queried. */
  probeTableExists(table: string): boolean {
    try {
      this.sql.all<Row>(`SELECT 1 FROM ${table} LIMIT 1`);
      return true;
    } catch {
      return false;
    }
  }
  /** Seed the headline product metrics (downloads / licensed) — idempotent. */
  ensureProductMetrics(): void {
    this.sql.run(PRODUCT_METRICS_SQL);
  }

  /**
   * Export a portable JSON snapshot of every business table — the "backup"
   * that lets the OS survive 6 months of operator absence. A fresh DO with the
   * same migrations can re-import this snapshot to fully reconstruct state.
   * (Auth sessions + credential hashes are included so the Founder can sign in
   * after a restore; uploads are included so files survive too.)
   */
  exportSnapshot(): Record<string, unknown[]> {
    const tables = [
      "executive_state", "founder_profile", "organizations", "objectives", "projects",
      "tasks", "decisions", "approval_requests", "risks", "metrics", "financial_snapshots",
      "products", "product_status", "engineering_issues", "marketing_campaigns", "persons",
      "delegations", "memories", "doctrine_rules", "policies", "audit_events", "chat_messages",
      "tool_executions", "workstreams", "pipeline_gates", "model_costs", "founder_credentials",
      "auth_sessions", "uploaded_files", "local_tasks", "agent_jobs", "settings",
      "calendar_events", "notes", "reminders", "contacts",
    ];
    const out: Record<string, unknown[]> = {};
    for (const table of tables) {
      try {
        out[table] = this.sql.all<Row>(`SELECT * FROM ${table}`).map((r) => rowTo<unknown>(r));
      } catch {
        out[table] = []; // table may not exist on an old schema
      }
    }
    return out;
  }

  /**
   * Import a snapshot produced by exportSnapshot() — overwrites each table
   * (DELETE then re-INSERT) so a restore fully reconstructs state. Idempotent:
   * running it twice yields the same result.
   */
  importSnapshot(snapshot: Record<string, unknown[]>): { tables: number; rows: number } {
    let rows = 0;
    const tables = Object.keys(snapshot);
    for (const table of tables) {
      const items = snapshot[table] ?? [];
      if (items.length === 0) continue;
      try {
        this.sql.run(`DELETE FROM ${table}`);
        for (const item of items) {
          this.insert(table, item as Row);
          rows += 1;
        }
      } catch {
        /* skip tables that don't exist / aren't writable */
      }
    }
    return { tables: tables.length, rows };
  }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function rowTo<T>(row: Row | undefined): T {
  return row as unknown as T;
}

/** Normalize values for SQL binding: undefined → null, booleans → 0/1. */
function normalize(value: unknown): SqlParam {
  if (value === undefined) return null;
  if (typeof value === "boolean") return value ? 1 : 0;
  if (typeof value === "object" && value !== null) return JSON.stringify(value);
  return value as SqlParam;
}
