# Zarlino Plugin Factory - deterministic factory kernel

The factory kernel turns a versioned **PluginSpec** document into a working audio
processor. No AI is involved at runtime, no C++ appears in the specification, and
the same document always produces the same graph, the same parameter mappings and
bit-identical audio.

```
PluginSpec (JSON on disk)
        |
        v
 [ strict schema validation ]      factory/src/PluginSpec.cpp
        |
        v
 [ module resolution ]             factory/src/ModuleRegistry.cpp
        |     certified modules only, by id + version
        v
 [ graph construction ]            factory/src/GraphEngine.cpp
        |     serial chain, bypass, dry/wet, macros
        v
 [ parameter + macro algebra ]     factory/src/ParameterSystem.cpp
        |
        v
 process(channels, numChannels, numSamples)
```

## Layout

| Path | Contents |
|---|---|
| `include/zfactory/` | the kernel's public API |
| `src/` | kernel implementation |
| `modules/` | the certified module library (11 capabilities) |
| `specs/` | committed example PluginSpecs |
| `tests/` | factory-level automated tests (`Factory_Tests`) |
| `tools/` | `ZarlinoFactory_Check` CLI |
| `compat/` | JuceHeader shim so plugin DSP sources can be reused |

Build targets:

```
cmake --build <build-dir> --target Factory_Tests ZarlinoFactory_Check
```

`Factory_Tests` is registered with CTest as `Factory.Unit` (labels
`Factory;DSP;release-gate`).

## The PluginSpec document

A spec is strict, versioned and total. Every field is required unless the schema
says otherwise, unknown fields are rejected, and every failure produces a stable
error code, JSON path and message:

```
ZFACTORY-2002 $/graph/modules[1]/moduleId: module 'zarlino.gain' version 3.0.0 is
incompatible with the registered version 1.0.0 (major versions must match and the
registry must not be older)
```

```jsonc
{
  "factorySchemaVersion": 1,
  "plugin": {
    "id": "zarlino.example.channel-strip",
    "name": "Example Channel Strip",
    "vendor": "Zarlino Audio",
    "version": "1.0.0",
    "effectType": "channel-strip",
    "channels": "stereo"
  },
  "graph": {
    "modules": [
      { "instanceId": "input", "moduleId": "zarlino.gain",
        "moduleVersion": "1.0.0", "parameters": { "gainDb": 0.0 } }
    ]
  },
  "parameters": [
    { "id": "inputTrim", "name": "Input Trim", "target": "input.gainDb",
      "range": { "min": -18.0, "max": 18.0, "default": 0.0 },
      "unit": "dB", "logarithmic": false, "group": "Input", "exposeToHost": true }
  ],
  "macros": [
    { "id": "intensity", "name": "Intensity", "defaultValue": 0.5,
      "mappings": [
        { "parameter": "inputTrim", "rangeStart": 0.0, "rangeEnd": 0.65,
          "curve": 1.0, "scale": 1.0, "invert": true }
      ] }
  ],
  "dryWet": { "enabled": true, "defaultMix": 1.0 },
  "bypass": { "mode": "hard", "defaultBypassed": false },
  "ui": {
    "accentColor": "#4C9AFF", "category": "Dynamics",
    "shortDescription": "...", "tags": ["channel-strip"],
    "layout": "vertical", "parameterOrder": ["inputTrim"]
  }
}
```

### Factory controls

Two controls belong to the kernel rather than to a module, and are addressed like
any other parameter:

| Target | Meaning |
|---|---|
| `$factory.dryWet` | global dry/wet mix, `0..1`, requires `dryWet.enabled` |
| `$factory.bypass` | global bypass, `0..1`; `0.5` and above bypasses |

## Module contract

Every module implements `zfactory::DspModule`:

```cpp
const ModuleManifest& getManifest() const noexcept;   // identity, version, params, latency
void prepare (const PrepareContext&) override;        // may allocate; control thread only
void reset() noexcept override;                       // allocation free
void process (float* const* channels, int numChannels, int numSamples) noexcept override;
void setParameter (int parameterIndex, float value) noexcept override;
float getParameter (int parameterIndex) const noexcept override;
int getLatencySamples() const noexcept override;
```

**Realtime safety**: `process()`, `reset()`, `setParameter()`, `getParameter()` and
the whole parameter/macro layer never allocate, lock or perform I/O. Parameters
are addressed by integer index resolved once at build time, so no string lookup
happens while audio is running. This is enforced, not just documented: the
`Factory Realtime Safety` suite replaces the global allocation operators with a
counting trap and runs the full 11-module chain with it armed.

`PrepareContext` carries the sample rate, maximum block size and channel count, so
a module is told about its environment rather than guessing it.

## Certified module library

| Module id | Category | Latency | Notes |
|---|---|---|---|
| `zarlino.gain` | utility | 0 | smoothed dB trim + polarity |
| `zarlino.filter.multimode` | filter | 0 | resonant LP/HP, 12 or 24 dB/oct |
| `zarlino.eq.tilt` | equalizer | 0 | matched shelf pair around a pivot |
| `zarlino.eq.parametric` | equalizer | 0 | 4 bands (low shelf, 2 peaks, high shelf) |
| `zarlino.dynamics.compressor` | dynamics | 0 | soft knee, RMS or peak detection |
| `zarlino.dynamics.gate` | dynamics | 0 | downward expander with hysteresis |
| `zarlino.saturation` | saturation | 0 | six ZScorch curves with ADAA anti-aliasing |
| `zarlino.limiter` | limiter | 1 ms | lookahead brick wall |
| `zarlino.shaping.transient` | shaping | 0 | dual-envelope attack/sustain shaper |
| `zarlino.stereo.width` | stereo | 0 | mid/side width + bass-mono guard |
| `zarlino.mix.drywet` | mixing | 0 | linear or equal-power blend |

All eleven are `CERTIFIED`. Lifecycle states are structural
(`EXPERIMENTAL`/`VERIFIED`/`CERTIFIED`/`DEPRECATED`) and the kernel's default
policy is **certified only**, so a V1 user-generated plugin cannot reach a module
that is still experimental.

Reuse: the modules are built on `shared/dsp` (`ZBiquadFilter`, `ZEqualizer`,
`ZDynamics`, `ZDspUtils`, `ZSmoother`) and `plugins/ZScorch/Source/Saturator.cpp`
is compiled into the kernel directly, so the factory adds no second saturation
engine and cannot drift from the shipped ZScorch sound.

## Parameter and macro algebra

Resolution happens once, at graph build time; every spec parameter becomes a
binding to exactly one `(module instance, module parameter index)` pair.

```
m = clamp01 (macro value)
m = invert ? 1 - m : m
m = pow (m, curve)
p = rangeStart + (rangeEnd - rangeStart) * m
p = clamp01 (p * scale)
value = binding.denormalise (p)          // clamped to the declared range
```

Consequences, all asserted by the test suite:

* a macro can never drive a parameter outside its declared range;
* macro values outside `0..1` are clamped, never extrapolated;
* mappings are applied in spec order, so a parameter targeted by two macros is
  owned by the later one (deterministic, documented);
* a macro-driven parameter ignores host writes to its base value.

Two exposed parameters may not target the same module parameter - that would make
the effective value order dependent, so it is rejected as ambiguous.

## State serialization

`GraphEngine::saveState()` produces a JSON blob carrying the factory schema
version, the fingerprint of the spec the graph was built from, every macro
position, every parameter base value, the dry/wet position and bypass.
`restoreState()` refuses a blob from a different schema
(`StateSchemaUnsupported`) or from a different spec (`StateSpecMismatch`) instead
of silently mis-mapping values. DSP working state is not serialized, matching the
convention of the shipped ZTame/ZScorch processors, which persist their parameter
tree only.

## Running it

```
# catalogue and its fingerprint
ZarlinoFactory_Check --catalog

# load, validate, build, prepare and report
ZarlinoFactory_Check factory/specs/vocal.zfactory.json

# ...and push a deterministic test signal through it
ZarlinoFactory_Check factory/specs/vocal.zfactory.json --render --sample-rate 96000 --block 257
```

Exit codes: `0` success, `1` spec/validation failure, `2` usage error.

## Example processors

Three committed specs prove the same kernel produces materially different
processors. All three are loaded from disk and rendered by the test suite:

| Spec | Channels | Chain |
|---|---|---|
| `channel-strip.zfactory.json` | stereo | HP -> compressor -> tilt EQ -> limiter |
| `saturation.zfactory.json` | stereo | trim -> saturator -> tilt -> trim -> limiter |
| `vocal.zfactory.json` | mono | gate -> HP -> parametric EQ -> compressor -> saturation -> limiter |
