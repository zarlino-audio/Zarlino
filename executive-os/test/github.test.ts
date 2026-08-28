import { afterEach, describe, expect, it, vi } from "vitest";
import { githubTools, sliceContentChunk } from "../src/integrations/github";

const env = { GITHUB_TOKEN: "tok", GITHUB_OWNER: "zarlino-audio", GITHUB_REPO: "Zarlino" };

describe("github integration (spec §14) — repo-aware access to website source", () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  function stubJson(value: unknown) {
    vi.stubGlobal(
      "fetch",
      vi.fn(async () => new Response(JSON.stringify(value), { status: 200, headers: { "Content-Type": "application/json" } })),
    );
  }

  it("defaults to GITHUB_REPO when no repo is given", async () => {
    const calls: string[] = [];
    vi.stubGlobal(
      "fetch",
      vi.fn(async (url: string) => {
        calls.push(String(url));
        return new Response(JSON.stringify({ default_branch: "main" }), { status: 200, headers: { "Content-Type": "application/json" } });
      }),
    );
    const tool = githubTools({ env }).find((t) => t.name === "github.read_repo")!;
    await tool.run({} as never, {});
    expect(calls[0]).toContain("/repos/zarlino-audio/Zarlino");
  });

  it("read_repo can target the zarlino-website repo", async () => {
    stubJson({ full_name: "zarlino-audio/zarlino-website", private: false });
    const tool = githubTools({ env }).find((t) => t.name === "github.read_repo")!;
    const out = (await tool.run({} as never, { repo: "zarlino-website" })) as { full_name: string };
    expect(out.full_name).toBe("zarlino-audio/zarlino-website");
  });

  it("get_file reads website source when repo='zarlino-website'", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn(async () =>
        new Response(JSON.stringify({ encoding: "base64", size: 5, content: Buffer.from("hello").toString("base64") }), {
          status: 200,
          headers: { "Content-Type": "application/json" },
        }),
      ),
    );
    const tool = githubTools({ env }).find((t) => t.name === "github.get_file")!;
    const out = (await tool.run({} as never, { repo: "zarlino-website", path: "src/index.html" })) as { content: string; repo: string };
    expect(out.content).toBe("hello");
    expect(out.repo).toBe("zarlino-website");
  });

  it("get_website_source reads the website repo directly (no repo param needed)", async () => {
    const calls: string[] = [];
    vi.stubGlobal(
      "fetch",
      vi.fn(async (url: string) => {
        calls.push(String(url));
        return new Response(JSON.stringify({ encoding: "base64", size: 5, content: Buffer.from("site!").toString("base64") }), {
          status: 200,
          headers: { "Content-Type": "application/json" },
        });
      }),
    );
    const tool = githubTools({ env }).find((t) => t.name === "github.get_website_source")!;
    const out = (await tool.run({} as never, { path: "src/app.tsx" })) as { content: string; repo: string };
    expect(out.content).toBe("site!");
    expect(out.repo).toBe("zarlino-website");
    expect(calls[0]).toContain("/repos/zarlino-audio/zarlino-website/contents/src/app.tsx");
  });

  it("list_repos falls back to /user/repos when the org lookup 404s (user account, not org)", async () => {
    const calls: string[] = [];
    vi.stubGlobal(
      "fetch",
      vi.fn(async (url: string) => {
        calls.push(String(url));
        if (String(url).includes("/orgs/")) {
          return new Response("not found", { status: 404, headers: { "Content-Type": "application/json" } });
        }
        return new Response(JSON.stringify([{ name: "zarlino-website" }, { name: "zarlino-feedback" }, { name: "Zarlino" }]), {
          status: 200,
          headers: { "Content-Type": "application/json" },
        });
      }),
    );
    const tool = githubTools({ env }).find((t) => t.name === "github.list_repos")!;
    const out = (await tool.run({} as never, {})) as Array<{ name: string }>;
    expect(out.map((r) => r.name)).toContain("zarlino-website");
    expect(calls.some((c) => c.includes("/user/repos"))).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// Large-file paging: an agent must be able to read the TAIL of a big worker
// source, not just the first truncated prefix (the "output keeps truncating
// at the same point" bug — run_87c9 could never reach handleAdminStats).
// ---------------------------------------------------------------------------

describe("github integration — file paging (offset/maxBytes)", () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  const BIG = "A".repeat(10_000) + "\nFETCH_GITHUB\n" + "B".repeat(10_000) + "\nHANDLE_ADMIN_STATS\n" + "C".repeat(10_000);

  function stubBigFile() {
    vi.stubGlobal(
      "fetch",
      vi.fn(async () =>
        new Response(
          JSON.stringify({ encoding: "base64", size: BIG.length, content: Buffer.from(BIG).toString("base64") }),
          { status: 200, headers: { "Content-Type": "application/json" } },
        ),
      ),
    );
  }

  it("get_website_source pages through a large file with offset", async () => {
    stubBigFile();
    const tool = githubTools({ env }).find((t) => t.name === "github.get_website_source")!;

    // First read: default maxBytes=6000 → only the head, hasMore + metadata.
    const first = (await tool.run({} as never, { path: "src/worker.ts" })) as {
      content: string; offset: number; totalChars: number; returnedChars: number; hasMore: boolean; note?: string;
    };
    expect(first.totalChars).toBe(BIG.length);
    expect(first.offset).toBe(0);
    expect(first.returnedChars).toBeLessThanOrEqual(6_000);
    expect(first.hasMore).toBe(true);
    expect(first.note).toContain("offset=");
    // The tail marker is NOT in the head chunk (the original bug).
    expect(first.content).not.toContain("HANDLE_ADMIN_STATS");

    // Middle read: reaches FETCH_GITHUB (lives at chars ~10_001..10_012).
    const mid = (await tool.run({} as never, { path: "src/worker.ts", offset: 9_000 })) as {
      content: string; offset: number; returnedChars: number; hasMore: boolean;
    };
    expect(mid.content).toContain("FETCH_GITHUB");

    // Tail read: reaches HANDLE_ADMIN_STATS.
    const tail = (await tool.run({} as never, { path: "src/worker.ts", offset: 20_000 })) as {
      content: string; offset: number; returnedChars: number; hasMore: boolean;
    };
    expect(tail.content).toContain("HANDLE_ADMIN_STATS");

    // Last read: reaches the end → hasMore false, complete tail.
    const last = (await tool.run({} as never, { path: "src/worker.ts", offset: 20_000 + tail.returnedChars })) as {
      content: string; hasMore: boolean;
    };
    expect(last.content.startsWith("C")).toBe(true);
    expect(last.hasMore).toBe(false);
  });

  it("get_file pages with offset and returns the tail", async () => {
    stubBigFile();
    const tool = githubTools({ env }).find((t) => t.name === "github.get_file")!;
    // Skip to where HANDLE_ADMIN_STATS lives (chars 20_015..20_032).
    const out = (await tool.run({} as never, { repo: "zarlino-website", path: "src/worker.ts", offset: 20_000, maxBytes: 5000 })) as {
      content: string; offset: number; totalChars: number; hasMore: boolean;
    };
    expect(out.offset).toBe(20_000);
    expect(out.content).toContain("HANDLE_ADMIN_STATS");
    expect(out.totalChars).toBe(BIG.length);
    expect(out.hasMore).toBe(true);
  });

  it("sliceContentChunk handles out-of-range and zero offsets", () => {
    const text = "0123456789";
    expect(sliceContentChunk(text, 0, 4).content).toBe("0123");
    expect(sliceContentChunk(text, 0, 4).hasMore).toBe(true);
    expect(sliceContentChunk(text, 8).content).toBe("89");
    expect(sliceContentChunk(text, 8).hasMore).toBe(false);
    expect(sliceContentChunk(text, 999).content).toBe("");
    expect(sliceContentChunk(text, -5, 3).content).toBe("012");
  });
});
