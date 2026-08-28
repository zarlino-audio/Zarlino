/**
 * Model gateway types (spec §3).
 *
 * The executive system must be model-agnostic. All LLM access flows through
 * the ModelGateway, which routes by task type / cost / latency / reasoning
 * requirement and records every call for cost control and observability.
 */

export type TaskType =
  | "classification"
  | "summarization"
  | "routine_operational"
  | "financial_reasoning"
  | "technical_reasoning"
  | "strategic_reasoning"
  | "lens_analysis"
  | "executive_synthesis"
  | "chat"
  | "briefing";

export type ModelTier = "cheap" | "medium" | "strong" | "strongest";

export interface ModelMessage {
  role: "system" | "user" | "assistant" | "tool";
  content: string;
  /** For assistant messages that requested tools. */
  tool_calls?: Array<{ id: string; type: string; function: { name: string; arguments: string } }>;
  /** For tool-role messages: which tool call this is the result of. */
  tool_call_id?: string;
}

/** OpenAI-compatible function/tool schema (converted from the typed registry). */
export interface ModelToolFunction {
  name: string;
  description?: string;
  parameters: unknown;
}

/** OpenAI wire format: each tool is { type: "function", function: {...} }. */
export interface ModelToolSchema {
  type: "function";
  function: ModelToolFunction;
}

export interface ModelRequest {
  /** Provider-specific model identifier, e.g. "deepseek-chat". */
  model: string;
  messages: ModelMessage[];
  temperature?: number;
  maxTokens?: number;
  /** Ask the provider for JSON output (provider-specific). */
  jsonMode?: boolean;
  /** Function-calling tools the model may invoke (OpenAI wire format). */
  tools?: ModelToolSchema[];
  toolChoice?: "auto" | "none";
}

export interface ModelResponse {
  content: string;
  provider: string;
  model: string;
  inputTokens: number;
  outputTokens: number;
  latencyMs: number;
  finishReason?: string;
  /** Tool invocations requested by the model (function calling). */
  toolCalls?: Array<{ id: string; name: string; arguments: string }>;
}

export interface ModelProvider {
  readonly id: string;
  /** Human label used in routing ("deepseek", "openai", ...). */
  readonly label: string;
  complete(req: ModelRequest): Promise<ModelResponse>;
  /** Optional: stream text deltas (OpenAI-compatible SSE). */
  stream?(req: ModelRequest): AsyncIterable<string>;
}

export interface CompleteOptions {
  taskType: TaskType;
  /** Explicit tier override; otherwise derived from taskType. */
  tier?: ModelTier;
  system?: string;
  user?: string;
  messages?: ModelMessage[];
  temperature?: number;
  maxTokens?: number;
  jsonMode?: boolean;
  /** Function-calling tools the model may invoke (OpenAI wire format). */
  tools?: ModelToolSchema[];
  toolChoice?: "auto" | "none";
}

export interface ModelCallRecord {
  provider: string;
  model: string;
  taskType: TaskType;
  inputTokens: number;
  outputTokens: number;
  cost: number;
  sessionId?: string;
  runId?: string;
  latencyMs: number;
  timestamp: string;
}

export interface BudgetDecision {
  allowed: boolean;
  reason?: string;
  cost: number;
}

export interface CostTracker {
  /** Returns false (with reason) when the call would exceed a budget. */
  check(call: ModelCallRecord): BudgetDecision;
  record(call: ModelCallRecord): void;
  currentBudgetState(): { daily: number; monthly: number; perTask: number; perRun: number };
}
