# Zarlino Plugin Factory - deterministic kernel (v1)

Status: **implemented** (`factory/`). This record is the contract the kernel holds
itself to, and the baseline the next slice (UI generation) builds on.

## Scope of this slice

Take a versioned PluginSpec document, validate it, resolve certified DSP modules
from a registry, construct the audio graph, expose parameters and macros, and run
the processor correctly - **with no AI involved at runtime**.

Explicitly out of scope: customer-facing web UI, subscriptions, marketplace,
macOS/AU/AAX, AI-generated DSP modules, arbitrary user C++.

## Decisions

### 1. The spec is data, and only data

`PluginSpec` (schema v1) can express plugin identity, version, effect type,
mono/stereo configuration, an ordered module list with ids and versions, module
parameter values, exposed parameter ranges/defaults, macros with mappings,
dry/wet, bypass and the UI metadata the next slice needs. It cannot express C++,
expressions, scripts or external references.

Two virtual controls, `$factory.dryWet` and `$factory.bypass`, belong to the
kernel and are addressed exactly like module parameters, so macros and automation
need no special cases.

### 2. Validation is two-phase and deterministic

1. **Schema** - shape, types, ranges, identifier grammar, duplicate ids, dangling
   references, collection limits. Registry independent.
2. **Resolution** - the module exists, the version is compatible, the
   certification policy is satisfied, the channel mode and sample rate are
   supported, exposed ranges fit inside module domains, units agree, and no two
   exposed parameters target the same module parameter (which would be
   ambiguous).

Every failure carries a stable numeric code, a JSON path and a message built only
from the input, so the same document always produces the same error. The schema
version is checked first, so a document from another schema generation is
rejected as such rather than field-by-field.

### 3. Certification is structural

`EXPERIMENTAL` / `VERIFIED` / `CERTIFIED` / `DEPRECATED` are part of the manifest.
The kernel's default policy is **certified only**, and `DEPRECATED` never counts
as certified even though it remains usable.

### 4. The graph is serial and fully determined by the spec

`in -> [bypass] -> [dry copy] -> m0 -> m1 -> ... -> mN -> [mix] -> out`

No parallel routing in v1 (it could not be completed to the same standard as the
rest of the slice, so it was left out rather than half-built). Latency is the sum
of module latencies; when it is non-zero the dry path is delayed by exactly that
amount so dry/wet mixing stays phase coherent.

### 5. Realtime safety is enforced, not documented

`process()`, `reset()`, `setParameter()`, the parameter layer and macro
evaluation never allocate, lock or perform I/O. Parameters are resolved to
integer indices at build time. The `Factory Realtime Safety` suite replaces the
global allocation operators with a counting trap and runs the full 11-module
chain with it armed - including macro automation and the first block after
`prepare()`.

### 6. Reuse over reimplementation

Modules are built on `shared/dsp` (`ZBiquadFilter`, `ZEqualizer`, `ZDynamics`,
`ZDspUtils`, `ZSmoother`). `plugins/ZScorch/Source/Saturator.cpp` is compiled into
the kernel so the factory has one saturation engine, not two.

## Findings that changed shipped code

Investigating the factory's own failures surfaced two real defects in reused code:

1. **`Saturator::updateHarmReduction()` allocated on the audio thread.** The
   cache comment claimed otherwise, but every change of the harmonic-reduction
   control constructed a new `IIR::Coefficients` object. It also did not match
   JUCE's storage layout, which keeps a second-order section as **five**
   normalised values (the raw cookbook set with the `a0` term dropped, everything
   divided by `a0`), not six. Fixed to compute `ArrayCoefficients` (a
   `std::array`, no heap) and write the five normalised values into the
   coefficient object `prepare()` primed - allocation free, and correct for
   ZScorch's own plugin too.

2. **A latent JUCE trap in object detection.** In this JUCE version an **array
   `var` reports `isObject() == true`** while `getDynamicObject()` returns
   `nullptr`. Any spec with an array root crashed the loader. All object checks
   now use `getDynamicObject()` and treat a null pointer as "not an object".

## Evidence

* `Factory_Tests` - 9 suites, 1842 assertions, 0 failures
  (`factory/tests/`, CTest `Factory.Unit`, label `release-gate`).
* `ZarlinoFactory_Check` loads each committed spec from disk, validates, builds,
  prepares and renders it; exit code 0 for all three examples.
* Example specs: `channel-strip` (stereo), `saturation` (stereo), `vocal` (mono).
* CI: `.github/workflows/factory-gate.yml` - build, ctest scoped to
  `^Factory\.`, then CLI validation and rendering of every committed spec.
