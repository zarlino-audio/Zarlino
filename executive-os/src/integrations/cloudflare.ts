/**
 * Cloudflare integration (spec §15).
 *
 * Read + deploy monitoring of Workers, deployments, zones, and analytics.
 * Production-changing operations are protected by the authority system
 * (deploy → APPROVAL; denied when deployments are disabled).
 *
 * NOTE on worker identity: the CF scripts-list endpoint returns the script
 * NAME in the `id` field (e.g. "zarlino-executive-os"), so all tools key on
 * the script name, and `cloudflare.list_workers` lets the model discover the
 * real names before calling the others.
 */

import { z } from "zod";
import type { Tool } from "../core/tools/types";

const CF_API = "https://api.cloudflare.com/client/v4";

function cfHeaders(ctx: { env: Record<string, string | undefined> }): Record<string, string> {
  const token = ctx.env.CF_API_TOKEN;
  if (!token) throw new Error("Cloudflare not configured: CF_API_TOKEN missing");
  return { Authorization: `Bearer ${token}`, "Content-Type": "application/json" };
}

function accountId(ctx: { env: Record<string, string | undefined> }): string {
  const acct = ctx.env.CF_ACCOUNT_ID;
  if (!acct) throw new Error("Cloudflare not configured: CF_ACCOUNT_ID missing");
  return acct;
}

async function cfFetch(ctx: { env: Record<string, string | undefined> }, path: string): Promise<unknown> {
  const res = await fetch(`${CF_API}${path}`, { headers: cfHeaders(ctx) });
  if (!res.ok) {
    const text = await res.text().catch(() => "");
    throw new Error(`Cloudflare API ${res.status}: ${text.slice(0, 200)}`);
  }
  const contentType = res.headers.get("content-type") ?? "";
  if (!contentType.includes("application/json")) {
    // e.g. the (deprecated) analytics endpoint returns the script as multipart —
    // never try to JSON.parse a script body.
    throw new Error(`Cloudflare API returned non-JSON (${contentType.split(";")[0]}) for ${path}`);
  }
  const json = (await res.json()) as { success: boolean; errors?: Array<{ message: string }>; result?: unknown };
  if (!json.success) {
    throw new Error(`Cloudflare API error: ${json.errors?.map((e) => e.message).join("; ") ?? "unknown"}`);
  }
  return json.result;
}

/**
 * Worker analytics. The legacy REST endpoint
 * (/workers/scripts/{name}/analytics/daily) has been deprecated by Cloudflare
 * and now returns the script body as multipart; the GraphQL Analytics API is
 * the supported path but requires the "Workers Analytics" scope on the token.
 * We try GraphQL first, then the legacy REST, and degrade to a clear message
 * rather than throwing a confusing parse error.
 */
async function workerAnalytics(
  ctx: { env: Record<string, string | undefined> },
  workerName: string,
  days: number,
): Promise<unknown> {
  const token = ctx.env.CF_API_TOKEN;
  const acct = accountId(ctx);

  // 1. GraphQL Analytics API (supported path).
  const since = new Date(Date.now() - days * 86_400_000).toISOString();
  const gql = {
    query: `query {
      viewer {
        accounts(filter: {accountTag: "${acct}"}) {
          workersInvocationsAdaptive(
            limit: ${days * 24}
            filter: {datetime_geq: "${since}", scriptName: "${workerName}"}
            orderBy: [datetime_ASC]
          ) {
            datetime requests errors cpuTime subrequests
          }
        }
      }
    }`,
  };
  try {
    const res = await fetch(`${CF_API}/graphql`, {
      method: "POST",
      headers: { Authorization: `Bearer ${token}`, "Content-Type": "application/json" },
      body: JSON.stringify(gql),
    });
    const body = (await res.json()) as {
      data?: { viewer?: { accounts?: Array<{ workersInvocationsAdaptive?: Array<Record<string, unknown>> }> } };
      errors?: Array<{ message: string }>;
    };
    const rows = body?.data?.viewer?.accounts?.[0]?.workersInvocationsAdaptive;
    if (rows) {
      return { source: "graphql", worker: workerName, days, rows };
    }
    if (body.errors?.length) {
      // GraphQL unavailable (permission scope) — fall through to legacy REST.
      // (fallback below)
    }
  } catch {
    // network/token failure — fall through
  }

  // 2. Legacy REST endpoint (may be deprecated → multipart; handled by cfFetch).
  const sinceDay = new Date(Date.now() - days * 86_400_000).toISOString().slice(0, 10);
  const untilDay = new Date().toISOString().slice(0, 10);
  try {
    const rest = await cfFetch(
      ctx,
      `/accounts/${acct}/workers/scripts/${encodeURIComponent(workerName)}/analytics/daily?since=${sinceDay}&until=${untilDay}`,
    );
    return { source: "rest", worker: workerName, days, data: rest };
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err);
    return {
      source: "unavailable",
      worker: workerName,
      days,
      note: `Worker analytics are unavailable: ${message}. The legacy REST endpoint is deprecated and the GraphQL Analytics API needs a token with the "Workers Analytics" scope (cfanalytics). Consider the cf account token used for deploys.`,
    };
  }
}

/** List the account's Worker scripts (id field = script name). */
async function listScripts(ctx: { env: Record<string, string | undefined> }): Promise<Array<{ id?: string; created_on?: string; modified_on?: string; etag?: string }>> {
  return (await cfFetch(ctx, `/accounts/${accountId(ctx)}/workers/scripts?per_page=50`)) as Array<{
    id?: string;
    created_on?: string;
    modified_on?: string;
    etag?: string;
  }>;
}

/**
 * Parse a Cloudflare multipart worker-script response into named parts
 * (e.g. index.js, metadata). We split on the boundary and only read the FIRST
 * header block of each part (before the first blank line), so script code
 * that happens to contain `name="metadata"` can never confuse the parser.
 */
function parseMultipart(body: string, boundary: string): Record<string, string> {
  const parts: Record<string, string> = {};
  const delimiter = `--${boundary}`;
  const chunks = body.split(delimiter);
  for (const chunk of chunks) {
    const trimmed = chunk.replace(/^\r?\n/, "").replace(/\r?\n--\s*$/, "");
    if (!trimmed.trim() || trimmed.trim() === "--") continue;
    const headerEnd = trimmed.indexOf("\r\n\r\n");
    const headerBlock = headerEnd >= 0 ? trimmed.slice(0, headerEnd) : trimmed;
    const content = headerEnd >= 0 ? trimmed.slice(headerEnd + 4) : "";
    const nameMatch = /name="([^"]+)"/.exec(headerBlock);
    if (nameMatch?.[1]) {
      parts[nameMatch[1]] = content.replace(/\r?\n$/, "");
    }
  }
  return parts;
}

/**
 * Download a Worker's source code (and metadata) from Cloudflare.
 * GET /workers/scripts/{name} returns multipart/form-data with parts named
 * `index.js` (source) and `metadata` (JSON). Falls back to a helpful error
 * when the worker isn't found.
 */
async function workerSource(
  ctx: { env: Record<string, string | undefined> },
  workerName: string,
): Promise<{ source: string; metadata: unknown; worker: string; sizeBytes: number }> {
  const token = ctx.env.CF_API_TOKEN;
  const acct = accountId(ctx);
  const res = await fetch(`${CF_API}/accounts/${acct}/workers/scripts/${encodeURIComponent(workerName)}`, {
    headers: { Authorization: `Bearer ${token}` },
  });
  if (!res.ok) {
    const text = await res.text().catch(() => "");
    throw new Error(`Cloudflare API ${res.status}: ${text.slice(0, 200)}`);
  }
  const contentType = res.headers.get("content-type") ?? "";
  const boundaryMatch = /boundary=([^;]+)/.exec(contentType);
  if (!boundaryMatch?.[1]) {
    throw new Error(`Unexpected worker response (content-type: ${contentType.split(";")[0]})`);
  }
  const body = await res.text();
  const parts = parseMultipart(body, boundaryMatch[1]);
  const source = parts["index.js"] ?? "";
  if (!source) {
    throw new Error(`No source part found in worker response (parts: ${Object.keys(parts).join(", ") || "none"})`);
  }
  let metadata: unknown;
  if (parts["metadata"]) {
    try {
      metadata = JSON.parse(parts["metadata"]);
    } catch {
      metadata = parts["metadata"];
    }
  }
  return { worker: workerName, source, metadata, sizeBytes: source.length };
}

/** Shared worker-name helper with a helpful error listing real names. */
async function resolveWorker(ctx: { env: Record<string, string | undefined> }, name: string): Promise<{ id: string; created_on?: string; modified_on?: string; etag?: string }> {
  const scripts = await listScripts(ctx);
  const found = scripts.find((s) => s.id === name);
  if (!found || !found.id) {
    const available = scripts.map((s) => s.id).filter(Boolean).join(", ") || "none";
    throw new Error(`Worker "${name}" not found. Available workers: ${available}`);
  }
  return found as { id: string; created_on?: string; modified_on?: string; etag?: string };
}

export function cloudflareTools(ctx: { env: Record<string, string | undefined> }): Tool[] {
  return [
    {
      name: "cloudflare.list_workers",
      description: "List all Worker scripts on the Cloudflare account (id = script name, created/modified dates). Call this first to discover real worker names before using get_worker/get_deployments.",
      inputSchema: z.object({}),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async () => {
        return { workers: await listScripts(ctx) };
      },
    },
    {
      name: "cloudflare.get_worker",
      description: "Get a Worker script's metadata by script name (id, created/modified, etag)",
      inputSchema: z.object({ workerName: z.string().min(1).max(128) }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        return resolveWorker(ctx, input.workerName);
      },
    },
    {
      name: "cloudflare.get_worker_source",
      description: "Download a Worker's deployed source code + metadata by script name (e.g. zarlino-executive-os, zarlino-website). The zarlino-website worker IS the deployed website source.",
      inputSchema: z.object({ workerName: z.string().min(1).max(128) }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        await resolveWorker(ctx, input.workerName); // validates the name + gives a good error
        return workerSource(ctx, input.workerName);
      },
    },
    {
      name: "cloudflare.get_deployments",
      description: "List recent deployments of a Worker (by script name)",
      inputSchema: z.object({ workerName: z.string().min(1).max(128) }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        await resolveWorker(ctx, input.workerName); // validates the name + gives a good error
        return cfFetch(ctx, `/accounts/${accountId(ctx)}/workers/scripts/${encodeURIComponent(input.workerName)}/deployments`);
      },
    },
    {
      name: "cloudflare.get_analytics",
      description: "Get daily analytics for a Worker (requests, errors, CPU, etc.) by script name",
      inputSchema: z.object({ workerName: z.string().min(1).max(128), days: z.number().int().min(1).max(90).optional() }),
      requiredPermission: "analyze",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        await resolveWorker(ctx, input.workerName);
        return workerAnalytics(ctx, input.workerName, input.days ?? 7);
      },
    },
    {
      name: "cloudflare.list_zones",
      description: "List zones (domains) on the Cloudflare account",
      inputSchema: z.object({ limit: z.number().int().min(1).max(100).optional() }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const perPage = input.limit ?? 50;
        const zones = (await cfFetch(ctx, `/zones?per_page=${perPage}`)) as Array<{ id?: string; name?: string; status?: string; paused?: boolean; name_servers?: string[] }>;
        return {
          zones: zones.map((z) => ({
            id: z.id,
            name: z.name,
            status: z.status,
            paused: z.paused,
            nameServers: z.name_servers,
          })),
        };
      },
    },
    {
      name: "cloudflare.get_zone",
      description: "Get a zone (domain) by its name, e.g. zarlinoaudio.com",
      inputSchema: z.object({ zoneName: z.string().min(1).max(253) }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const zones = (await cfFetch(ctx, `/zones?name=${encodeURIComponent(input.zoneName)}`)) as Array<Record<string, unknown>>;
        if (!zones.length) throw new Error(`Zone "${input.zoneName}" not found on this account`);
        return zones[0];
      },
    },
    {
      name: "cloudflare.get_dns_records",
      description: "List DNS records for a zone (domain) by zone name, e.g. zarlinoaudio.com",
      inputSchema: z.object({ zoneName: z.string().min(1).max(253), type: z.string().max(16).optional(), limit: z.number().int().min(1).max(500).optional() }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const zones = (await cfFetch(ctx, `/zones?name=${encodeURIComponent(input.zoneName)}`)) as Array<{ id?: string; name?: string }>;
        const zone = zones[0];
        if (!zone?.id) throw new Error(`Zone "${input.zoneName}" not found on this account`);
        const type = input.type ? `&type=${encodeURIComponent(input.type)}` : "";
        const records = (await cfFetch(ctx, `/zones/${zone.id}/dns_records?per_page=${input.limit ?? 100}${type}`)) as Array<{
          id?: string;
          type?: string;
          name?: string;
          content?: string;
          ttl?: number;
          proxied?: boolean;
        }>;
        return {
          zone: input.zoneName,
          records: records.map((r) => ({
            id: r.id,
            type: r.type,
            name: r.name,
            content: r.content,
            ttl: r.ttl,
            proxied: r.proxied,
          })),
        };
      },
    },
    {
      name: "cloudflare.get_account",
      description: "Get the Cloudflare account profile (id, name, type)",
      inputSchema: z.object({}),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async () => {
        const acct = accountId(ctx);
        const accounts = (await cfFetch(ctx, `/accounts?per_page=50`)) as Array<Record<string, unknown>>;
        const found = accounts.find((a) => (a as { id?: string }).id === acct) ?? accounts[0];
        return found ?? { id: acct, name: acct };
      },
    },
  ];
}
