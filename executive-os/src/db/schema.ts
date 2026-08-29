/**
 * Database schema (spec §4, §31).
 *
 * Normalized relational structures with JSON only for flexible metadata.
 * Every important record carries provenance (source), timestamps, and where
 * applicable confidence and status.
 *
 * This DDL is used verbatim by:
 *   - migrations/0001_initial.sql (checked in, deployable),
 *   - the Executive Agent's onStart migration runner,
 *   - tests (node:sqlite).
 */

export const SCHEMA_VERSION = 17;

/**
 * System Doctor (migration v17): persistent log of every Doctor run — what
 * was checked, what was found, and what was auto-fixed. The Doctor runs on DO
 * wake, on a 6-hourly cron, and on demand (`/api/doctor/run`, `system.doctor`),
 * and each run records a row here so the Founder can audit self-healing.
 */
export const DOCTOR_LOG_SQL = `
CREATE TABLE IF NOT EXISTS doctor_logs (
  id TEXT PRIMARY KEY,
  run_at TEXT NOT NULL,
  trigger TEXT NOT NULL DEFAULT 'manual',
  auto_fix INTEGER NOT NULL DEFAULT 0,
  summary_json TEXT NOT NULL,
  checks_json TEXT NOT NULL,
  source TEXT NOT NULL DEFAULT 'system',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_doctor_logs_run ON doctor_logs(run_at);
`;

/**
 * Auto-run (migration v15): marks whether a task may be executed AUTONOMOUSLY
 * (no Founder "Run" click). Default 1 = eligible; 0 = needs a human/manual.
 * The Executive sweeps eligible OPEN tasks on wake / task-creation and starts
 * a bounded long-horizon run for each, auto-marking DONE when it completes.
 * Skipped when the task's decision has a pending approval (needs Founder).
 */
export const AUTO_RUN_SQL = `
ALTER TABLE tasks ADD COLUMN auto_run INTEGER NOT NULL DEFAULT 1;
CREATE INDEX IF NOT EXISTS idx_tasks_auto_run ON tasks(auto_run, status);
CREATE INDEX IF NOT EXISTS idx_agent_jobs_task ON agent_jobs(task_id);
-- Tasks that exist ONLY for a Founder decision never auto-run (a human must
-- pick: release, A/B listening verdict, approval). Everything else is free.
UPDATE tasks SET auto_run = 0
 WHERE id IN ('tsk_zscorch_gate10', 'tsk_ztame_p8')
    OR title LIKE '%founder%decision%'
    OR title LIKE '%A/B listening%';
`;

/**
 * Product metrics (migration v16): adds the missing updated_at column to the
 * metrics table and seeds the two headline product counters the Founder
 * tracks — Downloads and Licensed seats. INSERT OR IGNORE by deterministic id
 * so Founder/Executive updates are never overwritten on redeploy.
 */
export const PRODUCT_METRICS_SQL = `
ALTER TABLE metrics ADD COLUMN updated_at TEXT;
INSERT OR IGNORE INTO metrics (id, name, value, unit, period, source, confidence, recorded_at, created_at)
VALUES
 ('metric_downloads', 'downloads', 238, 'downloads', strftime('%Y-%m','now'), 'founder', 'FACT', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('metric_licensed', 'licensed', 83, 'licenses', strftime('%Y-%m','now'), 'founder', 'FACT', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now'));
`;

/**
 * Day-to-day operating tables (migration v14): calendar, notes, reminders and
 * external contacts — the "CEO day" tools the agent needs to run the business
 * (schedule, capture decisions/notes, follow up, and track partners/vendors).
 */
export const PLANNER_SQL = `
CREATE TABLE IF NOT EXISTS calendar_events (
  id TEXT PRIMARY KEY,
  title TEXT NOT NULL,
  description TEXT,
  start_at TEXT NOT NULL,
  end_at TEXT,
  all_day INTEGER NOT NULL DEFAULT 0,
  location TEXT,
  attendees_json TEXT NOT NULL DEFAULT '[]',
  status TEXT NOT NULL DEFAULT 'SCHEDULED',
  source TEXT NOT NULL DEFAULT 'founder',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_calendar_start ON calendar_events(start_at);
CREATE INDEX IF NOT EXISTS idx_calendar_status ON calendar_events(status);

CREATE TABLE IF NOT EXISTS notes (
  id TEXT PRIMARY KEY,
  title TEXT NOT NULL,
  body TEXT,
  tags_json TEXT NOT NULL DEFAULT '[]',
  pinned INTEGER NOT NULL DEFAULT 0,
  source TEXT NOT NULL DEFAULT 'founder',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_notes_pinned ON notes(pinned);

CREATE TABLE IF NOT EXISTS reminders (
  id TEXT PRIMARY KEY,
  title TEXT NOT NULL,
  due_at TEXT NOT NULL,
  done INTEGER NOT NULL DEFAULT 0,
  done_at TEXT,
  source TEXT NOT NULL DEFAULT 'founder',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_reminders_due ON reminders(due_at);
CREATE INDEX IF NOT EXISTS idx_reminders_done ON reminders(done);

CREATE TABLE IF NOT EXISTS contacts (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  role TEXT,
  company TEXT,
  email TEXT,
  phone TEXT,
  tags_json TEXT NOT NULL DEFAULT '[]',
  notes TEXT,
  source TEXT NOT NULL DEFAULT 'founder',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
`;

/**
 * Settings table (migration v13): founder-editable key/value overrides that
 * persist across deploys — AI budget overrides (so the Founder can extend the
 * daily/monthly AI budget from the UI without redeploying) and the cached
 * AI-provider balance (so we don't hammer the provider's balance endpoint on
 * every overview render).
 */
export const SETTINGS_SQL = `
CREATE TABLE IF NOT EXISTS settings (
  key TEXT PRIMARY KEY,
  value TEXT,
  updated_at TEXT NOT NULL
);
`;

/**
 * Resiliency columns (migration v12): checkpoint/heartbeat/lease fields that
 * let the system survive machine restarts and DO wakes:
 *   - agent_jobs.checkpoint_json — durable tool-loop transcript snapshot, so a
 *     long-horizon run can resume from where it stopped after a DO restart.
 *   - agent_jobs.last_heartbeat — liveness; stale RUNNING jobs get reaped.
 *   - local_tasks.lease_expires_at — claim lease; a machine that goes offline
 *     mid-task releases its lease so another daemon can re-claim.
 *   - local_tasks.meta_json — structured brief (goal/context/acceptance).
 */
export const RESILIENCY_SQL = `
ALTER TABLE agent_jobs ADD COLUMN checkpoint_json TEXT;
ALTER TABLE agent_jobs ADD COLUMN last_heartbeat TEXT;
ALTER TABLE local_tasks ADD COLUMN lease_expires_at TEXT;
ALTER TABLE local_tasks ADD COLUMN meta_json TEXT;
`;

/**
 * Uploaded files (migration v11): files the Founder uploads from the web
 * console (screenshots, specs, audio, documents) that the Executive can
 * inspect — images via vision, text via direct reading, others by metadata.
 * Data is stored base64 in `data_base64` (bounded to a few MB per file).
 */
export const UPLOADED_FILES_SQL = `
CREATE TABLE IF NOT EXISTS uploaded_files (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  mime TEXT,
  size_bytes INTEGER,
  data_base64 TEXT,
  kind TEXT NOT NULL DEFAULT 'other',
  source TEXT NOT NULL DEFAULT 'founder',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_uploaded_files_created ON uploaded_files(created_at);
`;

/**
 * Local machine tasks (migration v10): a bridge between the live Executive
 * (Cloudflare) and the local machine. The agent (or Founder via the web
 * console) queues a task; a local daemon on the machine claims it, works on
 * it with local tools (read/write repo files, run commands, inspect plugin
 * UI source), and posts the result back. Status: PENDING → CLAIMED →
 * RUNNING → DONE/FAILED.
 */
export const LOCAL_TASKS_SQL = `
CREATE TABLE IF NOT EXISTS local_tasks (
  id TEXT PRIMARY KEY,
  task_id TEXT,
  prompt TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'PENDING',
  claimed_by TEXT,
  started_at TEXT,
  finished_at TEXT,
  result TEXT,
  error TEXT,
  source TEXT NOT NULL DEFAULT 'founder',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_local_tasks_status ON local_tasks(status);
CREATE INDEX IF NOT EXISTS idx_local_tasks_created ON local_tasks(created_at);
`;

/**
 * Auth tables (migration v2): founder username/password credentials and
 * bearer session tokens.
 */
export const AUTH_SCHEMA_SQL = `
CREATE TABLE IF NOT EXISTS founder_credentials (
  id TEXT PRIMARY KEY,
  username TEXT NOT NULL UNIQUE,
  password_hash TEXT NOT NULL,
  failed_attempts INTEGER NOT NULL DEFAULT 0,
  locked_until TEXT,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS auth_sessions (
  id TEXT PRIMARY KEY,
  token TEXT NOT NULL UNIQUE,
  username TEXT NOT NULL,
  created_at TEXT NOT NULL,
  expires_at TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_sessions_token ON auth_sessions(token);
CREATE INDEX IF NOT EXISTS idx_sessions_expires ON auth_sessions(expires_at);
`;

/**
 * Pipeline-state data migration (v3): sync the Executive OS with the real
 * 10-gate pipeline state for ZScorch and ZTame, verified from
 * pipeline/ZScorch/STATUS.md and pipeline/ZTame/STATUS.md (2026-08-24).
 *
 * Updates existing seeded gates (1-9 PASSED, 10 IN_PROGRESS) and inserts the
 * open tasks, risks, and the pending Gate 10 approval using deterministic
 * IDs (INSERT OR IGNORE keeps it idempotent). Fresh databases run this after
 * the seed's gate insert loop; already-seeded databases (the deployed DO)
 * get the UPDATEs applied to their existing rows.
 */
export const PIPELINE_STATE_SQL = `
-- ZScorch: gates 1-9 passed; gate 10 in progress (ready_for_release, awaiting Founder).
UPDATE pipeline_gates SET status = 'PASSED', updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now')
 WHERE plugin_id = 'ZScorch' AND gate_number BETWEEN 1 AND 9;
UPDATE pipeline_gates SET status = 'IN_PROGRESS',
       note = 'ready_for_release — awaiting Khritical approval (Gate 10)',
       updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now')
 WHERE plugin_id = 'ZScorch' AND gate_number = 10;

-- ZTame: gates 1-9 passed; gate 10 in progress (rework_complete, P8 open).
UPDATE pipeline_gates SET status = 'PASSED', updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now')
 WHERE plugin_id = 'ZTame' AND gate_number BETWEEN 1 AND 9;
UPDATE pipeline_gates SET status = 'IN_PROGRESS',
       note = 'rework_complete — P1-P7 verified; P8 suppression-quality audit awaiting Khritical re-review',
       updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now')
 WHERE plugin_id = 'ZTame' AND gate_number = 10;

-- Open tasks (deterministic ids).
INSERT OR IGNORE INTO tasks
 (id, title, description, status, priority, assignee_person_id, source, created_at, updated_at) VALUES
 ('tsk_ztame_p8',
  'ZTame P8 suppression-quality A/B listening test',
  'Run the documented A/B protocol (pipeline/ZTame/STATUS.md): sustained tone + resonant ringing, piano/plucked string, drums + pad; A/B vs bypass at 100% wet, sensitivity 50→90, range -12 dB. Report which symptom matches: (a) pumping/warbling on transients → add detection-path smoothing/peak-hold; (b) Selectivity knob inaudible → widen selectivity range or re-map the threshold formula.',
  'BLOCKED', 'P0', 'founder', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('tsk_ztame_signing',
  'ZTame production license key + code signing',
  'Replace the development license public-key placeholder in ZLicenseManager.h with the production key and configure code signing before public commercial distribution.',
  'OPEN', 'P1', NULL, 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('tsk_zscorch_gate10',
  'ZScorch Gate 10 — founder release decision',
  'Approve or reject ZScorch v1.0.0 release. Gates 1-9 all passed; release build + VST3/Standalone/Installer packages verified; production license key verified 2026-08-23.',
  'OPEN', 'P1', 'founder', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('tsk_zscorch_signing',
  'ZScorch code signing',
  'Configure code signing for the release build (deferred at Gate 7/9).',
  'OPEN', 'P2', NULL, 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now'));

-- Open risks.
INSERT OR IGNORE INTO risks
 (id, title, description, level, category, likelihood, impact, mitigation, status, owner_person_id, source, created_at, updated_at) VALUES
 ('risk_ztame_p8',
  'ZTame suppression quality (P8) unresolved',
  'Engineering audit (2026-08-15) found no DSP math bugs, but two candidates remain: (a) no temporal smoothing/peak-hold on the detection spectrum → pumping/warbling on transients; (b) Sensitivity/Selectivity overlap (Selectivity moves threshold only ±5.25 dB vs Sensitivity 12 dB).',
  'HIGH', 'QUALITY', 'MEDIUM', 'HIGH', 'Founder A/B listening test → rework identified stage (detection smoothing or sensitivity/selectivity remap).', 'OPEN', 'founder', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('risk_ztame_signing',
  'ZTame ships unsigned with dev license key',
  'ZLicenseManager.h still embeds the development public-key placeholder; release build is unsigned.',
  'HIGH', 'SECURITY', 'HIGH', 'HIGH', 'Embed production key and sign before commercial release; or ship as explicitly-disclosed trial/unsigned build.', 'OPEN', NULL, 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('risk_zscorch_signing',
  'ZScorch release build unsigned',
  'Code signing deferred at Gate 7/9; public distribution should be signed.',
  'MEDIUM', 'SECURITY', 'MEDIUM', 'MEDIUM', 'Configure signing certificate + CI signing before public distribution.', 'OPEN', NULL, 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('risk_golden_refs',
  'Golden-reference audio files missing',
  'Gate 5 golden-reference files not yet supplied for ZScorch or ZTame (non-blocking for initial release).',
  'LOW', 'QUALITY', 'LOW', 'LOW', 'Add golden-reference captures to the QA suite post-release.', 'OPEN', NULL, 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now'));

-- Pending approval: ZScorch Gate 10 release.
INSERT OR IGNORE INTO approval_requests
 (id, action_label, reason, expected_upside, max_downside, budget_available, recommendation, status, requested_by, source, created_at, updated_at) VALUES
 ('appr_zscorch_gate10',
  'Approve ZScorch v1.0.0 release (Gate 10)',
  'All gates 1-9 passed; release build + VST3/Standalone/Installer packages verified; production license key verified. Awaiting founder decision to ship.',
  'First commercial product shipped; revenue per unit (target unit economics).',
  'Shipping before full DAW host matrix testing; unsigned build.',
  'No budget required', 'APPROVE', 'PENDING', 'executive', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now'));
`;

/**
 * Technical-roles data migration (v4): onboard the orchestrator's role roster
 * into the persons table on already-seeded databases. Fresh databases get the
 * same rows from the seed; INSERT OR IGNORE keeps it idempotent either way.
 */
export const PERSON_ROLES_SQL = `
INSERT OR IGNORE INTO persons (id, name, role, source, created_at, updated_at) VALUES
 ('person_role_product_manager', 'Product Manager', 'Gate 1 — market analysis, spec authoring', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_architect', 'Architect', 'Gate 2 — signal chain design, algorithm selection', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_dsp_engineer', 'DSP Engineer', 'Gate 3 — C++/JUCE DSP implementation', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_ui_engineer', 'UI Engineer', 'Gate 3 — editor UI, Look & Feel', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_parameter_engineer', 'Parameter Engineer', 'Gate 3 — APVTS, presets, state', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_code_reviewer', 'Code Reviewer', 'Gate 4 — bug discovery, thread/memory safety', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_qa_engineer', 'QA Engineer', 'Gate 5 — tests, fuzz, golden references', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_performance_engineer', 'Performance Engineer', 'Gate 6 — CPU/memory/latency budgets', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_security_licensing_engineer', 'Security & Licensing Engineer', 'Gate 7 — license compliance, signing', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_documentation_writer', 'Documentation Writer', 'Gate 8 — manuals, parameter reference', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_release_engineer', 'Release Engineer', 'Gate 9 — CMake, CI, packaging', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_research_engineer', 'Research Engineer', 'Web research, competitive analysis', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now'));
`;

/**
 * Delegate-mode migration (v5): adds the Founder-granted delegate flag to the
 * executive state row (0 = off, 1 = on). New column defaults to 0 so existing
 * rows stay conservative until the Founder explicitly enables it.
 */
/**
 * Ship-state reconciliation (migration v7): ZScorch and ZTame are already
 * LIVE on zarlinoaudio.com (v1.0.0), so the pipeline previously under-reported
 * them (Gate 10 stuck at IN_PROGRESS while the site shipped). This marks both
 * products SHIPPED, sets their Gate 10 to SHIPPED, and records the shipment
 * history (guarded so fresh DBs stay clean — the seed records the same).
 */
/**
 * Full-access migration (v8): adds the Founder-granted full-access flag to
 * the executive state row (0 = off, 1 = on). When ON, the Executive runs
 * virtually everything autonomously (deploy, HIGH-reversible, spend) while
 * hard gates (delete/transfer/doctrine/CRITICAL/irreversible/emergency) hold.
 */
/**
 * Long-horizon agent runs (migration v9): persistent records of the Executive
 * working on a task across many tool calls. Lets the agent embark on long,
 * multi-step work and be resumed safely (status: RUNNING/DONE/FAILED/STOPPED/
 * TIMEOUT), with iteration + tool counters and a final result.
 */
export const AGENT_RUNS_SQL = `
CREATE TABLE IF NOT EXISTS agent_jobs (
  id TEXT PRIMARY KEY,
  task_id TEXT,
  prompt TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'RUNNING',
  iterations INTEGER NOT NULL DEFAULT 0,
  tools_called INTEGER NOT NULL DEFAULT 0,
  started_at TEXT NOT NULL,
  finished_at TEXT,
  result TEXT,
  error TEXT,
  source TEXT NOT NULL DEFAULT 'founder',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_agent_jobs_status ON agent_jobs(status);
CREATE INDEX IF NOT EXISTS idx_agent_jobs_created ON agent_jobs(created_at);
`;

export const FULL_ACCESS_SQL = `
ALTER TABLE executive_state ADD COLUMN full_access INTEGER NOT NULL DEFAULT 0;
`;

export const SHIP_STATE_SQL = `
-- product_status lacked updated_at (the Ship action records history) — add it.
ALTER TABLE product_status ADD COLUMN updated_at TEXT;
UPDATE products SET status = 'SHIPPED', version = '1.0.0', updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now')
 WHERE name IN ('ZScorch', 'ZTame');
UPDATE pipeline_gates SET status = 'SHIPPED',
       note = 'Shipped — live on zarlinoaudio.com (v1.0.0)',
       updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now')
 WHERE plugin_id IN ('ZScorch', 'ZTame') AND gate_number = 10;
INSERT OR IGNORE INTO product_status (id, product_id, status, gate, note, recorded_at, source, created_at)
SELECT 'ps_ship_zscorch', id, 'SHIPPED', 'Gate 10', 'Shipped — live on zarlinoaudio.com (v1.0.0)', strftime('%Y-%m-%dT%H:%M:%fZ','now'), 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now')
  FROM products WHERE name = 'ZScorch' AND NOT EXISTS (SELECT 1 FROM product_status WHERE id = 'ps_ship_zscorch');
INSERT OR IGNORE INTO product_status (id, product_id, status, gate, note, recorded_at, source, created_at)
SELECT 'ps_ship_ztame', id, 'SHIPPED', 'Gate 10', 'Shipped — live on zarlinoaudio.com (v1.0.0)', strftime('%Y-%m-%dT%H:%M:%fZ','now'), 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now')
  FROM products WHERE name = 'ZTame' AND NOT EXISTS (SELECT 1 FROM product_status WHERE id = 'ps_ship_ztame');
`;

export const DELEGATE_MODE_SQL = `
ALTER TABLE executive_state ADD COLUMN delegate_mode INTEGER NOT NULL DEFAULT 0;
`;

/**
 * Chat-stream migration (v6): buffers streamed chat reply chunks so the
 * Worker can poll them over DO RPC (async generators do not survive RPC).
 * chunks_json is a JSON array of text deltas appended by the agent's
 * background stream task and read incrementally by the SSE endpoint.
 */
export const CHAT_STREAM_SQL = `
CREATE TABLE IF NOT EXISTS chat_streams (
  id TEXT PRIMARY KEY,
  session_id TEXT,
  chunks_json TEXT NOT NULL DEFAULT '[]',
  complete INTEGER NOT NULL DEFAULT 0,
  error TEXT,
  source TEXT NOT NULL DEFAULT 'executive',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
`;

export const INITIAL_SCHEMA_SQL = `
CREATE TABLE IF NOT EXISTS executive_state (
  id TEXT PRIMARY KEY,
  overall_status TEXT NOT NULL DEFAULT 'GREEN',
  current_objective_id TEXT,
  top_priorities_json TEXT NOT NULL DEFAULT '[]',
  paused INTEGER NOT NULL DEFAULT 0,
  pause_reason TEXT,
  autonomy_mode TEXT NOT NULL DEFAULT 'CONTROLLED',
  spending_disabled INTEGER NOT NULL DEFAULT 0,
  deployments_disabled INTEGER NOT NULL DEFAULT 0,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS founder_profile (
  id TEXT PRIMARY KEY,
  name TEXT,
  email TEXT,
  role TEXT,
  authority_level TEXT NOT NULL DEFAULT 'FOUNDER',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS organizations (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  mission TEXT,
  source TEXT NOT NULL DEFAULT 'founder',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS objectives (
  id TEXT PRIMARY KEY,
  title TEXT NOT NULL,
  description TEXT,
  status TEXT NOT NULL DEFAULT 'ACTIVE',
  priority INTEGER NOT NULL DEFAULT 0,
  source TEXT NOT NULL DEFAULT 'founder',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  completed_at TEXT
);

CREATE TABLE IF NOT EXISTS projects (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  description TEXT,
  objective_id TEXT,
  status TEXT NOT NULL DEFAULT 'PLANNED',
  priority INTEGER NOT NULL DEFAULT 0,
  owner_person_id TEXT,
  start_date TEXT,
  target_date TEXT,
  success_condition TEXT,
  source TEXT NOT NULL DEFAULT 'founder',
  metadata_json TEXT,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS tasks (
  id TEXT PRIMARY KEY,
  title TEXT NOT NULL,
  description TEXT,
  project_id TEXT,
  decision_id TEXT,
  assignee_person_id TEXT,
  status TEXT NOT NULL DEFAULT 'OPEN',
  priority TEXT NOT NULL DEFAULT 'P2',
  due_date TEXT,
  dependency_task_id TEXT,
  success_condition TEXT,
  source TEXT NOT NULL DEFAULT 'founder',
  metadata_json TEXT,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  completed_at TEXT
);

CREATE TABLE IF NOT EXISTS decisions (
  id TEXT PRIMARY KEY,
  title TEXT NOT NULL,
  problem TEXT NOT NULL,
  objective_id TEXT,
  status TEXT NOT NULL DEFAULT 'DRAFT',
  category TEXT NOT NULL DEFAULT 'OPERATIONAL',
  confidence REAL,
  known_facts_json TEXT,
  unknowns_json TEXT,
  constraints_json TEXT,
  options_json TEXT,
  ceo_assessment TEXT,
  cfo_assessment TEXT,
  cto_assessment TEXT,
  risk_assessment TEXT,
  risks_json TEXT,
  recommended_action TEXT,
  expected_outcome TEXT,
  authority_required TEXT,
  approval_required INTEGER NOT NULL DEFAULT 0,
  execution_plan_json TEXT,
  success_metric TEXT,
  review_date TEXT,
  outcome_status TEXT,
  outcome_actual TEXT,
  outcome_variance TEXT,
  lesson TEXT,
  source TEXT NOT NULL DEFAULT 'founder',
  model_trace_json TEXT,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS approval_requests (
  id TEXT PRIMARY KEY,
  decision_id TEXT,
  action_label TEXT NOT NULL,
  reason TEXT NOT NULL,
  expected_upside TEXT,
  max_downside TEXT,
  budget_available TEXT,
  recommendation TEXT NOT NULL DEFAULT 'APPROVE',
  status TEXT NOT NULL DEFAULT 'PENDING',
  requested_by TEXT NOT NULL DEFAULT 'executive',
  decided_by TEXT,
  decision_note TEXT,
  source TEXT NOT NULL DEFAULT 'executive',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  decided_at TEXT
);

CREATE TABLE IF NOT EXISTS risks (
  id TEXT PRIMARY KEY,
  title TEXT NOT NULL,
  description TEXT,
  level TEXT NOT NULL DEFAULT 'MEDIUM',
  category TEXT NOT NULL DEFAULT 'OPERATIONAL',
  likelihood TEXT,
  impact TEXT,
  mitigation TEXT,
  status TEXT NOT NULL DEFAULT 'OPEN',
  owner_person_id TEXT,
  source TEXT NOT NULL DEFAULT 'executive',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS metrics (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  value REAL,
  unit TEXT,
  period TEXT,
  source TEXT NOT NULL DEFAULT 'manual',
  confidence TEXT NOT NULL DEFAULT 'ESTIMATE',
  recorded_at TEXT NOT NULL,
  created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS financial_snapshots (
  id TEXT PRIMARY KEY,
  period TEXT NOT NULL,
  cash REAL,
  revenue REAL,
  expenses REAL,
  runway_months REAL,
  gross_margin REAL,
  confidence TEXT NOT NULL DEFAULT 'ESTIMATE',
  notes TEXT,
  source TEXT NOT NULL DEFAULT 'manual',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS products (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  category TEXT,
  status TEXT NOT NULL DEFAULT 'PLANNED',
  version TEXT,
  description TEXT,
  priority INTEGER NOT NULL DEFAULT 0,
  price REAL,
  source TEXT NOT NULL DEFAULT 'founder',
  metadata_json TEXT,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS product_status (
  id TEXT PRIMARY KEY,
  product_id TEXT NOT NULL,
  status TEXT NOT NULL,
  gate TEXT,
  note TEXT,
  recorded_at TEXT NOT NULL,
  source TEXT NOT NULL DEFAULT 'executive',
  created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS engineering_issues (
  id TEXT PRIMARY KEY,
  title TEXT NOT NULL,
  description TEXT,
  product_id TEXT,
  severity TEXT NOT NULL DEFAULT 'MEDIUM',
  status TEXT NOT NULL DEFAULT 'OPEN',
  assignee_person_id TEXT,
  source TEXT NOT NULL DEFAULT 'executive',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS marketing_campaigns (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'PLANNED',
  channel TEXT,
  budget REAL,
  spent REAL,
  objective TEXT,
  metrics_json TEXT,
  source TEXT NOT NULL DEFAULT 'founder',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS persons (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  role TEXT,
  email TEXT,
  source TEXT NOT NULL DEFAULT 'founder',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS delegations (
  id TEXT PRIMARY KEY,
  delegate_person_id TEXT,
  role TEXT,
  scope TEXT,
  status TEXT NOT NULL DEFAULT 'ACTIVE',
  source TEXT NOT NULL DEFAULT 'founder',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS tool_executions (
  id TEXT PRIMARY KEY,
  tool_name TEXT NOT NULL,
  input_json TEXT NOT NULL,
  output_json TEXT,
  permission TEXT NOT NULL,
  verdict TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'PENDING',
  cost_estimate REAL,
  reversible INTEGER NOT NULL DEFAULT 1,
  run_id TEXT,
  error TEXT,
  started_at TEXT,
  finished_at TEXT,
  source TEXT NOT NULL DEFAULT 'executive',
  created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS agent_runs (
  id TEXT PRIMARY KEY,
  session_id TEXT,
  model TEXT,
  model_version TEXT,
  prompt_version TEXT,
  tools_called_json TEXT,
  tokens_json TEXT,
  latency_ms INTEGER,
  cost REAL,
  decision_id TEXT,
  approval_id TEXT,
  error TEXT,
  outcome TEXT,
  source TEXT NOT NULL DEFAULT 'executive',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS memories (
  id TEXT PRIMARY KEY,
  layer TEXT NOT NULL,
  kind TEXT NOT NULL,
  title TEXT NOT NULL,
  body TEXT NOT NULL,
  tags_json TEXT,
  entity_id TEXT,
  importance REAL NOT NULL DEFAULT 0.5,
  confidence REAL,
  source TEXT NOT NULL DEFAULT 'executive',
  status TEXT NOT NULL DEFAULT 'ACTIVE',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  expires_at TEXT
);

CREATE TABLE IF NOT EXISTS doctrine_rules (
  id TEXT PRIMARY KEY,
  code TEXT NOT NULL UNIQUE,
  principle TEXT NOT NULL,
  rationale TEXT,
  category TEXT NOT NULL DEFAULT 'GENERAL',
  status TEXT NOT NULL DEFAULT 'ACTIVE',
  version INTEGER NOT NULL DEFAULT 1,
  proposed_by TEXT,
  approved_by TEXT,
  source TEXT NOT NULL DEFAULT 'seed',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS policies (
  id TEXT PRIMARY KEY,
  code TEXT NOT NULL UNIQUE,
  title TEXT NOT NULL,
  body TEXT NOT NULL,
  category TEXT NOT NULL DEFAULT 'GENERAL',
  status TEXT NOT NULL DEFAULT 'ACTIVE',
  version INTEGER NOT NULL DEFAULT 1,
  source TEXT NOT NULL DEFAULT 'founder',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS audit_events (
  id TEXT PRIMARY KEY,
  actor TEXT NOT NULL,
  action TEXT NOT NULL,
  entity_type TEXT,
  entity_id TEXT,
  detail_json TEXT,
  verdict TEXT,
  ip TEXT,
  source TEXT NOT NULL DEFAULT 'system',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS chat_messages (
  id TEXT PRIMARY KEY,
  session_id TEXT NOT NULL,
  role TEXT NOT NULL,
  content TEXT NOT NULL,
  model TEXT,
  source TEXT NOT NULL DEFAULT 'founder',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS model_costs (
  id TEXT PRIMARY KEY,
  provider TEXT NOT NULL,
  model TEXT NOT NULL,
  task_type TEXT NOT NULL,
  input_tokens INTEGER NOT NULL DEFAULT 0,
  output_tokens INTEGER NOT NULL DEFAULT 0,
  cost REAL NOT NULL DEFAULT 0,
  session_id TEXT,
  run_id TEXT,
  latency_ms INTEGER NOT NULL DEFAULT 0,
  source TEXT NOT NULL DEFAULT 'executive',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS workstreams (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  description TEXT,
  kind TEXT NOT NULL DEFAULT 'PLUGIN_PIPELINE',
  status TEXT NOT NULL DEFAULT 'ACTIVE',
  config_json TEXT,
  source TEXT NOT NULL DEFAULT 'seed',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS pipeline_gates (
  id TEXT PRIMARY KEY,
  workstream_id TEXT NOT NULL,
  plugin_id TEXT NOT NULL,
  gate_number INTEGER NOT NULL,
  gate_name TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'NOT_STARTED',
  owner_role TEXT,
  fail_count INTEGER NOT NULL DEFAULT 0,
  note TEXT,
  source TEXT NOT NULL DEFAULT 'seed',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_decisions_created ON decisions(created_at);
CREATE INDEX IF NOT EXISTS idx_tasks_status ON tasks(status);
CREATE INDEX IF NOT EXISTS idx_tasks_project ON tasks(project_id);
CREATE INDEX IF NOT EXISTS idx_approvals_status ON approval_requests(status);
CREATE INDEX IF NOT EXISTS idx_audit_created ON audit_events(created_at);
CREATE INDEX IF NOT EXISTS idx_memories_layer ON memories(layer);
CREATE INDEX IF NOT EXISTS idx_risks_level ON risks(level);
CREATE INDEX IF NOT EXISTS idx_costs_created ON model_costs(created_at);
CREATE INDEX IF NOT EXISTS idx_chat_session ON chat_messages(session_id);
CREATE INDEX IF NOT EXISTS idx_gates_plugin ON pipeline_gates(plugin_id);
`;
