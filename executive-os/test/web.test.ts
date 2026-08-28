import { afterEach, describe, expect, it, vi } from "vitest";
import { webTools } from "../src/integrations/web";

const env = {};

describe("web integration (web search + web automation)", () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it("web.search parses DuckDuckGo result blocks into titles, urls, snippets", async () => {
    const ddgHtml = `
      <html><body>
        <div class="result">
          <a class="result__a" href="//duckduckgo.com/l/?uddg=https%3A%2F%2Fexample.com%2Fpage&amp;rut=abc">Example Result Title</a>
          <a class="result__snippet">A short snippet about the example.</a>
        </div>
        <div class="result">
          <a class="result__a" href="//duckduckgo.com/l/?uddg=https%3A%2F%2Fother.org%2Fdoc&amp;rut=def">Other Result</a>
          <a class="result__snippet">Another snippet.</a>
        </div>
      </body></html>`;
    vi.stubGlobal(
      "fetch",
      vi.fn(async () => new Response(ddgHtml, { status: 200, headers: { "Content-Type": "text/html" } })),
    );
    const tool = webTools({ env }).find((t) => t.name === "web.search")!;
    const out = (await tool.run({} as never, { query: "zarlino audio" })) as { results: Array<{ title: string; url: string; snippet: string }>; source: string };
    expect(out.results.length).toBe(2);
    expect(out.results[0]?.title).toContain("Example Result Title");
    expect(out.results[0]?.url).toBe("https://example.com/page");
    expect(out.results[0]?.snippet).toContain("snippet");
    expect(out.source).toBe("duckduckgo");
  });

  it("web.search falls back to Wikipedia when DuckDuckGo returns a CAPTCHA challenge", async () => {
    const ddgCaptcha = `<html><body><div class="anomaly-modal__description">Please complete the following challenge to confirm this search was made by a human.</div></body></html>`;
    const wikiBody = JSON.stringify({
      query: {
        search: [
          { title: "FL Studio", snippet: "A digital audio workstation with <span class='searchmatch'>plugins</span>." },
          { title: "VirtualDJ", snippet: "Software that supports VST3 <span class='searchmatch'>plugins</span>." },
        ],
      },
    });
    vi.stubGlobal(
      "fetch",
      vi.fn(async (url: string) => {
        const u = String(url);
        if (u.includes("wikipedia.org")) return new Response(wikiBody, { status: 200, headers: { "Content-Type": "application/json" } });
        return new Response(ddgCaptcha, { status: 200, headers: { "Content-Type": "text/html" } });
      }),
    );
    const tool = webTools({ env }).find((t) => t.name === "web.search")!;
    const out = (await tool.run({} as never, { query: "audio workstation" })) as { results: Array<{ title: string }>; source: string };
    expect(out.source).toBe("wikipedia");
    expect(out.results.length).toBeGreaterThan(0);
    expect(out.results.map((r) => r.title)).toContain("FL Studio");
  });

  it("web.search returns an empty note when both backends yield nothing", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn(async (url: string) => {
        const u = String(url);
        if (u.includes("wikipedia.org")) {
          return new Response(JSON.stringify({ query: { search: [] } }), { status: 200, headers: { "Content-Type": "application/json" } });
        }
        return new Response("<html><body>no results</body></html>", { status: 200, headers: { "Content-Type": "text/html" } });
      }),
    );
    const tool = webTools({ env }).find((t) => t.name === "web.search")!;
    const out = (await tool.run({} as never, { query: "qzxwv nonsense" })) as { results: unknown[]; note: string };
    expect(out.results.length).toBe(0);
    expect(out.note).toMatch(/No parseable results/i);
  });

  it("web.fetch_page strips HTML to readable text and reports status", async () => {
    const html = `<html><head><title>Zarlino Docs</title></head><body><h1>Hello</h1><p>Readable content here.</p><script>var x=1;</script></body></html>`;
    vi.stubGlobal(
      "fetch",
      vi.fn(async () => new Response(html, { status: 200, headers: { "Content-Type": "text/html" } })),
    );
    const tool = webTools({ env }).find((t) => t.name === "web.fetch_page")!;
    const out = (await tool.run({} as never, { url: "https://zarlinoaudio.com/docs" })) as { status: number; ok: boolean; title: string; text: string };
    expect(out.status).toBe(200);
    expect(out.ok).toBe(true);
    expect(out.title).toContain("Zarlino Docs");
    expect(out.text).toContain("Readable content here.");
    expect(out.text).not.toContain("var x=1");
  });

  it("web.check_url returns status and latency without following redirects", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn(async () =>
        new Response(null, { status: 301, headers: { location: "https://zarlinoaudio.com/new" } }),
      ),
    );
    const tool = webTools({ env }).find((t) => t.name === "web.check_url")!;
    const out = (await tool.run({} as never, { url: "https://zarlinoaudio.com/old" })) as { status: number; redirectTo: string };
    expect(out.status).toBe(301);
    expect(out.redirectTo).toBe("https://zarlinoaudio.com/new");
  });

  it("web.crawl_links extracts outbound links with anchor text", async () => {
    const html = `<html><body>
      <a href="/home">Home</a>
      <a href="https://zarlinoaudio.com/shop">Shop</a>
      <a href="#section">Anchor</a>
      <a href="javascript:void(0)">JS</a>
    </body></html>`;
    vi.stubGlobal(
      "fetch",
      vi.fn(async () => new Response(html, { status: 200, headers: { "Content-Type": "text/html" } })),
    );
    const tool = webTools({ env }).find((t) => t.name === "web.crawl_links")!;
    const out = (await tool.run({} as never, { url: "https://zarlinoaudio.com" })) as { linkCount: number; links: Array<{ href: string }> };
    expect(out.linkCount).toBe(2); // /home and https://zarlinoaudio.com/shop (anchor + javascript skipped)
    expect(out.links.map((l) => l.href)).toContain("https://zarlinoaudio.com/shop");
  });
});
