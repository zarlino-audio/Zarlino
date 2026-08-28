/**
 * Decision engine (spec §6).
 *
 * Every significant decision passes through the standard pipeline:
 *
 * REQUEST → CLASSIFY → IDENTIFY OBJECTIVE → LOAD STATE → LOAD MEMORY →
 * LOAD DOCTRINE → IDENTIFY CONSTRAINTS → GENERATE OPTIONS → CEO ANALYSIS →
 * CFO ANALYSIS → CTO ANALYSIS → RISK ANALYSIS → CHIEF-OF-STAFF SYNTHESIS →
 * SELECT ACTION → AUTHORITY CHECK → EXECUTE / ESCALATE → LOG DECISION →
 * MEASURE OUTCOME.
 *
 * The engine is deterministic orchestration over:
 *   - a Reasoner (LLM-driven lens analyses via the ModelGateway),
 *   - state/memory/doctrine providers (from the durable store),
 *   - the authority engine (backend-enforced permissions),
 *   - an approval queue (escalation path).
 *
 * It returns ONE consolidated executive recommendation — the Founder never
 * receives four contradictory answers.
 */

import { newId, nowIso } from "../../ids";
import { canTransition, createApprovalRequest, transitionApproval } from "../approvals/queue";
import { evaluateAction, type ActionSpec, type AuthorityContext, type AuthorityDecision } from "../authority/engine";
import { classifyRisk, type RiskInput } from "./risk";
import type {
  ApprovalRequest,
  AuditEvent,
  Decision,
  DecisionCategory,
  DecisionOption,
  DoctrineRule,
  Memory,
  Risk,
  Source,
} from "../../types";

// ---------------------------------------------------------------------------
// Providers the engine reads from
// ---------------------------------------------------------------------------

export interface ExecutiveContext {
  objectiveId?: string;
  objectiveTitle?: string;
  /** Current operational state facts (L1 operational memory). */
  stateFacts: Array<{ key: string; value: string | number | null; confidence: string }>;
  /** Relevant retrieved memory entries (L2 institutional). */
  memory: Memory[];
  /** Active Founder Doctrine (L3). */
  doctrine: DoctrineRule[];
  /** Budget available for spend decisions. */
  availableBudget?: number;
}

// ---------------------------------------------------------------------------
// Reasoner — the LLM-dependent part (implemented by the Agent via ModelGateway)
// ---------------------------------------------------------------------------

export type Lens = "ceo" | "cfo" | "cto" | "risk";

export interface Classification {
  intent: "decision" | "query" | "command";
  category: DecisionCategory;
  title: string;
}

export interface Synthesis {
  recommendedAction: string;
  confidence: number; // 0..1
  expectedOutcome: string;
  executionPlan: string[];
  successMetric: string;
  reviewDate?: string;
  uncertainties: string[];
}

export interface DecisionReasoner {
  classify(problem: string, categoryHint?: DecisionCategory): Promise<Classification>;
  analyzeLens(lens: Lens, problem: string, context: ExecutiveContext, options: DecisionOption[]): Promise<string>;
  synthesize(problem: string, context: ExecutiveContext, options: DecisionOption[], lensAssessments: Record<Lens, string>): Promise<Synthesis>;
  /** Generate options when the founder did not provide them. */
  generateOptions(problem: string, context: ExecutiveContext): Promise<DecisionOption[]>;
}

// ---------------------------------------------------------------------------
// Persistence sink
// ---------------------------------------------------------------------------

export interface EngineSink {
  insertDecision(d: Decision): void;
  insertApproval(a: ApprovalRequest): void;
  insertRisk(r: Risk): void;
  insertAudit(e: AuditEvent): void;
  createTasksFromDecision(d: Decision): void;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

export interface DecisionRequest {
  problem: string;
  categoryHint?: DecisionCategory;
  title?: string;
  proposedAction?: string;
  estimatedCost?: number;
  options?: DecisionOption[];
  objectiveId?: string;
  source?: Source;
  sessionId?: string;
  /** Skip lens reasoning and go straight to synthesis (cheap operational path). */
  fastPath?: boolean;
}

export interface DecisionResult {
  decision: Decision;
  authority: AuthorityDecision;
  approval?: ApprovalRequest;
  lensAssessments: Record<Lens, string>;
  classification: Classification;
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

export interface DecisionEngineDeps {
  reasoner: DecisionReasoner;
  sink: EngineSink;
  authority: (spec: ActionSpec) => AuthorityContext;
  /** Look up the canonical action spec for the proposed action, if known. */
  actionSpecFor?: (proposedAction: string, request: DecisionRequest) => ActionSpec | undefined;
}

function buildRiskEntities(risks: Array<{ title: string; level: string; description?: string; category: string }>, source: Source): Risk[] {
  const now = nowIso();
  return risks.map((r) => ({
    id: newId("risk"),
    created_at: now,
    updated_at: now,
    source,
    title: r.title,
    description: r.description,
    level: r.level as Risk["level"],
    category: r.category,
    status: "OPEN" as const,
  }));
}

export class DecisionEngine {
  constructor(private readonly deps: DecisionEngineDeps) {}

  async run(request: DecisionRequest, contextOverride?: ExecutiveContext): Promise<DecisionResult> {
    const source = request.source ?? "founder";

    // 1. Classify.
    const classification = await this.deps.reasoner.classify(request.problem, request.categoryHint);

    // 2. Identify objective + load context.
    const context = contextOverride ?? (await this.loadContext(request));

    // 3. Options.
    const options = request.options && request.options.length > 0
      ? request.options
      : await this.deps.reasoner.generateOptions(request.problem, context);

    // 4. Lens analyses.
    const lensAssessments: Record<Lens, string> = { ceo: "", cfo: "", cto: "", risk: "" };
    const lenses: Lens[] = ["ceo", "cfo", "cto", "risk"];
    for (const lens of lenses) {
      lensAssessments[lens] = await this.deps.reasoner.analyzeLens(lens, request.problem, context, options);
    }

    // 5. Chief-of-Staff synthesis — one consolidated recommendation.
    const synthesis = await this.deps.reasoner.synthesize(
      request.problem,
      context,
      options,
      lensAssessments,
    );

    // 6. Authority check on the proposed action.
    const spec: ActionSpec = this.deps.actionSpecFor
      ? (this.deps.actionSpecFor(request.proposedAction ?? "executive.create_decision", request) ?? {
          name: request.proposedAction ?? "executive.create_decision",
          permission: "execute",
          description: request.proposedAction ?? "Executing the recommended action",
          riskLevel: classifyRisk({
            financialImpact: request.estimatedCost,
            reversible: options.some((o) => o.reversible) || !request.estimatedCost,
          }).level,
          costEstimate: request.estimatedCost,
          reversible: true,
          auditRequired: true,
        })
      : {
          name: request.proposedAction ?? "executive.create_decision",
          permission: "execute",
          description: request.proposedAction ?? "Executing the recommended action",
          riskLevel: classifyRisk({
            financialImpact: request.estimatedCost,
            reversible: options.some((o) => o.reversible) || !request.estimatedCost,
          }).level,
          costEstimate: request.estimatedCost,
          reversible: true,
          auditRequired: true,
        };

    const authority = evaluateAction(spec, this.deps.authority(spec));

    // 7. Build the decision record.
    const now = nowIso();
    const riskInput: RiskInput = {
      financialImpact: request.estimatedCost,
      reversible: options.some((o) => o.reversible),
      legal: false,
      customerImpact: spec.riskLevel === "HIGH" || spec.riskLevel === "CRITICAL",
      productionImpact: spec.permission === "deploy",
      securityImpact: spec.permission === "delete" || spec.permission === "ownership_transfer",
    };
    const riskClass = classifyRisk(riskInput);
    const risks = buildRiskEntities(
      [{ title: `Risk of: ${request.problem.slice(0, 80)}`, level: riskClass.level, category: classification.category, description: riskClass.factors.join("; ") }],
      source,
    );

    const decision: Decision = {
      id: newId("dec"),
      created_at: now,
      updated_at: now,
      source,
      title: request.title ?? classification.title,
      problem: request.problem,
      objective_id: context.objectiveId,
      status: authority.verdict === "APPROVAL" ? "ESCALATED" : authority.verdict === "DENY" ? "REJECTED" : "APPROVED",
      category: classification.category,
      confidence: synthesis.confidence,
      known_facts_json: JSON.stringify(context.stateFacts.filter((f) => f.confidence === "FACT").map((f) => `${f.key}: ${f.value}`)),
      unknowns_json: JSON.stringify(context.stateFacts.filter((f) => f.confidence === "UNKNOWN").map((f) => f.key)),
      constraints_json: JSON.stringify(this.deriveConstraints(context)),
      options_json: JSON.stringify(options),
      ceo_assessment: lensAssessments.ceo,
      cfo_assessment: lensAssessments.cfo,
      cto_assessment: lensAssessments.cto,
      risk_assessment: lensAssessments.risk,
      risks_json: JSON.stringify(risks),
      recommended_action: synthesis.recommendedAction,
      expected_outcome: synthesis.expectedOutcome,
      authority_required: spec.permission,
      approval_required: authority.verdict === "APPROVAL",
      execution_plan_json: JSON.stringify(synthesis.executionPlan),
      success_metric: synthesis.successMetric,
      review_date: synthesis.reviewDate,
      outcome_status: "PENDING",
      model_trace_json: JSON.stringify({ uncertainties: synthesis.uncertainties, riskScore: riskClass.score, lensCount: 4 }),
    };

    // 8. Persist decision + risks + audit.
    this.deps.sink.insertDecision(decision);
    for (const r of risks) this.deps.sink.insertRisk(r);
    this.deps.sink.insertAudit({
      id: newId("aud"),
      created_at: now,
      updated_at: now,
      source: "executive",
      actor: "executive",
      action: "decision.create",
      entity_type: "decision",
      entity_id: decision.id,
      detail_json: JSON.stringify({ verdict: authority.verdict, category: classification.category }),
      verdict: authority.verdict,
    });

    // 9. Escalate → create approval request; else execute.
    let approval: ApprovalRequest | undefined;
    if (authority.verdict === "APPROVAL") {
      approval = createApprovalRequest({
        action_label: request.proposedAction ?? synthesis.recommendedAction,
        reason: request.problem,
        expected_upside: synthesis.expectedOutcome,
        max_downside: request.estimatedCost ? `$${request.estimatedCost}` : "unknown",
        budget_available: context.availableBudget !== undefined ? `$${context.availableBudget}` : "unknown",
        recommendation: "APPROVE",
        decision_id: decision.id,
        requested_by: "executive",
        source,
      });
      this.deps.sink.insertApproval(approval);
    } else {
      // Autonomous execution → Chief of Staff converts the decision into tasks.
      this.deps.sink.createTasksFromDecision(decision);
    }

    return { decision, authority, approval, lensAssessments, classification };
  }

  private async loadContext(request: DecisionRequest): Promise<ExecutiveContext> {
    // In the real agent, context is loaded from the store (state, memory,
    // doctrine). The sink can also provide context; here we use a minimal
    // context derived from the request. The Agent overrides this by
    // constructing the context before calling run().
    return {
      objectiveId: request.objectiveId,
      objectiveTitle: undefined,
      stateFacts: [],
      memory: [],
      doctrine: [],
      availableBudget: undefined,
    };
  }

  private deriveConstraints(context: ExecutiveContext): string[] {
    const constraints: string[] = [];
    if (context.availableBudget !== undefined) {
      constraints.push(`Available budget: $${context.availableBudget}`);
    }
    for (const rule of context.doctrine) {
      constraints.push(`Doctrine ${rule.code}: ${rule.principle}`);
    }
    return constraints;
  }
}

// Re-export helpers used by the agent layer.
export { classifyRisk };
