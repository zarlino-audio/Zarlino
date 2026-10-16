# Build and release — plugin monorepo

Verified against the local toolchain (MSVC 19.x / Visual Studio 2022+, CMake ≥ 3.22, Ninja).
Update this file if the toolchain changes.

## Prerequisites

- Visual Studio 2022+ with the "Desktop development with C++" workload (MSVC), or another C++17
  toolchain.
- CMake ≥ 3.22 and Ninja on `PATH`.
- JUCE submodule checked out: `git submodule update --init JUCE`
- Steinberg VST3 SDK cloned to `sdk/VST3_SDK` (gitignored; the root `CMakeLists.txt` points JUCE at
  it via `JUCE_VST3_SDK_PATH`).

## Configure and build

```bash
cmake -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build
```

- Ninja note: build the per-plugin `<Plugin>_All` target — the bare `<Plugin>` target is the
  shared-code library and produces no binaries.
- The first configure runs `juceaide` and can take 20–60 s per plugin touched.
- LTO links can take minutes; a long "Linking …" step is not a hang.

## Tests

```bash
ctest --test-dir build            # all registered plugin tests
ctest --test-dir build -L release-gate   # the release-gate label used by CI
```

- `-DBUILD_TESTING=ON` is required, otherwise plugins are not registered with CTest
  (`include(CTest)` lives in each plugin's own `CMakeLists.txt`).

## Release (local, one command)

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-release-local.ps1 [-Plugin ZScorch]
```

- Produces `artifacts\release-<Plugin>-v<version>\`: VST3 + Standalone zips, the NSIS installer,
  `SHA256SUMS.txt` and `RELEASE_MANIFEST.txt`.
- Mirrors `.github/workflows/ztame-release-gate.yml`.
- Plugin versions are defined in each plugin's own version header (`Source/Version.h`).

## Release assets in the cloud (R2) — publish, verify, roll back

Publishing is part of the release gate, not a manual step. After the manifest is finalized and the
evidence artifacts are uploaded, `ztame-release-gate.yml` runs:

```powershell
node tools/release/publish-release.mjs `
  --manifest artifacts/release-manifest.json --base artifacts `
  --audio-dir artifacts/audio --web-dir artifacts/audio/web `
  --bucket zarlino-releases --account <CF_ACCOUNT_ID> --product ztame
node tools/release/verify-website-release.mjs --product ztame --expect-commit <sha>
```

- `publish-release.mjs` refuses a manifest whose `status` is not `verified`, uploads the binaries,
  canonical screenshot, audio masters/comparisons and web previews, then **re-downloads every object
  and compares its SHA-256** before writing `releases/ztame/latest.json`. That pointer is the rollback
  handle: the website resolves every stable alias through it, so a new release reaches users the moment
  the pointer moves — verify-then-publish, never the other way round.
- `verify-website-release.mjs` proves the DELIVERY surface: production `/api/release/ztame` must report
  this exact commit (bounded polling absorbs the site's 60s pointer cache) and the installer alias must
  answer 200. Requires the `CLOUDFLARE_API_TOKEN` secret and the `CF_ACCOUNT_ID` variable; both steps
  are skipped on pull requests.
- Roll a release back (pointer move, no redeploy, no rebuild):

```powershell
node tools/release/rollback-release.mjs --product ztame --version 1.2.0 --commit <40-char-sha> `
  --account <CF_ACCOUNT_ID> [--dry-run]
```

  It refuses non-`verified` targets, re-downloads and SHA-256-verifies every published object of the
  target release (the R2 REST object API has no HEAD), records what it rolled back FROM in the pointer,
  and writes the pointer last. `--dry-run` performs all verifications without writing.
- The bucket is private; the website Worker is the only reader, and it serves only paths listed in the
  published pointer. Layout and alias surface: `zarlino-website/README.md`.

## Known local state

- The JUCE submodule shows as modified (` M JUCE`) in `git status` **on purpose**: 
  `JUCE/extras/Build/juceaide/CMakeLists.txt` carries a local workaround for an MSVC 19.50+ optimizer
  hang (forces `/Od` for the juceaide helper). Keep the patch applied locally; re-apply it after any
  JUCE update. It is intentionally not committed to the submodule pointer.

## Encoding conventions

- ASCII preferred in code; docs are UTF-8.
- PowerShell 5.1 and NSIS scripts must be saved as UTF-8 **with BOM** — see `.editorconfig`.
- Never commit `â€"`-style mojibake: it means a file was read or written with the Windows-1252
  codepage instead of UTF-8. Fix the tool/editor, not the characters.
