/**
 * Seed data (spec §4, §5).
 *
 * Seeds only real, verifiable facts about Zarlino from the repository:
 *   - the actual product catalog (specs/catalog.md),
 *   - the actual Founder Doctrine (spec §5, config.ts),
 *   - the actual 10-gate pipeline (orchestrator/pipeline.md, absorbed),
 *   - the actual organization.
 *
 * Financial figures, objectives, and personal details that are not
 * verifiable are intentionally NOT seeded — they are UNKNOWN until the
 * Founder provides them.
 */

import { SEED_DOCTRINE } from "../config";
import { newId, nowIso } from "../ids";
import type { ExecutiveStore } from "../db/store";
import type { Product, ProductStatus } from "../types";

export const PLUGIN_PIPELINE_GATES: Array<{ number: number; name: string; owner: string }> = [
  { number: 1, name: "Spec Check", owner: "Product Manager" },
  { number: 2, name: "Architecture", owner: "Architect" },
  { number: 3, name: "Implementation", owner: "DSP/UI/Parameter Engineer" },
  { number: 4, name: "Code Review", owner: "Code Reviewer" },
  { number: 5, name: "QA", owner: "QA Engineer" },
  { number: 6, name: "Performance", owner: "Performance Engineer" },
  { number: 7, name: "Security & Licensing", owner: "Security & Licensing Engineer" },
  { number: 8, name: "Documentation", owner: "Documentation Writer" },
  { number: 9, name: "Packaging", owner: "Release Engineer" },
  { number: 10, name: "Human Release Gate", owner: "Human (Founder)" },
];

interface CatalogEntry {
  name: string;
  category: string;
  status: ProductStatus;
  priority: number;
}

/** Verified from specs/catalog.md (2026-08). */
const CATALOG: CatalogEntry[] = [
  { name: "ZScorch", category: "Multi-Band Saturation/Exciter", status: "SHIPPED", priority: 1 },
  { name: "ZTame", category: "Resonance Suppressor", status: "SHIPPED", priority: 2 },
  { name: "ZVocals", category: "Vocal Channel Strip", status: "CONCEPT", priority: 3 },
  { name: "PresenceField", category: "Stereo Widener", status: "CONCEPT", priority: 4 },
  { name: "ZBloom", category: "Excitement/Width", status: "PLANNED", priority: 5 },
  { name: "ZForge", category: "Saturation", status: "PLANNED", priority: 6 },
  { name: "ZSpace", category: "Reverb", status: "PLANNED", priority: 7 },
  { name: "ZPhase", category: "Phase/Modulation", status: "PLANNED", priority: 8 },
  { name: "ZPunch", category: "Transient Shaping", status: "PLANNED", priority: 9 },
  { name: "ZGlue", category: "Bus Compression", status: "PLANNED", priority: 10 },
  { name: "ZLift", category: "Tilt EQ / Tone", status: "PLANNED", priority: 11 },
  { name: "ZEcho", category: "Delay", status: "PLANNED", priority: 12 },
  { name: "ZTune", category: "Pitch Correction", status: "PLANNED", priority: 13 },
  { name: "ZScope", category: "Analysis/Metering", status: "PLANNED", priority: 14 },
  { name: "ZScape", category: "Ambience/Reverb", status: "PLANNED", priority: 15 },
  { name: "ZFuse", category: "Multi-Band Dynamics", status: "PLANNED", priority: 16 },
  { name: "ZVault", category: "Mastering Limiter", status: "PLANNED", priority: 17 },
  { name: "ZWave", category: "Wavetable Modulator", status: "PLANNED", priority: 18 },
  { name: "ZCarve", category: "Dynamic EQ", status: "PLANNED", priority: 19 },
  { name: "ZMaster", category: "Mastering Suite", status: "PLANNED", priority: 20 },
  { name: "ZMojo", category: "Analog Color/Saturation", status: "PLANNED", priority: 21 },
  { name: "ZClear", category: "De-Noise/De-Reverb", status: "PLANNED", priority: 22 },
  { name: "MyFirstEQ", category: "Parametric EQ", status: "CONCEPT", priority: 23 },
  { name: "RACK_AI", category: "AI Plugin Host", status: "CONCEPT", priority: 24 },
];

/**
 * Real gate state for the plugins already inside the pipeline, verified from
 * pipeline/ZScorch/STATUS.md and pipeline/ZTame/STATUS.md (2026-08-24).
 * ZScorch: gates 1-9 passed, Gate 10 pending (ready_for_release).
 * ZTame: gates 1-9 passed, Gate 10 rework_complete (P8 audit open).
 */
function gateStatusFor(plugin: string, gate: number): { status: "NOT_STARTED" | "PASSED" | "IN_PROGRESS" | "SHIPPED"; note?: string } {
  if (plugin === "ZScorch" || plugin === "ZTame") {
    if (gate <= 9) return { status: "PASSED", note: "Verified in pipeline/STATUS.md (2026-08)" };
    // Both plugins are live on zarlinoaudio.com (v1.0.0).
    return { status: "SHIPPED", note: "Shipped — live on zarlinoaudio.com (v1.0.0)" };
  }
  return { status: "NOT_STARTED" };
}

/**
 * Seed the real open pipeline facts (tasks, risks, pending approval) that the
 * v3 migration also inserts. Each insert is guarded so a fresh database that
 * already ran migration v3 does not get duplicates (deterministic IDs).
 */
function seedPipelineFacts(store: ExecutiveStore, now: string): void {
  const tasks = [
    {
      id: "tsk_ztame_p8",
      title: "ZTame P8 suppression-quality A/B listening test",
      description:
        "Run the documented A/B protocol (pipeline/ZTame/STATUS.md): sustained tone + resonant ringing, piano/plucked string, drums + pad; A/B vs bypass at 100% wet, sensitivity 50→90, range -12 dB. Report which symptom matches: (a) pumping/warbling on transients → add detection-path smoothing/peak-hold; (b) Selectivity knob inaudible → widen selectivity range or re-map the threshold formula.",
      status: "BLOCKED" as const,
      priority: "P0" as const,
      assignee: "founder",
      autoRun: false,
    },
    {
      id: "tsk_ztame_signing",
      title: "ZTame production license key + code signing",
      description:
        "Replace the development license public-key placeholder in ZLicenseManager.h with the production key and configure code signing before public commercial distribution.",
      status: "OPEN" as const,
      priority: "P1" as const,
      assignee: undefined,
    },
    {
      id: "tsk_zscorch_gate10",
      title: "ZScorch Gate 10 — founder release decision",
      description:
        "Approve or reject ZScorch v1.0.0 release. Gates 1-9 all passed; release build + VST3/Standalone/Installer packages verified; production license key verified 2026-08-23.",
      status: "OPEN" as const,
      priority: "P1" as const,
      assignee: "founder",
      autoRun: false,
    },
    {
      id: "tsk_zscorch_signing",
      title: "ZScorch code signing",
      description: "Configure code signing for the release build (deferred at Gate 7/9).",
      status: "OPEN" as const,
      priority: "P2" as const,
      assignee: undefined,
    },
  ];
  for (const t of tasks) {
    if (store.getTask(t.id)) continue;
    store.insertTask({
      id: t.id,
      created_at: now,
      updated_at: now,
      source: "seed",
      title: t.title,
      description: t.description,
      status: t.status,
      priority: t.priority,
      assignee_person_id: t.assignee,
      auto_run: (t as { autoRun?: boolean }).autoRun ?? true,
    });
  }

  const risks = [
    {
      id: "risk_ztame_p8",
      title: "ZTame suppression quality (P8) unresolved",
      description:
        "Engineering audit (2026-08-15) found no DSP math bugs, but two candidates remain: (a) no temporal smoothing/peak-hold on the detection spectrum → pumping/warbling on transients; (b) Sensitivity/Selectivity overlap (Selectivity moves threshold only ±5.25 dB vs Sensitivity 12 dB).",
      level: "HIGH" as const,
      category: "QUALITY",
      likelihood: "MEDIUM",
      impact: "HIGH",
      mitigation: "Founder A/B listening test → rework identified stage (detection smoothing or sensitivity/selectivity remap).",
      owner: "founder",
    },
    {
      id: "risk_ztame_signing",
      title: "ZTame ships unsigned with dev license key",
      description: "ZLicenseManager.h still embeds the development public-key placeholder; release build is unsigned.",
      level: "HIGH" as const,
      category: "SECURITY",
      likelihood: "HIGH",
      impact: "HIGH",
      mitigation: "Embed production key and sign before commercial release; or ship as explicitly-disclosed trial/unsigned build.",
      owner: undefined,
    },
    {
      id: "risk_zscorch_signing",
      title: "ZScorch release build unsigned",
      description: "Code signing deferred at Gate 7/9; public distribution should be signed.",
      level: "MEDIUM" as const,
      category: "SECURITY",
      likelihood: "MEDIUM",
      impact: "MEDIUM",
      mitigation: "Configure signing certificate + CI signing before public distribution.",
      owner: undefined,
    },
    {
      id: "risk_golden_refs",
      title: "Golden-reference audio files missing",
      description: "Gate 5 golden-reference files not yet supplied for ZScorch or ZTame (non-blocking for initial release).",
      level: "LOW" as const,
      category: "QUALITY",
      likelihood: "LOW",
      impact: "LOW",
      mitigation: "Add golden-reference captures to the QA suite post-release.",
      owner: undefined,
    },
  ];
  const existingRisks = new Set(store.openRisks().map((r) => r.id));
  for (const r of risks) {
    if (existingRisks.has(r.id)) continue;
    store.insertRisk({
      id: r.id,
      created_at: now,
      updated_at: now,
      source: "seed",
      title: r.title,
      description: r.description,
      level: r.level,
      category: r.category,
      likelihood: r.likelihood,
      impact: r.impact,
      mitigation: r.mitigation,
      status: "OPEN",
      owner_person_id: r.owner,
    });
  }

  if (!store.getApproval("appr_zscorch_gate10")) {
    store.insertApproval({
      id: "appr_zscorch_gate10",
      created_at: now,
      updated_at: now,
      source: "seed",
      action_label: "Approve ZScorch v1.0.0 release (Gate 10)",
      reason:
        "All gates 1-9 passed; release build + VST3/Standalone/Installer packages verified; production license key verified. Awaiting founder decision to ship.",
      expected_upside: "First commercial product shipped; revenue per unit (target unit economics).",
      max_downside: "Shipping before full DAW host matrix testing; unsigned build.",
      budget_available: "No budget required",
      recommendation: "APPROVE",
      status: "PENDING",
      requested_by: "executive",
    });
  }
}

/**
 * Seed the durable store if empty. Idempotent — safe to call from onStart.
 * Returns whether seeding happened.
 */
export function seedIfEmpty(store: ExecutiveStore): boolean {
  if (store.listActiveDoctrine().length > 0) return false;
  const now = nowIso();

  // Doctrine (L3 — Founder doctrine, protected).
  for (const d of SEED_DOCTRINE) {
    store.insertDoctrineRule({
      id: newId("doc"),
      created_at: now,
      updated_at: now,
      source: "seed",
      code: d.code,
      principle: d.principle,
      rationale: d.rationale,
      category: d.category,
      status: "ACTIVE",
      version: 1,
    });
  }

  // Organization.
  store.insertOrganization({
    id: "org_zarlino",
    created_at: now,
    updated_at: now,
    source: "seed",
    name: "Zarlino Audio",
    mission: "Professional audio plugins — saturation, resonance control, and mixing tools.",
  });

  // Founder profile (placeholder identity; Founder updates via Settings).
  store.upsertFounderProfile({ id: "founder", name: "The Founder", role: "Founder & Owner" });

  // Technical roles onboarded from the orchestrator's role roster. Tasks can be
  // assigned to these via assignee_person_id (e.g. person_role_dsp_engineer),
  // which makes delegation work end-to-end from the dashboard.
  const TECHNICAL_ROLES: Array<{ id: string; name: string; role: string }> = [
    { id: "person_role_product_manager", name: "Product Manager", role: "Gate 1 — market analysis, spec authoring" },
    { id: "person_role_architect", name: "Architect", role: "Gate 2 — signal chain design, algorithm selection" },
    { id: "person_role_dsp_engineer", name: "DSP Engineer", role: "Gate 3 — C++/JUCE DSP implementation" },
    { id: "person_role_ui_engineer", name: "UI Engineer", role: "Gate 3 — editor UI, Look & Feel" },
    { id: "person_role_parameter_engineer", name: "Parameter Engineer", role: "Gate 3 — APVTS, presets, state" },
    { id: "person_role_code_reviewer", name: "Code Reviewer", role: "Gate 4 — bug discovery, thread/memory safety" },
    { id: "person_role_qa_engineer", name: "QA Engineer", role: "Gate 5 — tests, fuzz, golden references" },
    { id: "person_role_performance_engineer", name: "Performance Engineer", role: "Gate 6 — CPU/memory/latency budgets" },
    { id: "person_role_security_licensing_engineer", name: "Security & Licensing Engineer", role: "Gate 7 — license compliance, signing" },
    { id: "person_role_documentation_writer", name: "Documentation Writer", role: "Gate 8 — manuals, parameter reference" },
    { id: "person_role_release_engineer", name: "Release Engineer", role: "Gate 9 — CMake, CI, packaging" },
    { id: "person_role_research_engineer", name: "Research Engineer", role: "Web research, competitive analysis" },
  ];
  const existingPersons = new Set(store.listPersons().map((p) => p.id));
  for (const role of TECHNICAL_ROLES) {
    if (existingPersons.has(role.id)) continue;
    store.insertPerson({
      id: role.id,
      created_at: now,
      updated_at: now,
      source: "seed",
      name: role.name,
      role: role.role,
    });
  }

  // Objectives — derived from the actual orchestrator mandate, editable.
  store.insertObjective({
    id: newId("obj"),
    created_at: now,
    updated_at: now,
    source: "seed",
    title: "Ship the Zarlino plugin catalog through the 10-gate pipeline",
    description: "Move ZScorch and ZTame to release, then the prototype line, per specs/catalog.md.",
    status: "ACTIVE",
    priority: 1,
  });
  store.insertObjective({
    id: newId("obj"),
    created_at: now,
    updated_at: now,
    source: "seed",
    title: "Protect cash and establish measurable unit economics",
    description: "Per Founder Doctrine D-003 and D-007: no spend without a measurable hypothesis.",
    status: "ACTIVE",
    priority: 2,
  });

  // Products — verified from specs/catalog.md.
  for (const p of CATALOG) {
    store.insertProduct(productOf(p, now));
  }

  // Policies (from the Executive OS spec §41).
  store.insertPolicy({
    id: newId("pol"),
    created_at: now,
    updated_at: now,
    source: "seed",
    code: "POL-AUTONOMY-001",
    title: "Initial Autonomy Policy",
    body: "READ/ANALYZE/RECOMMEND autonomous. CREATE internal task/document autonomous. Low-risk reversible action autonomous. Spending requires approval above threshold. Production deployment, pricing changes, hiring, contracts require approval. Data deletion, ownership/legal/financial transfers, and Founder Doctrine changes are never autonomous.",
    category: "GOVERNANCE",
    status: "ACTIVE",
    version: 1,
  });
  store.insertPolicy({
    id: newId("pol"),
    created_at: now,
    updated_at: now,
    source: "seed",
    code: "POL-DOCTRINE-001",
    title: "Doctrine Protection",
    body: "The model may never silently modify Founder Doctrine. Doctrine changes require Founder approval and are recorded as versioned, immutable history.",
    category: "GOVERNANCE",
    status: "ACTIVE",
    version: 1,
  });

  // Workstream: absorbed 10-gate pipeline (orchestrator/pipeline.md).
  const wsId = newId("ws");
  store.insertWorkstream({
    id: wsId,
    created_at: now,
    updated_at: now,
    source: "seed",
    name: "Plugin Factory Pipeline",
    description: "The 10-gate pipeline absorbed from /orchestrator. Each plugin progresses through Spec → Architecture → Implementation → Review → QA → Performance → Security → Docs → Packaging → Human Release.",
    kind: "PLUGIN_PIPELINE",
    status: "ACTIVE",
    config_json: JSON.stringify({ source: "../orchestrator/pipeline.md" }),
  });

  const activePlugins = ["ZScorch", "ZTame", "ZVocals", "PresenceField"];
  for (const plugin of activePlugins) {
    for (const gate of PLUGIN_PIPELINE_GATES) {
      const st = gateStatusFor(plugin, gate.number);
      store.insertPipelineGate({
        id: newId("gate"),
        created_at: now,
        updated_at: now,
        source: "seed",
        workstream_id: wsId,
        plugin_id: plugin,
        gate_number: gate.number,
        gate_name: gate.name,
        status: st.status,
        owner_role: gate.owner,
        fail_count: 0,
        note: st.note,
      });
    }
  }

  // Real open pipeline facts (tasks, risks, pending approval) — guarded so a
  // fresh DB that already ran migration v3 does not get duplicates.
  seedPipelineFacts(store, now);

  // ZScorch and ZTame are already live on zarlinoaudio.com — record the
  // shipment (product_status history + version). Idempotent on re-seed.
  store.shipProduct("prod_zscorch", { version: "1.0.0", note: "Shipped — live on zarlinoaudio.com (v1.0.0)" });
  store.shipProduct("prod_ztame", { version: "1.0.0", note: "Shipped — live on zarlinoaudio.com (v1.0.0)" });

  // Executive state row.
  store.ensureExecutiveState();
  store.updateExecutiveState({
    overall_status: "GREEN",
    current_objective_id: store.listObjectives("ACTIVE")[0]?.id,
    top_priorities_json: JSON.stringify(["Ship ZScorch", "Ship ZTame", "Protect cash"]),
    autonomy_mode: "CONTROLLED",
    delegate_mode: 0,
    full_access: 0,
  });

  return true;
}

function productOf(p: CatalogEntry, now: string): Product {
  return {
    id: `prod_${p.name.toLowerCase().replace(/[^a-z0-9]/g, "")}`,
    created_at: now,
    updated_at: now,
    source: "seed",
    name: p.name,
    category: p.category,
    status: p.status,
    priority: p.priority,
    metadata_json: JSON.stringify({ catalogSource: "specs/catalog.md" }),
  };
}
