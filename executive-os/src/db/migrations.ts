/**
 * Migration runner (spec §31).
 *
 * Applies sequential, versioned migrations. Fresh databases run every
 * migration in order; already-migrated databases (e.g. the deployed DO) only
 * run the ones after their current version. Never mutate historical records —
 * append corrections (spec §21).
 */

import { AGENT_RUNS_SQL, AUTH_SCHEMA_SQL, AUTO_RUN_SQL, CHAT_STREAM_SQL, DELEGATE_MODE_SQL, FULL_ACCESS_SQL, INITIAL_SCHEMA_SQL, LOCAL_TASKS_SQL, PERSON_ROLES_SQL, PIPELINE_STATE_SQL, PLANNER_SQL, PRODUCT_METRICS_SQL, RESILIENCY_SQL, SCHEMA_VERSION, SETTINGS_SQL, SHIP_STATE_SQL, UPLOADED_FILES_SQL } from "./schema";
import type { Sql } from "./sql";

export interface MigrationRecord {
  version: number;
  applied_at: string;
}

const MIGRATIONS: Array<{ version: number; sql: string }> = [
  { version: 1, sql: INITIAL_SCHEMA_SQL },
  { version: 2, sql: AUTH_SCHEMA_SQL },
  { version: 3, sql: PIPELINE_STATE_SQL },
  { version: 4, sql: PERSON_ROLES_SQL },
  { version: 5, sql: DELEGATE_MODE_SQL },
  { version: 6, sql: CHAT_STREAM_SQL },
  { version: 7, sql: SHIP_STATE_SQL },
  { version: 8, sql: FULL_ACCESS_SQL },
  { version: 9, sql: AGENT_RUNS_SQL },
  { version: 10, sql: LOCAL_TASKS_SQL },
  { version: 11, sql: UPLOADED_FILES_SQL },
  { version: 12, sql: RESILIENCY_SQL },
  { version: 13, sql: SETTINGS_SQL },
  { version: 14, sql: PLANNER_SQL },
  { version: 15, sql: AUTO_RUN_SQL },
  { version: 16, sql: PRODUCT_METRICS_SQL },
];

export function ensureMigrationsTable(sql: Sql): void {
  sql.run(`CREATE TABLE IF NOT EXISTS schema_migrations (
    version INTEGER PRIMARY KEY,
    applied_at TEXT NOT NULL
  )`);
}

export function currentVersion(sql: Sql): number {
  ensureMigrationsTable(sql);
  const rows = sql.all<{ version: number }>("SELECT MAX(version) AS version FROM schema_migrations");
  return rows[0]?.version ?? 0;
}

/** Apply pending migrations (any version newer than the current one). Idempotent. */
export function migrate(sql: Sql): MigrationRecord[] {
  const applied: MigrationRecord[] = [];
  ensureMigrationsTable(sql);

  const current = currentVersion(sql);
  for (const m of MIGRATIONS) {
    if (m.version > current) {
      sql.transaction(() => {
        sql.exec(m.sql);
        sql.run("INSERT INTO schema_migrations (version, applied_at) VALUES (?, ?)", m.version, new Date().toISOString());
      });
      applied.push({ version: m.version, applied_at: new Date().toISOString() });
    }
  }

  return applied;
}

export { SCHEMA_VERSION };
