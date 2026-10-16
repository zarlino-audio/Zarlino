#!/usr/bin/env node
/**
 * Zarlino Audio - publish a VERIFIED release to the zarlino-releases R2 bucket.
 *
 * Only a manifest with status "verified" may be published. Every referenced
 * file is uploaded, then DOWNLOADED AGAIN and sha256-re-verified against the
 * manifest before the "latest" pointer is written - so the pointer can never
 * reference an object that failed integrity verification (spec section 17).
 *
 * Layout (all under releases/<product>/<version>/<commit>/):
 *   <binaries + screenshot at root>   audio/<masters + comparisons>
 *   audio/web/<mp3 previews>          release-manifest.json
 * Pointer: releases/<product>/latest.json (written LAST, lists every object)
 *
 * Auth: CLOUDFLARE_API_TOKEN (R2 write). REST API only - no S3 keys needed.
 *
 * Usage:
 *   node tools/release/publish-release.mjs \
 *     --manifest artifacts/release-manifest.json --base artifacts \
 *     --audio-dir artifacts/audio --web-dir artifacts/audio/web \
 *     --bucket zarlino-releases --account <account-id> [--product ztame]
 *
 * Exit 0 published+verified - 1 failed (nothing certified).
 */
import { createHash } from 'node:crypto';
import { readFileSync, statSync } from 'node:fs';
import { basename, join, resolve } from 'node:path';

function fail(msg) { console.error('publish-release: FAIL ' + msg); process.exit(1); }

const args = {};
for (let i = 2; i < process.argv.length; i++) {
  const a = process.argv[i];
  if (!a.startsWith('--')) continue;
  args[a.slice(2)] = process.argv[i + 1];
  i++;
}
for (const req of ['manifest', 'base', 'audio-dir', 'web-dir', 'bucket', 'account']) {
  if (!args[req]) fail('missing --' + req);
}
const PRODUCT = args.product || 'ztame';
const API = (args['api-base'] || 'https://api.cloudflare.com/client/v4').replace(/\/$/, '');
const TOKEN = process.env.CLOUDFLARE_API_TOKEN;
if (!TOKEN) fail('CLOUDFLARE_API_TOKEN is not set');

const manifest = JSON.parse(readFileSync(resolve(args.manifest), 'utf8'));
if (manifest.status !== 'verified') {
  fail(`refusing to publish a manifest with status "${manifest.status}" - only VERIFIED releases are publishable`);
}
if (typeof manifest.commit !== 'string' || !/^[0-9a-f]{40}$/.test(manifest.commit)) {
  fail('manifest commit is not a 40-hex git SHA');
}
const baseDir = resolve(args.base);
const audioDir = resolve(args['audio-dir']);
const webDir = resolve(args['web-dir']);
const prefix = `releases/${PRODUCT}/${manifest.version}/${manifest.commit}`;

const CT = {
  '.exe': 'application/octet-stream',
  '.zip': 'application/zip',
  '.png': 'image/png',
  '.wav': 'audio/wav',
  '.mp3': 'audio/mpeg',
  '.json': 'application/json',
  '.md': 'text/markdown',
};
const contentTypeFor = (name) => {
  const dot = name.lastIndexOf('.');
  return (dot >= 0 && CT[name.slice(dot).toLowerCase()]) || 'application/octet-stream';
};

/* ------------------------------------------------------------------ files */
const files = []; // { path (relative), abs, sha256, bytes }
const addFile = (relPath, absPath, expectSha) => {
  let buf;
  try {
    buf = readFileSync(absPath);
  } catch (e) {
    fail(`referenced file missing/unreadable: ${absPath} (${e.code || e.message})`);
  }
  const sha256 = createHash('sha256').update(buf).digest('hex');
  if (expectSha && sha256 !== expectSha) {
    fail(`local file does not match the manifest hash: ${relPath}\n  manifest ${expectSha}\n  actual   ${sha256}`);
  }
  files.push({ path: relPath, abs: absPath, sha256, bytes: buf.length, buf });
};

for (const key of ['installer', 'vst3', 'standalone']) {
  const a = manifest.artifacts?.[key];
  if (!a) fail(`manifest has no artifacts.${key}`);
  addFile(a.file, join(baseDir, a.file), a.sha256);
}
if (manifest.media?.heroScreenshot) {
  const s = manifest.media.heroScreenshot;
  addFile(s.file, join(baseDir, s.file), s.sha256);
}
for (const ex of manifest.audioExamples || []) {
  for (const k of ['dryMaster', 'wetRawMaster', 'dryComparison', 'wetComparison']) {
    const f = ex.render?.[k];
    if (f) addFile('audio/' + f.file, join(audioDir, f.file), f.sha256);
  }
  if (ex.web) {
    for (const k of ['dryMp3', 'wetMp3']) {
      const f = ex.web[k];
      if (f) addFile('audio/web/' + f.file, join(webDir, f.file), f.sha256);
    }
  }
}
addFile('release-manifest.json', resolve(args.manifest));

/* ---------------------------------------------------------------- helpers */
const objectUrl = (key) =>
  `${API}/accounts/${args.account}/r2/buckets/${args.bucket}/objects/` +
  key.split('/').map(encodeURIComponent).join('/');

/** Bounded retry for transient network errors (ECONNRESET etc). Retries never
 *  weaken verification: every object is still re-downloaded and hashed. */
async function fetchRetry(url, options, attempts = 4) {
  let lastErr;
  for (let i = 0; i < attempts; i++) {
    try {
      return await fetch(url, options);
    } catch (e) {
      lastErr = e;
      await new Promise((r) => setTimeout(r, 500 * (i + 1)));
    }
  }
  fail(`network failure after ${attempts} attempts for ${url}: ${lastErr?.message || lastErr}`);
}

async function putObject(key, buf, contentType) {
  const res = await fetchRetry(objectUrl(key), {
    method: 'PUT',
    headers: { Authorization: `Bearer ${TOKEN}`, 'Content-Type': contentType },
    body: buf,
  });
  const text = await res.text();
  if (!res.ok || !text.includes('"success":true')) {
    fail(`PUT ${key} -> HTTP ${res.status}: ${text.slice(0, 300)}`);
  }
}

async function getObject(key) {
  const res = await fetchRetry(objectUrl(key), { headers: { Authorization: `Bearer ${TOKEN}` } });
  if (!res.ok) fail(`verify GET ${key} -> HTTP ${res.status}`);
  return Buffer.from(await res.arrayBuffer());
}

/* ---------------------------------------------------------------- upload */
console.log(`publish-release: ${PRODUCT} v${manifest.version} @ ${manifest.commit.slice(0, 12)} -> r2://${args.bucket}/${prefix}/`);
for (const f of files) {
  process.stdout.write(`  put ${f.path} (${f.bytes} B) ... `);
  await putObject(`${prefix}/${f.path}`, f.buf, contentTypeFor(basename(f.path)));
  console.log('ok');
}

/* ------------------------------------------- verify AFTER upload (section 17) */
console.log('publish-release: verifying uploaded objects (re-download + sha256)');
for (const f of files) {
  const back = await getObject(`${prefix}/${f.path}`);
  const sha = createHash('sha256').update(back).digest('hex');
  if (sha !== f.sha256 || back.length !== f.bytes) {
    fail(`post-upload verification FAILED for ${f.path} (recorded ${f.sha256}, remote ${sha})`);
  }
}
console.log(`publish-release: all ${files.length} objects verified`);

/* ------------------------------------------------- latest pointer (LAST) */
const pointer = {
  publishSchemaVersion: 1,
  product: PRODUCT,
  version: manifest.version,
  commit: manifest.commit,
  publishedAt: new Date().toISOString(),
  prefix,
  manifest: 'release-manifest.json',
  files: files.map((f) => ({ path: f.path, sha256: f.sha256, bytes: f.bytes })),
};
const pointerKey = `releases/${PRODUCT}/latest.json`;
const pointerBuf = Buffer.from(JSON.stringify(pointer, null, 2) + '\n', 'utf8');
await putObject(pointerKey, pointerBuf, 'application/json');
const pointerBack = await getObject(pointerKey);
const parsed = JSON.parse(pointerBack.toString('utf8'));
if (parsed.commit !== manifest.commit || parsed.prefix !== prefix) {
  fail('latest pointer round-trip verification failed');
}

console.log(`publish-release: PUBLISHED + VERIFIED`);
console.log(`  objects  : ${files.length}`);
console.log(`  prefix   : ${prefix}`);
console.log(`  pointer  : ${pointerKey} -> commit ${manifest.commit.slice(0, 12)}`);
