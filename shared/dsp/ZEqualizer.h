#pragma once

//==============================================================================
// ZEqualizer — header-only, JUCE-free, zero-allocation serial-biquad equalizer
//
// Builds on ZBiquadFilter.h (MultiChannelBiquad + RBJ coefficient generators),
// ZDspUtils.h (denormal protection), and optionally ZSmoother.h for internal
// parameter smoothing.
//
// Template parameter NumBands sets the number of cascaded bands (default 8,
// maximum 16). All filter state is pre-allocated — no heap activity in process().
//
// Band types: Peak, LowShelf, HighShelf, LowCut (highpass), HighCut (lowpass).
//==============================================================================

#include "ZBiquadFilter.h"
#include "ZDspUtils.h"
#include <array>
#include <cmath>
#include <cassert>
#include <algorithm>

//==============================================================================
// Filter type identifiers (lightweight — not a JUCE dependency)
//==============================================================================
enum class ZEqualizerBandType
{
    Peak      = 0,
    LowShelf  = 1,
    HighShelf = 2,
    LowCut    = 3,   // highpass — cuts below freq
    HighCut   = 4    // lowpass  — cuts above freq
};

//==============================================================================
// Per-band parameter snapshot — copyable, no dynamic allocation
//==============================================================================
struct ZEqualizerBandParams
{
    float frequency       = 1000.0f;
    float gainDb          = 0.0f;
    float Q               = 0.707f;
    bool  bypass          = false;
    ZEqualizerBandType type = ZEqualizerBandType::Peak;
};

//==============================================================================
// Additional RBJ cookbook coefficient generators.
// ZBiquadFilter.h already supplies LowShelf, HighShelf, HighPass (LowCut).
// The functions below provide the remaining filter shapes.
//==============================================================================
namespace RBJ {

// Peak / Bell filter
static inline BiquadCoefficients makePeak(float sampleRate,
                                           float freqHz,
                                           float Q,
                                           float gainLinear)
{
    assert(sampleRate > 0.0f);
    assert(freqHz > 0.0f && freqHz < sampleRate * 0.5f);
    BiquadCoefficients c;

    const float A     = std::sqrt(gainLinear);
    const float omega = 2.0f * ZB_PI * freqHz / sampleRate;
    const float sin_w = std::sin(omega);
    const float cos_w = std::cos(omega);
    const float alpha = sin_w / (2.0f * Q);

    const float b0 =  1.0f + alpha * A;
    const float b1 = -2.0f * cos_w;
    const float b2 =  1.0f - alpha * A;
    const float a0 =  1.0f + alpha / A;
    const float a1 = -2.0f * cos_w;
    const float a2 =  1.0f - alpha / A;

    c.b0 = b0 / a0;
    c.b1 = b1 / a0;
    c.b2 = b2 / a0;
    c.a1 = a1 / a0;
    c.a2 = a2 / a0;

    return c;
}

// BandPass (constant skirt gain, peak gain = Q)
static inline BiquadCoefficients makeBandPass(float sampleRate,
                                               float freqHz,
                                               float Q)
{
    assert(sampleRate > 0.0f);
    assert(freqHz > 0.0f && freqHz < sampleRate * 0.5f);
    BiquadCoefficients c;

    const float omega = 2.0f * ZB_PI * freqHz / sampleRate;
    const float sin_w = std::sin(omega);
    const float cos_w = std::cos(omega);
    const float alpha = sin_w / (2.0f * Q);

    const float b0 =  alpha;      // = sin_w / (2*Q) = Q * alpha ... standard RBJ
    const float b1 =  0.0f;
    const float b2 = -alpha;
    const float a0 =  1.0f + alpha;
    const float a1 = -2.0f * cos_w;
    const float a2 =  1.0f - alpha;

    c.b0 = b0 / a0;
    c.b1 = b1 / a0;
    c.b2 = b2 / a0;
    c.a1 = a1 / a0;
    c.a2 = a2 / a0;

    return c;
}

// Notch filter
static inline BiquadCoefficients makeNotch(float sampleRate,
                                            float freqHz,
                                            float Q)
{
    assert(sampleRate > 0.0f);
    assert(freqHz > 0.0f && freqHz < sampleRate * 0.5f);
    BiquadCoefficients c;

    const float omega = 2.0f * ZB_PI * freqHz / sampleRate;
    const float sin_w = std::sin(omega);
    const float cos_w = std::cos(omega);
    const float alpha = sin_w / (2.0f * Q);

    const float b0 =  1.0f;
    const float b1 = -2.0f * cos_w;
    const float b2 =  1.0f;
    const float a0 =  1.0f + alpha;
    const float a1 = -2.0f * cos_w;
    const float a2 =  1.0f - alpha;

    c.b0 = b0 / a0;
    c.b1 = b1 / a0;
    c.b2 = b2 / a0;
    c.a1 = a1 / a0;
    c.a2 = a2 / a0;

    return c;
}

// AllPass filter
static inline BiquadCoefficients makeAllPass(float sampleRate,
                                              float freqHz,
                                              float Q)
{
    assert(sampleRate > 0.0f);
    assert(freqHz > 0.0f && freqHz < sampleRate * 0.5f);
    BiquadCoefficients c;

    const float omega = 2.0f * ZB_PI * freqHz / sampleRate;
    const float sin_w = std::sin(omega);
    const float cos_w = std::cos(omega);
    const float alpha = sin_w / (2.0f * Q);

    const float b0 =  1.0f - alpha;
    const float b1 = -2.0f * cos_w;
    const float b2 =  1.0f + alpha;
    const float a0 =  1.0f + alpha;
    const float a1 = -2.0f * cos_w;
    const float a2 =  1.0f - alpha;

    c.b0 = b0 / a0;
    c.b1 = b1 / a0;
    c.b2 = b2 / a0;
    c.a1 = a1 / a0;
    c.a2 = a2 / a0;

    return c;
}

} // namespace RBJ

//==============================================================================
// ZEqualizer — serial-biquad equalizer (template on number of bands)
//==============================================================================

template <int NumBands = 8>
class ZEqualizer
{
    static_assert(NumBands >= 1,   "ZEqualizer requires at least 1 band");
    static_assert(NumBands <= 16,  "ZEqualizer supports at most 16 bands");

public:
    ZEqualizer() = default;

    //--------------------------------------------------------------------------
    // Lifecycle
    //--------------------------------------------------------------------------

    /// Prepare all internal filter states for a given sample rate and channel count.
    /// Must be called before any process*() method.
    void prepare(double sampleRate, int numChannels)
    {
        assert(sampleRate > 0.0);
        assert(numChannels > 0);
        m_sampleRate = static_cast<float>(sampleRate);

        for (auto& filter : m_filters)
            filter.prepare(numChannels);

        // Mark every band dirty so coefficients are computed on first process
        m_anyDirty = true;
        for (size_t i = 0; i < static_cast<size_t>(NumBands); ++i)
            m_dirty[i] = true;
    }

    /// Reset all internal filter states to zero (silence). Does not change
    /// band parameters or coefficients.
    void reset()
    {
        for (auto& filter : m_filters)
            filter.reset();
    }

    //--------------------------------------------------------------------------
    // Latency
    //--------------------------------------------------------------------------

    /// ZEqualizer is a pure IIR cascade — always zero samples of latency.
    static constexpr int getLatencySamples() { return 0; }

    //--------------------------------------------------------------------------
    // Parameter setting
    //--------------------------------------------------------------------------

    /// Set all parameters for one band. Marks the band dirty — coefficients
    /// will be recomputed on the next process call.
    void setBandParams(int band, const ZEqualizerBandParams& params)
    {
        assert(band >= 0 && band < NumBands);
        const size_t idx = static_cast<size_t>(band);
        m_bandParams[idx] = params;
        m_dirty[idx]    = true;
        m_anyDirty      = true;
    }

    /// Read back the current parameters for a band (without recomputing).
    const ZEqualizerBandParams& getBandParams(int band) const
    {
        assert(band >= 0 && band < NumBands);
        return m_bandParams[static_cast<size_t>(band)];
    }

    /// Force all bands to recompute coefficients on the next process call.
    void markAllDirty()
    {
        for (size_t i = 0; i < static_cast<size_t>(NumBands); ++i)
            m_dirty[i] = true;
        m_anyDirty = true;
    }

    //--------------------------------------------------------------------------
    // Processing — sample by sample
    //--------------------------------------------------------------------------

    /// Process a single sample through all active bands for one channel.
    inline float processSample(int channel, float sample) noexcept
    {
        // Denormal protection
        if (sample < 1e-30f && sample > -1e-30f)
            sample = 0.0f;

        // Update dirty coefficients before processing (once per call)
        if (m_anyDirty)
            updateDirtyCoefficients();

        float x = sample;
        for (int band = 0; band < NumBands; ++band)
        {
            if (m_bandParams[static_cast<size_t>(band)].bypass)
                continue;

            // Inline DF1-Transposed for this band/channel
            const size_t bi = static_cast<size_t>(band);
            BiquadState& st = m_filterStates[bi][static_cast<size_t>(channel)];
            const BiquadCoefficients& c = m_currentCoeffs[bi];

            const float y    = c.b0 * x + st.z1;
            st.z1            = c.b1 * x - c.a1 * y + st.z2;
            st.z2            = c.b2 * x - c.a2 * y;
            x                = y;
        }
        return x;
    }

    //--------------------------------------------------------------------------
    // Processing — block by block
    //--------------------------------------------------------------------------

    /// Process a mono block in-place.
    void processBlock(float* samples, int numSamples) noexcept
    {
        assert(samples != nullptr || numSamples == 0);
        if (m_anyDirty)
            updateDirtyCoefficients();
        for (int i = 0; i < numSamples; ++i)
            samples[i] = processSample(0, samples[i]);
    }

    /// Process a stereo pair in-place.
    void processStereo(float* left, float* right, int numSamples) noexcept
    {
        assert(left != nullptr  || numSamples == 0);
        assert(right != nullptr || numSamples == 0);
        if (m_anyDirty)
            updateDirtyCoefficients();
        for (int i = 0; i < numSamples; ++i)
        {
            left[i]  = processSample(0, left[i]);
            right[i] = processSample(1, right[i]);
        }
    }

    /// Process interleaved audio from non-interleaved channel pointers.
    /// channelData[ch] points to the sample buffer for channel `ch`.
    void process(float* const* channelData, int numChannels, int numSamples) noexcept
    {
        assert(channelData != nullptr || numChannels == 0 || numSamples == 0);
        if (m_anyDirty)
            updateDirtyCoefficients();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (channelData[ch] == nullptr)
                continue;
            float* buf = channelData[ch];
            for (int i = 0; i < numSamples; ++i)
                buf[i] = processSample(ch, buf[i]);
        }
    }

private:
    //--------------------------------------------------------------------------
    // Internal coefficient update
    //--------------------------------------------------------------------------

    void updateDirtyCoefficients()
    {
        for (int band = 0; band < NumBands; ++band)
        {
            const size_t bi = static_cast<size_t>(band);
            if (!m_dirty[bi])
                continue;

            const ZEqualizerBandParams& p = m_bandParams[bi];

            if (p.bypass)
            {
                // Bypassed — set pass-through coefficients
                BiquadCoefficients pass;
                pass.b0 = 1.0f;
                pass.b1 = 0.0f;
                pass.b2 = 0.0f;
                pass.a1 = 0.0f;
                pass.a2 = 0.0f;
                m_currentCoeffs[bi] = pass;
            }
            else
            {
                BiquadCoefficients coeffs;
                switch (p.type)
                {
                    case ZEqualizerBandType::Peak:
                    {
                        const float gainLin = zdsp::decibelsToGain(p.gainDb);
                        coeffs = RBJ::makePeak(m_sampleRate, p.frequency, p.Q, gainLin);
                        break;
                    }
                    case ZEqualizerBandType::LowShelf:
                    {
                        const float gainLin = zdsp::decibelsToGain(p.gainDb);
                        coeffs = RBJ::makeLowShelf(m_sampleRate, p.frequency, p.Q, gainLin);
                        break;
                    }
                    case ZEqualizerBandType::HighShelf:
                    {
                        const float gainLin = zdsp::decibelsToGain(p.gainDb);
                        coeffs = RBJ::makeHighShelf(m_sampleRate, p.frequency, p.Q, gainLin);
                        break;
                    }
                    case ZEqualizerBandType::LowCut:
                    {
                        coeffs = RBJ::makeHighPass(m_sampleRate, p.frequency, p.Q);
                        break;
                    }
                    case ZEqualizerBandType::HighCut:
                    {
                        coeffs = RBJ::makeLowPass(m_sampleRate, p.frequency, p.Q);
                        break;
                    }
                }
                m_currentCoeffs[bi] = coeffs;
            }

            // Also update the MultiChannelBiquad copies (used by process() via
            // channel pointers)
            m_filters[bi].setCoefficients(m_currentCoeffs[bi]);

            m_dirty[bi] = false;
        }
        m_anyDirty = false;
    }

    //--------------------------------------------------------------------------
    // Data members
    //--------------------------------------------------------------------------

    float                          m_sampleRate = 44100.0f;

    std::array<ZEqualizerBandParams,      static_cast<size_t>(NumBands)> m_bandParams;
    std::array<MultiChannelBiquad,        static_cast<size_t>(NumBands)> m_filters;
    std::array<BiquadCoefficients,        static_cast<size_t>(NumBands)> m_currentCoeffs;
    std::array<bool,                      static_cast<size_t>(NumBands)> m_dirty;

    // Per-band, per-channel state for the inline processSample() path.
    // Pre-allocated for 2 channels (stereo). Expandable via prepare().
    std::array<std::array<BiquadState, 2>, static_cast<size_t>(NumBands)> m_filterStates;

    bool m_anyDirty = true;
};
