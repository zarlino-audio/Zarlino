/**
 * Model providers (spec §3).
 *
 * Each provider implements the same `ModelProvider` interface. Providers are
 * selected by the gateway based on config, never hard-coded into business
 * logic. The MockProvider gives deterministic output for local dev and tests.
 */

import type { ModelMessage, ModelProvider, ModelRequest, ModelResponse } from "./types";

const DEFAULT_TIMEOUT_MS = 60_000;
/** Transient failures we retry (network + 5xx + 429 rate-limit). */
function isRetryable(res: Response): boolean {
  return res.status === 429 || res.status >= 500;
}
function isRetryableError(err: unknown): boolean {
  return err instanceof Error && (err.name === "AbortError" || /fetch failed|network|ECONNRESET|ETIMEDOUT/i.test(err.message));
}
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

interface JsonBody {
  model: string;
  messages?: ModelMessage[];
  temperature?: number;
  max_tokens?: number;
  response_format?: { type: "json_object" };
}

async function postJsonOnce(url: string, headers: Record<string, string>, body: JsonBody, timeoutMs: number): Promise<Response> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    return await fetch(url, {
      method: "POST",
      headers: { "Content-Type": "application/json", ...headers },
      body: JSON.stringify(body),
      signal: controller.signal,
    });
  } finally {
    clearTimeout(timer);
  }
}

/**
 * POST JSON with bounded retries + exponential backoff for transient
 * failures (network errors, 5xx, 429 rate limits). Non-retryable 4xx errors
 * and successful responses return immediately. Wired to the previously-dead
 * `maxRetries` / `requestTimeoutMs` config so a transient model hiccup no
 * longer kills an entire agent run.
 */
async function postJson(
  url: string,
  headers: Record<string, string>,
  body: JsonBody,
  opts: { maxRetries?: number; timeoutMs?: number } = {},
): Promise<Response> {
  const maxRetries = Math.max(0, opts.maxRetries ?? 2);
  const timeoutMs = opts.timeoutMs ?? DEFAULT_TIMEOUT_MS;
  let lastErr: unknown;
  for (let attempt = 0; attempt <= maxRetries; attempt++) {
    try {
      const res = await postJsonOnce(url, headers, body, timeoutMs);
      if (!isRetryable(res) || attempt === maxRetries) return res;
      // drain body so the connection can be reused
      await res.text().catch(() => "");
    } catch (err) {
      lastErr = err;
      if (!isRetryableError(err) || attempt === maxRetries) throw err;
    }
    // exponential backoff with jitter: 400ms * 2^attempt
    const backoff = 400 * 2 ** attempt + Math.floor(Math.random() * 200);
    await sleep(backoff);
  }
  throw lastErr ?? new Error(`Request to ${url} failed after ${maxRetries + 1} attempts`);
}

async function readError(res: Response): Promise<string> {
  try {
    const text = await res.text();
    return `HTTP ${res.status}: ${text.slice(0, 300)}`;
  } catch {
    return `HTTP ${res.status}`;
  }
}

// ---------------------------------------------------------------------------
// OpenAI-compatible providers (DeepSeek, OpenAI, and any compatible host)
// ---------------------------------------------------------------------------

export function openAiCompatibleProvider(
  id: string,
  label: string,
  opts: {
    baseUrl: string;
    apiKey: () => string | undefined;
    // Map a provider response to our normalized shape.
    parse?: (json: any, model: string) => { content: string; inputTokens: number; outputTokens: number; finishReason?: string };
    // Transient-failure retries + per-attempt timeout (defaults 2 / 60s).
    maxRetries?: number;
    timeoutMs?: number;
  },
): ModelProvider {
  const retryOpts = { maxRetries: opts.maxRetries, timeoutMs: opts.timeoutMs };
  return {
    id,
    label,
    async complete(req: ModelRequest): Promise<ModelResponse> {
      const key = opts.apiKey();
      if (!key) throw new Error(`Provider "${id}" is not configured (missing API key)`);
      const started = Date.now();
      const res = await postJson(
        `${opts.baseUrl.replace(/\/$/, "")}/chat/completions`,
        { Authorization: `Bearer ${key}` },
        {
          model: req.model,
          messages: req.messages,
          temperature: req.temperature,
          max_tokens: req.maxTokens,
          ...(req.tools ? { tools: req.tools } : {}),
          ...(req.toolChoice ? { tool_choice: req.toolChoice } : {}),
          ...(req.jsonMode ? { response_format: { type: "json_object" } } : {}),
        },
        retryOpts,
      );
      if (!res.ok) throw new Error(`Provider "${id}" failed: ${await readError(res)}`);
      const json = (await res.json()) as any;
      const parsed = opts.parse
        ? opts.parse(json, req.model)
        : parseOpenAi(json, req.model);
      const toolCalls = json?.choices?.[0]?.message?.tool_calls as Array<{ id?: string; function?: { name?: string; arguments?: string } }> | undefined;
      return {
        content: parsed.content,
        provider: id,
        model: req.model,
        inputTokens: parsed.inputTokens,
        outputTokens: parsed.outputTokens,
        latencyMs: Date.now() - started,
        finishReason: parsed.finishReason,
        toolCalls: toolCalls?.length
          ? toolCalls.map((c) => ({
              id: c.id ?? "call_0",
              name: c.function?.name ?? "",
              arguments: c.function?.arguments ?? "{}",
            }))
          : undefined,
      };
    },
    async *stream(req: ModelRequest): AsyncGenerator<string> {
      const key = opts.apiKey();
      if (!key) throw new Error(`Provider "${id}" is not configured (missing API key)`);
      const res = await fetch(`${opts.baseUrl.replace(/\/$/, "")}/chat/completions`, {
        method: "POST",
        headers: { "Content-Type": "application/json", Authorization: `Bearer ${key}` },
        body: JSON.stringify({
          model: req.model,
          messages: req.messages,
          temperature: req.temperature,
          max_tokens: req.maxTokens,
          stream: true,
          ...(req.jsonMode ? { response_format: { type: "json_object" } } : {}),
        }),
      });
      if (!res.ok || !res.body) throw new Error(`Provider "${id}" failed: ${await readError(res)}`);
      yield* parseSseDeltas(res.body);
    },
  };
}

function parseOpenAi(json: any, model: string): { content: string; inputTokens: number; outputTokens: number; finishReason?: string } {
  const content = json?.choices?.[0]?.message?.content ?? "";
  const usage = json?.usage;
  return {
    content: typeof content === "string" ? content : JSON.stringify(content),
    inputTokens: usage?.prompt_tokens ?? 0,
    outputTokens: usage?.completion_tokens ?? 0,
    finishReason: json?.choices?.[0]?.finish_reason,
  };
}

/** Parse OpenAI-compatible SSE `data:` lines into text deltas. */
async function* parseSseDeltas(body: ReadableStream<Uint8Array>): AsyncGenerator<string> {
  const reader = body.getReader();
  const decoder = new TextDecoder();
  let buf = "";
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    buf += decoder.decode(value, { stream: true });
    let idx: number;
    while ((idx = buf.indexOf("\n")) !== -1) {
      const line = buf.slice(0, idx).trim();
      buf = buf.slice(idx + 1);
      if (!line.startsWith("data:")) continue;
      const data = line.slice(5).trim();
      if (data === "[DONE]") return;
      try {
        const json = JSON.parse(data) as { choices?: Array<{ delta?: { content?: string } }> };
        const delta = json.choices?.[0]?.delta?.content;
        if (typeof delta === "string" && delta.length > 0) yield delta;
      } catch {
        /* skip malformed keep-alive lines */
      }
    }
  }
}

export const deepseekProvider = (apiKey: () => string | undefined, retry?: { maxRetries?: number; timeoutMs?: number }): ModelProvider =>
  openAiCompatibleProvider("deepseek", "DeepSeek", {
    baseUrl: "https://api.deepseek.com",
    apiKey,
    maxRetries: retry?.maxRetries,
    timeoutMs: retry?.timeoutMs,
  });

export const openaiProvider = (apiKey: () => string | undefined, retry?: { maxRetries?: number; timeoutMs?: number }): ModelProvider =>
  openAiCompatibleProvider("openai", "OpenAI", {
    baseUrl: "https://api.openai.com/v1",
    apiKey,
    maxRetries: retry?.maxRetries,
    timeoutMs: retry?.timeoutMs,
  });

// ---------------------------------------------------------------------------
// Anthropic (Messages API)
// ---------------------------------------------------------------------------

export function anthropicProvider(apiKey: () => string | undefined, retry?: { maxRetries?: number; timeoutMs?: number }): ModelProvider {
  return {
    id: "anthropic",
    label: "Anthropic",
    async complete(req: ModelRequest): Promise<ModelResponse> {
      const key = apiKey();
      if (!key) throw new Error("Provider \"anthropic\" is not configured (missing API key)");
      const started = Date.now();
      const res = await postJson(
        "https://api.anthropic.com/v1/messages",
        {
          "x-api-key": key,
          "anthropic-version": "2023-06-01",
        },
        {
          model: req.model,
          messages: req.messages.filter((m) => m.role !== "system"),
          max_tokens: req.maxTokens ?? 4096,
          temperature: req.temperature,
        },
        { maxRetries: retry?.maxRetries, timeoutMs: retry?.timeoutMs },
      );
      if (!res.ok) throw new Error(`Provider "anthropic" failed: ${await readError(res)}`);
      const json = (await res.json()) as any;
      const content = (json?.content ?? [])
        .filter((b: any) => b.type === "text")
        .map((b: any) => b.text)
        .join("");
      return {
        content,
        provider: "anthropic",
        model: req.model,
        inputTokens: json?.usage?.input_tokens ?? 0,
        outputTokens: json?.usage?.output_tokens ?? 0,
        latencyMs: Date.now() - started,
      };
    },
  };
}

// ---------------------------------------------------------------------------
// Workers AI (Cloudflare)
// ---------------------------------------------------------------------------

export function workersAiProvider(opts: {
  apiToken: () => string | undefined;
  accountId: () => string | undefined;
  maxRetries?: number;
  timeoutMs?: number;
}): ModelProvider {
  return {
    id: "workers-ai",
    label: "Workers AI",
    async complete(req: ModelRequest): Promise<ModelResponse> {
      const token = opts.apiToken();
      const account = opts.accountId();
      if (!token || !account) {
        throw new Error("Provider \"workers-ai\" is not configured (missing CF_API_TOKEN / CF_ACCOUNT_ID)");
      }
      const started = Date.now();
      const res = await postJson(
        `https://api.cloudflare.com/client/v4/accounts/${account}/ai/run/${req.model}`,
        { Authorization: `Bearer ${token}` },
        {
          model: req.model,
          messages: req.messages,
          max_tokens: req.maxTokens,
        },
        { maxRetries: opts.maxRetries, timeoutMs: opts.timeoutMs },
      );
      if (!res.ok) throw new Error(`Provider "workers-ai" failed: ${await readError(res)}`);
      const json = (await res.json()) as any;
      const result = json?.result;
      const content =
        result?.response ??
        result?.content ??
        (Array.isArray(result) ? result.map((b: any) => b.text ?? "").join("") : "");
      return {
        content: String(content),
        provider: "workers-ai",
        model: req.model,
        inputTokens: result?.usage?.input_tokens ?? 0,
        outputTokens: result?.usage?.output_tokens ?? 0,
        latencyMs: Date.now() - started,
      };
    },
  };
}

// ---------------------------------------------------------------------------
// Mock provider (deterministic; used in tests and local dev without keys)
// ---------------------------------------------------------------------------

export interface MockProviderRule {
  /** Substring match on the joined messages; first match wins. */
  match: RegExp | string;
  respond: string;
}

export function mockProvider(rules?: MockProviderRule[]): ModelProvider {
  const defaultRules: MockProviderRule[] = [
    { match: /CEO ASSESSMENT/i, respond: "CEO: aligned with primary objective; reversible; low opportunity cost." },
    { match: /CFO ASSESSMENT/i, respond: "CFO: no fabricated figures; downside bounded; fits available budget." },
    { match: /CTO ASSESSMENT/i, respond: "CTO: minimal sufficient complexity; no new dependencies; observable." },
    { match: /RISK ASSESSMENT/i, respond: "Risk: LOW. Reversible; no customer or production impact." },
    { match: /SYNTHESIS/i, respond: "Synthesis: proceed with recommended action; confidence 0.8." },
  ];
  const rulesList = rules ?? defaultRules;

  const respondFor = (req: ModelRequest): string => {
    const joined = req.messages.map((m) => `${m.role}: ${m.content}`).join("\n");
    const rule = rulesList.find((r) =>
      typeof r.match === "string" ? joined.includes(r.match) : r.match.test(joined),
    );
    return rule ? rule.respond : `Mock response to ${req.messages.length} message(s).`;
  };

  /**
   * Mock function calling for tests: if the user's last message names a
   * registered tool (e.g. "tool:github.list_issues"), return a tool call for
   * it; otherwise plain text.
   */
  const mockToolCall = (req: ModelRequest): { id: string; name: string; arguments: string } | undefined => {
    if (!req.tools?.length) return undefined;
    const userMsg = [...req.messages].reverse().find((m) => m.role === "user");
    const text = userMsg?.content ?? "";
    const m = text.match(/tool:([a-zA-Z0-9_.]+)/);
    if (!m) return undefined;
    const requested = m[1] ?? "";
    if (!requested) return undefined;
    const tool = req.tools.find(
      (t) => t.function.name === requested || t.function.name === requested.replace(/\./g, "_"),
    );
    if (!tool) return undefined;
    return { id: "call_mock_1", name: tool.function.name, arguments: "{}" };
  };

  return {
    id: "mock",
    label: "Mock",
    async complete(req: ModelRequest): Promise<ModelResponse> {
      const content = respondFor(req);
      const joined = req.messages.map((m) => `${m.role}: ${m.content}`).join("\n");
      const toolCall = mockToolCall(req);
      return {
        content: toolCall ? "" : content,
        provider: "mock",
        model: req.model,
        inputTokens: Math.max(1, Math.ceil(joined.length / 4)),
        outputTokens: Math.max(1, Math.ceil(content.length / 4)),
        latencyMs: 1,
        toolCalls: toolCall ? [toolCall] : undefined,
      };
    },
    async *stream(req: ModelRequest): AsyncGenerator<string> {
      const content = respondFor(req);
      const step = 10;
      for (let i = 0; i < content.length; i += step) {
        yield content.slice(i, i + step);
      }
    },
  };
}

// ---------------------------------------------------------------------------
// Provider registry
// ---------------------------------------------------------------------------

export interface ProviderEnv {
  DEEPSEEK_API_KEY?: string;
  OPENAI_API_KEY?: string;
  ANTHROPIC_API_KEY?: string;
  CF_API_TOKEN?: string;
  CF_ACCOUNT_ID?: string;
}

export function buildProviders(env: ProviderEnv, includeMock = false, retry?: { maxRetries?: number; timeoutMs?: number }): ModelProvider[] {
  const providers: ModelProvider[] = [
    deepseekProvider(() => env.DEEPSEEK_API_KEY, retry),
    openaiProvider(() => env.OPENAI_API_KEY, retry),
    anthropicProvider(() => env.ANTHROPIC_API_KEY, retry),
    workersAiProvider({ apiToken: () => env.CF_API_TOKEN, accountId: () => env.CF_ACCOUNT_ID, ...retry }),
  ];
  if (includeMock) providers.push(mockProvider());
  return providers;
}
