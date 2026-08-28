const { DatabaseSync } = require('node:sqlite');
const db = new DatabaseSync(':memory:');
const sql = `
CREATE TABLE IF NOT EXISTS agent_runs (
  id TEXT PRIMARY KEY,
  status TEXT NOT NULL DEFAULT 'RUNNING'
);
CREATE INDEX IF NOT EXISTS idx_agent_runs_status ON agent_runs(status);
`;
try {
  db.exec('BEGIN');
  db.exec(sql);
  db.exec('COMMIT');
  console.log('BEGIN + multi-exec + COMMIT OK');
} catch (e) {
  console.log('ERR:', e.message);
  try { db.exec('ROLLBACK'); } catch {}
}
