/** Temp: populate 36 bug/feature requests as tasks (deleted after run). */
const TOKEN = process.env.FOUNDER_AUTH_TOKEN ?? "32513b44002e0f5db810077ab7fc9c226cd1a91ea04627bf1f25b36558e107c6";
const BASE = process.env.EXEC_OS_URL ?? "https://zarlino-executive-os.zarlino001.workers.dev";

const REQUESTS = [
  // ZScorch
  { t: "ZScorch: selectable crossover slopes (6/12/24/48 dB/oct) + linear-phase option", d: "Gap 1 (PLUGIN_GAP_RESEARCH.md): FabFilter Saturn 2 offers 6/12/24/48 dB/oct slopes + linear-phase band processing; ZScorch is fixed LR4 (24 dB/oct). Add slope selectability for mastering-grade multiband control.", p: "P2" },
  { t: "ZScorch: modulation matrix (LFO/envelope to drive, tilt, exciter, band gain, crossover)", d: "Gap 2: Saturn 2 ships a 50-slot mod matrix (16-step XLFO, envelope gens/followers, XY, MIDI). ZScorch's adaptive Control Layer is internal-only; expose user modulation.", p: "P2" },
  { t: "ZScorch: per-band solo + mute, A/B compare, undo/redo in the band display", d: "Gap 3 + Gap 5: Saturn 2 and Soothe2 both have per-band solo/mute and A/B+undo/redo. ZScorch has neither.", p: "P2" },
  { t: "ZScorch: add Good (8x) / Superb (32x) oversampling tiers for mastering", d: "Gap 4: ZScorch OS caps at 4x; Saturn 2 tiers are 8x/32x. Higher OS reduces alias on hot drives.", p: "P3" },
  { t: "ZScorch: Mid/Side processing mode", d: "Gap 6: M/S is explicitly out of scope but is standard (Saturn 2, Soothe2, S1). Add independent M/S band processing.", p: "P2" },
  { t: "ZScorch: verify Wavefold aliasing at 2x OS and surface in-UI oversampling warning", d: "Gap 24-related: Wavefold forces 4x today; verify 2x mode aliasing and show a UI note when a mode benefits from 4x.", p: "P1" },
  // ZTame
  { t: "ZTame: Delta/listen output (audition only the suppressed material, right-click lock)", d: "Gap 7: Soothe2's Delta (with lock) is the category QC tool. ZTame has Solo mode; add a true Delta wet-only output.", p: "P1" },
  { t: "ZTame: external sidechain input with per-DAW delay-compensation guidance", d: "Gap 9: Soothe2 supports key input with per-DAW delay-comp guidance. Add sidechain key to ZTame.", p: "P2" },
  { t: "ZTame: resolution tiers (eco/high/ultra) + offline-render quality override", d: "Gap 10: Soothe2 eco ≈ half CPU; separate offline-render quality. ZTame needs CPU/quality tiers.", p: "P2" },
  { t: "ZTame: 0-sample low-latency tracking mode for live/tracking", d: "Gap 11: Soothe3 low-latency = 0 samples @ base SR, ~1 ms @ high SR; Gullfoss Live <2 ms. ZTame needs a live path.", p: "P2" },
  { t: "ZTame P8: pumping/warbling on transients — detection-path smoothing / peak-hold", d: "Known P8 bug: add temporal smoothing/peak-hold on the detection spectrum to stop pumping on transients (pipeline/ZTame).", p: "P1" },
  { t: "ZTame: Selectivity knob inaudible (±5.25 dB vs Sensitivity 12 dB) — widen range / remap threshold", d: "Known P8 bug: Selectivity moves threshold only ±5.25 dB; widen range or remap the threshold formula so the knob is audible.", p: "P1" },
  // ZVocals
  { t: "ZVocals: re-orderable module chain (drag modules SSL-style)", d: "Gap 13: SSL Vocalstrip 2 lets you reorder modules; ZVocals chain is fixed (SSL EQ → FET → MB → Opto → De-Esser).", p: "P2" },
  { t: "ZVocals: add de-plosser stage alongside the de-esser", d: "Gap 14: SSL has de-esser + de-plosser; ZVocals only de-esses. Add plosive control.", p: "P2" },
  { t: "ZVocals: real-time spectrum/FFT overlay + per-stage GR history", d: "Gap 15: SSL shows EQ curve + real-time FFT + de-esser GR history. ZVocals has no visual feedback.", p: "P2" },
  { t: "ZVocals: automatic gain riding / auto-level mode", d: "Gap 16: Waves Vocal Rider (zero latency, ±12 dB) and Nectar ALM set the auto-level bar.", p: "P3" },
  { t: "ZVocals: per-stage bypass/mix + advanced parameter access", d: "Gap 17: stage-level control is out of scope today; add per-stage bypass/mix behind an 'advanced' toggle.", p: "P2" },
  { t: "ZVocals: verify de-esser stays content-adaptive at extreme BODY/PUNCH macro positions", d: "Gap 18-related bug: confirm the de-esser never gets forced harder by a macro (spec claim) at extreme settings.", p: "P1" },
  // PresenceField
  { t: "PresenceField: one-click mono-check button", d: "Gap 19: Polyverse Wider v2 added a mono-check button; PresenceField's Mono Listen is hidden. Surface it.", p: "P2" },
  { t: "PresenceField: low-frequency bypass / bass-shuffler control", d: "Gap 20: Wider Low Bypass and Waves S1 Shuffler widen bass while keeping mono compatibility.", p: "P2" },
  { t: "PresenceField: vectorscope + correlation meter", d: "Gap 21: Ozone Imager ships three vectorscope meters + correlation. Add metering.", p: "P2" },
  { t: "PresenceField: delay-based mono-safe stereoize mode (mono to stereo)", d: "Gap 22: Ozone Stereoize II (phase-inverted delayed copies cancel in mono) and Wider's all-pass comb are mono-safe upmix routes.", p: "P3" },
  { t: "PresenceField: mono-compatibility safety at width >150% (correlation safety factor benchmark)", d: "Gap 24: benchmark the correlation safety factor at 200% width; mono-sum artifact test.", p: "P1" },
  { t: "PresenceField: Air LFO modulation click/zipper at high width — verify fractional-delay interpolation", d: "Bug: confirm cubic-interpolated fractional-delay reads are click-free during LFO-modulated Air peak.", p: "P1" },
  // RACK_AI
  { t: "RACK_AI: per-layer output routing / multi-output stems", d: "Gap 25: Unify per-layer routing + Acon Remix multi-output (5 stems) set the bar.", p: "P2" },
  { t: "RACK_AI: publish real-time latency budget + disclosure for AI processing", d: "Gap 26: Acon states 'minimal/acceptable latency' for real-time AI; RACK_AI must disclose + compensate.", p: "P1" },
  { t: "RACK_AI: macro knobs with built-in LFOs (Unify-style)", d: "Gap 27: Unify 2 has 32 macro knobs each with a built-in LFO + Play Mode.", p: "P2" },
  { t: "Platform: NKS / Quick-Control integration (macros, tags, previews) across the catalog", d: "Gap 28: NKS is the industry standard for hardware+DAW integration.", p: "P3" },
  { t: "RACK_AI: plugin-in-plugin hosting (host any VST3 effect/instrument)", d: "Gap 29: Unify hosts any VST/VST3/AU instrument/effect; RACK_AI should match.", p: "P2" },
  { t: "RACK_AI: VST3 state/preset round-trip across DAWs (IStateHandler) — no corruption", d: "Gap 30: verify binary component state round-trips across DAWs with no cross-DAW corruption.", p: "P1" },
  // Platform
  { t: "Platform: AAX Native + AudioSuite support across the catalog", d: "Gap 31: AAX is the de-facto flagship requirement (Saturn 2, Soothe2, MicroShift all ship it).", p: "P2" },
  { t: "Platform: CLAP format support", d: "Gap 32: CLAP is the emerging cross-DAW standard; Saturn 2 ships it.", p: "P3" },
  { t: "Platform: shareable tagged presets + default-preset set", d: "Gap 33: Soothe2 .preset files are shareable/taggable cross-platform; add a similar preset system.", p: "P2" },
  { t: "Platform: per-plugin CPU benchmark matrix (eco vs high) + FFT-path optimization pass", d: "Gap 34: publish CPU/latency benchmarks per plugin and optimize FFT/oversampling paths.", p: "P2" },
  { t: "Platform: undo/redo + A/B compare + per-band solo in all flagship UIs", d: "Gap 35: industry-standard workflow features across ZScorch/ZTame/PresenceField.", p: "P3" },
  { t: "QA: golden-reference audio files missing for Gate 5 — capture null-test goldens for ZScorch & ZTame", d: "Gap 36: Golden-reference null tests are a release gate; capture + wire goldens.", p: "P1" },
];

let created = 0, failed = 0;
for (const r of REQUESTS) {
  try {
    const res = await fetch(`${BASE}/api/tasks`, {
      method: "POST",
      headers: { Authorization: `Bearer ${TOKEN}`, "Content-Type": "application/json" },
      body: JSON.stringify({ title: r.t, description: r.d, priority: r.p, autoRun: true }),
    });
    if (res.ok) { created++; console.log(`✓ ${r.t}`); }
    else { failed++; console.log(`✗ ${r.t} → ${res.status} ${(await res.text()).slice(0, 120)}`); }
  } catch (e) { failed++; console.log(`✗ ${r.t} → ${String(e).slice(0, 120)}`); }
}
console.log(`\nDONE: ${created} created, ${failed} failed`);
