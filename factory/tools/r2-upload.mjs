#!/usr/bin/env node
/**
 * Zarlino factory - upload a build's artifacts and evidence to R2.
 *
 *   node factory/tools/r2-upload.mjs \
 *     --account <cloudflare-account-id> \
 *     --bucket zarlino-artifacts \
 *     --prefix factory-builds/<buildId> \
 *     --file "artifact/Product.vst3.zip=work/package/Product.vst3.zip" \
 *     --file "qa-report.json=work/qa-report.json"
 *
 * Follows the same verify-then-publish discipline as tools/release/publish-release.mjs,
 * which is what the shipped plugin release pipeline uses: every object is
 * uploaded, DOWNLOADED AGAIN, and its sha256 compared with the local file. An
 * upload that cannot be read back byte-for-byte is a failed upload, not a
 * successful one with flaky verification.
 *
 * The credential is a Cloudflare API token scoped to object write on the
 * artifacts bucket. This tool never compiles, signs or executes anything.
 *
 * Exit codes: 0 all objects verified, 1 a failure, 2 usage error.
 */

import { createHash } from 'node:crypto';
import { readFileSync, statSync, writeFileSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));

function fail(message) {
  console.error(`r2-upload: FAIL ${message}`);
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

  if (value === undefined || value.startsWith('--')) fail(`${token} requires a value`);

  args[token.slice(2)] = value;
}

for (const required of ['account', 'bucket', 'prefix']) {
  if (!args[required]) {
    console.error('usage: r2-upload.mjs --account <id> --bucket <name> --prefix <key-prefix> --file <key>=<path> [...]');
    process.exit(2);
  }
}

if (files.length === 0) {
  console.error('usage: r2-upload.mjs --account <id> --bucket <name> --prefix <key-prefix> --file <key>=<path> [...]');
  process.exit(2);
}

const TOKEN = process.env.CLOUDFLARE_API_TOKEN;

if (!TOKEN) fail('CLOUDFLARE_API_TOKEN is not set');

const API = (args['api-base'] || 'https://api.cloudflare.com/client/v4').replace(/\/$/, '');

/* -------------------------------------------------------- key validation */
/**
 * Keys are relative and are validated, never trusted: a key that could escape
 * the prefix, contain a traversal segment, or encode a control character is
 * rejected before it is composed into a URL.
 */
function safeKey(key) {
  const normalised = String(key).replace(/^\/+/, '');

  if (normalised.length === 0 || normalised.length > 400) return null;
  if (normalised.includes('..') || normalised.includes('\\') || normalised.includes('\0')) return null;
  // Printable ASCII only. Product names legitimately contain spaces (a VST3
  // bundle is named after the product), and object keys are URL-encoded per
  // segment when the request is composed, so a space is safe. Traversal,
  // backslashes, control characters and non-ASCII are still rejected.
  if (!/^[\x20-\x7E]+$/.test(normalised)) return null;
  if (normalised.split('/').some((segment) => segment === '' || segment === '.')) return null;

  return normalised;
}

const prefix = safeKey(args.prefix);

if (!prefix) fail(`unsafe prefix '${args.prefix}'`);

const objectUrl = (key) =>
  `${API}/accounts/${args.account}/r2/buckets/${args.bucket}/objects/` +
  key.split('/').map(encodeURIComponent).join('/');

async function fetchRetry(url, options, attempts = 4) {
  let lastError;

  for (let attempt = 0; attempt < attempts; attempt++) {
    try {
      return await fetch(url, options);
    } catch (error) {
      lastError = error;
      await new Promise((r) => setTimeout(r, 500 * (attempt + 1)));
    }
  }

  fail(`network failure after ${attempts} attempts for ${url}: ${lastError?.message || lastError}`);
}

async function putObject(key, buffer, contentType) {
  const response = await fetchRetry(objectUrl(key), {
    method: 'PUT',
    headers: { Authorization: `Bearer ${TOKEN}`, 'Content-Type': contentType },
    body: buffer,
  });

  const text = await response.text();

  if (!response.ok || !text.includes('"success":true')) {
    fail(`PUT ${key} -> HTTP ${response.status}: ${text.slice(0, 300)}`);
  }
}

async function getObject(key) {
  const response = await fetchRetry(objectUrl(key), { headers: { Authorization: `Bearer ${TOKEN}` } });

  if (!response.ok) fail(`verify GET ${key} -> HTTP ${response.status}`);

  return Buffer.from(await response.arrayBuffer());
}

/* ---------------------------------------------------------------- upload */
console.log(`r2-upload: ${files.length} object(s) -> r2://${args.bucket}/${prefix}/`);

const uploaded = [];

for (const file of files) {
  const key = safeKey(file.key);

  if (!key) fail(`unsafe object key '${file.key}'`);

  const absolute = resolve(process.cwd(), file.path);

  let buffer;

  try {
    buffer = readFileSync(absolute);
  } catch (error) {
    fail(`cannot read ${absolute}: ${error.message}`);
  }

  if (buffer.length === 0) fail(`${file.path} is empty`);

  const sha256 = createHash('sha256').update(buffer).digest('hex');
  const contentType = key.endsWith('.json') ? 'application/json'
    : key.endsWith('.zip') ? 'application/zip'
    : 'application/octet-stream';

  await putObject(`${prefix}/${key}`, buffer, contentType);

  const readBack = await getObject(`${prefix}/${key}`);
  const readBackHash = createHash('sha256').update(readBack).digest('hex');

  if (readBackHash !== sha256 || readBack.length !== buffer.length) {
    fail(`post-upload verification FAILED for ${key} (recorded ${sha256}, remote ${readBackHash})`);
  }

  console.log(`  ok  ${key}  ${buffer.length} bytes  sha256 ${sha256}`);

  uploaded.push({ key: `${prefix}/${key}`, bytes: buffer.length, sha256 });
}

/* -------------------------------------------------------------- manifest */
/**
 * The upload manifest is the machine-readable receipt of what this build
 * published. It is written last so a reader that sees it knows every object it
 * lists was already verified.
 */
const manifest = {
  uploadManifestSchemaVersion: 1,
  bucket: args.bucket,
  prefix,
  uploadedAt: new Date().toISOString(),
  objects: uploaded,
};

const manifestBuffer = Buffer.from(JSON.stringify(manifest, null, 2), 'utf8');
const manifestKey = `${prefix}/upload-manifest.json`;

await putObject(manifestKey, manifestBuffer, 'application/json');

const manifestReadBack = await getObject(manifestKey);

if (manifestReadBack.length !== manifestBuffer.length) {
  fail('the upload manifest could not be verified');
}

console.log(`r2-upload: manifest written to r2://${args.bucket}/${manifestKey}`);

if (args['manifest-out']) {
  writeFileSync(resolve(process.cwd(), args['manifest-out']), manifestBuffer);
}

if (args['check-local-dir']) {
  // Sanity check used by the pipeline: the local artifact must still be there.
  for (const file of files) statSync(resolve(process.cwd(), file.path));
}

console.log(`r2-upload: verified ${uploaded.length} object(s)`);
