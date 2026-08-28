-- Zarlino Executive OS — migration v6: chat stream buffer.
-- Mirrors CHAT_STREAM_SQL in src/db/schema.ts (applied by the migration runner).
-- Streamed chat reply chunks are buffered here so the Worker can poll them
-- over DO RPC (async generators do not survive RPC).

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
