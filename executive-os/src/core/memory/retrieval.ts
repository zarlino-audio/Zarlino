/**
 * Memory retrieval (spec §27).
 *
 * Retrieval scores candidate memory entries by:
 *   - semantic/entity relevance (keyword + entity-id overlap),
 *   - recency,
 *   - importance,
 *   - decision relevance (LESSON / POLICY kinds weigh higher for decisions),
 *   - authority (higher-confidence entries weigh higher).
 *
 * A question about engineering must not load the entire marketing history.
 */

import type { Memory } from "../../types";

export interface RetrievalOptions {
  /** Free-text query (e.g. the decision problem). */
  query?: string;
  /** Restrict to specific layers, e.g. ["L1","L2"]. */
  layers?: Array<Memory["layer"]>;
  /** Restrict to specific kinds. */
  kinds?: Array<Memory["kind"]>;
  /** Restrict to a specific entity. */
  entityId?: string;
  /** Cap on results. */
  limit?: number;
  /** Boost recency more heavily (default 1). */
  recencyWeight?: number;
  /** Boost importance (default 1). */
  importanceWeight?: number;
  /** Minimum score to include. */
  minScore?: number;
}

const STOP_WORDS = new Set([
  "the", "a", "an", "and", "or", "but", "of", "to", "in", "on", "for", "with",
  "is", "are", "was", "were", "be", "been", "it", "this", "that", "what",
  "should", "can", "could", "would", "do", "does", "how", "why", "when",
  "about", "from", "at", "by", "as", "if", "not", "no", "we", "our", "us",
]);

function tokenize(text: string): string[] {
  return (text.toLowerCase().match(/[a-z0-9_]+/g) ?? []).filter((t) => !STOP_WORDS.has(t) && t.length > 1);
}

function jaccard(a: Set<string>, b: Set<string>): number {
  if (a.size === 0 || b.size === 0) return 0;
  let inter = 0;
  for (const t of a) if (b.has(t)) inter++;
  const union = new Set([...a, ...b]).size;
  return inter / union;
}

/** Days since entry creation, clamped to [0, 365]. */
function daysSince(iso: string, now: number): number {
  const t = new Date(iso).getTime();
  if (Number.isNaN(t)) return 365;
  return Math.max(0, Math.min(365, (now - t) / 86_400_000));
}

const KIND_BOOST: Record<Memory["kind"], number> = {
  FACT: 1.0,
  INFERENCE: 0.85,
  ASSUMPTION: 0.7,
  ESTIMATE: 0.8,
  UNKNOWN: 0.5,
  LESSON: 1.15,
  POLICY: 1.1,
};

export function scoreMemory(entry: Memory, query: string, now = Date.now()): number {
  const queryTokens = new Set(tokenize(query));
  const titleTokens = new Set(tokenize(entry.title));
  const bodyTokens = new Set(tokenize(entry.body));
  const tagsTokens = new Set(tokenize(entry.tags_json ?? ""));

  const relevance = jaccard(queryTokens, titleTokens) * 1.5 + jaccard(queryTokens, bodyTokens) * 1.0 + jaccard(queryTokens, tagsTokens) * 1.2;

  const recency = Math.max(0, 1 - daysSince(entry.created_at, now) / 365);
  const importance = entry.importance;
  const confidence = entry.confidence ?? 0.5;
  const kindBoost = KIND_BOOST[entry.kind];

  const score = relevance * 3 + recency * 1 + importance * 1.5 + confidence * 0.5 + kindBoost * 0.2;
  return score;
}

export function retrieve(memories: Memory[], opts: RetrievalOptions = {}): Memory[] {
  const now = Date.now();
  const query = opts.query ?? "";

  const filtered = memories.filter((m) => {
    if (opts.layers && !opts.layers.includes(m.layer)) return false;
    if (opts.kinds && !opts.kinds.includes(m.kind)) return false;
    if (opts.entityId && m.entity_id !== opts.entityId) return false;
    if (m.status !== "ACTIVE") return false;
    if (m.expires_at && new Date(m.expires_at).getTime() < now) return false;
    return true;
  });

  const scored = filtered.map((m) => ({ m, score: scoreMemory(m, query, now) }));
  scored.sort((a, b) => b.score - a.score);

  const limit = opts.limit ?? 10;
  const minScore = opts.minScore ?? 0;
  return scored.filter((s) => s.score >= minScore).slice(0, limit).map((s) => s.m);
}

/** Compose a compact memory context block for prompts (spec §26). */
export function renderMemoryContext(memories: Memory[], maxChars = 4000): string {
  if (memories.length === 0) return "(no relevant memory)";
  const lines = memories.map((m) => `[${m.layer}/${m.kind}] ${m.title}: ${m.body.slice(0, 300)}`);
  let out = lines.join("\n");
  if (out.length > maxChars) out = out.slice(0, maxChars) + "…";
  return out;
}
