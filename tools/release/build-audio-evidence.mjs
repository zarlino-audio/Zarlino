#!/usr/bin/env node
/**
 * Zarlino Audio - merge rendered audio evidence into the release manifest
 * fragment (audioExamples[]). Hashes every referenced file from disk so the
 * release manifest can integrity-check them again at finalize time.
 *
 * Usage:
 *   node tools/release/build-audio-evidence.mjs \
 *     --metrics artifacts/audio/render-metrics.json \
 *     --references plugins/ZTame/references/references.json \
 *     --dir artifacts/audio \
 *     [--webDir artifacts/audio/web] \
 *     --out artifacts/audio-evidence.json
 *
 * Exit 0 ok - 1 error. Dependency-free.
 */
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { dirname, join, resolve, basename } from 'node:path';

function fail(msg) { console.error('build-audio-evidence: FAIL ' + msg); process.exit(1); }

const args = {};
for (let i = 2; i < process.argv.length; i++) {
  const a = process.argv[i];
  if (!a.startsWith('--')) continue;
  args[a.slice(2)] = process.argv[i + 1];
  i++;
}
for (const req of ['metrics', 'references', 'dir', 'out']) {
  if (!args[req]) fail('missing --' + req);
}

const metrics = JSON.parse(readFileSync(resolve(args.metrics), 'utf8'));
const refDoc = JSON.parse(readFileSync(resolve(args.references), 'utf8'));
const audioDir = resolve(args.dir);
const refsDir = dirname(resolve(args.references));

const web = args.webDir
  ? JSON.parse(readFileSync(join(resolve(args.webDir), 'web-audio.json'), 'utf8'))
  : null;

function hashFile(absPath, displayPath) {
  const buf = readFileSync(absPath);
  return { file: displayPath, sha256: createHash('sha256').update(buf).digest('hex'), bytes: buf.length };
}
const audioFile = (name) => hashFile(join(audioDir, name), name);

const refById = new Map(refDoc.references.map((r) => [r.id, r]));
const examples = [];

for (const s of metrics.sources) {
  const ref = refById.get(s.name);
  if (!ref) fail('metrics source has no committed reference: ' + s.name);

  const entry = {
    id: s.name,
    title: ref.title,
    kind: ref.kind,
    reference: {
      file: ('plugins/' + refDoc.product + '/references/' + ref.file).replace(/\\/g, '/'),
      sha256: ref.sha256,
      bytes: ref.bytes,
      container: ref.container,
      sampleRate: ref.sampleRate,
      channels: ref.channels,
      bitDepth: ref.bitDepth,
      durationSec: ref.durationSec,
      origin: ref.origin,
      rights: ref.rights,
    },
    render: {
      dryMaster: audioFile(s.dryMaster.file),
      wetRawMaster: audioFile(s.wetRawMaster.file),
      dryComparison: audioFile(s.dryComparison.file),
      wetComparison: audioFile(s.wetComparison.file),
      measurements: {
        sourceSampleRate: s.sourceSampleRate,
        resampled: s.resampled === true,
        durationSec: s.durationSec,
        latencySamples: s.latencySamples,
        dryMaster: { peakDb: s.dryMaster.peakDb, rmsDb: s.dryMaster.rmsDb, lufs: s.dryMaster.lufs },
        wetRawMaster: { peakDb: s.wetRawMaster.peakDb, rmsDb: s.wetRawMaster.rmsDb, lufs: s.wetRawMaster.lufs },
        dryComparison: { peakDb: s.dryComparison.peakDb, lufs: s.dryComparison.lufs, gainDb: s.dryComparison.gainDb },
        wetComparison: { peakDb: s.wetComparison.peakDb, lufs: s.wetComparison.lufs, gainDb: s.wetComparison.gainDb },
        loudnessMatch: s.loudnessMatch,
      },
    },
  };

  if (web) {
    const wf = web.files.filter((f) => f.source === s.name);
    if (wf.length) {
      const webEntry = {};
      for (const f of wf) {
        const key = f.role === 'dry' ? 'dryMp3' : 'wetMp3';
        webEntry[key] = hashFile(join(resolve(args.webDir), f.file), f.file);
        webEntry[key].kbps = web.kbps;
        webEntry[key].sourceRole = f.role;
      }
      entry.web = webEntry;
    }
  }

  examples.push(entry);
}

const doc = {
  schemaVersion: 1,
  product: refDoc.product,
  version: metrics.version,
  renderCommit: metrics.gitHead ?? null,
  renderer: {
    tool: metrics.renderer,
    sampleRate: metrics.renderSampleRate,
    bitDepth: metrics.renderBitDepth,
    blockSize: metrics.blockSize,
    settings: metrics.settings,
  },
  normalization: {
    policy: 'wet comparison copy is loudness-matched to the dry reference; the wet raw master is never altered',
    loudnessToleranceLu: 0.2,
  },
  examples,
};

mkdirSync(dirname(resolve(args.out)), { recursive: true });
writeFileSync(resolve(args.out), JSON.stringify(doc, null, 2) + '\n', 'utf8');
console.log('build-audio-evidence: wrote ' + args.out + ' (' + examples.length + ' examples'
  + (web ? ', with web previews' : ', no web previews') + ')');
