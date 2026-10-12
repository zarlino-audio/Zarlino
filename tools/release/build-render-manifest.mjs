#!/usr/bin/env node
/**
 * Zarlino Audio - build the ZTame_RealRender input manifest from the committed
 * reference set (plugins/<P>/references/references.json).
 *
 * Usage:
 *   node tools/release/build-render-manifest.mjs \
 *     --references plugins/ZTame/references/references.json \
 *     --outDir artifacts/audio --out artifacts/audio-manifest.json \
 *     --version 1.2.0 --gitHead <sha>
 *
 * Exit 0 ok - 1 error. Dependency-free.
 */
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';

function fail(msg) { console.error('build-render-manifest: FAIL ' + msg); process.exit(1); }

const args = {};
for (let i = 2; i < process.argv.length; i++) {
  const a = process.argv[i];
  if (!a.startsWith('--')) continue;
  args[a.slice(2)] = process.argv[i + 1];
  i++;
}

for (const req of ['references', 'outDir', 'out', 'version', 'gitHead']) {
  if (!args[req]) fail('missing --' + req);
}

const refsPath = resolve(args.references);
let refs;
try {
  refs = JSON.parse(readFileSync(refsPath, 'utf8'));
} catch (e) {
  fail('cannot read references: ' + e.message);
}
if (!Array.isArray(refs.references) || refs.references.length === 0) {
  fail('references.json has no references');
}

const refsDir = dirname(refsPath);
const sources = refs.references.map((r) => ({
  name: r.id,
  path: join(refsDir, r.file).replace(/\\/g, '/'),
  startSec: 0,
  seconds: r.durationSec,
}));

const manifest = {
  outputDir: resolve(args.outDir).replace(/\\/g, '/'),
  version: args.version,
  gitHead: args.gitHead,
  sources,
};

mkdirSync(dirname(resolve(args.out)), { recursive: true });
writeFileSync(resolve(args.out), JSON.stringify(manifest, null, 2) + '\n', 'utf8');
console.log('build-render-manifest: wrote ' + args.out + ' (' + sources.length + ' sources)');
