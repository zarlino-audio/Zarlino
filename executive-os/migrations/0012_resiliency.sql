-- Zarlino Executive OS — migration v12: resiliency columns.
-- Mirrors RESILIENCY_SQL in src/db/schema.ts (applied by the migration runner).
--
-- Surviving machine restarts / DO wakes mid-task:
--   agent_jobs.checkpoint_json   — durable tool-loop transcript snapshot so a
--                                  long-horizon run resumes where it stopped.
--   agent_jobs.last_heartbeat    — liveness; stale RUNNING jobs get reaped so
--                                  they can be continued after a DO restart.
--   local_tasks.lease_expires_at — claim lease; an offline machine releases
--                                  its lease so another daemon re-claims.
--   local_tasks.meta_json        — structured brief (goal/context/acceptance).

ALTER TABLE agent_jobs ADD COLUMN checkpoint_json TEXT;
ALTER TABLE agent_jobs ADD COLUMN last_heartbeat TEXT;
ALTER TABLE local_tasks ADD COLUMN lease_expires_at TEXT;
ALTER TABLE local_tasks ADD COLUMN meta_json TEXT;
