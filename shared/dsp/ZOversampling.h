#pragma once

#include <vector>
#include <cmath>
#include <cassert>
#include <algorithm>

// ZOversampling2x: header-only, no JUCE dependency.
//
// Linear-phase FIR half-band polyphase 2x oversampler. Replaces the former
// Butterworth-biquad implementation with a filter that has:
//   * Exact integer group delay (K base-rate samples per direction), so
//     dry/wet delay compensation and host latency reporting are precise.
//   * Linear phase (no passband phase distortion, sonically transparent).
//   * Unity round-trip gain: up output = unity amplitude (even branch delay
//     with gain 1.0, odd branch taps scaled to sum 1.0); down filter is the
//     half-band lowpass (center 0.5, odd taps sum 0.5) with unity gain.
//   * No state recursion => no denormal concerns, real-time safe.
//
// Design: half-band prototype h with h[2k]=0 (k!=K), h[center]=0.5, odd taps
// (2K of them) from a Kaiser-windowed sinc, normalized so odd taps sum to 0.5.
// Round-trip transfer is exactly T(z) = E0(z)^2 + z^-1 E1(z)^2 = 0.5*z^-2K
// (flat -6.02dB before the up-path x2 staging; net 0 dB after).

class ZOversampling2x
{
public:
    // K = half-band length / 2 (taps count for each polyphase branch).
    // Larger K = steeper transition, more latency. 24 gives ~0.45*baseNyquist
    // passband with negligible aliasing; 8..32 are all usable.
    static constexpr int kDefaultHalfTaps = 24;
    static constexpr int kMaxHalfTaps = 32;

    ZOversampling2x() = default;

    // Prepare: allocate buffers and state. Sample rate is optional; if not
    // provided the filter is designed for 44100 Hz internally. Call reset on
    // sample-rate changes and call prepare again (or call designHalfBandFilter).
    void prepare(int numChannels, int maxBlockSize)
    {
        assert(numChannels > 0 && maxBlockSize > 0);
        assert(m_numTaps >= 2 && m_numTaps <= kMaxHalfTaps);

        m_numChannels = numChannels;
        m_maxBlockSize = maxBlockSize;
        const int osCap = maxBlockSize * 2;

        m_osBuffers.assign(m_numChannels, std::vector<float>(osCap, 0.0f));
        m_states.assign(m_numChannels, FilterState{});
        for (auto& st : m_states)
        {
            st.upHist.resize(2 * m_numTaps, 0.0f);
            st.evenRing.resize(m_numTaps + 1, 0.0f);
            st.oddRing.resize(2 * m_numTaps + 1, 0.0f);
        }
        m_osNumSamples = 0;

        // Default design at 44100 Hz base rate (upsampled rate = 88200).
        // Half-band taps are rate-normalized, so this is valid at any rate.
        designHalfBandFilter(44100.0);

        // One-direction group delay = K base-rate samples (round trip = 2K).
        m_latencySamples = m_numTaps;
    }

    void reset()
    {
        for (auto& st : m_states)
        {
            std::fill(st.upHist.begin(), st.upHist.end(), 0.0f);
            std::fill(st.evenRing.begin(), st.evenRing.end(), 0.0f);
            std::fill(st.oddRing.begin(), st.oddRing.end(), 0.0f);
            st.upW = 0;
            st.evenW = 0;
            st.oddW = 0;
        }
    }

    int getLatencySamples() const noexcept { return m_latencySamples; }

    // Upsample: inputChannels pointer array must point to per-channel arrays
    // of length numSamples. Output is written into internal OS buffers and the
    // returned value is the oversampled sample count (numSamples * 2).
    //
    // Polyphase structure (m = base sample index):
    //   os[2m]   = x[m - K]                    (even branch: pure delay, gain 1)
    //   os[2m+1] = sum_j (2 * E1[j]) * x[m - j] (odd branch: interpolating taps)
    // The even branch output keeps unity amplitude so the downstream FFT chain
    // sees the same full-scale signal it would see without oversampling.
    int processSamplesUp(const float* const* inputChannels, int numSamples)
    {
        assert(m_numChannels > 0);
        assert(numSamples > 0 && numSamples <= m_maxBlockSize);
        assert(!m_halfbandE1.empty());

        const int osLen = numSamples * 2;
        m_osNumSamples = osLen;
        const int ntaps = 2 * m_numTaps;

        for (int ch = 0; ch < m_numChannels; ++ch)
        {
            float* os = m_osBuffers[ch].data();
            const float* in = inputChannels[ch];
            auto& st = m_states[ch];

            for (int n = 0; n < numSamples; ++n)
            {
                // Push current input sample into the history ring (delay line).
                st.upW = (st.upW + 1) % ntaps;
                st.upHist[st.upW] = in[n];

                // Even branch: x[m-K] (K ring slots behind the newest sample).
                os[2 * n] = st.upHist[(st.upW + ntaps - m_numTaps) % ntaps];

                // Odd branch: interpolating FIR. m_halfbandE1 sums to 0.5;
                // scale by 2 so the up path has unity DC/interp gain.
                float acc = 0.0f;
                for (int j = 0; j < ntaps; ++j)
                {
                    const int idx = st.upW - j;
                    acc += 2.0f * m_halfbandE1[j] * st.upHist[idx < 0 ? idx + ntaps : idx];
                }
                os[2 * n + 1] = acc;
            }
        }

        return m_osNumSamples;
    }

    // Downsample: expects outputChannels to point to per-channel buffers of
    // length numSamples (base rate). The internal oversampled buffer (after
    // processing at 2x rate) is half-band filtered and decimated. Uses a
    // dedicated polyphase state so the down path is independent of the up path.
    //
    // y[m] = 0.5 * evenRing[K] + sum_j E1[j] * oddRing[1+j]
    // (the z^-1 offset between the two branches is what makes the round-trip
    // transfer exactly T(z) = E0^2 + z^-1 * E1^2).
    void processSamplesDown(float* const* outputChannels, int numSamples)
    {
        assert(m_numChannels > 0);
        assert(numSamples > 0 && numSamples <= m_maxBlockSize);
        assert(m_osNumSamples == numSamples * 2);
        assert(!m_halfbandE1.empty());

        const int ntaps = 2 * m_numTaps;

        for (int ch = 0; ch < m_numChannels; ++ch)
        {
            const float* os = m_osBuffers[ch].data();
            float* out = outputChannels[ch];
            auto& st = m_states[ch];

            for (int n = 0; n < numSamples; ++n)
            {
                const float ve = os[2 * n];
                const float vo = os[2 * n + 1];

                // Push into the two branch delay rings.
                st.evenW = (st.evenW + 1) % (m_numTaps + 1);
                st.evenRing[st.evenW] = ve;
                st.oddW = (st.oddW + 1) % (ntaps + 1);
                st.oddRing[st.oddW] = vo;

                // evenRing[K] = ve delayed by K base samples (gain 0.5).
                const float evenPart = 0.5f * st.evenRing[(st.evenW + m_numTaps + 1 - m_numTaps) % (m_numTaps + 1)];

                // odd branch reads the 2K samples *behind* the newest vo:
                // oddRing[1..2K] in delay-line order = offsets 1..2K.
                float acc = 0.0f;
                for (int j = 0; j < ntaps; ++j)
                {
                    const int idx = st.oddW - (j + 1);
                    acc += m_halfbandE1[j] * st.oddRing[idx < 0 ? idx + ntaps + 1 : idx];
                }
                out[n] = evenPart + acc;
            }
        }
    }

    // Access oversampled buffers for in-place processing
    float* getOSChannelPointer(int channel) noexcept
    {
        assert(channel >= 0 && channel < m_numChannels);
        return m_osBuffers[channel].data();
    }

    const float* getOSChannelPointer(int channel) const noexcept
    {
        assert(channel >= 0 && channel < m_numChannels);
        return m_osBuffers[channel].data();
    }

    int getOSNumSamples() const noexcept { return m_osNumSamples; }
    int getOSNumChannels() const noexcept { return m_numChannels; }

    // Redesign the half-band filter. Half-band taps are normalized to the
    // base rate (cutoff = 0.5 * base Nyquist), so baseSampleRate only has to be
    // positive; it is kept for interface compatibility.
    void designHalfBandFilter(double baseSampleRate)
    {
        (void) baseSampleRate;
        assert(m_numTaps >= 2 && m_numTaps <= kMaxHalfTaps);

        const int ntaps = 2 * m_numTaps;
        const double beta = 9.0;

        m_halfbandE1.resize(ntaps);

        // Odd taps of the half-band prototype: Kaiser-windowed sinc at the odd
        // tap offsets. Normalize so the odd taps sum to 0.5 (half-band
        // identity H(w)+H(w+pi) = 0.5, H(0) = 1).
        const double center = 2.0 * m_numTaps;
        const double i0Beta = besselI0(beta);
        double oddSum = 0.0;

        for (int j = 0; j < ntaps; ++j)
        {
            const double off = static_cast<double>(2 * j + 1) - center; // odd signed offset
            const double sinc = std::sin(0.5 * jucePi_ * off) / (0.5 * jucePi_ * off);
            const double u = std::abs(off) / center;
            const double w = (u >= 1.0) ? 0.0 : besselI0(beta * std::sqrt(1.0 - u * u)) / i0Beta;
            m_halfbandE1[j] = static_cast<float>(sinc * w);
            oddSum += m_halfbandE1[j];
        }

        // Scale so that sum(odd taps) = 0.5. The up path multiplies by 2.
        const double norm = (2.0 * oddSum);
        for (int j = 0; j < ntaps; ++j)
            m_halfbandE1[j] = static_cast<float>(static_cast<double>(m_halfbandE1[j]) / norm);

        m_latencySamples = m_numTaps;
    }

    // Set the half-band filter length. Must be called before prepare() so the
    // history rings are sized correctly. Default is kDefaultHalfTaps.
    void setHalfTaps(int halfTaps)
    {
        assert(halfTaps >= 2 && halfTaps <= kMaxHalfTaps);
        m_numTaps = halfTaps;
    }

    int getHalfTaps() const noexcept { return m_numTaps; }

private:
    static constexpr double jucePi_ = 3.14159265358979323846;

    // Modified Bessel function of the first kind, order 0. Converges rapidly
    // for the arguments used here (<= beta = 9.0). Header-only, no <cmath>
    // special-function dependency.
    static double besselI0(double x)
    {
        double sum = 1.0;
        double term = 1.0;
        const double x2 = x * x;
        for (int k = 1; k < 32; ++k)
        {
            term *= x2 / (4.0 * k * k);
            sum += term;
            if (term < 1e-18 * sum)
                break;
        }
        return sum;
    }

    struct FilterState
    {
        std::vector<float> upHist;    // up path history ring, size 2K
        std::vector<float> evenRing;  // down path even-branch delay, size K+1
        std::vector<float> oddRing;   // down path odd-branch delay, size 2K+1
        int upW = 0;
        int evenW = 0;
        int oddW = 0;
    };

    int m_numChannels = 0;
    int m_maxBlockSize = 0;
    int m_numTaps = kDefaultHalfTaps;      // K (half of the half-band length)
    std::vector<std::vector<float>> m_osBuffers; // per-channel oversampled buffers
    std::vector<FilterState> m_states;     // per-channel polyphase history
    std::vector<float> m_halfbandE1;       // odd taps, length 2K, sum = 0.5
    int m_osNumSamples = 0;
    int m_latencySamples = 0;
};
