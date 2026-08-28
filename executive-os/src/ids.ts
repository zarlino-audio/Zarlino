/**
 * ID generation. We generate IDs ourselves (UUID v4) so the system never
 * depends on DB-specific lastRowId semantics — important because the same
 * store code runs on DO SQLite (this.sql) and node:sqlite in tests.
 */

export function newId(prefix?: string): string {
  const uuid = crypto.randomUUID();
  return prefix ? `${prefix}_${uuid}` : uuid;
}

export function newSessionId(): string {
  return `sess_${crypto.randomUUID()}`;
}

export function nowIso(): string {
  return new Date().toISOString();
}

export function isoDaysFromNow(days: number): string {
  return new Date(Date.now() + days * 86_400_000).toISOString();
}

export function shortId(): string {
  return crypto.randomUUID().slice(0, 8);
}
