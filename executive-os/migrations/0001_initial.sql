-- Zarlino Executive OS — initial schema (spec §4, §31)
-- Normalized relational structures; JSON only for flexible metadata.
-- Mirrors src/db/schema.ts. Applied idempotently by migrations/0001.

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
