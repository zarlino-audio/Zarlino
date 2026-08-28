-- Zarlino Executive OS — migration v5: delegate mode.
-- Mirrors DELEGATE_MODE_SQL in src/db/schema.ts (applied by the migration runner).
-- Founder-granted flag: when ON, the Executive may autonomously execute real
-- work (deployments, MEDIUM-risk reversible actions). Defaults to 0 (OFF).

ALTER TABLE executive_state ADD COLUMN delegate_mode INTEGER NOT NULL DEFAULT 0;
