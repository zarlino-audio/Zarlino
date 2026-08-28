/**
 * Test helpers: in-memory store (node:sqlite), mock gateway, and reasoners.
 */

import { createMemorySql } from "../src/db/sql";
import { ExecutiveStore } from "../src/db/store";
import { seedIfEmpty } from "../src/seed/seed";
import { ModelGateway } from "../src/core/model/gateway";
import { mockProvider, type MockProviderRule } from "../src/core/model/providers";
import { DefaultCostTracker, MemoryCostStorage } from "../src/core/model/cost";
import { GatewayReasoner } from "../src/agent/reasoner";
import { DEFAULT_CONFIG } from "../src/config";
import type { DecisionReasoner } from "../src/core/decision/engine";

export function createTestStore(seed = true): ExecutiveStore {
  const store = new ExecutiveStore(createMemorySql());
  store.migrate();
  if (seed) seedIfEmpty(store);
  return store;
}

/** Gateway backed by the deterministic mock provider + memory cost storage. */
export function createMockGateway(rules?: MockProviderRule[]): {
  gateway: ModelGateway;
  cost: DefaultCostTracker;
  storage: MemoryCostStorage;
} {
  const storage = new MemoryCostStorage();
  const cost = new DefaultCostTracker(
    {
      daily: 2,
      monthly: 50,
      perTask: 0.5,
      perRun: 1,
    },
    storage,
  );
  const gateway = new ModelGateway({
    config: {
      modelTierCheap: "mock:mock-model",
      modelTierMedium: "mock:mock-model",
      modelTierStrong: "mock:mock-model",
      modelTierStrongest: "mock:mock-model",
    },
    providers: [mockProvider(rules)],
    cost,
  });
  return { gateway, cost, storage };
}

/** Reasoner over the mock gateway (exercises the real reasoner + JSON parsing). */
export function createMockReasoner(rules?: MockProviderRule[]): {
  reasoner: DecisionReasoner;
  gateway: ModelGateway;
} {
  const { gateway } = createMockGateway(rules);
  return { reasoner: new GatewayReasoner({ gateway }), gateway };
}

export { DEFAULT_CONFIG };
