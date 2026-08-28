-- Zarlino Executive OS — migration v9: long-horizon agent runs.
-- Mirrors AGENT_RUNS_SQL in src/db/schema.ts (applied by the migration runner).
-- Persistent records of the Executive working on a task across many tool calls,
-- so it can embark on long, multi-step work and be resumed safely.
-- NOTE: table is named `agent_jobs` — the v1 schema already reserves `agent_runs`
-- for decision logging, so the long-horizon table uses a distinct name.

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
