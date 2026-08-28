-- Zarlino Executive OS — migration v11: uploaded files.
-- Mirrors UPLOADED_FILES_SQL in src/db/schema.ts (applied by the migration runner).
-- Files the Founder uploads from the web console (screenshots, specs, audio,
-- documents) that the Executive can inspect — images via vision, text directly,
-- others by metadata. Content is stored base64 (bounded to a few MB per file).

CREATE TABLE IF NOT EXISTS uploaded_files (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  mime TEXT,
  size_bytes INTEGER,
  data_base64 TEXT,
  kind TEXT NOT NULL DEFAULT 'other',
  source TEXT NOT NULL DEFAULT 'founder',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_uploaded_files_created ON uploaded_files(created_at);
