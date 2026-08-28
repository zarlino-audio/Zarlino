/**
 * DeepSeek live smoke test.
 *
 * Verifies the ModelGateway end-to-end against the real DeepSeek API:
 *   1. classification  → cheap tier (deepseek-chat)
 *   2. CFO lens        → strong tier (deepseek-reasoner)
 *
 * The API key is read from .dev.vars (git-ignored) and is never printed.
 * Run: npm run smoke:deepseek
 */

import { resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { ModelGateway } from "../src/core/model/gateway";
import { deepseekProvider } from "../src/core/model/providers";
import { DefaultCostTracker, MemoryCostStorage } from "../src/core/model/cost";
import { loadDotEnv } from "./dotenv";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const apiKey = loadDotEnv(resolve(root, ".dev.vars")).DEEPSEEK_API_KEY;

if (!apiKey) {
  console.error("DEEPSEEK_API_KEY not found in .dev.vars — add it first.");
  process.exit(1);
}

const storage = new MemoryCostStorage();
const cost = new DefaultCostTracker(
  { daily: 2, monthly: 50, perTask: 0.5, perRun: 1 },
  storage,
);

const gateway = new ModelGateway({
  config: {
    modelTierCheap: "deepseek:deepseek-chat",
    modelTierMedium: "deepseek:deepseek-chat",
    modelTierStrong: "deepseek:deepseek-reasoner",
    modelTierStrongest: "deepseek:deepseek-reasoner",
  },
  providers: [deepseekProvider(() => apiKey)],
  cost,
  onCall: (c) => {
    console.log(`  [gateway] ${c.provider}/${c.model} · ${c.inputTokens}+${c.outputTokens} tok · $${c.cost.toFixed(5)} · ${c.taskType}`);
  },
});

console.log("== 1. Classification → cheap tier (deepseek-chat) ==");
const cls = await gateway.complete({
  taskType: "classification",
  system:
    'Classify the Founder request into exactly one JSON object: { "intent": "decision" | "query" | "command", "category": "STRATEGIC" | "FINANCIAL" | "TECHNICAL" | "OPERATIONAL" | "MARKETING", "title": "short title" }.',
  user: "Should we spend $250 validating the affiliate acquisition funnel?",
  jsonMode: true,
  maxTokens: 150,
});
console.log(cls.content.trim());

console.log("\n== 2. CFO lens → strong tier (deepseek-reasoner) ==");
const cfo = await gateway.complete({
  taskType: "financial_reasoning",
  system:
    "You are the CFO lens. Evaluate cash, runway, downside, and ROI numerically. Distinguish Known / Estimated / Assumed / Unknown. NEVER fabricate financial data.",
  user: "Proposed spend: $250 on affiliate validation. Cash: UNKNOWN. Monthly expenses: ~$1800. What should the CFO say?",
  maxTokens: 350,
});
console.log(cfo.content.trim());

console.log(`\nTotal cost for this run: $${storage.spentToday().toFixed(5)}`);
