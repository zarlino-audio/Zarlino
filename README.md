# Zarlino Audio — plugin monorepo

JUCE-based VST3 plugin development monorepo for Zarlino Audio.

## Products in this repository

| Plugin | What it is |
|---|---|
| `ZTame` | Tone-shaping plugin (v1.2 shipped as public beta). |
| `ZScorch` | Saturation / "scorch" processor. |
| `ZVocal` | Vocal processor (in progress). |
| `PresenceField` | Presence / spatial processor (in progress). |
| `MyFirstEQ`, `ZAdaptiveHarmonics`, `ZPunch`, `RACK_AI` | Earlier scaffolds and planned products. |
| `_template` | Starter template for new plugins. |
| `factory` | **Zarlino Plugin Factory** - the deterministic factory kernel. Turns a versioned PluginSpec JSON document into a working processor (validated spec, certified modules, serial graph, parameters/macros) with no AI at runtime. See [`factory/README.md`](factory/README.md). |

## Layout

| Path | What |
|---|---|
| `plugins/` | One folder per plugin (each a CMake project). |
| `factory/` | Plugin Factory kernel, certified module library, example PluginSpecs, factory tests and CLI. |
| `shared/` | Reusable DSP/UI/licensing/test code. |
| `specs/` | Product specifications. |
| `docs/` | Documentation hub — start at [`docs/README.md`](docs/README.md). |
| `tools/` | Build / release / QA helpers — registry in [`tools/TOOL_MANIFEST.md`](tools/TOOL_MANIFEST.md). |
| `cmake/` | Shared CMake modules (`ZarlinoTest.cmake`). |
| `company-readiness/` | Company operations: audits, gap ledgers, playbooks, VM setup kit. |
| `decisions/` | Recorded decisions. |
| `dissection/` | JUCE/DSP study notes. |
| `JUCE/` | JUCE framework (git submodule). |
| `sdk/VST3_SDK/` | Steinberg VST3 SDK — clone required, gitignored. |

## Build & release

See [`docs/operations/BUILD_AND_RELEASE.md`](docs/operations/BUILD_AND_RELEASE.md). In short:

```bash
git submodule update --init JUCE
cmake -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build
```

Releases: `powershell -ExecutionPolicy Bypass -File tools\build-release-local.ps1 [-Plugin ZScorch]`.

## Related repositories

Separate repositories (kept as sibling folders when working locally):

- **zarlino-source** — cloud stem-separation / model engine (`github.com/zarlino-audio/zarlino-source`).
- **zarlino-website** — zarlinoaudio.com site and storefront (`github.com/zarlino-audio/zarlino-website`).
- **zarlino-executive-os** — the company operating system (`github.com/zarlino-audio/zarlino-executive-os`).

## Encoding policy (UTF-8)

The repo is UTF-8 everywhere. Smart characters (em/en dashes — –, curly quotes
“ ” ‘ ’, ellipsis …, ×, etc.) must never appear as `â€"` mojibake — that happens
when UTF-8 bytes are read with the ANSI/Windows-1252 codepage.

- `.editorconfig` enforces `charset = utf-8` for all files, and `utf-8-bom` for
  `.nsi` and `.ps1` (NSIS 3.x and PowerShell 5.1 only honour UTF-8 when a BOM is
  present).
- When creating new docs or installer scripts, type the real character (e.g. `—`)
  and let the editor save it as UTF-8 — do NOT pre-encode it. If you ever see
  `â€"` in a file, the content was saved/read with the wrong codepage: fix the
  tool/editor (set UTF-8), not the characters.

## Notes

Only `main` carries repository content; local-only working folders (build trees, artifacts,
backups, sibling checkouts) are gitignored — see `.gitignore`.


