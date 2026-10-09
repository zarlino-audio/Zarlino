# Documentation index

Documentation hub for the Zarlino Audio plugin monorepo. Code lives in `../plugins/`; everything
written lives in this folder, in [`../specs/`](../specs/) (product specifications) or in
[`../company-readiness/`](../company-readiness/) (company operations).

## Product
- `product/rack-ai/` — RACK/AI master spec (v2), spec v1.2, extracted text and the implementation
  summary (scaffold stage — never compiled).
- Product specifications for the plugin lineup live in `../specs/`:
  - `ZTame_Gate1_Spec_and_Differentiators.md` + `ZTame_Gate1_P8_TestReadiness_Checklist.md`
  - `ZTame_v1.1_rework_design.md` + `ZTame_v1.2_zero-latency_design.md`
  - `ZVocal_Gate1_Spec_and_Differentiators.md`
  - `code-signing-pipeline.md`

## Platform
- `platform/openclaw-zarlino-orchestrator-spec.md` — orchestrator specification.
- `platform/zarlino-qa-platform-spec-2.md` — 20-plugin QA platform plan.
- `platform/cloud-migration/` — cloud-only MCP migration: the recovered mandate plus the
  infrastructure inventory and GitHub/secret-audit trackers.

## Organization
- `organization/aoi-technical-spec.md` — Artificial Organizational Intelligence technical spec
  (implementation-facing version of the AOrgI book).
- `organization/aorgi-retrofit-plan.md` — AOrgI retrofit plan (all phases 1–5 operational as of
  2026-09-08).

## Operations
- `operations/BUILD_AND_RELEASE.md` — build, test and local release procedure for the plugins.

## Reports
- `reports/ZScorch_AB_Render_Report.md` — ZScorch A/B render verification report.
- `reports/presencefield-real-ab-report.md` — PresenceField real-audio A/B report.

## Company
- `../company-readiness/` — founder gap & runway audit, gap-closure ledger, five-group org audit,
  marketing/affiliate playbook, vendor & card register, operating plan, VM setup kit (`vm-setup/`),
  skills mirrors (`skills/`) and the continuity kit.

## Tools
- `../tools/TOOL_MANIFEST.md` — registry of every helper tool (what it does, when it was created,
  whether it is obsolete). Keep the `.md` and `.json` in sync when adding tools.
