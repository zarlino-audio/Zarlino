-- Migration v13: settings (key/value overrides — AI budget + provider balance cache)
-- Lets the Founder extend the AI budget from the UI (survives redeploys) and
-- caches the AI provider balance so we don't hammer the balance endpoint.
CREATE TABLE IF NOT EXISTS settings (
  key TEXT PRIMARY KEY,
  value TEXT,
  updated_at TEXT NOT NULL
);
