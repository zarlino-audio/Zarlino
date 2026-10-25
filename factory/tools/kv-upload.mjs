#!/usr/bin/env node
/**
 * Zarlino factory - upload a build's artifact and evidence to Cloudflare KV.
 *
 *   node factory/tools/kv-upload.mjs \
 *     --account <cloudflare-account-id> \
 *     --namespace <kv-namespace-id> \
 *     --prefix factory-builds/<buildId> \
 *     --file "artifact/Product.vst3.zip=work/package/Product.vst3.zip" \
 *     --file "build-record.json=work/package/build-record.json"
 *
 * WHY KV AND NOT R2
 *
 * The factory used to publish to an R2 bucket. R2 is a paid product, and when
 * the account's R2 subscription lapsed every deployment that bound the bucket
 * failed with `10136 Please enable R2`, taking the whole customer site with it.
 * KV is available on the free plan, so the factory's storage moves here: the
 * same `factory-builds/<buildId>/...` key layout, the same verify-and-read-back
 * discipline, one dependency fewer.
 *
 * The trade-off is explicit rather than hidden: a KV value is capped at 25 MiB
 * (our packaged VST3 is about 3.5 MB) and KV is a cache-grade store, so this
 * tool refuses anything that would not read back byte-for-byte.
 *
 * Verify-then-publish (as tools/release/publish-release.mjs and r2-upload.mjs
 * already do): every value is uploaded, DOWNLOADED AGAIN, and its sha256
 * compared with the local file. An upload that cannot be read back
 * byte-for-byte is a failed upload, not a successful one with flaky checks.
 *
 * The credential is a Cloudflare API token allowed to write KV values. This
 * tool never compiles, signs or executes anything.
 *
 * Exit codes: 0 all values verified, 1 a failure, 2 usage error.
 */

import { createHash } from 'node:crypto';
import { readFileSync, statSync } from 'node:fs';

const API = 'https://api.cloudflare.com/client/v4';

/** Cloudflare's hard per-value ceiling. */
const MAX_VALUE_BYTES = 25 * 1024 * 1024;

function fail(message) {
  console.error(`kv-upload: FAIL ${message}`);
  process.exit(1);
}

/* ------------------------------------------------------------------ args */
const argv = process.argv.slice(2);
const args = {};
const files = [];

for (let i = 0; i < argv.length; i++) {
  const token = argv[i];

  if (token === '--file') {
    const value = argv[++i];

    if (!value) fail('--file requires <key>=<path>');

    const separator = value.indexOf('=');

    if (separator <= 0) fail(`--file must be <key>=<path>, got '${value}'`);

    files.push({ key: value.slice(0, separator), path: value.slice(separator + 1) });
    continue;
  }

  if (!token.startsWith('--')) fail(`unexpected argument '${token}'`);

  const value = argv[++i];

  if (value === undefined) fail(`${token} requires a value`);

  args[token.slice(2)] = value;
}

if (!args.account) fail('--account is required');
if (!args.namespace) fail('--namespace is required');
if (files.length === 0) fail('at least one --file is required');

const prefix = (args.prefix ?? '').replace(/^\/+|\/+$/g, '');
const token = process.env.CLOUDFLARE_API_TOKEN;

if (!token) fail('CLOUDFLARE_API_TOKEN is not set');

// A build key must stay inside its own prefix: the same rule the website and
// the orchestrator enforce when they read these objects back.
for (const file of files) {
  if (!file.key || file.key.startsWith('/') || file.key.includes('..') || file.key.includes('\\')) {
    fail(`refusing unsafe key '${file.key}'`);
  }
}

const sha256 = (buffer) => createHash('sha256').update(buffer).digest('hex');
const valueUrl = (key) =>
  `${API}/accounts/${encodeURIComponent(args.account)}/storage/kv/namespaces/${encodeURIComponent(args.namespace)}/values/${encodeURIComponent(key)}`;

async function put(key, bytes) {
  const response = await fetch(valueUrl(key), {
    method: 'PUT',
    headers: {
      authorization: 'Bearer ' + token,
      'content-type': 'application/octet-stream',
    },
    body: bytes,
  });

  if (!response.ok) {
    const detail = await response.text().catch(() => '');
    fail(`PUT ${key} -> HTTP ${response.status} ${detail.slice(0, 200)}`);
  }
}

async function readBack(key) {
  const response = await fetch(valueUrl(key), {
    headers: { authorization: 'Bearer ' + token },
  });

  if (!response.ok) fail(`GET ${key} -> HTTP ${response.status} (upload not verifiable)`);

  return Buffer.from(await response.arrayBuffer());
}

/* ------------------------------------------------------------------ run */
let uploaded = 0;
let totalBytes = 0;

for (const file of files) {
  let size;

  try {
    size = statSync(file.path).size;
  } catch (error) {
    fail(`cannot read ${file.path}: ${error.message}`);
  }

  if (size > MAX_VALUE_BYTES) {
    fail(`${file.path} is ${size} bytes, over the ${MAX_VALUE_BYTES}-byte KV value limit`);
  }

  const bytes = readFileSync(file.path);
  const key = prefix ? `${prefix}/${file.key}` : file.key;
  const expected = sha256(bytes);

  await put(key, bytes);

  const actual = sha256(await readBack(key));

  if (actual !== expected) {
    fail(`${key} read back as ${actual}, expected ${expected} - the object is not verifiable`);
  }

  uploaded += 1;
  totalBytes += size;
  console.log(`kv-upload: ok ${key} ${size} bytes sha256=${expected}`);
}

console.log(`kv-upload: ${uploaded} objects verified, ${totalBytes} bytes total`);
