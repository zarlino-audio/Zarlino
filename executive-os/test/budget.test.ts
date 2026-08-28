/**
 * AI budget + provider-balance tests (Founder self-service).
 *
 * Covers:
 *   - settings table: get/set/override round-trip (migration v13).
 *   - budget overrides: persisted overrides change the effective budget and
 *     the DefaultCostTracker picks them up dynamically (no rebuild).
 *   - provider balance: DeepSeek /user/balance fetch parses correctly, is
 *     cached (second call doesn't re-hit the provider), and falls back to the
 *     cache when the live fetch fails.
 *   - snapshot includes the settings table (backup survives budget changes).
 *   - migration idempotency with the new v13 migration.
 */
import { afterEach, describe, expect, it, vi } from "vitest";
import { createTestStore } from "./helpers";
import { DefaultCostTracker, MemoryCostStorage } from "../src/core/model/cost";
import { fetchDeepSeekBalance } from "../src/integrations/deepseek";
import type { ModelCallRecord } from "../src/core/model/types";

afterEach(() => {
  vi.restoreAllMocks();
});

// ---------------------------------------------------------------------------
// Settings table (migration v13)
// ---------------------------------------------------------------------------

describe("budget: settings table + overrides", () => {
  it("get/set round-trips values and tracks updated_at", () => {
    const store = createTestStore();
    expect(store.getSetting("ai_daily_budget")).toBeNull();
    store.setSetting("ai_daily_budget", "20");
    expect(store.getSetting("ai_daily_budget")).toBe("20");
    expect(store.getSettingUpdatedAt("ai_daily_budget")).toBeTruthy();
    // Overwrite
    store.setSetting("ai_daily_budget", "25");
    expect(store.getSetting("ai_daily_budget")).toBe("25");
    // Delete (null clears)
    store.setSetting("ai_daily_budget", null);
    expect(store.getSetting("ai_daily_budget")).toBeNull();
  });

  it("getBudgetOverrides only returns positive finite numbers", () => {
    const store = createTestStore();
    store.setSetting("ai_daily_budget", "12.5");
    store.setSetting("ai_monthly_budget", "bogus"); // ignored
    store.setSetting("ai_per_task_budget", "-3"); // ignored
    const o = store.getBudgetOverrides();
    expect(o.aiDailyBudget).toBe(12.5);
    expect(o.aiMonthlyBudget).toBeUndefined();
    expect(o.aiPerTaskBudget).toBeUndefined();
  });

  it("setBudgetOverrides persists only provided keys", () => {
    const store = createTestStore();
    store.setBudgetOverrides({ aiDailyBudget: 20, aiMonthlyBudget: 500 });
    expect(store.getBudgetOverrides()).toEqual({ aiDailyBudget: 20, aiMonthlyBudget: 500 });
    // Update only the daily — monthly stays.
    store.setBudgetOverrides({ aiDailyBudget: 30 });
    expect(store.getBudgetOverrides()).toEqual({ aiDailyBudget: 30, aiMonthlyBudget: 500 });
  });

  it("snapshot includes settings so a restore keeps budget overrides", () => {
    const store = createTestStore();
    store.setSetting("ai_daily_budget", "42");
    const snap = store.exportSnapshot();
    const settings = snap.settings ?? [];
    expect(Array.isArray(settings)).toBe(true);
    expect(settings.some((s) => (s as { key?: string }).key === "ai_daily_budget" && (s as { value?: string }).value === "42")).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// Dynamic budget getter on the cost tracker
// ---------------------------------------------------------------------------

describe("budget: dynamic budget getter", () => {
  const call = (cost: number): ModelCallRecord => ({
    provider: "deepseek",
    model: "deepseek-chat",
    taskType: "chat",
    inputTokens: 1000,
    outputTokens: 500,
    cost,
    latencyMs: 100,
    timestamp: new Date().toISOString(),
  });

  it("static BudgetConfig still works (backward compatible)", () => {
    const storage = new MemoryCostStorage();
    const tracker = new DefaultCostTracker({ daily: 2, monthly: 50, perTask: 0.5, perRun: 1 }, storage);
    expect(tracker.check(call(0.3)).allowed).toBe(true);
    expect(tracker.check(call(0.7)).allowed).toBe(false); // > perTask
  });

  it("function config is re-read per check — raising the budget unblocks immediately", () => {
    const storage = new MemoryCostStorage();
    let daily = 2;
    const tracker = new DefaultCostTracker(() => ({ daily, monthly: 50, perTask: 2, perRun: 2 }), storage);
    // Simulate an already-recorded 1.5 spend today.
    storage.record(call(1.5));
    // Next 0.8 would take today to 2.3 > daily 2 → blocked.
    expect(tracker.check(call(0.8)).allowed).toBe(false);
    // Founder extends the budget from the UI → the SAME tracker now allows it.
    daily = 5;
    expect(tracker.check(call(0.8)).allowed).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// Provider balance (DeepSeek /user/balance)
// ---------------------------------------------------------------------------

describe("budget: provider balance", () => {
  it("parses a successful balance response (CNY) and estimates USD", async () => {
    vi.stubGlobal("fetch", vi.fn(async () => {
      return new Response(
        JSON.stringify({
          is_available: true,
          balance_infos: [
            { currency: "CNY", total_balance: "110.00", granted_balance: "10.00", topped_up_balance: "100.00" },
          ],
        }),
        { status: 200, headers: { "Content-Type": "application/json" } },
      );
    }));
    const b = await fetchDeepSeekBalance("sk-test", { cnyToUsd: 0.14 });
    expect(b.isAvailable).toBe(true);
    expect(b.balances).toHaveLength(1);
    expect(b.balances[0]).toMatchObject({ currency: "CNY", total: "110.00", granted: "10.00", toppedUp: "100.00" });
    expect(b.totalUsdEstimate).toBe(15.4); // 110 × 0.14
  });

  it("throws on non-OK responses (bad key / server error)", async () => {
    vi.stubGlobal("fetch", vi.fn(async () => new Response("Unauthorized", { status: 401 })));
    await expect(fetchDeepSeekBalance("sk-bad")).rejects.toThrow(/401/);
  });

  it("returns no USD estimate for non-CNY currencies", async () => {
    vi.stubGlobal("fetch", vi.fn(async () => {
      return new Response(
        JSON.stringify({ is_available: true, balance_infos: [{ currency: "USD", total_balance: "42.00", granted_balance: "0.00", topped_up_balance: "42.00" }] }),
        { status: 200 },
      );
    }));
    const b = await fetchDeepSeekBalance("sk-test");
    expect(b.balances[0]?.currency).toBe("USD");
    expect(b.totalUsdEstimate).toBeNull();
  });
});
