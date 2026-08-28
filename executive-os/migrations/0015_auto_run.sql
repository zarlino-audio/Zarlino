-- Auto-run tasks (migration v15): tasks that don't need Founder approval are
-- executed autonomously by the Executive (bounded long-horizon run per task,
-- auto-marked DONE on completion). auto_run = 1 (default) means eligible;
-- 0 means manual only. The agent_jobs.task_id index makes the
-- has-running-run-for-task check cheap.
ALTER TABLE tasks ADD COLUMN auto_run INTEGER NOT NULL DEFAULT 1;
CREATE INDEX IF NOT EXISTS idx_tasks_auto_run ON tasks(auto_run, status);
CREATE INDEX IF NOT EXISTS idx_agent_jobs_task ON agent_jobs(task_id);
-- Tasks that exist ONLY for a Founder decision never auto-run (a human must
-- pick: release, A/B listening verdict, approval). Everything else is free.
UPDATE tasks SET auto_run = 0
 WHERE id IN ('tsk_zscorch_gate10', 'tsk_ztame_p8')
    OR title LIKE '%founder%decision%'
    OR title LIKE '%A/B listening%';
