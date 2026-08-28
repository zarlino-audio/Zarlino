/**
 * Vision integration — DeepSeek V4 vision (deepseek-v4-flash-vision-exp).
 *
 * Lets the Executive actually SEE images: plugin screenshots, website
 * visuals, marketing assets, charts, screenshots of competitor UIs, etc.
 *
 * Supports two input modes:
 *   - imageUrl  — a public URL (DeepSeek downloads it server-side), or a
 *                 data: URL with base64 payload.
 *   - imageBase64 + mimeType — raw base64 you pass in directly (best for
 *                 small images; the worker can fetch/encode first).
 *
 * All vision calls are read-only, LOW risk, reversible → ALLOW by default.
 */

import { z } from "zod";
import type { Tool } from "../core/tools/types";
import type { ExecutiveStore } from "../db/store";
import { estimateCost } from "../core/model/cost";

const DS_API = "https://api.deepseek.com";
const VISION_MODEL_DEFAULT = "deepseek-v4-flash-vision-exp";
const MAX_BASE64_CHARS = 4_000_000; // ~3 MB base64, well within model limits

function dsHeaders(ctx: { env: Record<string, string | undefined> }): Record<string, string> {
  const key = ctx.env.DEEPSEEK_API_KEY;
  if (!key) throw new Error("DeepSeek not configured: DEEPSEEK_API_KEY missing (needed for vision)");
  return { "Content-Type": "application/json", Authorization: `Bearer ${key}` };
}

function visionModel(ctx: { env: Record<string, string | undefined> }): string {
  return ctx.env.DEEPSEEK_VISION_MODEL ?? VISION_MODEL_DEFAULT;
}

async function visionComplete(
  ctx: { env: Record<string, string | undefined> },
  prompt: string,
  imageUrl: string,
  maxTokens: number,
  store?: ExecutiveStore,
  taskType: "lens_analysis" | "chat" = "lens_analysis",
): Promise<{ content: string; reasoning: string; model: string; usage: { prompt_tokens?: number; completion_tokens?: number } }> {
  const model = visionModel(ctx);
  const started = Date.now();
  const res = await fetch(`${DS_API}/chat/completions`, {
    method: "POST",
    headers: dsHeaders(ctx),
    body: JSON.stringify({
      model,
      messages: [
        {
          role: "user",
          content: [
            { type: "text", text: prompt },
            { type: "image_url", image_url: { url: imageUrl } },
          ],
        },
      ],
      max_tokens: maxTokens,
    }),
  });
  if (!res.ok) {
    const text = await res.text().catch(() => "");
    throw new Error(`DeepSeek vision ${res.status}: ${text.slice(0, 300)}`);
  }
  const json = (await res.json()) as {
    choices?: Array<{ message?: { content?: string; reasoning_content?: string } }>;
    model?: string;
    usage?: { prompt_tokens?: number; completion_tokens?: number };
  };
  const message = json.choices?.[0]?.message;
  const content = message?.content ?? "";
  if (!content) throw new Error("DeepSeek vision returned an empty response");
  const inputTokens = json.usage?.prompt_tokens ?? 0;
  const outputTokens = json.usage?.completion_tokens ?? 0;
  // Record tokens + cost so vision usage shows up in the AI Fuel / token budget
  // (vision bypasses the model gateway, so it must self-report).
  if (store && (inputTokens > 0 || outputTokens > 0)) {
    store.record({
      provider: "deepseek",
      model: json.model ?? model,
      taskType,
      inputTokens,
      outputTokens,
      cost: estimateCost("deepseek", json.model ?? model, inputTokens, outputTokens),
      latencyMs: Date.now() - started,
      timestamp: new Date().toISOString(),
    });
  }
  return {
    content,
    reasoning: message?.reasoning_content ?? "",
    model: json.model ?? model,
    usage: json.usage ?? {},
  };
}

export function visionTools(ctx: { env: Record<string, string | undefined> }): Tool[] {
  return [
    {
      name: "vision.analyze_image",
      description:
        "Analyze an image with DeepSeek V4 vision: describe what it shows, read text/logos/UI, evaluate a plugin screenshot, chart, or marketing asset. Pass either a public imageUrl (or data: URL) or imageBase64 + mimeType. Returns the model's analysis.",
      inputSchema: z.object({
        prompt: z.string().min(1).max(2000).optional(),
        imageUrl: z.string().url().max(4000).optional(),
        imageBase64: z.string().min(1).max(MAX_BASE64_CHARS).optional(),
        mimeType: z.string().max(128).optional(),
        maxTokens: z.number().int().min(50).max(2000).optional(),
      }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (tctx, input) => {
        if (!input.imageUrl && !input.imageBase64) {
          throw new Error("Provide either imageUrl or imageBase64 (with mimeType)");
        }
        let url = input.imageUrl;
        if (!url) {
          const mime = input.mimeType ?? "image/png";
          url = `data:${mime};base64,${input.imageBase64}`;
        }
        const prompt =
          input.prompt ??
          "Describe this image in detail: what it shows, any visible text, UI elements, colors, and its purpose. If it is a plugin UI, evaluate layout and readability.";
        const out = await visionComplete(ctx, prompt, url, input.maxTokens ?? 800, tctx.store);
        return {
          model: out.model,
          analysis: out.content,
          reasoning: out.reasoning || undefined,
          usage: out.usage,
        };
      },
    },
    {
      name: "vision.analyze_url_image",
      description:
        "Fetch an image from a URL and analyze it with DeepSeek V4 vision. Use when the image is on the web (website asset, screenshot URL, avatar) and the worker should download it first (handles cases where the model can't reach the URL).",
      inputSchema: z.object({
        prompt: z.string().min(1).max(2000).optional(),
        url: z.string().url().max(4000),
        mimeType: z.string().max(128).optional(),
        maxTokens: z.number().int().min(50).max(2000).optional(),
      }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (tctx, input) => {
        // Download the image ourselves (worker fetch), then pass as base64 so
        // DeepSeek doesn't have to reach the URL (works for private/protected).
        const res = await fetch(input.url, {
          headers: { "User-Agent": "Mozilla/5.0 (compatible; zarlino-executive-os/1.0)" },
          redirect: "follow",
        });
        if (!res.ok) throw new Error(`Failed to fetch image ${input.url}: HTTP ${res.status}`);
        const buf = Buffer.from(await res.arrayBuffer());
        const contentType = input.mimeType ?? res.headers.get("content-type") ?? "image/png";
        const b64 = buf.toString("base64");
        if (b64.length > MAX_BASE64_CHARS) throw new Error(`Image too large (${(buf.length / 1024 / 1024).toFixed(1)} MB) — pass imageUrl directly instead`);
        const prompt =
          input.prompt ??
          "Describe this image in detail: what it shows, any visible text, UI elements, colors, and its purpose. If it is a plugin UI, evaluate layout and readability.";
        const out = await visionComplete(ctx, prompt, `data:${contentType};base64,${b64}`, input.maxTokens ?? 800, tctx.store);
        return {
          model: out.model,
          source: input.url,
          bytes: buf.length,
          contentType,
          analysis: out.content,
          reasoning: out.reasoning || undefined,
          usage: out.usage,
        };
      },
    },
  ];
}
