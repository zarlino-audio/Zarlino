import { createMemorySql } from '../src/db/sql';
import { INITIAL_SCHEMA_SQL, AUTH_SCHEMA_SQL, PIPELINE_STATE_SQL, PERSON_ROLES_SQL, DELEGATE_MODE_SQL, CHAT_STREAM_SQL, SHIP_STATE_SQL, FULL_ACCESS_SQL, AGENT_RUNS_SQL } from '../src/db/schema';

const list = [
  [1, INITIAL_SCHEMA_SQL],
  [2, AUTH_SCHEMA_SQL],
  [3, PIPELINE_STATE_SQL],
  [4, PERSON_ROLES_SQL],
  [5, DELEGATE_MODE_SQL],
  [6, CHAT_STREAM_SQL],
  [7, SHIP_STATE_SQL],
  [8, FULL_ACCESS_SQL],
] as const;

const sql = createMemorySql();
for (const [v, s] of list) {
  sql.transaction(() => {
    sql.exec(s);
  });
  console.log(`v${v} OK`);
}

// Now v9 inside transaction, like migrate()
try {
  sql.transaction(() => {
    sql.exec(AGENT_RUNS_SQL);
  });
  console.log('v9 OK');
} catch (e) {
  console.log(`v9 FAIL: ${(e as Error).message}`);
}

// Check the table
const cols = sql.all('PRAGMA table_info(agent_jobs)') as { name: string }[];
console.log('agent_jobs cols:', cols.length ? cols.map((c) => c.name).join(',') : '(none)');
const tables = sql.all("SELECT name FROM sqlite_master WHERE type='table'") as { name: string }[];
console.log('has agent_runs (legacy):', tables.some((t) => t.name === 'agent_runs'));
console.log('has agent_jobs (new):', tables.some((t) => t.name === 'agent_jobs'));
