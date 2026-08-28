import { afterEach, describe, expect, it, vi } from "vitest";
import { visionTools } from "../src/integrations/vision";

const env = { DEEPSEEK_API_KEY: "sk-test" };

describe("vision integration (DeepSeek V4 vision)", () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  const visionResponse = JSON.stringify({
    choices: [
      {
        message: {
          role: "assistant",
          content: "This is a plugin UI with a dark theme and a large gain knob.",
          reasoning_content: "The user wants a description.",
        },
      },
    ],
    model: "deepseek-v4-flash-vision-exp",
    usage: { prompt_tokens: 200, completion_tokens: 30 },
  });

  it("analyze_image sends base64 data URL to DeepSeek vision and returns the analysis", async () => {
    const calls: Array<{ url: string; body: string }> = [];
    vi.stubGlobal(
      "fetch",
      vi.fn(async (url: string, init?: RequestInit) => {
        calls.push({ url: String(url), body: String(init?.body) });
        return new Response(visionResponse, { status: 200, headers: { "Content-Type": "application/json" } });
      }),
    );
    const tool = visionTools({ env }).find((t) => t.name === "vision.analyze_image")!;
    const out = (await tool.run({} as never, {
      imageBase64: "aGVsbG8=",
      mimeType: "image/png",
      prompt: "Describe this plugin UI.",
    })) as { analysis: string; model: string };
    expect(out.analysis).toContain("plugin UI");
    expect(out.model).toBe("deepseek-v4-flash-vision-exp");
    const body = JSON.parse(calls[0]?.body ?? "{}") as { model: string; messages: Array<{ content: Array<{ type: string; image_url: { url: string } }> }> };
    expect(body.model).toBe("deepseek-v4-flash-vision-exp");
    expect(body.messages[0]?.content[1]?.image_url.url).toBe("data:image/png;base64,aGVsbG8=");
  });

  it("analyze_image requires an image source", async () => {
    const tool = visionTools({ env }).find((t) => t.name === "vision.analyze_image")!;
    await expect(tool.run({} as never, { prompt: "hi" })).rejects.toThrow(/imageUrl or imageBase64/);
  });

  it("analyze_url_image downloads the image first, then sends base64", async () => {
    const calls: string[] = [];
    vi.stubGlobal(
      "fetch",
      vi.fn(async (url: string, init?: RequestInit) => {
        calls.push(String(url));
        if (String(url).startsWith("https://example.com/img.png")) {
          return new Response(Buffer.from("fake-png-bytes"), { status: 200, headers: { "Content-Type": "image/png" } });
        }
        return new Response(visionResponse, { status: 200, headers: { "Content-Type": "application/json" } });
      }),
    );
    const tool = visionTools({ env }).find((t) => t.name === "vision.analyze_url_image")!;
    const out = (await tool.run({} as never, { url: "https://example.com/img.png" })) as { source: string; bytes: number; analysis: string };
    expect(out.source).toBe("https://example.com/img.png");
    expect(out.bytes).toBe("fake-png-bytes".length);
    expect(out.analysis).toContain("plugin UI");
    // Two fetches: the image download + the DeepSeek API.
    expect(calls.filter((c) => c.startsWith("https://example.com")).length).toBe(1);
    expect(calls.some((c) => c.includes("api.deepseek.com"))).toBe(true);
  });

  it("throws a clear error when DeepSeek vision is not configured", async () => {
    const tool = visionTools({ env: {} }).find((t) => t.name === "vision.analyze_image")!;
    await expect(tool.run({} as never, { imageBase64: "aGVsbG8=" })).rejects.toThrow(/DEEPSEEK_API_KEY/);
  });

  it("records token usage + cost into the store when one is provided", async () => {
    const recorded: unknown[] = [];
    const store = {
      record: (rec: unknown) => { recorded.push(rec); },
    } as never;
    vi.stubGlobal(
      "fetch",
      vi.fn(async () => new Response(visionResponse, { status: 200, headers: { "Content-Type": "application/json" } })),
    );
    const tool = visionTools({ env }).find((t) => t.name === "vision.analyze_image")!;
    const tctx = { store } as never;
    await tool.run(tctx, { imageUrl: "https://example.com/x.png" });
    expect(recorded.length).toBe(1);
    const rec = recorded[0] as { inputTokens: number; outputTokens: number; provider: string; model: string; cost: number; taskType: string };
    expect(rec.inputTokens).toBe(200);
    expect(rec.outputTokens).toBe(30);
    expect(rec.provider).toBe("deepseek");
    expect(rec.model).toBe("deepseek-v4-flash-vision-exp");
    expect(rec.cost).toBeGreaterThan(0);
    expect(rec.taskType).toBe("lens_analysis");
    // A second call without a store does not crash and records nothing.
    recorded.length = 0;
    await tool.run({} as never, { imageUrl: "https://example.com/x.png" });
    expect(recorded.length).toBe(0);
  });
});
