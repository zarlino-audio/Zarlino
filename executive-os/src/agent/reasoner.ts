/**
 * GatewayReasoner — the LLM-driven part of the decision engine.
 *
 * Implements DecisionReasoner by composing context blocks (spec §26) and
 * calling the ModelGateway. Falls back to the mock provider when a provider
 * is not configured (local dev / tests without API keys) so the system still
 * runs end-to-end.
 */

import type { ModelGateway } from "../core/model/gateway";
import type {
  Classification,
  DecisionReasoner,
  ExecutiveContext,
  Lens,
  Synthesis,
} from "../core/decision/engine";
import type { DecisionCategory, DecisionOption } from "../types";
import {
  ceoLensPrompt,
  cfoLensPrompt,
  classificationPrompt,
  ctoLensPrompt,
  doctrineBlock,
  memoryBlock,
  objectiveBlock,
  optionsPrompt,
  riskLensPrompt,
  stateBlock,
  synthesisPrompt,
} from "./prompts";

/**
 * Deterministic command keywords handled by the Executive's command handler.
 * The classifier only routes to "command" when one of these is the LEADING
 * intent — the model over-triggers on imperatives ("hello", "reply with X")
 * and the substring "status" in prose ("check the website status") would
 * otherwise hijack natural chat into the canned overview.
 */
export const COMMAND_WORDS = new Set([
  "BRIEF", "STATUS", "OVERVIEW", "PRIORITIES", "RISKS", "DECISIONS",
  "FINANCE", "ENGINEERING", "PRODUCTS", "MARKETING", "TASKS", "APPROVALS",
  "PAUSE", "RESUME", "AUDIT",
]);

export function isCommandLike(message: string): boolean {
  const upper = message.toUpperCase();
  const words = upper.split(/[^A-Z0-9_']+/).filter((w) => w.length > 0);
  if (words.length === 0) return false;
  const isCmd = (w: string) => COMMAND_WORDS.has(w);
  // 1. The command IS the first word: "STATUS", "status please", "risks now".
  if (isCmd(words[0] ?? "")) return true;
  // 2. Short imperative containing a command word: "run a brief", "show risks".
  if (words.length <= 3 && words.some(isCmd)) return true;
  // 3. Emergency control verbs at the start: "pause", "resume", "audit…".
  if (words.length <= 4 && (upper.startsWith("PAUSE") || upper.startsWith("RESUME") || upper.startsWith("AUDIT"))) return true;
  return false;
}

export interface ReasonerDeps {
  gateway: ModelGateway;
  /** Render current objective (injected by the agent). */
  getObjective?: () => { id?: string; title?: string } | undefined;
  availableBudget?: number;
}

function lensPrompt(lens: Lens): string {
  switch (lens) {
    case "ceo": return ceoLensPrompt();
    case "cfo": return cfoLensPrompt();
    case "cto": return ctoLensPrompt();
    case "risk": return riskLensPrompt();
  }
}

function contextBlock(context: ExecutiveContext): string {
  const objective = context.objectiveTitle
    ? { title: context.objectiveTitle, status: "ACTIVE" as const, description: undefined }
    : undefined;
  return [
    "--- CURRENT OBJECTIVE ---",
    objectiveBlock(objective),
    "--- CURRENT STATE ---",
    stateBlock(context.stateFacts),
    "--- RELEVANT MEMORY ---",
    memoryBlock(context.memory),
    "--- FOUNDER DOCTRINE ---",
    doctrineBlock(context.doctrine),
  ].join("\n");
}

export class GatewayReasoner implements DecisionReasoner {
  constructor(private readonly deps: ReasonerDeps) {}

  private async safeComplete(opts: Parameters<ModelGateway["complete"]>[0]) {
    try {
      return await this.deps.gateway.complete(opts);
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      // Provider not configured / no key → fall back to deterministic mock so
      // the system remains operable in dev and tests.
      if (/not configured|No provider|missing API key/i.test(msg)) {
        return this.deps.gateway.complete({ ...opts, tier: undefined, taskType: "summarization" }).catch(() => {
          throw err;
        });
      }
      throw err;
    }
  }

  async classify(problem: string, categoryHint?: DecisionCategory): Promise<Classification> {
    const res = await this.safeComplete({
      taskType: "classification",
      system: classificationPrompt(),
      user: `Founder request: ${problem}`,
      jsonMode: true,
      maxTokens: 300,
    });
    try {
      const parsed = JSON.parse(extractJson(res.content)) as {
        intent?: string;
        category?: string;
        title?: string;
      };
      let intent: "decision" | "query" | "command" =
        parsed.intent === "decision" || parsed.intent === "command" ? parsed.intent : "query";
      // Guard: the model over-triggers "command" on imperatives ("hello",
      // "reply with X"). Only honor it when the message actually contains a
      // command keyword — otherwise treat it as a normal query so the model
      // replies naturally instead of returning the canned overview.
      if (intent === "command" && !isCommandLike(problem)) intent = "query";
      return {
        intent,
        category: (parsed.category as DecisionCategory) ?? categoryHint ?? "OPERATIONAL",
        title: parsed.title ?? problem.slice(0, 80),
      };
    } catch {
      return { intent: "query", category: categoryHint ?? "OPERATIONAL", title: problem.slice(0, 80) };
    }
  }

  async analyzeLens(lens: Lens, problem: string, context: ExecutiveContext, options: DecisionOption[]): Promise<string> {
    const res = await this.safeComplete({
      taskType: "lens_analysis",
      system: `${lensPrompt(lens)}\n\n${contextBlock(context)}`,
      user: `Decision: ${problem}\n\nOptions:\n${options.map((o) => `- ${o.id}: ${o.label}${o.description ? ` (${o.description})` : ""}`).join("\n")}`,
      // Reasoning models spend output tokens on internal reasoning before
      // producing content — give them generous headroom so content is not cut off.
      maxTokens: 2000,
    });
    return res.content.trim();
  }

  async generateOptions(problem: string, context: ExecutiveContext): Promise<DecisionOption[]> {
    const res = await this.safeComplete({
      taskType: "routine_operational",
      system: optionsPrompt(),
      user: `Decision: ${problem}\n\n${contextBlock(context)}`,
      jsonMode: true,
      maxTokens: 900,
    });
    try {
      const parsed = JSON.parse(extractJson(res.content)) as { options?: DecisionOption[] };
      const options = (parsed.options ?? []).map((o, i) => ({
        id: o.id ?? `opt${i + 1}`,
        label: o.label ?? `Option ${i + 1}`,
        description: o.description,
        cost: o.cost,
        reversible: o.reversible ?? true,
      }));
      if (options.length >= 2) return options;
    } catch {
      // fall through
    }
    return [
      { id: "opt1", label: "Proceed with the proposed action", reversible: true },
      { id: "opt2", label: "Run a small reversible experiment first", reversible: true },
      { id: "opt3", label: "Do nothing / defer", reversible: true },
    ];
  }

  async synthesize(
    problem: string,
    context: ExecutiveContext,
    options: DecisionOption[],
    lensAssessments: Record<Lens, string>,
  ): Promise<Synthesis> {
    const res = await this.safeComplete({
      taskType: "executive_synthesis",
      system: synthesisPrompt(),
      user: [
        `Decision: ${problem}`,
        contextBlock(context),
        `Options:\n${options.map((o) => `- ${o.id}: ${o.label}`).join("\n")}`,
        `CEO: ${lensAssessments.ceo}`,
        `CFO: ${lensAssessments.cfo}`,
        `CTO: ${lensAssessments.cto}`,
        `Risk: ${lensAssessments.risk}`,
      ].join("\n\n"),
      jsonMode: true,
      maxTokens: 2000,
    });
    try {
      const parsed = JSON.parse(extractJson(res.content)) as Partial<Synthesis>;
      return {
        recommendedAction: parsed.recommendedAction ?? "Run a small reversible experiment first.",
        confidence: clamp01(parsed.confidence),
        expectedOutcome: parsed.expectedOutcome ?? "Outcome to be measured against the success metric.",
        executionPlan: Array.isArray(parsed.executionPlan) ? parsed.executionPlan.map(String) : ["Follow up and measure the outcome."],
        successMetric: parsed.successMetric ?? "To be defined at execution.",
        reviewDate: parsed.reviewDate,
        uncertainties: Array.isArray(parsed.uncertainties) ? parsed.uncertainties.map(String) : [],
      };
    } catch {
      return {
        recommendedAction: "Run a small reversible experiment first.",
        confidence: 0.5,
        expectedOutcome: "Outcome to be measured against the success metric.",
        executionPlan: ["Follow up and measure the outcome."],
        successMetric: "To be defined at execution.",
        uncertainties: [],
      };
    }
  }
}

function extractJson(content: string): string {
  const trimmed = content.trim();
  const start = trimmed.indexOf("{");
  const end = trimmed.lastIndexOf("}");
  if (start >= 0 && end > start) return trimmed.slice(start, end + 1);
  return trimmed;
}

function clamp01(v: unknown): number {
  const n = typeof v === "number" ? v : Number(v);
  if (!Number.isFinite(n)) return 0.5;
  return Math.max(0, Math.min(1, n));
}
