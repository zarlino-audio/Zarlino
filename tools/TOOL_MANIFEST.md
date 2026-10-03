# Zarlino Local Tool Manifest

**File:** tools/TOOL_MANIFEST.md (companion machine-read registry: tools/TOOL_MANIFEST.json)
**Maintainer:** `zarlino-local-agent` (daemon / worker). Keep the `.md` and `.json` in sync.
**Last updated:** 2026-10-03 (release pipeline rebuild — tools/build-release-local.ps1 registered; PRD-01 remediation)

This manifest lists every SELF-INVENTED (ad-hoc) tool currently on this dev machine so none of
them stays ad-hoc or undiscoverable. It is the central registry that was missing before: many
throw-away tools/*.py / *.ps1, a stack of scratch build dirs (build-zscorch-repro/, build-clean-
zscorch/, build-zscorch-vs2026/, build-msvc/, build-vs-ci/, build_ci/) and recovered-source
residue (_recovered_*.h, _rec/*.cpp|.h) existed with no record of what commissioned them.

---

## Convention: SELF-COMMISSION rule

> When the local worker needs a capability **nothing already provides**, it MAY
> **self-commission a new tool**, but it MUST **register** that tool in
> `tools/TOOL_MANIFEST.json` **and** this `.md` (one row) **BEFORE using it in anger**.
> Nothing invented is ever left unregistered / undiscoverable.

Mechanics:
- Add one row (name, path, purpose, created date, status, commissionedBy) before first use.
- Status vocabulary: `OBSOLETE` (one-shot; generated output already captured in a commit/branch,
  so re-running now mostly no-ops) | `USED` (ran successfully at least once) | `PROTOTYPE`
  (thrown together for a one-off investigation, not validated as repeatable) | `TRACKED`
  (checked into git, part of normal tooling) | `ORPHAN_DUPLICATE` (stray/duplicate of a preferred
  sibling) | `SCRATCH_NOT_A_TOOL` (I/O test residue, not a capability).
- Timestamp every new row with its creation date so the register stays auditable.
- When a tool is superseded, flip its status and name the surviving sibling instead of leaving a
  silent duplicate.
- Scratch residue that is NOT a tool (self-tests, stray .txt, dumps) should be deleted or filed as
  a `SCRATCH_NOT_A_TOOL` note, never silently retained.

---

## Inventory (honest on-disk findings, verified 2026-09-03)

Only rows backed by an actual file present on disk are listed. No fabricated tools. Build-output
dirs and recovered-source residue are NOT tools; they are called out in the trailing "not tools" note.

### A. ZScorch standalone UI screenshot capture & analysis  (2026-08-30, founder sound/UI review pass)
| Tool | Created | Status | Purpose |
|---|---|---|---|
| tools/capture_zscorch_shot.ps1 | 2026-08-30 | USED | Launch ZScorch standalone, wait for window, screenshot window rect to artifacts/shots/zscorch-ui.png, then close app. |
| tools/capture_zscorch_shot2.ps1 | 2026-08-30 | USED | Shot variant with longer settle time + client-area capture. |
| tools/capture_zscorch_full.ps1 | 2026-08-30 | USED | Full client-area capture to zscorch-ui-full.png with a long settle. |
| tools/analyze_zscorch_shot.ps1 | 2026-08-30 | USED | Sample a pixel grid (~60x30), print coarse ASCII luminance/colour map of the shot. |
| tools/analyze_zscorch_shot2.ps1 | 2026-08-30 | USED | Deeper shot analysis: colour histogram + hi-res ASCII map. |
| tools/analyze_zscorch_bands.ps1 | 2026-08-30 | USED | Split full UI into 12 horizontal bands; print orange/light/dark/mid pixel stats + full-image. |
| tools/analyze_zscorch_full_map.ps1 | 2026-08-30 | USED | Fine hi-res ASCII luminance/colour map of the full-fidelity UI shot. |
| tools/analyze_zscorch_regions.ps1 | 2026-08-30 | USED | High-res region study: header, upper-left content, right control block. |

Commissioned by: founder sound/UI review pass (remote-operator handover / UI screenshot queue).

### B. ZScorch DSP re-engineer source patchers  (2026-09-02/03) -> SAFEGUARDED
One-shot scripts that produced the previously-dirty, at-risk ZScorch re-engineer DSP. That output
is now captured on durable `wip/zscorch-dsp-reengineer` @ eed75a3a (base 68425dc9) with a matching
named stash stash@{0} (d3078266). Because the generated code is committed there, mark them
OBSOLETE (one-shot; re-running now would mostly no-op). Keep for traceability.
| Tool | Created | Status | Purpose |
|---|---|---|---|
| tools/patch_zscorch_dsp.py | 2026-09-02 | OBSOLETE | Applied 4x oversampling, replaced arctan limiter with smoothed 1ms-lookahead peak-hold limiter, added per-band + master DC blockers in PluginProcessor.cpp. |
| tools/patch_zscorch_cmake.py | 2026-09-02 | OBSOLETE | Appended ZScorch_OfflineRender console-app target to plugins/ZScorch/CMakeLists.txt (real dry+processed A/B render). |
| tools/fix_zscorch_line.py | 2026-09-02 | OBSOLETE | Repaired a corrupt curly-quote/em-dash comment/brace fragment that broke compilation of PluginProcessor.cpp. |
| tools/fix_zscorch_dtor.py | 2026-09-02 | OBSOLETE | Out-of-lined ~ZScorchAudioProcessor (decl in .h / def in .cpp TU) so unique_ptr<Engine> is destroyed where Engine is fully defined (fixes incomplete type in ZScorch_OfflineRender). |
| tools/_fix_engine_dtor.py | 2026-09-03 | ORPHAN_DUPLICATE | Near-duplicate of fix_zscorch_dtor.py (relative-path flavour), same edit. Recommend deleting the underscore copy and keeping fix_zscorch_dtor.py. |

Commissioned by: ZScorch DSP re-engineer (4x os + per-band DC block + ceiling limiter + refactor of
DSP into an anon-namespace Engine) and its real-audio OfflineRender A/B tooling.

### C. Gap 24 ZScorch Wavefold oversampling alias study  (2026-08-31)  spec f5f7b5dc
Result fed the 4x-oversampling decision that is now part of the safeguarded DSP.
| Tool | Created | Status | Purpose |
|---|---|---|---|
| tools/gap24_wavefold_helpers.py | 2026-08-31 | USED | NumPy WOLA up/down-samplers + sat_wavefold model (helpers). |
| tools/gap24_wavefold_measure.py | 2026-08-31 | USED | Measure alias floor (strongest in-band alias + total non-harmonic alias) for a sine through wavefold+oversampling at a chosen OS. |
| tools/gap24_wavefold_run.py | 2026-08-31 | USED | Run 2x vs 4x alias cases (1k / 10k / 18k / 20k Hz, drive 100%). |
| tools/gap24_wavefold_sweep.py | 2026-08-31 | USED | Frequency + drive sweep comparing 2x vs 4x alias gap. |

Commissioned by: ZScorch Wavefold Gap 24 spec (aliasing-floor measurement + in-UI 4x recommendation).

### D. PresenceField PROCESSED-mode DSP variant  (2026-08-31)
| Tool | Created | Status | Purpose |
|---|---|---|---|
| tools/apply_presencefield_processed.py | 2026-08-31 | USED | Surgical patcher inserting a compile-time-guarded PRESENCEFIELD_PROCESSED ("Option B") applyProcessedWidth mid/side path alongside the default unprocessed applyWidth path in plugins/PresenceField/Source/PluginProcessor.cpp. |

Commissioned by: PresenceField processed-mode (Option B) DSP comparison (processing-mode-flag branch).

### E. Build helpers  (MSVC19/VS2026 + Ninja local builds, 2026-09-02/03)
| Tool | Created | Status | Purpose |
|---|---|---|---|
| tools/build_presencefield.bat | 2026-09-02 | USED | vcvars64 + cmake configure/build of PresenceField and PresenceField_OfflineRender (Ninja, Release). |
| tools/ninja_reconf.bat | 2026-09-02 | USED | vcvars64 + cmake -G Ninja reconfigure of the build dir, then forwards ninja args. |
| tools/ninja_run.bat | 2026-09-02 | USED | vcvars64 + ninja -C build dir forwarding args. |
| tools/_run_vs_zinja.cmd | 2026-09-03 | ORPHAN_DUPLICATE | Alternate ninja runner (name looks like a typo of ninja -> "zinja"); overlaps ninja_run.bat. Recommend keep one; delete or rename this stray copy. |

### F. ZTame licensing + P8 test stems
| Tool | Created | Status | Purpose |
|---|---|---|---|
| tools/ztame_license.py | 2026-09-02 | USED | Offline Ed25519 ZTame license producer/verifier (genkey/produce/verify/fingerprint). Private key stays local under tools/keys/ (gitignored); public key embedded in the plugin. |
| tools/gen_ztame_p8_stems.py | 2026-08-30 | USED | Deterministic generator of ZTame P8 suppression-quality A/B test stems (ringing, plucked-string, drums+pad) to artifacts/ztame-p8. |

Commissioned by: ZTame license-key scheme and ZTame P8 A/B test schedule.

### G. Real-audio A/B render + analysis scratch under repo-root _rec/  (2026-09-01)
| Tool | Created | Status | Purpose |
|---|---|---|---|
| _rec/render_zscorch.ps1 | 2026-09-01 | USED | Render a real source file through current ZScorch DSP to dry+processed WAVs. |
| _rec/stage_ab.ps1 | 2026-09-01 | USED | Stage the A (dry) vs B (processed) pair for a listening test. |
| _rec/analyze_wavs.py | 2026-09-01 | USED | Post-analysis of the rendered WAVs (metrics). |
| _rec/spectral_check.py | 2026-09-01 | USED | Spectral comparison of A vs B WAVs. |
| _rec/envelope_check.py | 2026-09-01 | USED | Envelope/transient comparison of the pair. |
| _rec/verify_fresh.py | 2026-09-01 | PROTOTYPE | Fresh-build verification pass of the render output. |
| _rec/final_verify.py | 2026-09-01 | PROTOTYPE | Final end-to-end verification of the A/B deliverables. |

Commissioned by: ZScorch / general real-audio A/B listening presentation (offline render tooling).

### H. Tracked / committed build tooling  (checked in; for completeness)
| Tool | Created | Status | Purpose |
|---|---|---|---|
| tools/new_plugin.sh | 2026-06-07 | TRACKED | Scaffolder (template -> new plugin) kept in git. |
| tools/clean_build.sh | 2026-06-07 | TRACKED | Clean configure/build wrapper. |
| tools/install_vst3.sh | 2026-06-07 | TRACKED | Installs a built .vst3 into the shared VST3 folder. |

(These three are committed and were themselves "restored after data loss" in prior incidents -
root-cause commits a118ce9d / 1a83d1c0 / 476f1fb1.)

---

### I. Release pipeline  (2026-10-03, PRD-01 remediation — founder-personal releases)
| Tool | Created | Status | Purpose |
|---|---|---|---|
| tools/build-release-local.ps1 | 2026-10-03 | TRACKED | One-command local release: MSVC/Ninja Release build -> ctest release-gate -> VST3+Standalone zips -> NSIS installer (embeds artefacts from -BuildDir via ZTame.nsi /DBUILD_DIR override) -> SHA256SUMS.txt + RELEASE_MANIFEST.txt. Mirrors .github/workflows/ztame-release-gate.yml. |

Commissioned by: company-readiness audit PRD-01 (local release process was founder-personal;
the old packaging script no longer existed). Registered before first real use per SELF-COMMISSION.

---

### Not tools (verified residue - not registered, some recommended for deletion)
- tools/py_test.txt, py_test2.txt, py_test3.txt, _echotest.txt, _writetest.txt: I/O self-tests.
  Recommend DELETE (SCRATCH_NOT_A_TOOL).
- tools/__pycache__/: python bytecode cache. Safe to delete.
- build-zscorch-repro/, build-clean-zscorch/, build-zscorch-vs2026/, build-msvc/, build-vs-ci/,
  build_ci/, build-clean/ : scratch CI/build output dirs. Not tools; archive or purge as deemed fit.
- _recovered_AnalysisCore.h, _recovered_ControlLayer.h, _recovered_Exciter.h, _recovered_PluginProcessor.h,
  _recovered_Saturator.h, _recovered_Tilt.h and _rec/*.cpp|.h, tools/keys/: recovered-source / key residue
  from prior data-loss wipe events - do NOT delete Key/Recon material blindly. Guard before any purge.
- _pem.txt, _pem_hex.txt, _live_status.txt, _offlinerender_save.txt, logs *.log: scratch state; review before purge.
- executive-os daemon worker scripts (writeb64.mjs, upload-audio.ts, daemon start/stop/uninstall *.ps1):
  separate subsystem tooling; tracked with the daemon, covered by its own docs. Not registered here.

---

### Recovery appendix (source-safety for the ZScorch DSP re-engineer)
Recover the previously-dirty re-engineer DSP (4x os / per-band DC blockers / ceiling limiter /
Engine refactor + OfflineRender.cpp) with either:
- git checkout wip/zscorch-dsp-reengineer      # commit eed75a3a8668a5d09fd245e2efb4168f912875e8
- OR: git stash apply stash@{0}                 # stash commit d30782669b4e3d3dd862f581ab6165dd9793b34e
Original branch feat/presencefield-processing-mode-flag stays at 68425dc9, clean. main/master untouched.
Nothing was pushed; the WIP branch has no upstream. (House WIP-protection pattern mirrors commits
9bee11d1 / e4309a3a "commit on feature branch to survive branch switches".)

