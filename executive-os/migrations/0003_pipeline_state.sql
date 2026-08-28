-- Zarlino Executive OS — migration v3: pipeline state sync.
-- Mirrors PIPELINE_STATE_SQL in src/db/schema.ts (applied by the migration runner).
-- Verified from pipeline/ZScorch/STATUS.md and pipeline/ZTame/STATUS.md (2026-08-24).

-- ZScorch: gates 1-9 passed; gate 10 in progress (ready_for_release, awaiting Founder).
UPDATE pipeline_gates SET status = 'PASSED', updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now')
 WHERE plugin_id = 'ZScorch' AND gate_number BETWEEN 1 AND 9;
UPDATE pipeline_gates SET status = 'IN_PROGRESS',
       note = 'ready_for_release — awaiting Khritical approval (Gate 10)',
       updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now')
 WHERE plugin_id = 'ZScorch' AND gate_number = 10;

-- ZTame: gates 1-9 passed; gate 10 in progress (rework_complete, P8 open).
UPDATE pipeline_gates SET status = 'PASSED', updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now')
 WHERE plugin_id = 'ZTame' AND gate_number BETWEEN 1 AND 9;
UPDATE pipeline_gates SET status = 'IN_PROGRESS',
       note = 'rework_complete — P1-P7 verified; P8 suppression-quality audit awaiting Khritical re-review',
       updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now')
 WHERE plugin_id = 'ZTame' AND gate_number = 10;

-- Open tasks (deterministic ids).
INSERT OR IGNORE INTO tasks
 (id, title, description, status, priority, assignee_person_id, source, created_at, updated_at) VALUES
 ('tsk_ztame_p8',
  'ZTame P8 suppression-quality A/B listening test',
  'Run the documented A/B protocol (pipeline/ZTame/STATUS.md): sustained tone + resonant ringing, piano/plucked string, drums + pad; A/B vs bypass at 100% wet, sensitivity 50→90, range -12 dB. Report which symptom matches: (a) pumping/warbling on transients → add detection-path smoothing/peak-hold; (b) Selectivity knob inaudible → widen selectivity range or re-map the threshold formula.',
  'BLOCKED', 'P0', 'founder', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('tsk_ztame_signing',
  'ZTame production license key + code signing',
  'Replace the development license public-key placeholder in ZLicenseManager.h with the production key and configure code signing before public commercial distribution.',
  'OPEN', 'P1', NULL, 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('tsk_zscorch_gate10',
  'ZScorch Gate 10 — founder release decision',
  'Approve or reject ZScorch v1.0.0 release. Gates 1-9 all passed; release build + VST3/Standalone/Installer packages verified; production license key verified 2026-08-23.',
  'OPEN', 'P1', 'founder', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('tsk_zscorch_signing',
  'ZScorch code signing',
  'Configure code signing for the release build (deferred at Gate 7/9).',
  'OPEN', 'P2', NULL, 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now'));

-- Open risks.
INSERT OR IGNORE INTO risks
 (id, title, description, level, category, likelihood, impact, mitigation, status, owner_person_id, source, created_at, updated_at) VALUES
 ('risk_ztame_p8',
  'ZTame suppression quality (P8) unresolved',
  'Engineering audit (2026-08-15) found no DSP math bugs, but two candidates remain: (a) no temporal smoothing/peak-hold on the detection spectrum → pumping/warbling on transients; (b) Sensitivity/Selectivity overlap (Selectivity moves threshold only ±5.25 dB vs Sensitivity 12 dB).',
  'HIGH', 'QUALITY', 'MEDIUM', 'HIGH', 'Founder A/B listening test → rework identified stage (detection smoothing or sensitivity/selectivity remap).', 'OPEN', 'founder', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('risk_ztame_signing',
  'ZTame ships unsigned with dev license key',
  'ZLicenseManager.h still embeds the development public-key placeholder; release build is unsigned.',
  'HIGH', 'SECURITY', 'HIGH', 'HIGH', 'Embed production key and sign before commercial release; or ship as explicitly-disclosed trial/unsigned build.', 'OPEN', NULL, 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('risk_zscorch_signing',
  'ZScorch release build unsigned',
  'Code signing deferred at Gate 7/9; public distribution should be signed.',
  'MEDIUM', 'SECURITY', 'MEDIUM', 'MEDIUM', 'Configure signing certificate + CI signing before public distribution.', 'OPEN', NULL, 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('risk_golden_refs',
  'Golden-reference audio files missing',
  'Gate 5 golden-reference files not yet supplied for ZScorch or ZTame (non-blocking for initial release).',
  'LOW', 'QUALITY', 'LOW', 'LOW', 'Add golden-reference captures to the QA suite post-release.', 'OPEN', NULL, 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now'));

-- Pending approval: ZScorch Gate 10 release.
INSERT OR IGNORE INTO approval_requests
 (id, action_label, reason, expected_upside, max_downside, budget_available, recommendation, status, requested_by, source, created_at, updated_at) VALUES
 ('appr_zscorch_gate10',
  'Approve ZScorch v1.0.0 release (Gate 10)',
  'All gates 1-9 passed; release build + VST3/Standalone/Installer packages verified; production license key verified. Awaiting founder decision to ship.',
  'First commercial product shipped; revenue per unit (target unit economics).',
  'Shipping before full DAW host matrix testing; unsigned build.',
  'No budget required', 'APPROVE', 'PENDING', 'executive', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now'));
