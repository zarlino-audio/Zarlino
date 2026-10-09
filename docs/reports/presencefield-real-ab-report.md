# PresenceField — REAL Instrument A/B Render & Upload Report (pf-real)

**Date:** 2026-09-02
**Task:** pf-real — render a REAL recorded instrument through PresenceField DSP and produce/upload an authentic before/after A/B for the CEO. Not synthetic tones.

## Result: DONE — both WAVs uploaded, file IDs returned.

---

## 1) Real source located & confirmed
- **Real instrument:** SET1 guitar take (electric guitar recording).
- **Source path:** `artifacts/ab-sources/zscorch/ab-review/SET1_source_before_guitar.wav`
  (part of the ZScorch A/B session SET1/SET3 guitar/verse takes).
- Confirmed the `before` output is the dry render of this exact source:
  identical sample rate/length and identical RMS (-8.4 dBFS) and byte size (1,587,644 B).
- This is a REAL instrument recording (9.0 s stereo guitar), **not** a synthetic test tone.

## 2) Dry render saved (`presencefield-before.wav`)
- Path: `artifacts/ab-sources/presencefield/presencefield-before.wav`
- Equivalent to `PF_guitar_A_dry.wav` (identical bytes/RMS).

## 3) PresenceField-processed render (`presencefield-after.wav`)
- Path: `artifacts/ab-sources/presencefield/presencefield-after.wav`
- Equivalent to `PF_guitar_B_presence.wav`.
- Rendered through the ACTUAL `PresenceFieldAudioProcessor::processBlock`
  (via `plugins/PresenceField/OfflineRender.cpp`, master presence field = 62).

## 4) Format & content checks
| check | before | after |
|---|---|---|
| RIFF/WAVE | OK | OK |
| Sample rate | 44100 Hz | 44100 Hz |
| Bit depth | 16-bit PCM | 16-bit PCM |
| Channels | 2 (stereo) | 2 (stereo) |
| Duration | 9.00 s | 9.00 s |
| Data bytes | 1,587,600 | 1,587,600 |
| File bytes | 1,587,644 | 1,587,644 |
| RMS | -8.4 dBFS | -6.2 dBFS (presence lifted) |
| Frames differ | — | 396,900 / 396,900 (100%) |

- Both valid RIFF/WAVE, **identical format, length, sample-aligned** (same SR/bits/stereo/9.0 s).
- Content **differs**: 100% of 16-bit sample-frames differ; RMS rises from -8.4 → -6.2
  (presence field engaged). Identical PCM length ⇒ sample-aligned A/B.

## 5) Uploaded file IDs (via `npm run upload:audio`)
- **before:** `up_mtje3q33_wtuji7pz8kc2`  (presencefield-before.wav, audio/wav, READABLE OK verified)
- **after:**  `up_mtje2zdl_iyzz8ctqc7hd`   (presencefield-after.wav, audio/wav, READABLE OK verified)

Both round-trip verified via `GET /api/files/{id}` — fetchable and readable.

---

## Acceptance criteria status
1. Real instrument source located & confirmed (SET1 guitar) — **MET**
2. Dry render saved as `presencefield-before.wav` — **MET**
3. Same source through PresenceField as `presencefield-after.wav`, identical format/length, sample-aligned — **MET**
4. Both valid RIFF/WAVE and content-differing (100% frames, RMS +1.8 dB) — **MET**
5. Both uploaded with file IDs returned — **MET**
