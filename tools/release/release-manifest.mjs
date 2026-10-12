#!/usr/bin/env node
/**
 * Zarlino Audio — RELEASE MANIFEST tool (schema v1)
 * =====================================================================
 * A Zarlino plugin release is the ATOMIC unit: binaries + installer +
 * standalone + canonical screenshot + (canonical audio examples) + version
 * metadata + commit SHA + hashes — all belonging to the SAME release
 * candidate. The manifest is the authoritative relationship between them;
 * nothing else (website, monitoring, agents) may pair assets independently.
 *
 * This tool is dependency-free (node:crypto only) and is wired into
 * .github/workflows/ztame-release-gate.yml:
 *
 *   generate  ->  builds the manifest + sha256 for every artifact, status
 *                 "verifying". Refuses to run when a required artifact is
 *                 missing (a release without its binaries is not a release).
 *   validate  ->  structural validation of the manifest itself.
 *   finalize  ->  re-reads every referenced file from disk, RECOMPUTES the
 *                 sha256s, compares against the recorded ones, and only then
 *                 flips status to "verified" (+ verifiedAt). Any mismatch
 *                 fails closed: the published manifest must never point at an
 *                 artifact that failed integrity verification.
 *
 * STATUS ENUM (schema v1):
 *   building | testing | rendering | verifying | verified | published | failed
 * Only "verified" may become publicly releasable.
 *
 * USAGE
 *   node tools/release/release-manifest.mjs generate \
 *     --product ZTame --version 1.2.0 --commit <40-hex> \
 *     --out artifacts/release-manifest.json \
 *     --installer artifacts/ZTame-v1.2.0-Windows-Installer.exe \
 *     --vst3 artifacts/ZTame-v1.2.0-Windows-VST3.zip \
 *     --standalone artifacts/ZTame-v1.2.0-Windows-Standalone.zip \
 *     [--screenshot artifacts/ztame-ui.png] [--gaps "..."]...
 *   node tools/release/release-manifest.mjs validate --manifest <file>
 *   node tools/release/release-manifest.mjs finalize --manifest <file>
 *
 * EXIT 0 ok · 1 invalid / failed closed
 */

import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";
import { basename } from "node:path";

const SCHEMA_VERSION = 1;
const RELEASABLE_STATUSES = ["verifying", "verified"];
const ALL_STATUSES = [
  "building",
  "testing",
  "rendering",
  "verifying",
  "verified",
  "published",
  "failed",
];

const SHA256_RE = /^[0-9a-f]{64}$/;
const COMMIT_RE = /^[0-9a-f]{40}$/;
const VERSION_RE = /^\d+\.\d+\.\d+([-.+][0-9A-Za-z.-]+)?$/;

function fail(message) {
  console.error("release-manifest: FAIL " + message);
  process.exit(1);
}

function sha256File(path) {
  const buf = readFileSync(path);
  return { sha256: createHash("sha256").update(buf).digest("hex"), bytes: buf.length };
}

function parseFlags(argv) {
  const flags = { gaps: [] };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (!a.startsWith("--")) continue;
    const key = a.slice(2);
    const value = argv[i + 1];
    if (value === undefined || value.startsWith("--")) {
      flags[key] = true;
    } else {
      if (key === "gaps") flags.gaps.push(value);
      else flags[key] = value;
      i++;
    }
  }
  return flags;
}

/** Structural validation. Returns [] when valid, otherwise a list of errors. */
function validateManifest(m, { forFinalize = false } = {}) {
  const errors = [];
  if (!m || typeof m !== "object") return ["manifest is not an object"];
  if (m.schemaVersion !== SCHEMA_VERSION) errors.push(`schemaVersion must be ${SCHEMA_VERSION}`);
  if (typeof m.product !== "string" || !m.product.trim()) errors.push("product is required");
  if (typeof m.version !== "string" || !VERSION_RE.test(m.version)) {
    errors.push("version must be semver-like (e.g. 1.2.0)");
  }
  if (typeof m.commit !== "string" || !COMMIT_RE.test(m.commit)) {
    errors.push("commit must be a 40-char lowercase hex git SHA");
  }
  if (typeof m.builtAt !== "string" || Number.isNaN(Date.parse(m.builtAt))) {
    errors.push("builtAt must be an ISO timestamp");
  }
  if (!ALL_STATUSES.includes(m.status)) {
    errors.push("status must be one of " + ALL_STATUSES.join("|"));
  }
  const artifacts = m.artifacts;
  if (!artifacts || typeof artifacts !== "object") {
    errors.push("artifacts object is required");
  } else {
    for (const key of ["installer", "vst3", "standalone"]) {
      const a = artifacts[key];
      if (!a || typeof a !== "object") {
        errors.push(`artifacts.${key} is required (schema v1: all three Windows artifacts)`);
        continue;
      }
      if (typeof a.file !== "string" || !a.file || a.file !== basename(a.file)) {
        errors.push(`artifacts.${key}.file must be a plain file name`);
      }
      if (typeof a.sha256 !== "string" || !SHA256_RE.test(a.sha256)) {
        errors.push(`artifacts.${key}.sha256 must be 64-char lowercase hex`);
      }
      if (!Number.isInteger(a.bytes) || a.bytes <= 0) {
        errors.push(`artifacts.${key}.bytes must be a positive integer`);
      }
    }
  }
  const media = m.media || {};
  const shot = media.heroScreenshot;
  if (shot) {
    if (typeof shot.file !== "string" || !shot.file || shot.file !== basename(shot.file)) {
      errors.push("media.heroScreenshot.file must be a plain file name");
    }
    if (typeof shot.sha256 !== "string" || !SHA256_RE.test(shot.sha256)) {
      errors.push("media.heroScreenshot.sha256 must be 64-char lowercase hex");
    }
    if (!Number.isInteger(shot.bytes) || shot.bytes <= 0) {
      errors.push("media.heroScreenshot.bytes must be a positive integer");
    }
    if (typeof shot.generatedFromCommit !== "string" || !COMMIT_RE.test(shot.generatedFromCommit)) {
      errors.push("media.heroScreenshot.generatedFromCommit must be the 40-hex commit it was rendered from");
    }
    if (shot.generatedFromCommit && m.commit && shot.generatedFromCommit !== m.commit) {
      errors.push("media.heroScreenshot.generatedFromCommit must equal commit (same release candidate)");
    }
  }
  if (!Array.isArray(m.audioExamples)) errors.push("audioExamples must be an array (may be empty)");
  if (m.gaps !== undefined && !Array.isArray(m.gaps)) errors.push("gaps must be an array when present");

  if (forFinalize) {
    if (!shot) {
      errors.push("finalize requires media.heroScreenshot (canonical release evidence, §9)");
    }
    if (Array.isArray(m.audioExamples) && m.audioExamples.length === 0) {
      // Honest record, not a failure: the absence is explicit and machine-readable.
      m.gaps = Array.from(new Set([...(m.gaps || []), "audioExamples: canonical audio not yet produced in CI"]));
    }
    if (!RELEASABLE_STATUSES.includes(m.status)) {
      errors.push(`cannot finalize from status "${m.status}" (must be verifying)`);
    }
  }
  return errors;
}

function cmdGenerate(flags) {
  for (const req of ["product", "version", "commit", "out", "installer", "vst3", "standalone"]) {
    if (!flags[req]) fail(`generate requires --${req}`);
  }
  if (!COMMIT_RE.test(flags.commit)) fail("--commit must be a 40-char lowercase hex git SHA");
  if (!VERSION_RE.test(flags.version)) fail("--version must be semver-like (e.g. 1.2.0)");

  const now = new Date().toISOString();
  const artifacts = {};
  for (const key of ["installer", "vst3", "standalone"]) {
    const path = flags[key];
    let hash;
    try {
      hash = sha256File(path);
    } catch (e) {
      fail(`required artifact missing or unreadable: ${path} (${e.code || e.message})`);
    }
    artifacts[key] = { file: basename(path), sha256: hash.sha256, bytes: hash.bytes };
  }

  let heroScreenshot = null;
  if (flags.screenshot) {
    let hash;
    try {
      hash = sha256File(flags.screenshot);
    } catch (e) {
      fail(`screenshot missing or unreadable: ${flags.screenshot} (${e.code || e.message})`);
    }
    heroScreenshot = {
      file: basename(flags.screenshot),
      sha256: hash.sha256,
      bytes: hash.bytes,
      generatedFromCommit: flags.commit,
    };
  }

  const manifest = {
    schemaVersion: SCHEMA_VERSION,
    product: flags.product,
    version: flags.version,
    commit: flags.commit,
    builtAt: now,
    status: "verifying",
    artifacts,
    media: { heroScreenshot },
    audioExamples: [],
    gaps: flags.gaps.length ? flags.gaps : [],
  };

  const errors = validateManifest(manifest);
  if (errors.length) fail("generated manifest is invalid:\n  - " + errors.join("\n  - "));

  writeFileSync(flags.out, JSON.stringify(manifest, null, 2) + "\n", "utf8");
  console.log(`release-manifest: wrote ${flags.out}`);
  for (const [k, v] of Object.entries(artifacts)) {
    console.log(`  ${k}: ${v.file} (${v.bytes} bytes) sha256=${v.sha256.slice(0, 16)}…`);
  }
  if (heroScreenshot) {
    console.log(`  heroScreenshot: ${heroScreenshot.file} sha256=${heroScreenshot.sha256.slice(0, 16)}…`);
  }
}

function cmdValidate(flags) {
  if (!flags.manifest) fail("validate requires --manifest");
  let m;
  try {
    m = JSON.parse(readFileSync(flags.manifest, "utf8"));
  } catch (e) {
    fail("cannot read/parse manifest: " + e.message);
  }
  const errors = validateManifest(m);
  if (errors.length) fail("manifest invalid:\n  - " + errors.join("\n  - "));
  console.log(`release-manifest: VALID (${m.product} v${m.version} status=${m.status})`);
}

function cmdFinalize(flags) {
  if (!flags.manifest) fail("finalize requires --manifest");
  let m;
  try {
    m = JSON.parse(readFileSync(flags.manifest, "utf8"));
  } catch (e) {
    fail("cannot read/parse manifest: " + e.message);
  }

  const errors = validateManifest(m, { forFinalize: true });
  if (errors.length) fail("cannot finalize:\n  - " + errors.join("\n  - "));

  // INTEGRITY: recompute every hash from disk before certifying VERIFIED.
  const checkPath = (label, entry, candidateDirs) => {
    let path = null;
    for (const dir of candidateDirs) {
      const p = dir ? dir.replace(/\/$/, "") + "/" + entry.file : entry.file;
      try {
        readFileSync(p);
        path = p;
        break;
      } catch {
        /* try next */
      }
    }
    if (!path) {
      fail(`integrity: ${label} file not found: ${entry.file} (searched: ${candidateDirs.join(", ")})`);
    }
    const { sha256, bytes } = sha256File(path);
    if (sha256 !== entry.sha256) {
      fail(`integrity: ${label} sha256 MISMATCH for ${path}\n  recorded ${entry.sha256}\n  actual   ${sha256}`);
    }
    if (bytes !== entry.bytes) {
      fail(`integrity: ${label} size MISMATCH for ${path} (recorded ${entry.bytes}, actual ${bytes})`);
    }
    console.log(`  integrity ok: ${label} ${entry.file}`);
  };

  const dirs = [flags.dir || "", "."];
  for (const key of ["installer", "vst3", "standalone"]) {
    checkPath("artifacts." + key, m.artifacts[key], dirs);
  }
  if (m.media?.heroScreenshot) {
    checkPath("media.heroScreenshot", m.media.heroScreenshot, dirs);
  }

  m.status = "verified";
  m.verifiedAt = new Date().toISOString();
  const post = validateManifest(m);
  if (post.length) fail("post-finalize manifest invalid:\n  - " + post.join("\n  - "));

  writeFileSync(flags.manifest, JSON.stringify(m, null, 2) + "\n", "utf8");
  console.log(`release-manifest: VERIFIED (${m.product} v${m.version} @ ${m.commit.slice(0, 12)})`);
  if ((m.gaps || []).length) {
    console.log("release-manifest: recorded gaps (machine-readable):");
    for (const g of m.gaps) console.log("  - " + g);
  }
}

function usage() {
  console.log("release-manifest.mjs — generate | validate | finalize (schema v" + SCHEMA_VERSION + ")");
  console.log("see the header of this file for the full usage");
}

const [cmd, ...rest] = process.argv.slice(2);
const flags = parseFlags(rest);
if (cmd === "generate") cmdGenerate(flags);
else if (cmd === "validate") cmdValidate(flags);
else if (cmd === "finalize") cmdFinalize(flags);
else {
  usage();
  process.exit(cmd ? 1 : 0);
}
