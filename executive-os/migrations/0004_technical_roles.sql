-- Zarlino Executive OS — migration v4: onboard technical roles.
-- Mirrors PERSON_ROLES_SQL in src/db/schema.ts (applied by the migration runner).
-- INSERT OR IGNORE keeps it idempotent; the seed also provides these on fresh DBs.

INSERT OR IGNORE INTO persons (id, name, role, source, created_at, updated_at) VALUES
 ('person_role_product_manager', 'Product Manager', 'Gate 1 — market analysis, spec authoring', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_architect', 'Architect', 'Gate 2 — signal chain design, algorithm selection', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_dsp_engineer', 'DSP Engineer', 'Gate 3 — C++/JUCE DSP implementation', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_ui_engineer', 'UI Engineer', 'Gate 3 — editor UI, Look & Feel', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_parameter_engineer', 'Parameter Engineer', 'Gate 3 — APVTS, presets, state', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_code_reviewer', 'Code Reviewer', 'Gate 4 — bug discovery, thread/memory safety', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_qa_engineer', 'QA Engineer', 'Gate 5 — tests, fuzz, golden references', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_performance_engineer', 'Performance Engineer', 'Gate 6 — CPU/memory/latency budgets', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_security_licensing_engineer', 'Security & Licensing Engineer', 'Gate 7 — license compliance, signing', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_documentation_writer', 'Documentation Writer', 'Gate 8 — manuals, parameter reference', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_release_engineer', 'Release Engineer', 'Gate 9 — CMake, CI, packaging', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now')),
 ('person_role_research_engineer', 'Research Engineer', 'Web research, competitive analysis', 'migration', strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now'));
