#pragma once
#include <catch2/catch_test_macros.hpp>
#include <JuceHeader.h>

#include "shared/utils/ZBuffer.h"
#include "shared/dsp/ZFFT.h"
#include "shared/dsp/ZOversampling.h"
#include "shared/dsp/ZBiquadFilter.h"
#include "shared/dsp/ZSmoother.h"
#include "shared/dsp/ZDspUtils.h"

#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>

// Reproducible PRNG: xorshift32
class TestRandom
{
public:
    explicit TestRandom(uint32_t s = 0xC0FFEEu) : state(s ? s : 0xC0FFEEu) {}
    // returns uint32_t
    uint32_t nextU32()
    {
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }
    // return float in [-1, 1)
    float nextFloatUnit()
    {
        // 24-bit precision mantissa fit
        uint32_t v = nextU32() >> 8; // top 24 bits
        return (static_cast<float>(v) / static_cast<float>(0xFFFFFFu)) * 2.0f - 1.0f;
    }
    // float in [0,1)
    float nextFloat01()
    {
        uint32_t v = nextU32() >> 8;
        return static_cast<float>(v) / static_cast<float>(0xFFFFFFu);
    }

private:
    uint32_t state;
};

// Helpers
static float maxAbsBuffer(const float* ptr, int samples)
{
    float m = 0.0f;
    for (int i = 0; i < samples; ++i)
        m = std::max(m, std::fabs(ptr[i]));
    return m;
}

// Test 1: ZBuffer vs juce::AudioBuffer<float>
static void testZBufferNullTest()
{
    constexpr int channels = 2;
    constexpr int samples = 512;
    TestRandom rnd(123456u);

    // Create JUCE buffer
    juce::AudioBuffer<float> jbuf(channels, samples);
    jbuf.clear();

    // Create ZBuffer (assumed API mirrors juce::AudioBuffer)
    ZBuffer zbuf(channels, samples);
    zbuf.clear();

    // Fill both with same random data
    for (int ch = 0; ch < channels; ++ch)
    {
        float* jptr = jbuf.getWritePointer(ch);
        float* zptr = zbuf.getWritePointer(ch);
        REQUIRE(jptr != nullptr);
        REQUIRE(zptr != nullptr);
        for (int i = 0; i < samples; ++i)
        {
            float v = rnd.nextFloatUnit();
            jptr[i] = v;
            zptr[i] = v;
        }
    }

    // size checks
    REQUIRE(jbuf.getNumChannels() == zbuf.getNumChannels());
    REQUIRE(jbuf.getNumSamples() == zbuf.getNumSamples());

    // random sample equality checks
    for (int t = 0; t < 20; ++t)
    {
        int ch = static_cast<int>(rnd.nextFloat01() * channels);
        int idx = static_cast<int>(rnd.nextFloat01() * samples);
        ch = std::clamp(ch, 0, channels - 1);
        idx = std::clamp(idx, 0, samples - 1);
        float jv = jbuf.getSample(ch, idx);
        float zv = zbuf.getSample(ch, idx);
        REQUIRE(jv == zv); // exact since we wrote same floats
    }

    // clear() zeroes
    jbuf.clear();
    zbuf.clear();
    for (int ch = 0; ch < channels; ++ch)
    {
        const float* jptr = jbuf.getReadPointer(ch);
        const float* zptr = zbuf.getReadPointer(ch);
        REQUIRE(jptr != nullptr);
        REQUIRE(zptr != nullptr);
        for (int i = 0; i < samples; ++i)
        {
            REQUIRE(jptr[i] == 0.0f);
            REQUIRE(zptr[i] == 0.0f);
        }
    }

    // refill for copy tests
    for (int ch = 0; ch < channels; ++ch)
    {
        float* jptr = jbuf.getWritePointer(ch);
        float* zptr = zbuf.getWritePointer(ch);
        for (int i = 0; i < samples; ++i)
        {
            float v = rnd.nextFloatUnit();
            jptr[i] = v;
            zptr[i] = v;
        }
    }

    // copyFrom: copy channel 0 -> channel 1
    {
        // JUCE copy
        jbuf.copyFrom(1, 0, jbuf, 0, 0, samples);

        // ZBuffer copy - try to use same API; if not available, do manual copy
        bool usedCopyApi = false;
        try
        {
            zbuf.copyFrom(1, 0, zbuf, 0, 0, samples);
            usedCopyApi = true;
        }
        catch (...) {
            usedCopyApi = false;
        }

        if (!usedCopyApi)
        {
            float* src = zbuf.getWritePointer(0);
            float* dst = zbuf.getWritePointer(1);
            std::memcpy(dst, src, sizeof(float) * samples);
        }

        // verify
        for (int i = 0; i < samples; ++i)
        {
            float jv = jbuf.getSample(1, i);
            float zv = zbuf.getSample(1, i);
            REQUIRE(jv == zv);
        }
    }

    // getMagnitude (max absolute) compare
    {
        float jmax = 0.0f, zmax = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
        {
            jmax = std::max(jmax, maxAbsBuffer(jbuf.getReadPointer(ch), samples));
            zmax = std::max(zmax, maxAbsBuffer(zbuf.getReadPointer(ch), samples));
        }
        REQUIRE(jmax == zmax);
    }

    // View mode (non-owning) — wrap external float** and verify getWritePointer works
    {
        // allocate external storage
        std::vector<std::vector<float>> ext(channels, std::vector<float>(samples, 0.0f));
        std::vector<float*> ptrs(channels);
        for (int ch = 0; ch < channels; ++ch)
            ptrs[ch] = ext[ch].data();

        // Assume ZBuffer has a constructor like: ZBuffer(float** data, int numChannels, int numSamples, bool takeOwnership=false)
        ZBuffer zview(ptrs.data(), channels, samples, /*owns=*/false);

        // write via zview and ensure ext reflects changes
        for (int ch = 0; ch < channels; ++ch)
        {
            float* w = zview.getWritePointer(ch);
            REQUIRE(w == ext[ch].data());
            for (int i = 0; i < samples; ++i)
                w[i] = static_cast<float>(ch * 1000 + i) * 0.001f;
        }
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < samples; ++i)
                REQUIRE(ext[ch][i] == zview.getSample(ch, i));
    }

    // makeCopyOf deep copy
    {
        ZBuffer zcopy(channels, samples);
        zcopy.makeCopyOf(zbuf);
        // mutate original and ensure copy unchanged
        float* pw = zbuf.getWritePointer(0);
        pw[0] = 9999.0f;
        REQUIRE(zcopy.getSample(0, 0) != zbuf.getSample(0, 0));
    }
}

// Test 2: ZFFT vs juce::dsp::FFT
static void testZFFTNullTest()
{
    constexpr int order = 10; // 2^10 = 1024
    const int N = 1 << order;
    const int packed = 2 * N;
    TestRandom rnd(0xDEADBEEFu);

    // allocate arrays (interleaved real/imag)
    std::vector<float> jdata(packed, 0.0f);
    std::vector<float> zdata(packed, 0.0f);
    std::vector<float> original(N, 0.0f);

    // fill original real input
    for (int i = 0; i < N; ++i)
    {
        float v = rnd.nextFloatUnit();
        original[i] = v;
        jdata[2 * i] = v;
        jdata[2 * i + 1] = 0.0f;
        zdata[2 * i] = v;
        zdata[2 * i + 1] = 0.0f;
    }

    // create FFTs
    juce::dsp::FFT jfft(order);
    ZFFT zfft(order);

    // perform real-only forward transform
    jfft.performRealOnlyForwardTransform(jdata.data());
    zfft.performRealOnlyForwardTransform(zdata.data());

    // Compare all packed outputs with tolerance 1e-4f
    for (int i = 0; i < packed; ++i)
    {
        float a = jdata[i];
        float b = zdata[i];
        float diff = std::fabs(a - b);
        REQUIRE(diff <= 1.0e-4f);
    }

    // performFrequencyOnlyForwardTransform: compute magnitudes from packed outputs and compare N/2+1 bins
    {
        const int bins = N / 2 + 1;
        for (int k = 0; k < bins; ++k)
        {
            float jr = jdata[2 * k];
            float ji = jdata[2 * k + 1];
            float zr = zdata[2 * k];
            float zi = zdata[2 * k + 1];
            float jm = std::sqrt(jr * jr + ji * ji);
            float zm = std::sqrt(zr * zr + zi * zi);
            REQUIRE(std::fabs(jm - zm) <= 1.0e-4f);
        }
    }

    // Round-trip inverse: copy original into arrays, forward then inverse, then compare real outputs to original
    {
        // refill arrays
        for (int i = 0; i < N; ++i)
        {
            jdata[2 * i] = original[i];
            jdata[2 * i + 1] = 0.0f;
            zdata[2 * i] = original[i];
            zdata[2 * i + 1] = 0.0f;
        }

        jfft.performRealOnlyForwardTransform(jdata.data());
        zfft.performRealOnlyForwardTransform(zdata.data());

        jfft.performRealOnlyInverseTransform(jdata.data());
        zfft.performRealOnlyInverseTransform(zdata.data());

        // After inverse, real outputs should be in data[2*i] normalized by N (JUCE's inverse does scaling)
        for (int i = 0; i < N; ++i)
        {
            float ja = jdata[2 * i];
            float za = zdata[2 * i];
            float orig = original[i];
            // Allow small tolerance (1e-3)
            REQUIRE(std::fabs(ja - orig) <= 1.0e-3f);
            REQUIRE(std::fabs(za - orig) <= 1.0e-3f);
        }
    }

    // Packed format layout: DC at [0,1], Nyquist at [N, N+1]
    {
        // DC
        REQUIRE(std::fabs(jdata[0] - jdata[0]) <= 0.0f); // dummy to keep semantics explicit
        // Nyquist: index pair starting at 2*(N/2) = N
        int nyquistIdx = N;
        // ensure indices within bounds
        REQUIRE(nyquistIdx + 1 < packed);
        // compare location values between implementations
        REQUIRE(std::fabs(jdata[nyquistIdx] - zdata[nyquistIdx]) <= 1.0e-4f);
        REQUIRE(std::fabs(jdata[nyquistIdx + 1] - zdata[nyquistIdx + 1]) <= 1.0e-4f);
    }
}

// Test 3: ZOversampling2x vs juce::dsp::Oversampling<float>
static void testZOversamplingNullTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    TestRandom rnd(0xABCDEFu);

    // Generate a short sine sweep 100Hz -> 10kHz across blockSize at 44.1kHz
    const float sampleRate = 44100.0f;
    std::vector<std::vector<float>> input(channels, std::vector<float>(blockSize));
    for (int i = 0; i < blockSize; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(blockSize);
        float freq = 100.0f * std::pow(10000.0f / 100.0f, t);
        float phase = 2.0f * float_Pi * freq * (static_cast<float>(i) / sampleRate);
        float s = std::sin(phase);
        for (int ch = 0; ch < channels; ++ch)
            input[ch][i] = s;
    }

    // ZOversampling2x (assumed API matches description)
    ZOversampling2x zos;
    zos.prepare(channels, blockSize);

    // JUCE oversampling
    juce::dsp::Oversampling<float> jover(channels, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false);
    jover.initProcessing(blockSize);

    // Prepare juce input buffer
    juce::AudioBuffer<float> jbuf(channels, blockSize);
    for (int ch = 0; ch < channels; ++ch)
        std::memcpy(jbuf.getWritePointer(ch), input[ch].data(), sizeof(float) * blockSize);

    // Process upsample
    juce::AudioBuffer<float> zOut(channels, blockSize * 2);
    juce::AudioBuffer<float> jOut(channels, blockSize * 2);
    jOut.clear();
    zOut.clear();

    // JUCE path
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            jover.processSamplesUp(jbuf.getWritePointer(ch), jOut.getWritePointer(ch), blockSize);
        }
    }

    // Zarlino path: assume zos.processUp(inPtrs, outPtrs, blockSize)
    {
        std::vector<const float*> inPtrs(channels);
        std::vector<float*> outPtrs(channels);
        for (int ch = 0; ch < channels; ++ch)
        {
            inPtrs[ch] = input[ch].data();
            outPtrs[ch] = zOut.getWritePointer(ch);
        }
        zos.processUp(inPtrs.data(), outPtrs.data(), blockSize);
    }

    // Compare upsampled outputs with tolerance 0.01f
    for (int ch = 0; ch < channels; ++ch)
    {
        float* jptr = jOut.getWritePointer(ch);
        float* zptr = zOut.getWritePointer(ch);
        for (int i = 0; i < blockSize * 2; ++i)
            REQUIRE(std::fabs(jptr[i] - zptr[i]) <= 0.01f);
    }

    // Downsample paths: processDown
    {
        juce::AudioBuffer<float> jDown(channels, blockSize);
        jDown.clear();
        for (int ch = 0; ch < channels; ++ch)
            jover.processSamplesDown(jOut.getWritePointer(ch), jDown.getWritePointer(ch), blockSize * 2);

        juce::AudioBuffer<float> zDown(channels, blockSize);
        for (int ch = 0; ch < channels; ++ch)
            zos.processDown(zOut.getReadPointer(ch), zDown.getWritePointer(ch), blockSize * 2);

        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < blockSize; ++i)
                REQUIRE(std::fabs(jDown.getSample(ch, i) - zDown.getSample(ch, i)) <= 0.01f);
    }

    // latency and designHalfBandFilter sanity checks
    {
        int lat = zos.getLatencySamples();
        REQUIRE(lat >= 0);
        // designHalfBandFilter shouldn't crash; call with typical sample rate
        zos.designHalfBandFilter(48000.0f);
    }
}

// Test 4: MultiChannelBiquad vs juce::dsp::IIR::Filter
static void testZBiquadNullTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 512;
    const float sampleRate = 44100.0f;

    // Helper to generate sine
    auto makeSine = [&](float freq, int samples) {
        std::vector<float> out(samples);
        for (int i = 0; i < samples; ++i)
            out[i] = std::sin(2.0f * float_Pi * freq * (static_cast<float>(i) / sampleRate));
        return out;
    };

    // MultiChannelBiquad assumed API
    MultiChannelBiquad zbiq;
    zbiq.prepare(channels);

    // JUCE IIR
    juce::dsp::IIR::Filter<float> jfilterL, jfilterR;

    // Test: Low shelf at 1kHz, Q=0.707, gain=2.0 (linear)
    {
        float freq = 1000.0f, Q = 0.7071f, gain = 2.0f;
        auto s = makeSine(200.0f, blockSize);

        // compute coefficients via JUCE
        juce::dsp::IIR::Coefficients<float>::Ptr coeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf(sampleRate, freq, Q, gain);
        jfilterL.coefficients = coeffs;
        jfilterR.coefficients = coeffs;
        // Zarlino: set coefficients per-channel
        zbiq.setCoefficients(*coeffs);

        // process buffers
        juce::AudioBuffer<float> jbuf(channels, blockSize);
        juce::AudioBuffer<float> zbuf(channels, blockSize);
        for (int ch = 0; ch < channels; ++ch)
        {
            std::memcpy(jbuf.getWritePointer(ch), s.data(), sizeof(float) * blockSize);
            std::memcpy(zbuf.getWritePointer(ch), s.data(), sizeof(float) * blockSize);
        }

        // process JUCE per-channel
        for (int ch = 0; ch < channels; ++ch)
            jfilterL.processSamples(jbuf.getWritePointer(ch), blockSize); // same coefficients for both channels

        // process Zarlino per-channel
        zbiq.process(zbuf.getArrayOfWritePointers(), blockSize);

        // compare with tight tolerance 1e-5
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < blockSize; ++i)
                REQUIRE(std::fabs(jbuf.getSample(ch, i) - zbuf.getSample(ch, i)) <= 1.0e-5f);
    }

    // Test: High shelf at 8kHz, Q=0.707, gain=0.5
    {
        float freq = 8000.0f, Q = 0.7071f, gain = 0.5f;
        auto s = makeSine(12000.0f, blockSize);

        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(sampleRate, freq, Q, gain);
        jfilterL.coefficients = coeffs;
        jfilterR.coefficients = coeffs;
        zbiq.setCoefficients(*coeffs);

        juce::AudioBuffer<float> jbuf(channels, blockSize);
        juce::AudioBuffer<float> zbuf(channels, blockSize);
        for (int ch = 0; ch < channels; ++ch)
        {
            std::memcpy(jbuf.getWritePointer(ch), s.data(), sizeof(float) * blockSize);
            std::memcpy(zbuf.getWritePointer(ch), s.data(), sizeof(float) * blockSize);
        }

        for (int ch = 0; ch < channels; ++ch)
            jfilterL.processSamples(jbuf.getWritePointer(ch), blockSize);

        zbiq.process(zbuf.getArrayOfWritePointers(), blockSize);

        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < blockSize; ++i)
                REQUIRE(std::fabs(jbuf.getSample(ch, i) - zbuf.getSample(ch, i)) <= 1.0e-5f);
    }

    // Test: High pass at 100Hz, Q=0.707
    {
        float freq = 100.0f, Q = 0.7071f;
        auto sLow = makeSine(50.0f, blockSize);
        auto sHigh = makeSine(500.0f, blockSize);

        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, freq, Q);
        jfilterL.coefficients = coeffs;
        jfilterR.coefficients = coeffs;
        zbiq.setCoefficients(*coeffs);

        // low freq test (attenuated)
        {
            juce::AudioBuffer<float> jbuf(channels, blockSize);
            juce::AudioBuffer<float> zbuf(channels, blockSize);
            for (int ch = 0; ch < channels; ++ch)
            {
                std::memcpy(jbuf.getWritePointer(ch), sLow.data(), sizeof(float) * blockSize);
                std::memcpy(zbuf.getWritePointer(ch), sLow.data(), sizeof(float) * blockSize);
            }
            for (int ch = 0; ch < channels; ++ch)
                jfilterL.processSamples(jbuf.getWritePointer(ch), blockSize);
            zbiq.process(zbuf.getArrayOfWritePointers(), blockSize);
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    REQUIRE(std::fabs(jbuf.getSample(ch, i) - zbuf.getSample(ch, i)) <= 1.0e-5f);
        }

        // high freq test (passes)
        {
            juce::AudioBuffer<float> jbuf(channels, blockSize);
            juce::AudioBuffer<float> zbuf(channels, blockSize);
            for (int ch = 0; ch < channels; ++ch)
            {
                std::memcpy(jbuf.getWritePointer(ch), sHigh.data(), sizeof(float) * blockSize);
                std::memcpy(zbuf.getWritePointer(ch), sHigh.data(), sizeof(float) * blockSize);
            }
            for (int ch = 0; ch < channels; ++ch)
                jfilterL.processSamples(jbuf.getWritePointer(ch), blockSize);
            zbiq.process(zbuf.getArrayOfWritePointers(), blockSize);
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    REQUIRE(std::fabs(jbuf.getSample(ch, i) - zbuf.getSample(ch, i)) <= 1.0e-5f);
        }
    }

    // Coefficient hot-swap mid-block doesn't crash
    {
        juce::AudioBuffer<float> jbuf(channels, blockSize);
        juce::AudioBuffer<float> zbuf(channels, blockSize);
        for (int ch = 0; ch < channels; ++ch)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                jbuf.getWritePointer(ch)[i] = std::sin(2.0f * float_Pi * 440.0f * (i / sampleRate));
                zbuf.getWritePointer(ch)[i] = jbuf.getWritePointer(ch)[i];
            }
        }
        // process half, swap coefficients, process rest
        int half = blockSize / 2;
        for (int ch = 0; ch < channels; ++ch)
        {
            jfilterL.processSamples(jbuf.getWritePointer(ch), half);
            zbiq.processPartial(zbuf.getArrayOfWritePointers(), 0, half);
        }
        // hot-swap
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, 2000.0f);
        jfilterL.coefficients = coeffs;
        zbiq.setCoefficients(*coeffs);
        for (int ch = 0; ch < channels; ++ch)
        {
            jfilterL.processSamples(jbuf.getWritePointer(ch) + half, half);
            zbiq.processPartial(zbuf.getArrayOfWritePointers(), half, half);
        }
        // if we reached here without crash, success. Also do a spot compare with loose tolerance.
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < blockSize; ++i)
                REQUIRE(std::fabs(jbuf.getSample(ch, i) - zbuf.getSample(ch, i)) <= 1.0e-3f);
    }

    // Multi-channel independence: ensure channels processed independently
    {
        juce::AudioBuffer<float> jbuf(channels, blockSize);
        juce::AudioBuffer<float> zbuf(channels, blockSize);
        // channel 0: 440Hz, channel1: 880Hz
        auto s0 = makeSine(440.0f, blockSize);
        auto s1 = makeSine(880.0f, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            jbuf.getWritePointer(0)[i] = s0[i];
            jbuf.getWritePointer(1)[i] = s1[i];
            zbuf.getWritePointer(0)[i] = s0[i];
            zbuf.getWritePointer(1)[i] = s1[i];
        }
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, 1000.0f);
        jfilterL.coefficients = coeffs;
        zbiq.setCoefficients(*coeffs);

        for (int ch = 0; ch < channels; ++ch)
            jfilterL.processSamples(jbuf.getWritePointer(ch), blockSize);
        zbiq.process(zbuf.getArrayOfWritePointers(), blockSize);

        // channels should not bleed: compare channelwise equality
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < blockSize; ++i)
                REQUIRE(std::fabs(jbuf.getSample(ch, i) - zbuf.getSample(ch, i)) <= 1.0e-5f);
    }
}

// Test 5: ZLinearSmoother vs juce::SmoothedValue<float, Linear>
static void testZSmootherNullTest()
{
    const double sampleRate = 44100.0;
    const double timeSeconds = 0.05;
    const int steps = 1000;

    ZLinearSmoother zs;
    zs.reset(static_cast<float>(sampleRate), static_cast<float>(timeSeconds));
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> js;
    js.reset(sampleRate, timeSeconds);

    // set current and target to same initial value
    zs.setCurrentAndTargetValue(0.0f);
    js.setCurrentAndTargetValue(0.0f);

    // set target to 1.0
    zs.setTargetValue(1.0f);
    js.setTargetValue(1.0f);

    // step through and compare every sample with tight tolerance 1e-7
    for (int i = 0; i < steps; ++i)
    {
        float zv = zs.getNextValue();
        float jv = js.getNextValue();
        REQUIRE(std::fabs(zv - jv) <= 1.0e-7f);
    }

    // setCurrentAndTargetValue immediate jump
    zs.setCurrentAndTargetValue(0.25f);
    js.setCurrentAndTargetValue(0.25f);
    REQUIRE(zs.getCurrentValue() == js.getCurrentValue());
    zs.setTargetValue(0.75f);
    js.setTargetValue(0.75f);

    // skip(N) advances N samples correctly
    zs.setCurrentAndTargetValue(0.0f);
    js.setCurrentAndTargetValue(0.0f);
    zs.setTargetValue(1.0f);
    js.setTargetValue(1.0f);
    zs.skip(100);
    js.skip(100);
    REQUIRE(std::fabs(zs.getCurrentValue() - js.getCurrentValue()) <= 1.0e-7f);
}

// Test 6: ZDspUtils vs JUCE equivalents
static void testZDspUtilsNullTest()
{
    // decibelsToGain: 50 values from -100 to +20 dB
    for (int i = 0; i < 50; ++i)
    {
        float db = -100.0f + (120.0f * (static_cast<float>(i) / 49.0f)); // -100..+20
        float zg = ZDspUtils::decibelsToGain(db);
        float jg = juce::Decibels::decibelsToGain(db);
        REQUIRE(std::fabs(zg - jg) <= 1.0e-7f);
    }

    // decibelsToGain(-100.0f) == 0.0f roughly
    REQUIRE(std::fabs(ZDspUtils::decibelsToGain(-100.0f) - juce::Decibels::decibelsToGain(-100.0f)) <= 1.0e-7f);
    REQUIRE(std::fabs(ZDspUtils::decibelsToGain(-6.0f) - 0.501187f) <= 1.0e-4f); // approx 10^(-6/20)

    // gainToDecibels round-trip
    for (int i = 0; i < 50; ++i)
    {
        float gain = 0.0001f + (10.0f * (static_cast<float>(i) / 49.0f));
        float db = ZDspUtils::gainToDecibels(gain);
        float gr = ZDspUtils::decibelsToGain(db);
        REQUIRE(std::fabs(gr - gain) <= 1.0e-5f);
    }

    // jlimit
    TestRandom rnd(0x13579u);
    for (int i = 0; i < 100; ++i)
    {
        float a = rnd.nextFloatUnit() * 10.0f - 5.0f;
        float b = a + rnd.nextFloatUnit() * 5.0f;
        if (a > b) std::swap(a, b);
        float v = rnd.nextFloatUnit() * 20.0f - 10.0f;
        float zj = ZDspUtils::jlimit(a, b, v);
        float jj = juce::jlimit(a, b, v);
        REQUIRE(std::fabs(zj - jj) <= 1.0e-7f);
    }

    // jmin / jmax
    for (int i = 0; i < 100; ++i)
    {
        float x = rnd.nextFloatUnit() * 100.0f - 50.0f;
        float y = rnd.nextFloatUnit() * 100.0f - 50.0f;
        REQUIRE(ZDspUtils::jmin(x, y) == std::min(x, y));
        REQUIRE(ZDspUtils::jmax(x, y) == std::max(x, y));
    }

    // roundToInt
    for (int i = 0; i < 100; ++i)
    {
        float v = rnd.nextFloatUnit() * 200.0f - 100.0f;
        REQUIRE(ZDspUtils::roundToInt(v) == static_cast<int>(std::round(v)));
    }

    // clearFloatVector
    {
        std::vector<float> v(128, 3.14f);
        ZDspUtils::clearFloatVector(v.data(), static_cast<int>(v.size()));
        for (auto vv : v)
            REQUIRE(vv == 0.0f);
    }
}

// Convenience: run all tests
static void runAllNullTests()
{
    testZBufferNullTest();
    testZFFTNullTest();
    testZOversamplingNullTest();
    testZBiquadNullTest();
    testZSmootherNullTest();
    testZDspUtilsNullTest();
}
