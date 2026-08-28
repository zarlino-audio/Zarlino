-- Zarlino Executive OS — migration v10: local machine tasks.
-- Mirrors LOCAL_TASKS_SQL in src/db/schema.ts (applied by the migration runner).
-- Bridge between the live Executive (Cloudflare) and the local machine: the
-- agent/Founder queues a task; a local daemon claims it, works with local
-- tools (repo files, plugin UI source, commands, builds), and posts the
-- result back. Status: PENDING → CLAIMED → RUNNING → DONE/FAILED.

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
