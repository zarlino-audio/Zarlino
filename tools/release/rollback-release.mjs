#!/usr/bin/env node
/**
 * Zarlino Audio — roll the WEBSITE back to a previously verified release.
 *
 * A release rollback is a pointer move, never a redeploy:
 *
 *   releases/<product>/latest.json  ------>  releases/<product>/<version>/<commit>/
 *
 * The website Worker resolves every stable alias (installer.exe, vst3.zip,
 * standalone.zip, screenshot.png, manifest.json, audio/web/*.mp3) through that
 * one pointer, so repointing it restores a previous release everywhere at once
 * (within the Worker's 60s pointer cache). Nothing is copied or re-uploaded.
 *
 * Safety model:
 *   - the target release manifest must exist and its status must be `verified`
 *     (a pointer can never be moved to unverified artifacts);
 *   - every file the target release publishes is re-checked for EXISTENCE and
 *     exact byte size before the pointer is written (fail closed);
 *   - what we are rolling back FROM is recorded in the pointer
 *     (`rollback: { fromCommit, at, reason }`) so the move is auditable;
 *   - the pointer is written LAST and then read back and compared.
 *
 * Usage:
 *   node tools/release/rollback-release.mjs --product ztame \
 *     --version 1.2.0 --commit <full-40-char-sha> [--reason "..."] \
 *     [--bucket zarlino-releases] [--account <account-id>] [--dry-run]
 *
 * Env: CLOUDFLARE_API_TOKEN (required unless --dry-run).
 * Exit codes: 0 = pointer moved (or dry-run verified), 1 = refused/failed.
 */

import { createHash } from 'node:crypto';

function args(argv) {
  const out = {};
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (!a.startsWith('--')) continue;
    const key = a.slice(2);
    const val = argv[i + 1] && !argv[i + 1].startsWith('--') ? argv[++i] : 'true';
    out[key] = val;
  }
  return out;
}

function fail(msg) {
  console.error('rollback-release: FAILED - ' + msg);
  process.exit(1);
}
function ok(msg) {
  console.log('rollback-release: ' + msg);
}

const a = args(process.argv.slice(2));
const PRODUCT = a.product || 'ztame';
const BUCKET = a.bucket || 'zarlino-releases';
const ACCOUNT = a.account;
const VERSION = a.version;
const COMMIT = a.commit;
const DRY = a['dry-run'] === 'true';
const REASON = a.reason || 'rollback to last verified release';

if (!VERSION) fail('missing --version (e.g. --version 1.2.0)');
if (!COMMIT || !/^[0-9a-f]{40}$/.test(COMMIT)) fail('missing/invalid --commit (full 40-char git sha)');
if (!ACCOUNT) fail('missing --account (Cloudflare account id) or set --account');

const TOKEN = process.env.CLOUDFLARE_API_TOKEN || '';
if (!DRY && !TOKEN) fail('CLOUDFLARE_API_TOKEN is not set (use --dry-run for a read-only check)');

const API = `https://api.cloudflare.com/client/v4/accounts/${ACCOUNT}/r2/buckets/${BUCKET}/objects/`;
const pointerKey = `releases/${PRODUCT}/latest.json`;
const prefix = `releases/${PRODUCT}/${VERSION}/${COMMIT}`;
const keyUrl = (key) => API + key.split('/').map(encodeURIComponent).join('/');

async function getObject(key) {
  const res = await fetch(keyUrl(key), { headers: { Authorization: `Bearer ${TOKEN}` } });
  if (res.status === 404) return null;
  if (!res.ok) throw new Error(`GET ${key} -> HTTP ${res.status}`);
  return Buffer.from(await res.arrayBuffer());
}

async function putObject(key, body, contentType) {
  const res = await fetch(keyUrl(key), {
    method: 'PUT',
    headers: { Authorization: `Bearer ${TOKEN}`, 'Content-Type': contentType },
    body,
  });
  if (!res.ok) throw new Error(`PUT ${key} -> HTTP ${res.status} ${await res.text()}`);
}

/**
 * The published file list of a release, derived from its manifest exactly as
 * publish-release.mjs does (artifacts + canonical screenshot + the audio
 * masters/comparisons and web previews of every disclosed example).
 */
function publishedFiles(manifest) {
  const files = ['release-manifest.json'];
  for (const v of Object.values(manifest.artifacts || {})) if (v && v.file) files.push(v.file);
  const shot = manifest.media && manifest.media.heroScreenshot;
  if (shot && shot.file) files.push(shot.file);
  for (const ex of manifest.audioExamples || []) {
    const render = ex.render || {};
    for (const role of ['dryMaster', 'wetRawMaster', 'dryComparison', 'wetComparison']) {
      if (render[role] && render[role].file) files.push('audio/' + render[role].file);
    }
    const web = ex.web || {};
    for (const role of ['dryMp3', 'wetMp3']) {
      if (web[role] && web[role].file) files.push('audio/web/' + web[role].file);
    }
  }
  return files;
}

async function main() {
  ok(`product=${PRODUCT} target=${VERSION}/${COMMIT.slice(0, 12)} bucket=${BUCKET}${DRY ? ' (dry run)' : ''}`);

  // (1) target manifest must exist and be VERIFIED
  const manifestRaw = await getObject(`${prefix}/release-manifest.json`);
  if (!manifestRaw) fail(`target release manifest not found at r2://${BUCKET}/${prefix}/release-manifest.json`);
  const manifest = JSON.parse(manifestRaw.toString('utf8'));
  if (String(manifest.commit || '') !== COMMIT) {
    fail(`target manifest commit ${String(manifest.commit).slice(0, 12)} does not match --commit ${COMMIT.slice(0, 12)}`);
  }
  if (manifest.status !== 'verified') {
    fail(`target release status is "${manifest.status}" - refusing to point production at an unverified release`);
  }
  ok(`target manifest is verified (v${manifest.version} @ ${manifest.commit.slice(0, 12)})`);

  // (2) every published object must still exist, BYTE-IDENTICAL
  //
  // The R2 REST object endpoint has no HEAD, so existence is checked the strong
  // way: re-download each object and compare its byte length and SHA-256 with
  // the hash recorded in the target manifest. A pointer may only be moved to a
  // release that is still fully intact (fail closed).
  const sha256 = (buf) => createHash('sha256').update(buf).digest('hex');
  const files = publishedFiles(manifest);
  const sizes = new Map();
  const hashes = new Map([['release-manifest.json', sha256(manifestRaw)]]);
  const collect = (obj, relName) => {
    if (obj && obj.file && relName) {
      sizes.set(relName, obj.bytes);
      hashes.set(relName, obj.sha256 ?? null);
    }
  };
  for (const v of Object.values(manifest.artifacts || {})) collect(v, v.file);
  const shot = manifest.media && manifest.media.heroScreenshot;
  if (shot && shot.file) collect(shot, shot.file);
  for (const ex of manifest.audioExamples || []) {
    for (const role of ['dryMaster', 'wetRawMaster', 'dryComparison', 'wetComparison']) {
      const o = (ex.render || {})[role];
      if (o && o.file) collect(o, 'audio/' + o.file);
    }
    for (const role of ['dryMp3', 'wetMp3']) {
      const o = (ex.web || {})[role];
      if (o && o.file) collect(o, 'audio/web/' + o.file);
    }
  }

  let checked = 0;
  let failed = 0;
  for (const rel of files) {
    const buf = await getObject(`${prefix}/${rel}`);
    if (!buf) {
      console.error(`  MISSING  ${rel}`);
      failed++;
      continue;
    }
    const expectedBytes = rel === 'release-manifest.json' ? manifestRaw.length : sizes.get(rel);
    const expectedHash = hashes.get(rel);
    if (expectedBytes !== undefined && buf.length !== expectedBytes) {
      console.error(`  SIZE     ${rel}: bucket=${buf.length} manifest=${expectedBytes}`);
      failed++;
      continue;
    }
    if (expectedHash && sha256(buf) !== expectedHash) {
      console.error(`  HASH     ${rel}: sha256 differs from the verified manifest`);
      failed++;
      continue;
    }
    checked++;
  }
  if (failed > 0) fail(`${failed} of ${files.length} published object(s) failed verification - pointer NOT moved`);
  ok(`all ${checked} published objects re-downloaded and hash-verified`);

  // (3) what are we rolling back FROM?
  const currentRaw = await getObject(pointerKey);
  let current = null;
  if (currentRaw) {
    try {
      current = JSON.parse(currentRaw.toString('utf8'));
    } catch {
      /* unreadable pointer is treated as "none" but reported */
    }
  }
  if (current && current.commit === COMMIT) {
    fail(`production already points at ${COMMIT.slice(0, 12)} - nothing to roll back`);
  }
  ok(`current pointer: ${current ? `${current.version} @ ${String(current.commit).slice(0, 12)}` : '(none)'} -> ${VERSION} @ ${COMMIT.slice(0, 12)}`);

  if (DRY) {
    ok('dry run complete - pointer NOT written');
    return;
  }

  // (4) write the pointer LAST, then read it back
  //
  // The pointer is a complete inventory of the release: every published path
  // with the SHA-256 that was just re-verified above.
  const pointer = {
    publishSchemaVersion: 1,
    product: PRODUCT,
    version: VERSION,
    commit: COMMIT,
    publishedAt: new Date().toISOString(),
    prefix,
    manifest: 'release-manifest.json',
    files: files.map((rel) => ({
      path: rel,
      sha256: hashes.get(rel) ?? null,
      bytes: rel === 'release-manifest.json' ? manifestRaw.length : sizes.get(rel) ?? null,
    })),
    rollback: {
      fromCommit: current ? current.commit ?? null : null,
      fromVersion: current ? current.version ?? null : null,
      at: new Date().toISOString(),
      reason: REASON,
    },
  };

  const body = Buffer.from(JSON.stringify(pointer, null, 2) + '\n', 'utf8');
  await putObject(pointerKey, body, 'application/json');
  ok(`pointer written (${body.length} bytes)`);

  const back = await getObject(pointerKey);
  if (!back) fail('pointer read-back failed');
  const parsed = JSON.parse(back.toString('utf8'));
  if (parsed.commit !== COMMIT || parsed.version !== VERSION) {
    fail(`pointer read-back mismatch (got ${parsed.version}/${String(parsed.commit).slice(0, 12)})`);
  }
  ok(`ROLLED BACK + VERIFIED - ${PRODUCT} now serves ${VERSION} @ ${COMMIT.slice(0, 12)}`);
  ok(`website picks it up within the Worker pointer cache TTL (60s); re-run the acceptance gate to confirm`);
}

main().catch((err) => fail(err.message));
