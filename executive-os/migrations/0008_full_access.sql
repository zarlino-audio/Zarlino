-- Zarlino Executive OS — migration v8: full access.
-- Mirrors FULL_ACCESS_SQL in src/db/schema.ts (applied by the migration runner).
-- Founder-granted flag: when ON, the Executive runs virtually everything
-- autonomously (deploy, HIGH-risk reversible actions, spending) while hard
-- gates (delete, ownership transfer, doctrine changes, CRITICAL risk,
-- irreversible HIGH, and every emergency switch) still hold. Defaults 0 (OFF).

ALTER TABLE executive_state ADD COLUMN full_access INTEGER NOT NULL DEFAULT 0;
