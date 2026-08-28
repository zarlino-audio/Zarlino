/**
 * Sql abstraction.
 *
 * The Executive OS runs on two SQLite runtimes:
 *   - Production: the Durable Object's embedded SQLite, accessed via the
 *     Agents SDK tagged-template `this.sql`.
 *   - Tests / local tooling: Node's built-in `node:sqlite` DatabaseSync.
 *
 * This module defines a single `Sql` interface both adapters satisfy, so the
 * entire DB layer is portable and fully unit-testable without Cloudflare.
 */

export type SqlParam = string | number | bigint | Uint8Array | null | undefined | boolean;

export interface SqlRunResult {
  changes: number;
}

export interface Sql {
  /** Run a SELECT and return all rows. Use `?` placeholders. */
  all<T = Record<string, unknown>>(sql: string, ...params: SqlParam[]): T[];
  /** Run an INSERT/UPDATE/DELETE/DDL statement. */
  run(sql: string, ...params: SqlParam[]): SqlRunResult;
  /** Execute raw SQL that may contain multiple statements (DDL/migrations). */
  exec(sql: string): void;
  /** Run a transaction over the given function. */
  transaction<T>(fn: () => T): T;
}

/**
 * Adapter for the Agents SDK `this.sql` tagged-template function.
 *
 * `this.sql` is called as a tagged template: sql`SELECT * FROM t WHERE id = ${id}`.
 * We emulate that by splitting the SQL on `?` markers to reconstruct the
 * TemplateStringsArray, then passing the params as the interpolated values.
 */
export function fromDoSql(
  doSql: (strings: TemplateStringsArray, ...values: any[]) => unknown,
): Sql {
  function template(parts: string[]): TemplateStringsArray {
    const arr = [...parts] as unknown as TemplateStringsArray;
    Object.defineProperty(arr, "raw", { value: [...parts] });
    return arr;
  }

  return {
    all<T = Record<string, unknown>>(sql: string, ...params: SqlParam[]): T[] {
      const parts = sql.split("?");
      const rows = doSql(template(parts), ...params) as T[];
      return Array.isArray(rows) ? rows : ([] as T[]);
    },
    run(sql: string, ...params: SqlParam[]): SqlRunResult {
      const parts = sql.split("?");
      doSql(template(parts), ...params);
      return { changes: 1 };
    },
    exec(sql: string): void {
      doSql(template([sql]));
    },
    transaction<T>(fn: () => T): T {
      // DO SQLite runs each statement in its own implicit transaction; the
      // Agent's single-threaded execution already serializes our operations.
      return fn();
    },
  };
}

/** Adapter for Node's built-in node:sqlite (used in tests and local tools). */
export function fromNodeSqlite(db: {
  exec(sql: string): void;
  prepare(sql: string): {
    run(...params: unknown[]): { changes: number | bigint };
    all(...params: unknown[]): unknown[];
  };
}): Sql {
  return {
    all<T = Record<string, unknown>>(sql: string, ...params: SqlParam[]): T[] {
      return db.prepare(sql).all(...params) as T[];
    },
    run(sql: string, ...params: SqlParam[]): SqlRunResult {
      const r = db.prepare(sql).run(...params);
      return { changes: Number(r.changes) };
    },
    exec(sql: string): void {
      db.exec(sql);
    },
    transaction<T>(fn: () => T): T {
      db.exec("BEGIN");
      try {
        const result = fn();
        db.exec("COMMIT");
        return result;
      } catch (err) {
        db.exec("ROLLBACK");
        throw err;
      }
    },
  };
}

/**
 * Create an in-memory node:sqlite-backed Sql for tests.
 * Imported lazily so worker bundles never pull in node:sqlite.
 */
export function createMemorySql(): Sql {
  // eslint-disable-next-line @typescript-eslint/no-var-requires
  const { DatabaseSync } = require("node:sqlite") as typeof import("node:sqlite");
  const db = new DatabaseSync(":memory:");
  return fromNodeSqlite(db);
}

/** JSON helpers for SQL columns. */
export function toJson(value: unknown): string {
  return JSON.stringify(value);
}

export function fromJson<T = unknown>(value: string | null | undefined, fallback: T): T {
  if (!value) return fallback;
  try {
    return JSON.parse(value) as T;
  } catch {
    return fallback;
  }
}
