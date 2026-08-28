-- Zarlino Executive OS — migration v2: founder credentials + sessions.
-- Mirrors AUTH_SCHEMA_SQL in src/db/schema.ts (applied by the migration runner).

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
