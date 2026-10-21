#!/usr/bin/env node
/**
 * Compare the committed module catalogue with one generated from the compiled
 * kernel.
 *
 *   node factory/tools/catalog-compare.mjs factory/catalog/modules.json generated.json
 *
 * Exit 0 when they describe the same modules, parameters, domains, defaults,
 * stepping and latency; exit 1 with a readable diff otherwise.
 *
 * The comparison is semantic, not textual: the kernel prints floats with six
 * significant digits, so numbers are compared with a tolerance instead of as
 * strings. Everything else must match exactly, including parameter order (the
 * order is part of the UI contract).
 */

import { readFileSync } from "node:fs";

const TOLERANCE = 1e-4;

const [expectedPath, actualPath] = process.argv.slice(2);
if (!expectedPath || !actualPath) {
  console.error("usage: catalog-compare.mjs <expected.json> <actual.json>");
  process.exit(2);
}

function load(path) {
  try {
    // Tolerate a UTF-8 BOM: PowerShell and some editors add one.
    const text = readFileSync(path, "utf8").replace(/^\uFEFF/, "");
    const parsed = JSON.parse(text);
    if (!Array.isArray(parsed)) throw new Error("root is not an array");
    return parsed;
  } catch (error) {
    console.error(`cannot read ${path}: ${error instanceof Error ? error.message : String(error)}`);
    process.exit(2);
  }
}

const expected = load(expectedPath);
const actual = load(actualPath);
const problems = [];

const byId = (list) => new Map(list.map((module) => [module.id, module]));

const expectedById = byId(expected);
const actualById = byId(actual);

for (const id of expectedById.keys()) {
  if (!actualById.has(id)) problems.push(`module '${id}' is in ${expectedPath} but not in the kernel`);
}
for (const id of actualById.keys()) {
  if (!expectedById.has(id)) problems.push(`module '${id}' is in the kernel but missing from ${expectedPath}`);
}

for (const [id, left] of expectedById) {
  const right = actualById.get(id);
  if (!right) continue;

  for (const field of ["version", "displayName", "description", "category", "certification", "referenceLatencySamples"]) {
    if (JSON.stringify(left[field]) !== JSON.stringify(right[field])) {
      problems.push(`${id}.${field}: committed '${JSON.stringify(left[field])}' vs kernel '${JSON.stringify(right[field])}'`);
    }
  }

  const leftChannels = [...(left.channels ?? [])].sort().join(",");
  const rightChannels = [...(right.channels ?? [])].sort().join(",");
  if (leftChannels !== rightChannels) problems.push(`${id}.channels: '${leftChannels}' vs '${rightChannels}'`);

  const leftParameters = left.parameters ?? [];
  const rightParameters = right.parameters ?? [];
  if (leftParameters.length !== rightParameters.length) {
    problems.push(`${id}: ${leftParameters.length} committed parameters vs ${rightParameters.length} in the kernel`);
  }

  const count = Math.min(leftParameters.length, rightParameters.length);
  for (let index = 0; index < count; index += 1) {
    const a = leftParameters[index];
    const b = rightParameters[index];
    if (a.id !== b.id) {
      problems.push(`${id}.parameters[${index}]: committed '${a.id}' vs kernel '${b.id}' (order matters)`);
      continue;
    }
    const label = `${id}.${a.id}`;
    if (a.displayName !== b.displayName) problems.push(`${label}.displayName: '${a.displayName}' vs '${b.displayName}'`);
    if (a.unit !== b.unit) problems.push(`${label}.unit: '${a.unit}' vs '${b.unit}'`);
    if (a.group !== b.group) problems.push(`${label}.group: '${a.group}' vs '${b.group}'`);
    if (Boolean(a.logarithmic) !== Boolean(b.logarithmic)) problems.push(`${label}.logarithmic differs`);
    if (Number(a.stepCount ?? 0) !== Number(b.stepCount ?? 0)) problems.push(`${label}.stepCount differs`);
    for (const field of ["min", "max", "default"]) {
      if (Math.abs(Number(a[field]) - Number(b[field])) > TOLERANCE) {
        problems.push(`${label}.${field}: ${a[field]} vs ${b[field]}`);
      }
    }
  }
}

if (problems.length > 0) {
  console.error(`catalogue mismatch (${problems.length}):`);
  for (const problem of problems) console.error(`  - ${problem}`);
  console.error("\nRegenerate factory/catalog/modules.json from `ZarlinoFactory_Check --catalog-json`");
  console.error("and update zarlino-website/src/factory/catalog.ts to match.");
  process.exit(1);
}

console.log(`catalogue matches the compiled kernel (${expected.length} modules)`);
