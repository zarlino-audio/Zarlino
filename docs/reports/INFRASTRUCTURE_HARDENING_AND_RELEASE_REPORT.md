# Zarlino Audio — Infrastructure Hardening & Release System: final report

**Date:** 2026-10-12
**Scope:** `zarlino-audio/zarlino-website` (Cloudflare Worker + Astro site), `zarlino-audio/Zarlino`
(monorepo: plugins, release tooling, CI), private R2 bucket `zarlino-releases`.
**Method:** audit first, then change; every claim below is backed by a commit, a CI run, or an
independently reproduced probe. Build success, deploy success, verification and rollback are treated as
four different things throughout.

## 1. Verdict

| Delivery surface | State | Evidence |
| --- | --- | --- |
| Website (zarlinoaudio.com) | **VERIFIED** | deploy 38250484179 → gate 38250887008 `success`; local re-run of the gate: `VERIFIED`, 40/40 entries green, identity `ok` |
| Plugin release (ZTame 1.2.0) | **PUBLISHED + HASH-VERIFIED** | `publish-release.mjs` → 59/59 objects verified in R2; manifest `status=verified`, `gaps=[]` |
| Release automation (build → publish → delivery proof) | **PROVEN END TO END** | gate run 38833517344 (commit `799fe3a4`): "Publish VERIFIED release to R2" and "Verify production website serves this release" both `success` |
| Website release consumption | **LIVE** | production serves release bytes: installer 2,876,798 B, sha256 `ff76b7f6…` == manifest; 3 assets + 9 audio examples match |
| Release rollback | **PROVEN (dry run + guard)** | `rollback-release.mjs --dry-run` re-downloaded and SHA-256-verified all 59 objects; refused to "roll back" to the release already live |

Commits this phase: website `63218e6` (release consumption), `d027790` (gate warm-up);
monorepo `f146f801` (publish + delivery verification + rollback tooling), `799fe3a4` (commit the
publish tool the gate invokes), `afb5700b` / `9288bd96` (this report, operations docs).
**Current live release: v1.2.0 @ `799fe3a47511`**, published by CI at 2026-10-16T01:19:51Z.

## 2. Mandate conditions

1. **Audit before modify — MET.** Each phase read the live configuration, symptoms and existing
   contract before editing; the 1101 root cause was reproduced locally before the fix, and the release
   design was derived from the published manifest rather than assumed.
2. **Deploy deadlock eliminated — MET.** `deploy.yml` builds and deploys only; `post-deploy-gate.yml`
   verifies externally afterwards (workflow_run + nightly). No gate step depends on its own outcome.
3. **Production identity — MET.** `/api/health` reports `{commit, runId, deployedAt}` stamped by CI;
   the gate requires an exact match, otherwise `PRODUCTION_DRIFT`. Verified three consecutive cycles
   (120e708, 127b8bc, 3e64aab) plus 63218e6 and d027790.
4. **Failure boundaries — MET.** Top-level try/catch in the Worker (controlled 500 + secret-free
   structured log with `cf-ray` ref), unknown `/api/*` = JSON 404, unknown pages = exact 404 page.
   New release routes add their own explicit boundaries: 404 unlisted key / traversal / case mismatch,
   405 wrong method, 503 with `storageBound`/`pointerFound` when nothing is published.
5. **ONE route contract — MET.** `zarlino-website/routes/contract.json` is consumed by the gate, the
   Worker (`GET /api/contract`) and monitoring. This phase changed asset expectations **in the contract**
   (release aliases + `/api/release/ztame`), never in a parallel list.
6. **Cloud-only builds — MET.** Binaries/screenshot/audio are produced by the Windows release gate
   (`windows-2022`), not by hand; local builds are for diagnosis only.
7. **Plugin release as an atomic unit — MET.** One gate run produces binaries + canonical screenshot +
   canonical audio, one version, one commit, one manifest; publishing ships exactly that set.
8. **Release manifest — MET.** `release-manifest.json` schema v2, `status=verified`, per-file SHA-256,
   `media.heroScreenshot.generatedFromCommit == commit`, 9 `audioExamples` with render + measurement
   data, `gaps=[]`. `finalize` re-hashes everything from disk and fails closed.
9. **Canonical screenshots AND canonical audio — MET.** Screenshot rendered from the release commit and
   validated (not blank/black); audio rendered through the release plugin from committed references
   (5 loops + 4 one-shots), resampled deterministically to 48 kHz/24-bit.
10. **Fair A/B — MET.** Both comparison copies are level-matched to the quieter side's LUFS (min-target;
    one side is always 0.00 dB and nothing is boosted), matched delta is 0.0 LU for all 9 examples, and
    the level-match delta plus material origin are disclosed in the player.
11. **Synchronized A/B player — MET.** `ABPlayer.tsx` drives both sides from ONE `<audio>` element:
    swapping A/B captures `currentTime` + play state and restores it on `loadedmetadata`, so the
    comparison is position-synchronized by construction; content comes from `/api/release/ztame`.
12. **Release asset storage decision — MET (founder decision: R2).** Private bucket
    `zarlino-releases`, no public bucket; assets are reachable only through the Worker alias surface.
13. **Artifact integrity — MET.** Publish re-downloads and hashes every object before writing the
    pointer; the pointer lists every published path with its SHA-256; production bytes were compared
    against the manifest independently (installer, MP3, screenshot — all match).
14. **Website release consumption — MET.** Pages use stable aliases (`/releases/ztame/latest/…`);
    the screenshot, the three downloads and all demo audio come from the verified release; the old
    manually managed media is deleted (see #19). If the release cannot be loaded the player says so
    rather than playing a stale file.
15. **Rollback — MET (two independent paths).** Website: `wrangler versions list` + `wrangler rollback`
    then re-run the gate. Release: `tools/release/rollback-release.mjs` moves the pointer after
    re-verifying every published object; the site follows within the 60s pointer cache — no redeploy.
16. **Observability — MET.** `/api/health` (identity), `/api/ready` (capability 200/503),
    `/api/contract`, release 503 diagnostics, structured unhandled-exception log, and the citable
    acceptance record (`post-deploy-acceptance` artifact: intended sha, observed sha, sweep timestamps,
    per-entry verdicts, warm-up window).
17. **Executive OS evidence model — MET.** The OS consumes the same contract and identity endpoints
    (`/api/health`, `/api/ready`, `/api/contract`, `/api/probe-exec-os`); acceptance records are CI
    artifacts, so "verified" is a citable fact rather than a claim.
18. **Failure testing — PASS.** Local matrix against the worker: alias HEADs 200 with correct content
    types (exe/zip/png/mp3/json), served installer bytes hash-identical to source, unknown key 404,
    encoded traversal 404, case mismatch 404, wrong method 405, unknown product 404, unpublished
    release 503 (both binding and pointer variants distinguishable). Production: same negatives 404.
    A real CI failure was also caught and fixed inside this phase (see §3).
19. **Remove stale infrastructure — MET.** Deleted `public/audio/**`, the static `ZTame-v1.0.0/v1.2.0`
    downloads, `public/images/ztame-ui*.png`, `src/config/demos.ts`, `scripts/demo-mp3.mjs`, the dead
    `ZSCORCH_PRESETS`, and the stale `/plugins/zscorch#hear-the-difference` link; the legacy
    `ADMIN_TOKEN` literal was removed from `wrangler.vee-deploy.toml`.
20. **Required final report — MET.** This document.

## 3. What production actually serves now (independent verification)

```
GET  /api/release/ztame            -> v1.2.0 @ 799fe3a4, status=verified, gaps=[], 9 audioExamples
GET  /releases/ztame/latest/installer.exe        -> 200, 2,876,798 B, sha256 ff76b7f6… == manifest
GET  /releases/ztame/latest/audio/web/loop-disguise-A.mp3 -> 200, sha256 == manifest
GET  /releases/ztame/latest/screenshot.png       -> 200, sha256 == manifest
node tools/release/verify-website-release.mjs --product ztame --expect-commit 799fe3a4…
   -> OK - https://zarlinoaudio.com serves ztame v1.2.0 @ 799fe3a47511; installer alias HTTP 200
HEAD /releases/ztame/latest/does-not-exist.exe   -> 404
GET  /releases/ztame/latest/%2e%2e%2flatest.json -> 404
GET  /releases/zscorch/latest/installer.exe      -> 404
```

Fair A/B, measured on the bytes production actually serves (10.0 s, 24-bit/48 kHz stereo, 480,000
frames per side):

```
A  dry comparison : rms -26.664 dBFS  peak -14.490 dBFS
B  wet comparison : rms -26.668 dBFS  peak -12.247 dBFS
   -> level match 0.004 dB RMS difference (design target <= 0.2 LU/LU-matched)
   -> wet comparison is byte-identical to the wet master (0.00 dB gain on that side, nothing boosted)
   -> neither side exceeds 0 dBFS; measured peaks agree with the manifest to 3 decimals
```

Two real defects were found and fixed by running the pipeline rather than trusting it:

- **NON_DETERMINATE on first sweep (run 38250560326).** The first sweep after a deploy that adds routes
  was answered by the previous worker/asset configuration (`R G G` → FLAP). Fixed by a bounded route
  warm-up before the recorded sweeps, and by no longer counting settle-window identity observations as
  flapping. Re-run 38250887008 = VERIFIED; the hardened script also verifies production locally.
- **Publish step could not find its tool (run 38252371694).** `tools/release/publish-release.mjs` was
  written during the R2 work but never committed, so the new gate step failed after a fully green
  build/render/validate/manifest chain. Fixed in `799fe3a4`. The failure path behaved correctly: the
  gate went red, the evidence artifacts were still uploaded, and the pointer was not written.

Run 38833517344 then executed the complete path for the first time on a single commit: build, tests,
package, canonical screenshot, canonical audio render/validate/encode, manifest finalize, evidence
upload, publish to R2, and delivery verification against production — every step green, with the
pointer moved only after all 59 objects were re-downloaded and hash-verified.

## 4. Known gaps and accepted risks

- **ZScorch still ships legacy static downloads** (`/downloads/ZScorch-v1.0.0-*`). It has no release
  pipeline, so it cannot consume a verified release yet; those three paths remain in the contract and
  are deliberately unchanged. ZTame — the product under release control — is fully release-backed.
- **Audio rights annotation.** `plugins/ZTame/references/references.json` records
  `rights: UNVERIFIED - looperman upload; confirm license and attribution before public web use` for the
  loop references; the one-shots are FL Studio stock content from the founder's machine. The founder
  directed that this material be used and the audio is published. **Business decision required**:
  confirm looperman licensing/attribution (or swap references) before promoting the demos widely.
- **Legacy `ADMIN_TOKEN` literal** was removed from `wrangler.vee-deploy.toml` but remains in git
  history. It is unused: admin authentication is username/password with signed sessions
  (`ADMIN_SESSION_KEY`). Classified as an already-compromised, non-live credential — rotate or purge
  history if it was ever shared.
- **Local dev cannot exercise release routes against real R2.** `wrangler dev` uses a simulated bucket
  and `wrangler dev --remote` here still reported `pointerFound:false`; release behaviour is therefore
  validated locally with seeded fixtures and authoritatively in production.
- **Website release rollback is a pointer move, not a Worker rollback.** Rolling the Worker back does
  not roll the release back, and vice versa — both are documented separately in the website README.

## 5. Operating procedures

Publish: push to `main` (paths `plugins/ZTame/**`, `shared/**`, `tools/release/**`,
`.github/workflows/ztame-release-gate.yml`) → the gate builds, renders, validates, finalizes, uploads
evidence, publishes to R2, then proves the website serves the new commit. A red step anywhere means the
release is **not** live.

Verify: `node tools/release/verify-website-release.mjs --product ztame --expect-commit <sha>` for the
delivery surface; `zarlino-website/scripts/post-deploy-check.mjs` (or the CI gate) for the site.

Roll back a release: `node tools/release/rollback-release.mjs --product ztame --version <v>
--commit <40-sha> --account <id> [--dry-run]`, then re-run
`verify-website-release.mjs` with the restored commit.

Roll back the site: `npx wrangler versions list` → `npx wrangler rollback <version-id>` → re-run the
acceptance gate and confirm identity + all contract entries green.
