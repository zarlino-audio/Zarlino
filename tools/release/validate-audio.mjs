#!/usr/bin/env node
/**
 * Zarlino Audio - canonical audio evidence validator.
 *
 * Fails closed on:
 *   - a reference without a rendered entry (renderer skipped/failed silently)
 *   - missing or implausibly small render files on disk
 *   - non-finite measurements (silence / NaN loudness)
 *   - a comparison pair that is NOT level-matched within the documented
 *     tolerance (fair A/B: a louder wet must never pass)
 *   - clipped or silent outputs
 *
 * Usage:
 *   node tools/release/validate-audio.mjs \
 *     --metrics artifacts/audio/render-metrics.json \
 *     --dir artifacts/audio \
 *     --references plugins/ZTame/references/references.json \
 *     --out artifacts/audio-validation.json \
 *     [--maxMatchedDeltaLu 0.2] [--maxPeakDb 0.5] [--minPeakDb -60]
 *
 * Exit 0 pass - 1 fail. Dependency-free.
 */
import { readFileSync, writeFileSync, statSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';

function fail(msg) { console.error('validate-audio: FAIL ' + msg); process.exit(1); }

const args = {};
for (let i = 2; i < process.argv.length; i++) {
  const a = process.argv[i];
  if (!a.startsWith('--')) continue;
  args[a.slice(2)] = process.argv[i + 1];
  i++;
}

for (const req of ['metrics', 'dir', 'out']) {
  if (!args[req]) fail('missing --' + req);
}
const maxMatchedDeltaLu = Number(args.maxMatchedDeltaLu ?? 0.2);
const maxPeakDb = Number(args.maxPeakDb ?? 0.5);
const minPeakDb = Number(args.minPeakDb ?? -60);

const metrics = JSON.parse(readFileSync(resolve(args.metrics), 'utf8'));
const audioDir = resolve(args.dir);
const failures = [];
const checked = [];

const isFiniteNum = (v) => typeof v === 'number' && Number.isFinite(v);

const refs = args.references
  ? JSON.parse(readFileSync(resolve(args.references), 'utf8')).references
  : null;

if (!Array.isArray(metrics.sources) || metrics.sources.length === 0) {
  fail('metrics has no sources');
}

// cross-check: every committed reference must have a rendered entry
if (refs) {
  const rendered = new Set(metrics.sources.map((s) => s.name));
  for (const r of refs) {
    if (!rendered.has(r.id)) failures.push('reference not rendered: ' + r.id);
  }
}

function checkFile(label, base) {
  try {
    const st = statSync(join(audioDir, base));
    if (st.size < 4096) return label + ' implausibly small (' + st.size + ' bytes)';
  } catch {
    return label + ' missing on disk: ' + base;
  }
  return null;
}

for (const s of metrics.sources) {
  const problems = [];

  for (const part of ['dryMaster', 'wetRawMaster', 'dryComparison', 'wetComparison']) {
    if (!s[part] || typeof s[part].file !== 'string') { problems.push(part + ' entry missing'); continue; }
    const fileProblem = checkFile(part, s[part].file);
    if (fileProblem) problems.push(fileProblem);
    if (!isFiniteNum(s[part].lufs)) problems.push(part + ' lufs not finite');
    if (!isFiniteNum(s[part].peakDb)) problems.push(part + ' peak not finite');
    if (isFiniteNum(s[part].peakDb) && s[part].peakDb < minPeakDb) {
      problems.push(part + ' is silent (peak ' + s[part].peakDb + ' dBFS)');
    }
    if (isFiniteNum(s[part].peakDb) && s[part].peakDb > maxPeakDb) {
      problems.push(part + ' clips (peak ' + s[part].peakDb + ' dBFS)');
    }
  }

  const lm = s.loudnessMatch || {};
  if (!isFiniteNum(lm.rawDeltaLu)) problems.push('loudnessMatch.rawDeltaLu not finite');
  if (!isFiniteNum(lm.matchedDeltaLu)) problems.push('loudnessMatch.matchedDeltaLu not finite');
  if (!isFiniteNum(lm.dryGainDb)) problems.push('loudnessMatch.dryGainDb not finite');
  if (!isFiniteNum(lm.wetGainDb)) problems.push('loudnessMatch.wetGainDb not finite');
  if (isFiniteNum(lm.matchedDeltaLu) && Math.abs(lm.matchedDeltaLu) > maxMatchedDeltaLu) {
    problems.push('comparison pair NOT level-matched: matchedDeltaLu ' + lm.matchedDeltaLu
      + ' exceeds tolerance ' + maxMatchedDeltaLu);
  }
  // min-target convention: the quieter side is untouched (0 dB gain)
  if (isFiniteNum(lm.dryGainDb) && isFiniteNum(lm.wetGainDb)
      && Math.min(lm.dryGainDb, lm.wetGainDb) > 0.001) {
    problems.push('min-target violated: both sides were boosted (dryGainDb ' + lm.dryGainDb
      + ', wetGainDb ' + lm.wetGainDb + ')');
  }
  if (!isFiniteNum(s.durationSec) || s.durationSec <= 0.15) problems.push('duration too short');
  if (!isFiniteNum(s.latencySamples) || s.latencySamples < 0) problems.push('latencySamples invalid');
  if (!isFiniteNum(s.sourceSampleRate) || s.sourceSampleRate < 8000) problems.push('sourceSampleRate invalid');

  checked.push({
    name: s.name,
    pass: problems.length === 0,
    problems,
    matchedDeltaLu: lm.matchedDeltaLu,
    dryGainDb: lm.dryGainDb,
    wetGainDb: lm.wetGainDb,
    resampled: s.resampled === true,
  });
  for (const p of problems) failures.push(s.name + ': ' + p);
}

const record = {
  validator: 'validate-audio',
  checkedAt: new Date().toISOString(),
  metricsFile: args.metrics,
  renderVersion: metrics.version,
  renderCommit: metrics.gitHead,
  tolerance: { maxMatchedDeltaLu, minPeakDb, maxPeakDb },
  sourceCount: metrics.sources.length,
  referenceCount: refs ? refs.length : null,
  pass: failures.length === 0,
  failures,
  sources: checked,
};

mkdirSync(dirname(resolve(args.out)), { recursive: true });
writeFileSync(resolve(args.out), JSON.stringify(record, null, 2) + '\n', 'utf8');

if (failures.length) {
  console.error('validate-audio: FAIL (' + failures.length + ' problem(s))');
  for (const f of failures) console.error('  - ' + f);
  process.exit(1);
}
console.log('validate-audio: PASS (' + metrics.sources.length + ' sources, matched delta <= '
  + maxMatchedDeltaLu + ' LU, peaks within [' + minPeakDb + ', ' + maxPeakDb + '] dBFS)');
