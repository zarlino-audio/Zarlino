#!/usr/bin/env node
/**
 * Zarlino Audio — verify that the WEBSITE actually serves a given release.
 *
 * The plugin release gate publishes a verified release to R2; the website is
 * the delivery surface. A release is only truly live when production reports
 * the same commit AND the stable asset aliases resolve. This tool is the last
 * step of the release gate: bounded polling (the website caches the publish
 * pointer for up to 60s), then an exact commit match plus an alias HEAD check.
 *
 * Usage:
 *   node tools/release/verify-website-release.mjs \
 *     --expect-commit <full-sha> [--product ztame] [--base https://zarlinoaudio.com] \
 *     [--attempts 10] [--delay-ms 15000]
 *
 * Exit codes: 0 = website serves the expected release, 1 = not (with reason).
 */

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
  console.error('verify-website-release: FAILED - ' + msg);
  process.exit(1);
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const a = args(process.argv.slice(2));
const PRODUCT = a.product || 'ztame';
const BASE = (a.base || 'https://zarlinoaudio.com').replace(/\/+$/, '');
const EXPECT = a['expect-commit'];
const ATTEMPTS = Number(a.attempts || 10);
const DELAY_MS = Number(a['delay-ms'] || 15000);

if (!EXPECT) fail('missing --expect-commit <full-sha>');

const API = `${BASE}/api/release/${PRODUCT}`;
const ALIAS = `${BASE}/releases/${PRODUCT}/latest/installer.exe`;

console.log(`verify-website-release: expecting ${PRODUCT} @ ${EXPECT.slice(0, 12)} at ${BASE}`);

for (let attempt = 1; attempt <= ATTEMPTS; attempt++) {
  try {
    const res = await fetch(API, { headers: { accept: 'application/json' } });
    if (!res.ok) {
      console.log(
        `verify-website-release: attempt ${attempt}/${ATTEMPTS} -> HTTP ${res.status} from ${API} (release surface not live yet)`,
      );
      await sleep(DELAY_MS);
      continue;
    }
    const data = await res.json();
    const commit = typeof data?.commit === 'string' ? data.commit : '';
    const version = data?.version ?? '?';
    if (commit === EXPECT) {
      const head = await fetch(ALIAS, { method: 'HEAD' });
      if (head.status !== 200) {
        fail(`release is live but the installer alias returned HTTP ${head.status} (${ALIAS})`);
      }
      const length = head.headers.get('content-length') ?? '?';
      console.log(
        `verify-website-release: OK - ${BASE} serves ${PRODUCT} v${version} @ ${commit.slice(0, 12)}; ` +
          `installer alias HTTP 200 (${length} bytes)`,
      );
      process.exit(0);
    }
    console.log(
      `verify-website-release: attempt ${attempt}/${ATTEMPTS} -> website still serves ` +
        `${commit ? commit.slice(0, 12) : 'no published release'} (pointer cache TTL up to 60s)`,
    );
  } catch (err) {
    console.log(`verify-website-release: attempt ${attempt}/${ATTEMPTS} -> ${err.message}`);
  }
  await sleep(DELAY_MS);
}

fail(`website never reported commit ${EXPECT.slice(0, 12)} within ${ATTEMPTS} attempt(s)`);
