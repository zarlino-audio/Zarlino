import { afterEach, describe, expect, it, vi } from "vitest";
import { cloudflareTools } from "../src/integrations/cloudflare";

const env = { CF_API_TOKEN: "tok", CF_ACCOUNT_ID: "acct" };

describe("cloudflare integration (spec §15)", () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it("get_worker returns metadata from the JSON scripts list", async () => {
    const fetchMock = vi.fn(async (_url: string, _init?: RequestInit) =>
      new Response(
        JSON.stringify({
          success: true,
          result: [
            { id: "zarlino-executive-os", created_on: "2026-08-24T00:00:00Z", modified_on: "2026-08-24T01:00:00Z", etag: "abc" },
            { id: "zarlino-website", created_on: "2026-08-16T00:00:00Z", modified_on: "2026-08-24T02:00:00Z", etag: "def" },
          ],
        }),
        { status: 200, headers: { "Content-Type": "application/json" } },
      ),
    );
    vi.stubGlobal("fetch", fetchMock);

    const tool = cloudflareTools({ env }).find((t) => t.name === "cloudflare.get_worker");
    expect(tool).toBeTruthy();

    const out = (await tool!.run({} as never, { workerName: "zarlino-website" })) as {
      id: string;
      etag: string;
      modified_on: string;
    };
    expect(out.id).toBe("zarlino-website");
    expect(out.etag).toBe("def");
    expect(out.modified_on).toBe("2026-08-24T02:00:00Z");

    // The request must target the account's scripts list.
    const url = (fetchMock.mock.calls[0]?.[0] as string) ?? "";
    expect(url).toContain("/accounts/acct/workers/scripts?per_page=50");
  });

  it("get_worker throws when the worker is not found", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn(async () =>
        new Response(JSON.stringify({ success: true, result: [] }), {
          status: 200,
          headers: { "Content-Type": "application/json" },
        }),
      ),
    );
    const tool = cloudflareTools({ env }).find((t) => t.name === "cloudflare.get_worker")!;
    await expect(tool.run({} as never, { workerName: "nope" })).rejects.toThrow(/not found/);
  });

  it("get_deployments returns the JSON result", async () => {
    const deployBody = { success: true, result: [{ id: "d1", created_on: "2026-08-24T00:00:00Z" }] };
    const scriptsBody = {
      success: true,
      result: [{ id: "zarlino-website", created_on: "2026-08-16T00:00:00Z", modified_on: "2026-08-24T02:00:00Z", etag: "def" }],
    };
    vi.stubGlobal(
      "fetch",
      vi.fn(async (url: string) => {
        const u = String(url);
        return new Response(JSON.stringify(u.includes("/scripts?per_page=") ? scriptsBody : deployBody), {
          status: 200,
          headers: { "Content-Type": "application/json" },
        });
      }),
    );
    const tool = cloudflareTools({ env }).find((t) => t.name === "cloudflare.get_deployments")!;
    const out = (await tool.run({} as never, { workerName: "zarlino-website" })) as Array<{ id: string }>;
    expect(out[0]?.id).toBe("d1");
  });

  it("get_deployments throws a helpful error listing available workers when the name is wrong", async () => {
    const scriptsBody = { success: true, result: [{ id: "zarlino-executive-os" }, { id: "zarlino-website" }] };
    vi.stubGlobal(
      "fetch",
      vi.fn(async () =>
        new Response(JSON.stringify(scriptsBody), { status: 200, headers: { "Content-Type": "application/json" } }),
      ),
    );
    const tool = cloudflareTools({ env }).find((t) => t.name === "cloudflare.get_deployments")!;
    await expect(tool.run({} as never, { workerName: "zarlino" })).rejects.toThrow(/Available workers: zarlino-executive-os, zarlino-website/);
  });

  it("list_workers returns all worker scripts", async () => {
    const scriptsBody = {
      success: true,
      result: [
        { id: "zarlino-executive-os", created_on: "2026-08-24T00:00:00Z", modified_on: "2026-08-25T00:00:00Z", etag: "a" },
        { id: "zarlino-website", created_on: "2026-08-16T00:00:00Z", modified_on: "2026-08-24T00:00:00Z", etag: "b" },
      ],
    };
    vi.stubGlobal(
      "fetch",
      vi.fn(async () =>
        new Response(JSON.stringify(scriptsBody), { status: 200, headers: { "Content-Type": "application/json" } }),
      ),
    );
    const tool = cloudflareTools({ env }).find((t) => t.name === "cloudflare.list_workers")!;
    const out = (await tool.run({} as never, {})) as { workers: Array<{ id: string }> };
    expect(out.workers.map((w) => w.id)).toEqual(["zarlino-executive-os", "zarlino-website"]);
  });

  it("get_analytics degrades gracefully when analytics are unavailable (never throws a parse error)", async () => {
    // First call (resolveWorker) → scripts list; GraphQL → error body; REST → multipart (deprecated).
    const scriptsBody = JSON.stringify({ success: true, result: [{ id: "zarlino-executive-os" }] });
    const gqlError = JSON.stringify({ data: null, errors: [{ message: "unknown field" }] });
    const multipart = `--boundary\r\nContent-Disposition: form-data; name="index.js"\r\n\r\nvar x=1;\r\n--boundary--`;
    const calls: string[] = [];
    vi.stubGlobal(
      "fetch",
      vi.fn(async (url: string) => {
        const u = String(url);
        calls.push(u);
        if (u.includes("/graphql")) return new Response(gqlError, { status: 200, headers: { "Content-Type": "application/json" } });
        if (u.includes("/scripts?per_page=")) return new Response(scriptsBody, { status: 200, headers: { "Content-Type": "application/json" } });
        return new Response(multipart, { status: 200, headers: { "Content-Type": "multipart/form-data; boundary=boundary" } });
      }),
    );
    const tool = cloudflareTools({ env }).find((t) => t.name === "cloudflare.get_analytics")!;
    const out = (await tool.run({} as never, { workerName: "zarlino-executive-os", days: 3 })) as { source: string; note?: string };
    // It must not throw — it returns an "unavailable" summary instead.
    expect(out.source).toBe("unavailable");
    expect(out.note).toMatch(/analytics are unavailable/i);
    expect(calls.some((c) => c.includes("/graphql"))).toBe(true);
  });

  it("get_worker_source parses the multipart worker download into source + metadata", async () => {
    const scriptsBody = JSON.stringify({ success: true, result: [{ id: "zarlino-website" }] });
    const boundary = "testboundary123";
    const multipart = [
      `--${boundary}`,
      `Content-Disposition: form-data; name="index.js"`,
      ``,
      `const handler = { fetch() { return new Response("hi"); } };`,
      `export default handler;`,
      `--${boundary}`,
      `Content-Disposition: form-data; name="metadata"`,
      ``,
      `{"main_module":"index.js","compatibility_date":"2024-01-01"}`,
      `--${boundary}--`,
    ].join("\r\n");
    vi.stubGlobal(
      "fetch",
      vi.fn(async (url: string) => {
        const u = String(url);
        if (u.includes("/scripts?per_page=")) return new Response(scriptsBody, { status: 200, headers: { "Content-Type": "application/json" } });
        return new Response(multipart, { status: 200, headers: { "Content-Type": `multipart/form-data; boundary=${boundary}` } });
      }),
    );
    const tool = cloudflareTools({ env }).find((t) => t.name === "cloudflare.get_worker_source")!;
    const out = (await tool.run({} as never, { workerName: "zarlino-website" })) as { source: string; metadata: { main_module: string }; worker: string };
    expect(out.worker).toBe("zarlino-website");
    expect(out.source).toContain('return new Response("hi")');
    expect(out.metadata).toMatchObject({ main_module: "index.js" });
  });
});
