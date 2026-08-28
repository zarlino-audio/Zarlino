/**
 * Web integration — web search + web automation (read + low-risk).
 *
 * Gives the Executive real web access from the Worker runtime:
 *   - web.search       — DuckDuckGo HTML search (no API key required).
 *   - web.fetch_page   — fetch a URL and return its readable text content.
 *   - web.check_url    — HEAD/GET status + latency probe for any URL.
 *   - web.crawl_links  — fetch a page and return its outbound links (up to N).
 *
 * All tools are read-only, LOW risk, reversible — they go straight through the
 * authority engine like every other tool (read → ALLOW by default).
 */

import { z } from "zod";
import type { Tool } from "../core/tools/types";

const DDG_HTML = "https://html.duckduckgo.com/html/";
const WIKI_SEARCH = "https://en.wikipedia.org/w/api.php";

/** Strip HTML to readable text (rough but fine for model consumption). */
function htmlToText(html: string): string {
  return html
    .replace(/<script[\s\S]*?<\/script>/gi, " ")
    .replace(/<style[\s\S]*?<\/style>/gi, " ")
    .replace(/<[^>]+>/g, " ")
    .replace(/&nbsp;/g, " ")
    .replace(/&amp;/g, "&")
    .replace(/&lt;/g, "<")
    .replace(/&gt;/g, ">")
    .replace(/&quot;/g, '"')
    .replace(/&#39;/g, "'")
    .replace(/\s+/g, " ")
    .trim();
}

function truncate(text: string, max: number): string {
  return text.length > max ? text.slice(0, max) + `\n…(truncated at ${max} chars)` : text;
}

interface SearchResult {
  title: string;
  url: string;
  snippet: string;
}

/** DuckDuckGo HTML search (no key). May be CAPTCHA-blocked on datacenter IPs. */
async function duckduckgoSearch(query: string, limit: number): Promise<SearchResult[]> {
  const params = new URLSearchParams({ q: query });
  const res = await fetch(`${DDG_HTML}?${params}`, {
    headers: {
      "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
      "Accept-Language": "en-US,en;q=0.9",
      Accept: "text/html,application/xhtml+xml",
    },
  });
  if (!res.ok) throw new Error(`DuckDuckGo search HTTP ${res.status}`);
  const html = await res.text();
  // A CAPTCHA/anomaly challenge means no parseable organic results.
  if (/anomaly|challenge|unusual traffic|select all squares/i.test(html)) {
    return [];
  }
  const results: SearchResult[] = [];
  const anchorRe = /<a[^>]*class="[^"]*result__a[^"]*"[^>]*href="([^"]+)"[^>]*>([\s\S]*?)<\/a>/gi;
  const snippetRe = /<a[^>]*class="[^"]*result__snippet[^"]*"[^>]*>([\s\S]*?)<\/a>/gi;
  const anchors = [...html.matchAll(anchorRe)];
  const snippets = [...html.matchAll(snippetRe)];
  for (let i = 0; i < anchors.length && results.length < limit; i++) {
    const m = anchors[i];
    if (!m) continue;
    let url = m[1] ?? "";
    if (url.startsWith("//")) url = `https:${url}`;
    const uddg = /uddg=([^&]+)/.exec(url);
    if (uddg?.[1]) url = decodeURIComponent(uddg[1]);
    results.push({
      title: htmlToText(m[2] ?? "").slice(0, 160),
      url: url.slice(0, 500),
      snippet: htmlToText(snippets[i]?.[1] ?? "").slice(0, 300),
    });
  }
  return results;
}

/** Wikipedia search API fallback (reliable, no key, generous rate limits). */
async function wikipediaSearch(query: string, limit: number): Promise<SearchResult[]> {
  const params = new URLSearchParams({
    action: "query",
    list: "search",
    srsearch: query,
    format: "json",
    srlimit: String(limit),
    srprop: "snippet",
  });
  const res = await fetch(`${WIKI_SEARCH}?${params}`, {
    headers: {
      "User-Agent": "ZarlinoExecOS/1.0 (zarlinoaudio.com; plugin research)",
      Accept: "application/json",
    },
  });
  if (!res.ok) throw new Error(`Wikipedia search HTTP ${res.status}`);
  const body = (await res.json()) as {
    query?: { search?: Array<{ title?: string; snippet?: string }> };
  };
  return (body.query?.search ?? []).slice(0, limit).map((r) => ({
    title: r.title ?? "",
    url: `https://en.wikipedia.org/wiki/${encodeURIComponent((r.title ?? "").replace(/ /g, "_"))}`,
    snippet: htmlToText(r.snippet ?? "").slice(0, 300),
  }));
}

export function webTools(ctx: { env: Record<string, string | undefined> }): Tool[] {
  return [
    {
      name: "web.search",
      description:
        "Search the web and return top result titles + URLs + snippets. Uses DuckDuckGo, with a Wikipedia fallback. Use this for market research, competitor analysis, docs lookup, and anything you need to learn from the internet.",
      inputSchema: z.object({
        query: z.string().min(1).max(300),
        limit: z.number().int().min(1).max(20).optional(),
      }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const limit = input.limit ?? 8;
        // 1. DuckDuckGo first (broad web coverage).
        let results = await duckduckgoSearch(input.query, limit);
        let source = "duckduckgo";
        // 2. Fall back to Wikipedia if DDG returned nothing (CAPTCHA / empty).
        if (results.length === 0) {
          try {
            results = await wikipediaSearch(input.query, limit);
            source = "wikipedia";
          } catch {
            // keep empty result set
          }
        }
        if (results.length === 0) {
          return {
            query: input.query,
            source,
            results: [],
            note: "No parseable results — the search engine may have rate-limited or CAPTCHA-blocked this request. Try a different phrasing.",
          };
        }
        return { query: input.query, source, results };
      },
    },
    {
      name: "web.fetch_page",
      description: "Fetch a URL and return its readable text content (title + body, capped). Use this to read an article, docs page, or competitor page.",
      inputSchema: z.object({
        url: z.string().url().max(2000),
        maxChars: z.number().int().min(500).max(60_000).optional(),
      }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const maxChars = input.maxChars ?? 20_000;
        const res = await fetch(input.url, {
          headers: {
            "User-Agent": "Mozilla/5.0 (compatible; zarlino-executive-os/1.0)",
            Accept: "text/html,text/plain;q=0.9,*/*;q=0.8",
          },
          redirect: "follow",
        });
        const contentType = res.headers.get("content-type") ?? "";
        const text = await res.text();
        const body = contentType.includes("html") || /<[a-z][\s\S]*>/i.test(text.slice(0, 1000))
          ? htmlToText(text)
          : text;
        const title = /<title[^>]*>([\s\S]*?)<\/title>/i.exec(text)?.[1] ? htmlToText(/<title[^>]*>([\s\S]*?)<\/title>/i.exec(text)?.[1] ?? "") : undefined;
        return {
          url: input.url,
          status: res.status,
          ok: res.ok,
          contentType,
          title,
          text: truncate(body, maxChars),
        };
      },
    },
    {
      name: "web.check_url",
      description: "Check whether a URL is reachable: HTTP status, redirect target, and latency. Use this to verify a site/page is up or a link works.",
      inputSchema: z.object({ url: z.string().url().max(2000) }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const started = Date.now();
        const res = await fetch(input.url, { method: "GET", redirect: "manual" });
        const headers: Record<string, string> = {};
        res.headers.forEach((v, k) => {
          if (["location", "content-type", "server", "cf-ray"].includes(k.toLowerCase())) headers[k] = v;
        });
        return {
          url: input.url,
          status: res.status,
          ok: res.status >= 200 && res.status < 400,
          redirectTo: res.headers.get("location") ?? undefined,
          latencyMs: Date.now() - started,
          headers,
          checkedAt: new Date().toISOString(),
        };
      },
    },
    {
      name: "web.crawl_links",
      description: "Fetch a page and return up to N outbound links found in its HTML (with anchor text). Use this for link discovery / light automation of multi-page research.",
      inputSchema: z.object({
        url: z.string().url().max(2000),
        limit: z.number().int().min(1).max(50).optional(),
      }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const limit = input.limit ?? 20;
        const res = await fetch(input.url, {
          headers: { "User-Agent": "Mozilla/5.0 (compatible; zarlino-executive-os/1.0)" },
          redirect: "follow",
        });
        const html = await res.text();
        const links: Array<{ href: string; text: string }> = [];
        const re = /<a[^>]*href="([^"]+)"[^>]*>([\s\S]*?)<\/a>/gi;
        for (const m of html.matchAll(re)) {
          const href = (m[1] ?? "").trim();
          if (!href || href.startsWith("#") || href.startsWith("javascript:")) continue;
          const text = htmlToText(m[2] ?? "").slice(0, 120);
          links.push({ href: href.slice(0, 500), text });
          if (links.length >= limit) break;
        }
        return {
          url: input.url,
          status: res.status,
          ok: res.ok,
          linkCount: links.length,
          links,
        };
      },
    },
  ];
}

/** Re-exported for use elsewhere. */
export type { Tool };
