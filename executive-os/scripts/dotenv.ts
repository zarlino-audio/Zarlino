/**
 * Minimal .env-style loader (key=value, # comments, blank lines ignored).
 * Used by scripts so API keys never appear on the command line.
 */
import { readFileSync } from "node:fs";

export function loadDotEnv(filePath: string): Record<string, string> {
  const out: Record<string, string> = {};
  try {
    const text = readFileSync(filePath, "utf8");
    for (const rawLine of text.split(/\r?\n/)) {
      const line = rawLine.trim();
      if (!line || line.startsWith("#")) continue;
      const eq = line.indexOf("=");
      if (eq <= 0) continue;
      const key = line.slice(0, eq).trim();
      let value = line.slice(eq + 1).trim();
      if (value.startsWith('"') && value.endsWith('"')) value = value.slice(1, -1);
      out[key] = value;
    }
  } catch {
    // file missing → empty
  }
  return out;
}
