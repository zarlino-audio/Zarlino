/**
 * Product-metrics tests (migration v16): Downloads & Licensed counters.
 *
 * The Founder tracks headline traction (238 downloads / 83 licensed); these
 * live in the metrics table, are exposed in the overview, and are updatable
 * via POST /api/finance/metrics without overwriting on redeploy.
 */
import { describe, expect, it } from "vitest";
import { createTestStore } from "./helpers";
import { nowIso } from "../src/ids";

describe("product metrics: downloads & licensed", () => {
  it("migration v16 seeds downloads=238 and licensed=83", () => {
    const store = createTestStore();
    expect(store.latestMetricByName("downloads")?.value).toBe(238);
    expect(store.latestMetricByName("licensed")?.value).toBe(83);
  });

  it("migrate() is idempotent and never overwrites a Founder update", () => {
    const store = createTestStore();
    // Simulate the Founder raising the counters after launch.
    store.upsertMetric({
      id: "metric_downloads",
      created_at: nowIso(),
      updated_at: nowIso(),
      source: "founder",
      name: "downloads",
      value: 300,
      period: "2026-08",
      confidence: "FACT",
      recorded_at: nowIso(),
    });
    store.migrate(); // re-run must not reset to 238
    expect(store.latestMetricByName("downloads")?.value).toBe(300);
  });

  it("latestMetricByName returns the most recent record per name", () => {
    const store = createTestStore();
    const old = nowIso();
    const fresh = new Date(Date.now() + 60_000).toISOString();
    store.insertMetric({ id: "met_old", created_at: old, updated_at: old, source: "founder", name: "licensed", value: 10, period: "2026-01", confidence: "FACT", recorded_at: old });
    store.insertMetric({ id: "met_fresh", created_at: fresh, updated_at: fresh, source: "founder", name: "licensed", value: 99, period: "2026-08", confidence: "FACT", recorded_at: fresh });
    expect(store.latestMetricByName("licensed")?.value).toBe(99);
  });
});
