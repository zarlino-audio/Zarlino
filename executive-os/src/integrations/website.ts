/**
 * Website integration (spec §13) — read-only status checks.
 */

import { z } from "zod";
import type { Tool } from "../core/tools/types";

export function websiteTools(ctx: { env: Record<string, string | undefined> }): Tool[] {
  const baseUrl = ctx.env.WEBSITE_URL ?? "https://zarlinoaudio.com";

  return [
    {
      name: "website.get_status",
      description: "Check whether the Zarlino website is reachable and measure latency",
      inputSchema: z.object({ path: z.string().optional() }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async () => {
        const target = `${baseUrl.replace(/\/$/, "")}/`;
        const started = Date.now();
        const res = await fetch(target, { method: "GET", redirect: "follow" });
        return {
          url: target,
          status: res.status,
          ok: res.ok,
          latencyMs: Date.now() - started,
          checkedAt: new Date().toISOString(),
        };
      },
    },
    {
      name: "website.get_page",
      description: "Fetch a specific page on the Zarlino website (path) and return its HTTP status, latency, and readable text content",
      inputSchema: z.object({
        path: z.string().min(1).max(500),
        maxChars: z.number().int().min(500).max(60_000).optional(),
      }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const target = `${baseUrl.replace(/\/$/, "")}/${input.path.replace(/^\/+/, "")}`;
        const started = Date.now();
        const res = await fetch(target, { method: "GET", redirect: "follow" });
        const contentType = res.headers.get("content-type") ?? "";
        const text = await res.text();
        const body = contentType.includes("html") || /<[a-z][\s\S]*>/i.test(text.slice(0, 1000))
          ? text.replace(/<script[\s\S]*?<\/script>/gi, " ")
            .replace(/<style[\s\S]*?<\/style>/gi, " ")
            .replace(/<[^>]+>/g, " ")
            .replace(/&nbsp;/g, " ")
            .replace(/&amp;/g, "&")
            .replace(/&lt;/g, "<")
            .replace(/&gt;/g, ">")
            .replace(/&quot;/g, '"')
            .replace(/\s+/g, " ")
            .trim()
          : text;
        const maxChars = input.maxChars ?? 20_000;
        return {
          url: target,
          path: input.path,
          status: res.status,
          ok: res.ok,
          contentType,
          latencyMs: Date.now() - started,
          text: body.length > maxChars ? body.slice(0, maxChars) + `\n…(truncated at ${maxChars} chars)` : body,
          checkedAt: new Date().toISOString(),
        };
      },
    },
  ];
}
