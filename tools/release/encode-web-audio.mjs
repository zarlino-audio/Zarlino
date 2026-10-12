#!/usr/bin/env node
/**
 * Zarlino Audio - encode web listening previews (MP3) from the level-matched
 * comparison WAVs. The canonical WAV masters are never touched: web copies are
 * separate, explicitly identified derivatives (spec section 14).
 *
 * Usage:
 *   node tools/release/encode-web-audio.mjs \
 *     --metrics artifacts/audio/render-metrics.json \
 *     --dir artifacts/audio --outDir artifacts/audio/web \
 *     [--kbps 128]
 *
 * Resolution: '@breezystack/lamejs' is required from the CURRENT WORKING
 * DIRECTORY (run from a folder whose node_modules has it, e.g. after
 * `npm install --no-save @breezystack/lamejs@<ver>` in CI, or from the website
 * repo locally). Exit 0 ok - 1 error.
 */
import { readFileSync, writeFileSync, mkdirSync, statSync } from 'node:fs';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';
import { createHash } from 'node:crypto';
import { dirname, join, resolve } from 'node:path';

function fail(msg) { console.error('encode-web-audio: FAIL ' + msg); process.exit(1); }

const args = {};
for (let i = 2; i < process.argv.length; i++) {
  const a = process.argv[i];
  if (!a.startsWith('--')) continue;
  args[a.slice(2)] = process.argv[i + 1];
  i++;
}
for (const req of ['metrics', 'dir', 'outDir']) {
  if (!args[req]) fail('missing --' + req);
}
const KBPS = Number(args.kbps ?? 128);

let lamejs;
try {
  const req = createRequire(join(process.cwd(), 'package.json'));
  const entry = req.resolve('@breezystack/lamejs');
  // The package's require entry is an IIFE build; the ESM build next to it is
  // the one used by the website's demo-mp3 script.
  const esmPath = entry.endsWith('lamejs.iife.js')
    ? entry.slice(0, -'lamejs.iife.js'.length) + 'lamejs.js'
    : entry;
  const mod = await import(pathToFileURL(esmPath).href);
  lamejs = mod.default ?? mod;
  if (typeof lamejs.Mp3Encoder !== 'function') throw new Error('Mp3Encoder not found in ' + esmPath);
} catch (e) {
  fail("cannot load '@breezystack/lamejs' from cwd " + process.cwd() + ': ' + e.message);
}

/** Minimal WAV reader: PCM 16/24/32-bit, 1-2 channels (our render outputs are 24-bit). */
function readWavPcm(path) {
  const b = readFileSync(path);
  let d = 12, fmt = null, dataStart = -1, dataSize = 0;
  while (d + 8 <= b.length) {
    const id = b.toString('ascii', d, d + 4);
    const sz = b.readUInt32LE(d + 4);
    if (id === 'fmt ') {
      fmt = {
        channels: b.readUInt16LE(d + 10),
        sampleRate: b.readUInt32LE(d + 12),
        bits: b.readUInt16LE(d + 22),
      };
    }
    if (id === 'data') { dataStart = d + 8; dataSize = sz; break; }
    d += 8 + sz + (sz % 2);
  }
  if (!fmt || dataStart < 0) throw new Error('bad wav: ' + path);
  const { channels, sampleRate, bits } = fmt;
  const bytesPerSample = bits / 8;
  const frames = Math.floor(dataSize / (channels * bytesPerSample));
  const out = [];
  for (let c = 0; c < channels; c++) out.push(new Float32Array(frames));

  for (let f = 0; f < frames; f++) {
    for (let c = 0; c < channels; c++) {
      const off = dataStart + (f * channels + c) * bytesPerSample;
      let v;
      if (bits === 16) {
        v = b.readInt16LE(off) / 32768;
      } else if (bits === 24) {
        const raw = b[off] | (b[off + 1] << 8) | (b[off + 2] << 16 << 0);
        const signed = raw & 0x800000 ? raw - 0x1000000 : raw;
        v = signed / 8388608;
      } else if (bits === 32) {
        v = b.readInt32LE(off) / 2147483648;
      } else {
        throw new Error('unsupported bit depth ' + bits + ' in ' + path);
      }
      out[c][f] = v;
    }
  }
  return { channels: out, sampleRate, frames };
}

function encodeMp3(channelsData, sampleRate) {
  const chCount = channelsData.length >= 2 ? 2 : 1;
  const frames = channelsData[0].length;
  const enc = new lamejs.Mp3Encoder(chCount, sampleRate, KBPS);
  const chunks = [];
  const BLOCK = 1152;

  for (let start = 0; start < frames; start += BLOCK) {
    const n = Math.min(BLOCK, frames - start);
    let left, right;
    if (chCount === 2) {
      left = new Int16Array(n);
      right = new Int16Array(n);
      for (let i = 0; i < n; i++) {
        left[i] = Math.max(-32768, Math.min(32767, Math.round(channelsData[0][start + i] * 32767)));
        right[i] = Math.max(-32768, Math.min(32767, Math.round(channelsData[1][start + i] * 32767)));
      }
    } else {
      left = new Int16Array(n);
      for (let i = 0; i < n; i++) {
        left[i] = Math.max(-32768, Math.min(32767, Math.round(channelsData[0][start + i] * 32767)));
      }
    }
    const buf = chCount === 2 ? enc.encodeBuffer(left, right) : enc.encodeBuffer(left);
    if (buf.length > 0) chunks.push(Buffer.from(buf));
  }
  const end = enc.flush();
  if (end.length > 0) chunks.push(Buffer.from(end));
  return Buffer.concat(chunks);
}

const metrics = JSON.parse(readFileSync(resolve(args.metrics), 'utf8'));
const audioDir = resolve(args.dir);
const outDir = resolve(args.outDir);
mkdirSync(outDir, { recursive: true });

const sha256 = (buf) => createHash('sha256').update(buf).digest('hex');
const web = { schemaVersion: 1, codec: 'mp3', kbps: KBPS, encoder: '@breezystack/lamejs', files: [] };

for (const s of metrics.sources) {
  const jobs = [
    { role: 'dry', file: s.dryComparison.file, out: s.name + '-A.mp3' },
    { role: 'wet', file: s.wetComparison.file, out: s.name + '-B.mp3' },
  ];
  for (const j of jobs) {
    const wavPath = join(audioDir, j.file);
    statSync(wavPath); // throws if missing
    const wav = readWavPcm(wavPath);
    const mp3 = encodeMp3(wav.channels, wav.sampleRate);
    const outPath = join(outDir, j.out);
    writeFileSync(outPath, mp3);
    web.files.push({ source: s.name, role: j.role, file: j.out, sha256: sha256(mp3), bytes: mp3.length });
    console.log('  ' + j.out + '  ' + mp3.length + ' bytes');
  }
}

const webJson = join(outDir, 'web-audio.json');
writeFileSync(webJson, JSON.stringify(web, null, 2) + '\n', 'utf8');
console.log('encode-web-audio: wrote ' + web.files.length + ' MP3(s) + ' + webJson);
