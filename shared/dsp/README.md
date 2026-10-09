# Zarlino DSP Library — Standalone JUCE Replacements

Header-only, zero-dependency DSP primitives that replace the JUCE DSP classes
used across the Zarlino plugin suite. All modules are **JUCE-free** — they use
only `<cmath>`, `<vector>`, `<cassert>`, `<algorithm>`, `<cstring>`, and `<complex>`.

---

## Module Index

| Module | File | Replaces | Lines |
|---|---|---|---|
| ZBuffer | `shared/utils/ZBuffer.h` | `juce::AudioBuffer<float>` | 393 |
| ZFFT | `shared/dsp/ZFFT.h` | `juce::dsp::FFT` | 143 |
| ZOversampling2x | `shared/dsp/ZOversampling.h` | `juce::dsp::Oversampling<float>` | 232 |
| MultiChannelBiquad | `shared/dsp/ZBiquadFilter.h` | `juce::dsp::IIR::Filter` | 181 |
| ZLinearSmoother | `shared/dsp/ZSmoother.h` | `juce::SmoothedValue<float, Linear>` | 93 |
| ZDspUtils | `shared/dsp/ZDspUtils.h` | `Decibels`, clamp, denormals, math | 121 |

---

## 1. ZBuffer — Audio Buffer Replacement

**File:** `shared/utils/ZBuffer.h`  
**JUCE equivalent:** `juce::AudioBuffer<float>`

### Design

ZBuffer is a dual-mode buffer:
- **Owned mode** (`m_ownsData = true`): allocates per-channel `std::vector<float>` storage. Used when the buffer manages its own memory.
- **View mode** (`m_ownsData = false`): wraps external `float**` pointers. Used in `processBlock` where the host provides the buffer — **no allocation, no copy**.

The mode is determined at construction time:
```cpp
// Owned: allocates numChannels × numSamples floats
ZBuffer buf(numChannels, numSamples);

// View: wraps host-provided pointers (no allocation)
ZBuffer buf(hostChannelPointers, numChannels, numSamples, false);

// Copy-in view: allocates and copies data from external pointers
ZBuffer buf(hostChannelPointers, numChannels, numSamples, true);
```

### API Reference

| Method | Signature | Notes |
|---|---|---|
| `getNumSamples()` | `int` | — |
| `getNumChannels()` | `int` | — |
| `setSize(ch, n, keep, clear, avoid)` | `void` | See flags below |
| `getWritePointer(ch)` | `float*` | Returns `nullptr` if channel out of range |
| `getReadPointer(ch)` | `const float*` | Returns `nullptr` if channel out of range |
| `getSample(ch, idx)` | `float` | Returns 0.0f if out of bounds |
| `clear()` | `void` | Zeroes all channels (view or owned) |
| `clear(ch, start, n)` | `void` | Zeroes a region |
| `makeCopyOf(other, avoidRealloc)` | `void` | Deep copy from another ZBuffer |
| `copyFrom(dstCh, dstStart, src, srcCh, srcStart, n)` | `void` | Region copy between buffers |
| `getMagnitude(ch, start, n)` | `float` | Max absolute value in region |

**`setSize` flags:**
- `keepExisting`: preserve old data where dimensions overlap
- `clearExtraSpace`: zero newly allocated regions
- `avoidReallocation`: reuse existing capacity if possible (avoids heap)

**Copy semantics:** ZBuffer is non-copyable. Move is allowed (defaulted). Use `makeCopyOf()` for explicit deep copies.

### Key Behaviors

1. **View → Owned transition:** Calling `setSize()` when in view mode switches to owned storage. If `keepExisting` is set, existing external data is copied in.
2. **Memory safety:** `getWritePointer`/`getReadPointer` do bounds checks and return `nullptr` for invalid channels (unlike JUCE which asserts).
3. **`copyFrom` with overlap:** Uses `std::memmove` for safety — correct even if source and destination regions overlap.
4. **`getMagnitude`:** Iterates over samples computing `fabsf()`. For the common case of checking a full channel, pass `0` for start and `getNumSamples()` for n.

### Migration from JUCE

```cpp
// BEFORE (JUCE)
juce::AudioBuffer<float> buffer;
buffer.setSize(2, 512);
buffer.copyFrom(0, 0, otherBuffer, 0, 0, 512);
float mag = buffer.getMagnitude(0, 0, 512);
float* ptr = buffer.getWritePointer(0);

// AFTER (ZBuffer)
ZBuffer buffer(2, 512);
buffer.copyFrom(0, 0, otherBuffer, 0, 0, 512);
float mag = buffer.getMagnitude(0, 0, 512);
float* ptr = buffer.getWritePointer(0);
```

### Limitations vs JUCE

- No `AudioBuffer<Type>` templating — float only
- No `applyGain()` / `addFrom()` convenience methods (add as needed)
- No `findMinMax()` — use `getMagnitude()` for level detection
- No `getArrayOfWritePointers()` — pass individual channel pointers

---

## 2. ZFFT — Radix-2 FFT

**File:** `shared/dsp/ZFFT.h`  
**JUCE equivalent:** `juce::dsp::FFT`

### Design

Iterative Cooley–Tukey radix-2 FFT with precomputed twiddle factors. Order-based sizing (like JUCE): `ZFFT(10)` = 1024-point FFT. Twiddles are computed once at construction time.

**Performance:** O(N log N). Precomputed twiddles avoid per-transform trig calls. No SIMD — the polar complex multiply in the inner loop is scalar.

### API Reference

| Method | Signature | Notes |
|---|---|---|
| `ZFFT(order)` | constructor | `order=10` → 1024 points |
| `getSize()` | `int` | Returns `1 << order` |
| `performFrequencyOnlyForwardTransform(data)` | `void` | In-place: real in → magnitudes out |
| `performRealOnlyForwardTransform(data)` | `void` | Real in → packed complex out (2×size array) |
| `performRealOnlyInverseTransform(data)` | `void` | Packed complex in → real out |

### Packed Format (Critical)

`performRealOnlyForwardTransform` outputs the **JUCE-compatible packed format**:

```
Index:  [0]   [1]     [2]      [3]      [4]      [5]     ...  [N-2]        [N-3]
Data:   DC_re DC_im   bin1_re  bin1_im  bin2_re  bin2_im  ...  Nyquist_re   Nyquist_im
        (=0)                                                         (=0)
```

- **Input:** `data[0..size-1]` — N real time-domain samples
- **Output:** `data[0..size*2-1]` — N complex bins in packed form (needs 2×N floats!)
- DC imaginary and Nyquist imaginary are always ~0 (real input guarantees this)
- Only bins 0..N/2 are stored (Hermitian symmetry: bin N-k = conj(bin k))

**ZTame relies on this exact layout** for magnitude calculation (`Saturator.cpp` line ~80).

`performFrequencyOnlyForwardTransform`:
- **Input:** `data[0..size-1]` — N real time-domain samples
- **Output:** `data[0..size/2]` — magnitude spectrum (DC through Nyquist)
- Remaining indices `[size/2+1..size-1]` are zeroed

`performRealOnlyInverseTransform`:
- **Input:** `data[0..size*2-1]` — packed complex (from forward transform or modified)
- **Output:** `data[0..size-1]` — N real time-domain samples (scaled by 1/N)

### Migration from JUCE

```cpp
// BEFORE (JUCE)
juce::dsp::FFT fft(10); // 1024-point
fft.performRealOnlyForwardTransform(fftData);   // fftData is float[2048]
fft.performRealOnlyInverseTransform(fftData);

// AFTER (ZFFT)
ZFFT fft(10); // 1024-point
fft.performRealOnlyForwardTransform(fftData);   // fftData is float[2048]
fft.performRealOnlyInverseTransform(fftData);
```

### Limitations vs JUCE

- No `performFrequencyOnlyInverseTransform` (not needed by any plugin)
- Radix-2 only (size must be power of 2) — same as JUCE
- No SIMD acceleration (JUCE's FFT also doesn't use SIMD by default)
- Slight precision differences expected (~1e-6 on a single transform) due to different twiddle computation and accumulation order

---

## 3. ZOversampling2x — 2× Polyphase Oversampling

**File:** `shared/dsp/ZOversampling.h`  
**JUCE equivalent:** `juce::dsp::Oversampling<float>`

### Design

Zero-stuffing + cascaded biquad lowpass filtering. Not a true polyphase implementation —
it stuffs zeros then filters at 2× rate. This is simpler but computationally identical
to a polyphase-allpass decomposition for 2×.

**Filter:** Two cascaded 2nd-order Butterworth lowpass biquads (4th order total) in
Transposed Direct Form II, with cutoff at **0.45 × baseSampleRate**. At 44.1 kHz base
rate, the filter cutoff for the upsampled 88.2 kHz stream is ~19.8 kHz.

**Latency:** Approximately 2 base-rate samples (conservative estimate).

### API Reference

| Method | Signature | Notes |
|---|---|---|
| `prepare(numChannels, maxBlockSize)` | `void` | Allocates oversampled buffers. Uses 44.1 kHz default. |
| `reset()` | `void` | Zeroes all filter states |
| `getLatencySamples()` | `int` | Returns ~2 |
| `processSamplesUp(inputChannels, numSamples)` | `int` | Returns `numSamples * 2` |
| `processSamplesDown(outputChannels, numSamples)` | `void` | Filters + decimates back to base rate |
| `getOSChannelPointer(ch)` | `float*` | Access oversampled buffer for processing |
| `getOSNumSamples()` | `int` | Current oversampled sample count |
| `getOSNumChannels()` | `int` | Number of channels |
| `designHalfBandFilter(baseSampleRate)` | `void` | Redesign filter for a different sample rate |

### Processing Flow

```
Input (N samples @ Fs)
    │
    ▼ processSamplesUp()
Zero-stuff → N×2 samples @ 2Fs → 4th-order LP filter
    │
    ▼ (user processes oversampled audio via getOSChannelPointer)
    │
    ▼ processSamplesDown()
4th-order LP filter → decimate (take even samples) → N samples @ Fs
```

### Migration from JUCE

```cpp
// BEFORE (JUCE)
oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
    2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false);
oversampling->initProcessing(512);
auto block = juce::dsp::AudioBlock<float>(buffer);
auto osBlock = oversampling->processSamplesUp(block);
// ... process osBlock ...
oversampling->processSamplesDown(block);

// AFTER (ZOversampling2x)
ZOversampling2x os;
os.prepare(numChannels, 512);
const float* inputs[1] = { inputBuffer };
os.processSamplesUp(inputs, 512);
float* osData = os.getOSChannelPointer(0);
// ... process osData[0..1023] ...
float* outputs[1] = { outputBuffer };
os.processSamplesDown(outputs, 512);
```

### Limitations vs JUCE

- **2× only** — JUCE supports 2×, 4×, 8×, 16×. If higher oversampling is needed, cascade two ZOversampling2x instances.
- **Butterworth filter** — JUCE uses 8th-order elliptic (steeper). Our 4th-order Butterworth has gentler rolloff but zero passband ripple. Adequate for harmonic processing.
- **No `AudioBlock` integration** — works with raw `float*` channel pointers.
- **Composite latency untracked** — cascading instances requires manual latency summation.
- **Redesign on sample rate change** — call `designHalfBandFilter(newSampleRate)` in your `prepareToPlay`.

---

## 4. MultiChannelBiquad — RBJ Cookbook Biquad Filters

**File:** `shared/dsp/ZBiquadFilter.h`  
**JUCE equivalent:** `juce::dsp::IIR::Filter<float>` + `juce::dsp::IIR::Coefficients<float>`

### Design

Implements the **RBJ Audio EQ Cookbook** formulas for high-shelf, low-shelf, and high-pass
filters. Uses **Direct Form I Transposed** (DF1T) structure:

```
y[n] = b0·x[n] + z1
z1   = b1·x[n] − a1·y[n] + z2
z2   = b2·x[n] − a2·y[n]
```

DF1T was chosen because:
1. It handles coefficient changes with less transient than DF2
2. It's the same structure JUCE uses internally for `juce::dsp::IIR::Filter`
3. State variables have a direct physical interpretation

### Coefficient Generation (`RBJ` namespace)

```cpp
namespace RBJ {
    // gainLinear = linear gain factor (1.0 = unity, >1.0 = boost)
    // Q = shelf slope (NOT traditional Q — use 0.707 for no overshoot)
    BiquadCoefficients makeLowShelf(sampleRate, freqHz, Q, gainLinear);
    BiquadCoefficients makeHighShelf(sampleRate, freqHz, Q, gainLinear);
    BiquadCoefficients makeHighPass(sampleRate, freqHz, Q);
}
```

**Important:** The `Q` parameter in RBJ shelf filters controls **shelf slope**, not resonance.
For `makeHighPass`, traditional Q applies (0.707 = Butterworth, higher = resonant).

### MultiChannelBiquad API

| Method | Signature | Notes |
|---|---|---|
| `setCoefficients(coeffs)` | `void` | Hot-swappable — call during processing |
| `prepare(numChannels)` | `void` | Allocates per-channel state vectors |
| `reset()` | `void` | Zeroes all filter states |
| `processChannel(ch, samples, n)` | `void` | Process a single channel in-place |
| `process(channelData, numChannels, n)` | `void` | Process all channels (lazy-prepare if needed) |

### Migration from JUCE

```cpp
// BEFORE (JUCE)
using Filter = juce::dsp::IIR::Filter<float>;
using Coeffs = juce::dsp::IIR::Coefficients<float>;

auto coeffs = Coeffs::makeHighShelf(44100, 1000, 0.707, 2.0);
Filter filter;
filter.coefficients = coeffs;
filter.reset();
juce::dsp::AudioBlock<float> block(buffer);
juce::dsp::ProcessContextReplacing<float> ctx(block);
filter.process(ctx);

// AFTER (MultiChannelBiquad)
auto coeffs = RBJ::makeHighShelf(44100, 1000, 0.707, 2.0);
MultiChannelBiquad filter;
filter.setCoefficients(coeffs);
filter.prepare(numChannels);
filter.reset();
// ... in processBlock:
filter.process(channelPointers, numChannels, numSamples);
```

### Dynamic Coefficient Updates

Safe to call `setCoefficients()` mid-block between channel batches:

```cpp
filter.setCoefficients(RBJ::makeHighShelf(sr, 1000, 0.707f, gain));
filter.process(channelPointers, numChannels, numSamples);
// new gain immediately — no reset() needed
```

Changed coefficients take effect on the **next sample** within `process()`.

### Limitations vs JUCE

- Only 3 filter types (high shelf, low shelf, high pass) — these are all that Zarlino uses
- No `Coefficients::makeLowPass`, `makePeakFilter`, `makeBandPass` — add as RBJ:: functions if needed
- No `ProcessContextReplacing` integration — works with raw `float*` pointers
- No `Snapshot` / coefficient smoothing — apply `ZLinearSmoother` to the gain parameter upstream

---

## 5. ZLinearSmoother — Parameter Smoothing

**File:** `shared/dsp/ZSmoother.h`  
**JUCE equivalent:** `juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>`

### Design

Linear interpolating smoother that approaches a target value at a constant per-sample step
rate. The step size is computed once during `reset()`:

```
step = 1.0 / (sampleRate × rampTimeSeconds)
```

For the Zarlino default of 50ms at 44.1 kHz:
```
step = 1.0 / (44100 × 0.05) = 1.0 / 2205 ≈ 0.000454
```

### API Reference

| Method | Signature | Notes |
|---|---|---|
| `reset(sampleRate, rampTimeSeconds)` | `void` | Configures ramp rate |
| `setCurrentAndTargetValue(value)` | `void` | Jump immediately (no smoothing) |
| `setTargetValue(value)` | `void` | Begin approaching target |
| `getCurrentValue()` | `float` | Read without advancing |
| `getNextValue()` | `float` | Advance 1 step, return new value |
| `skip(numSamples)` | `void` | Advance N steps without reading |

### Migration from JUCE

```cpp
// BEFORE (JUCE)
juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> value;
value.reset(44100, 0.05);
value.setTargetValue(0.5f);
for (int i = 0; i < numSamples; ++i)
    buffer[i] *= value.getNextValue();

// AFTER (ZLinearSmoother)
ZLinearSmoother value;
value.reset(44100, 0.05);
value.setTargetValue(0.5f);
for (int i = 0; i < numSamples; ++i)
    buffer[i] *= value.getNextValue();
```

### Migration Pattern for JUCE `SmoothedValue` with `skip()`

JUCE's SmoothedValue's `skip()` advances the internal state:
```cpp
// BEFORE (JUCE)
smoothed.skip(numSamples); // advance silently
float val = smoothed.getCurrentValue();

// AFTER (ZLinearSmoother)
smoother.skip(numSamples);  // advance silently
float val = smoother.getCurrentValue();  // same
```

### Limitations vs JUCE

- No `isSmoothing()` — the `m_isConstant` flag is private. If needed, add a public accessor.
- No `Multiplicative` smoothing type — only linear. Zarlino only uses Linear.
- Ramp time is fixed after `reset()` — there's no mid-stream ramp time change. To change the
  ramp rate, call `reset()` again (which doesn't change the current/target values).

---

## 6. ZDspUtils — DSP Utilities

**File:** `shared/dsp/ZDspUtils.h`  
**JUCE equivalents:** `juce::Decibels`, `juce::FloatVectorOperations`, `juce::jlimit`/`jmin`/`jmax`, `juce::roundToInt`, `juce::ScopedNoDenormals`, `juce::MathConstants`

### Namespace

All utilities live in `namespace zdsp`. This avoids collisions with JUCE names during the
transition period.

### API Reference

```cpp
namespace zdsp {
    // Gain ↔ Decibels conversion
    float gainToDecibels(float gain, float minusInfinityDb = -100.0f);
    float decibelsToGain(float decibels, float minusInfinityDb = -100.0f);

    // Clamping (both work — clamp follows std convention)
    template<typename T> T clamp(T value, T minVal, T maxVal);
    template<typename T> T jlimit(T lower, T upper, T value);  // identical to juce::jlimit

    // Min/Max
    template<typename T> T jmin(T a, T b);  // identical to juce::jmin
    template<typename T> T jmax(T a, T b);

    // Rounding
    int roundToInt(float x);  // uses std::lround

    // Vector operations
    void clearFloatVector(float* data, int numSamples);  // std::memset

    // Denormal prevention (RAII)
    class ScopedNoDenormals {
        // Sets SSE FTZ (bit 15) and DAZ (bit 6) flags
        // Restores previous MXCSR state on destruction
    };

    // Math constants (templated)
    template<typename T> struct MathConstants {
        static constexpr T pi, twoPi, halfPi;
    };

    // Suppress unused-variable warnings
    template<typename... Args> void ignoreUnused(Args&&...);
}
```

### `decibelsToGain` — Negative dB handling

```cpp
zdsp::decibelsToGain(-6.0f);   // → 0.501
zdsp::decibelsToGain(-100.0f); // → 0.0f (at/below minusInfinityDb → 0.0f)
zdsp::decibelsToGain(-200.0f); // → 0.0f
```

This matches JUCE behavior: `juce::Decibels::decibelsToGain(-100.0f)` returns 0.0f.

### `ScopedNoDenormals` — When to use

Place at the top of `processBlock()` or any audio callback that performs heavy math:

```cpp
void processBlock(float** channels, int numSamples) {
    zdsp::ScopedNoDenormals noDenormals;  // prevents CPU spikes from denormal floats
    // ... DSP processing ...
}
```

On x86/x64, denormal floats (values < ~1.18e-38) can cause 100× CPU slowdowns. This RAII
guard sets the SSE MXCSR register to flush them to zero for the scope of the block.

On ARM (Apple Silicon), denormals are less of an issue, but the guard compiles to a no-op safely.

### Migration Quick Reference

| JUCE | ZDspUtils |
|---|---|
| `juce::Decibels::gainToDecibels(g)` | `zdsp::gainToDecibels(g)` |
| `juce::Decibels::decibelsToGain(dB)` | `zdsp::decibelsToGain(dB)` |
| `juce::jlimit(lo, hi, v)` | `zdsp::jlimit(lo, hi, v)` |
| `juce::jmin(a, b)` | `zdsp::jmin(a, b)` |
| `juce::roundToInt(f)` | `zdsp::roundToInt(f)` |
| `juce::FloatVectorOperations::clear(d, n)` | `zdsp::clearFloatVector(d, n)` |
| `juce::ScopedNoDenormals` | `zdsp::ScopedNoDenormals` |
| `juce::MathConstants<float>::pi` | `zdsp::MathConstants<float>::pi` |
| `juce::ignoreUnused(x)` | `zdsp::ignoreUnused(x)` |

### Limitations

- `clearFloatVector` uses `std::memset` (byte-level zeroing). This is correct for IEEE 754
  float (0.0f is all zero bits) but won't work for non-IEEE float types.
- `roundToInt` uses `std::lround` which respects the current rounding mode. JUCE's
  `roundToInt` uses a manual trick. Both produce identical results for normal values.
- No `FloatVectorOperations::multiply`, `add`, `copy` — add as needed.

---

## Performance Characteristics

All modules are tuned for real-time audio:

| Module | Per-Sample Cost | Memory | Notes |
|---|---|---|---|
| ZBuffer::clear() | O(N) memset | 0 alloc | View mode also zeroes external data |
| ZFFT::performRealOnlyForward | O(N log N) | 0 alloc | Twiddles precomputed at construction |
| ZOversampling2x::processSamplesUp | O(N) × 4 biquads | 0 alloc | 8 multiplies + 8 adds per sample per channel |
| MultiChannelBiquad::processChannel | O(N) | 0 alloc | 5 multiplies + 4 adds per sample |
| ZLinearSmoother::getNextValue | O(1) | 0 alloc | 1 compare + 1 add per call |
| zdsp::ScopedNoDenormals | O(1) | 0 alloc | 2 SSE intrinsics |

**All modules are allocation-free during processing.** All heap allocations happen at
construction or `prepare()` time. This is critical for real-time audio thread safety.

---

## Integration Notes

### CMake

The `shared/CMakeLists.txt` already includes `${CMAKE_CURRENT_SOURCE_DIR}/dsp` in
`target_include_directories`. To use any module:

```cpp
#include "shared/dsp/ZFFT.h"
#include "shared/dsp/ZOversampling.h"
#include "shared/dsp/ZBiquadFilter.h"
#include "shared/dsp/ZSmoother.h"
#include "shared/dsp/ZDspUtils.h"
```

### Coexistence with JUCE

All modules can coexist with JUCE during migration. They use no JUCE headers and
namespace their symbols (e.g., `zdsp::jlimit` vs `juce::jlimit`). This allows
incremental replacement — convert one file at a time.

### Testing Strategy

Null tests should verify:
1. **ZFFT vs juce::dsp::FFT**: identical packed output (±1e-5 tolerance) for random input
2. **ZOversampling2x vs juce::dsp::Oversampling**: identical output samples for a sine sweep
3. **MultiChannelBiquad vs juce::dsp::IIR**: identical output for same RBJ coefficients
4. **ZLinearSmoother vs juce::SmoothedValue**: identical per-sample sequence
5. **ZDspUtils vs JUCE equivalents**: bit-exact for decibelsToGain, roundToInt, jlimit

See the `dsp-null-tests` task for implementation.
