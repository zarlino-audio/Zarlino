#pragma once

#include <vector>
#include <cmath>
#include <cassert>
#include <algorithm>

// ZDynamics: Header-only dynamics processing for Zarlino plugins.
// No JUCE dependency — all math is self-contained.
// Provides: compressor, limiter, gate/expander, and envelope followers.
//
// All classes are designed for per-sample processing from the audio thread.
// No heap allocations after construction.

// ============================================================================
// Envelope Follower — RMS and Peak detection with attack/release smoothing
// ============================================================================

class ZEnvelopeFollower
{
public:
    ZEnvelopeFollower() = default;

    /** Configure detector parameters. Call in prepareToPlay. */
    void prepare(double sampleRate)
    {
        m_sampleRate = static_cast<float>(sampleRate);
        m_rmsWindowSamples = static_cast<int>(m_rmsWindowMs * 0.001f * m_sampleRate);
        if (m_rmsWindowSamples < 1) m_rmsWindowSamples = 1;
        m_rmsBuffer.assign(m_rmsWindowSamples, 0.0f);
        m_rmsWritePos = 0;
        m_rmsSum = 0.0f;
        m_rmsSampleCount = 0;
        m_rmsRecalibInterval = static_cast<int>(m_sampleRate * 10.0f);  // ~10s
        m_envState = 0.0f;
    }

    /** Set attack time in milliseconds. */
    void setAttack(float ms)
    {
        m_attackCoeff = timeConstantToCoeff(ms, m_sampleRate);
    }

    /** Set release time in milliseconds. */
    void setRelease(float ms)
    {
        m_releaseCoeff = timeConstantToCoeff(ms, m_sampleRate);
    }

    /** Set RMS window in milliseconds (typical: 20–200 ms). */
    void setRmsWindow(float ms)
    {
        m_rmsWindowMs = ms;
        if (m_sampleRate > 0.0f)
        {
            m_rmsWindowSamples = static_cast<int>(ms * 0.001f * m_sampleRate);
            if (m_rmsWindowSamples < 1) m_rmsWindowSamples = 1;
            m_rmsBuffer.assign(m_rmsWindowSamples, 0.0f);
            m_rmsWritePos = 0;
            m_rmsSum = 0.0f;
            m_rmsSampleCount = 0;
        }
    }

    /** Set detection mode: true = peak, false = RMS. */
    void setPeakMode(bool usePeak) { m_peakMode = usePeak; }

    /** Reset envelope memory. */
    void reset()
    {
        std::fill(m_rmsBuffer.begin(), m_rmsBuffer.end(), 0.0f);
        m_rmsSum = 0.0f;
        m_rmsWritePos = 0;
        m_rmsSampleCount = 0;
        m_envState = 0.0f;
    }

    /** Process a single sample. Returns the smoothed envelope level in linear amplitude. */
    float process(float input)
    {
        const float level = m_peakMode ? std::abs(input) : processRms(std::abs(input));

        // Attack / release smoothing (branchless analog-style RC)
        const float coeff = (level > m_envState) ? m_attackCoeff : m_releaseCoeff;
        m_envState = level + coeff * (m_envState - level);

        // Flush to zero when envelope decays to subnormal range.
        // Without this, prolonged silence causes m_envState to enter the
        // subnormal float range (< ~1.2e-38), triggering microcode-assisted
        // CPU stalls (10–100× slowdown) on x86/x64.  JUCE's ScopedNoDenormals
        // mitigates this at the CPU register level, but defense-in-depth in
        // shared header-only code is worth the branch.
        if (m_envState < 1e-30f && m_envState > -1e-30f)
            m_envState = 0.0f;

        return m_envState;
    }

    /** Get current smoothed envelope. */
    float getEnvelope() const { return m_envState; }

private:
    float processRms(float absSample)
    {
        const float sq = absSample * absSample;
        m_rmsSum -= m_rmsBuffer[m_rmsWritePos];
        m_rmsSum += sq;
        m_rmsBuffer[m_rmsWritePos] = sq;
        m_rmsWritePos = (m_rmsWritePos + 1) % m_rmsWindowSamples;

        // Periodically recalibrate the running sum to prevent floating-point
        // drift from accumulating over millions of samples.
        ++m_rmsSampleCount;
        if (m_rmsSampleCount >= m_rmsRecalibInterval)
        {
            m_rmsSum = 0.0f;
            for (int i = 0; i < m_rmsWindowSamples; ++i)
                m_rmsSum += m_rmsBuffer[i];
            m_rmsSampleCount = 0;
        }

        return std::sqrt(m_rmsSum / static_cast<float>(m_rmsWindowSamples));
    }

    static float timeConstantToCoeff(float ms, float sampleRate)
    {
        if (ms <= 0.0f) return 0.0f; // instantaneous
        const float tc = ms * 0.001f * sampleRate;
        return std::exp(-1.0f / tc);
    }

    float m_sampleRate = 44100.0f;
    float m_attackCoeff = 0.0f;
    float m_releaseCoeff = 0.0f;
    float m_rmsWindowMs = 50.0f;
    bool m_peakMode = false;

    int m_rmsWindowSamples = 2205;
    std::vector<float> m_rmsBuffer;
    int m_rmsWritePos = 0;
    float m_rmsSum = 0.0f;
    int m_rmsSampleCount = 0;
    int m_rmsRecalibInterval = 441000;  // recalibrate every ~10s @ 44.1kHz
    float m_envState = 0.0f;
};

// ============================================================================
// Compressor — Feedforward RMS/Peak compressor with soft knee
// ============================================================================

class ZCompressor
{
public:
    ZCompressor() = default;

    /** Configure for a sample rate. Call in prepareToPlay. */
    void prepare(double sampleRate)
    {
        m_envelope.prepare(sampleRate);
        m_sampleRate = static_cast<float>(sampleRate);
    }

    /** Set threshold in dB (typical: -60 to 0). */
    void setThreshold(float db) { m_thresholdLin = dbToLin(db); }

    /** Set ratio (typical: 1 to 20, where 1 = no compression). */
    void setRatio(float ratio)
    {
        m_ratio = ratio;
        m_slope = 1.0f - (1.0f / ratio);
    }

    /** Set attack time in milliseconds. */
    void setAttack(float ms) { m_envelope.setAttack(ms); }

    /** Set release time in milliseconds. */
    void setRelease(float ms) { m_envelope.setRelease(ms); }

    /** Set knee width in dB (0 = hard knee, typical: 0–12). */
    void setKnee(float db) { m_kneeDb = db; }

    /** Set makeup gain in dB. */
    void setMakeupGain(float db) { m_makeupLin = dbToLin(db); }

    /** Set RMS window in milliseconds. */
    void setRmsWindow(float ms) { m_envelope.setRmsWindow(ms); }

    /** Set detection mode: true = peak, false = RMS. */
    void setPeakMode(bool usePeak) { m_envelope.setPeakMode(usePeak); }

    /** Reset internal state. */
    void reset() { m_envelope.reset(); }

    /**
     * Process one sample.
     * @param input   Input sample (linear amplitude).
     * @param outGainReductionDb  Optional output: gain reduction in dB applied.
     * @return Compressed sample (linear amplitude, with makeup gain applied).
     */
    float processSample(float input, float* outGainReductionDb = nullptr)
    {
        const float level = m_envelope.process(input);

        // Compute gain reduction with soft knee
        const float levelDb = linToDb(level + 1e-30f);
        float gainReductionDb = 0.0f;

        if (m_kneeDb <= 0.0f)
        {
            // Hard knee
            if (levelDb > m_thresholdDb())
                gainReductionDb = (levelDb - m_thresholdDb()) * m_slope;
        }
        else
        {
            // Soft knee: quadratic transition region
            const float kneeHalf = m_kneeDb * 0.5f;
            const float kneeLow = m_thresholdDb() - kneeHalf;
            const float kneeHigh = m_thresholdDb() + kneeHalf;

            if (levelDb > kneeHigh)
                gainReductionDb = (levelDb - m_thresholdDb()) * m_slope;
            else if (levelDb > kneeLow)
            {
                const float excess = levelDb - kneeLow;
                gainReductionDb = (excess * excess) / (m_kneeDb * 2.0f) * m_slope;
            }
        }

        if (outGainReductionDb)
            *outGainReductionDb = gainReductionDb;

        const float gainLin = dbToLin(-gainReductionDb);
        return input * gainLin * m_makeupLin;
    }

private:
    float m_thresholdDb() const { return linToDb(m_thresholdLin); }

    static float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }
    static float linToDb(float lin) { return 20.0f * std::log10(lin); }

    ZEnvelopeFollower m_envelope;
    float m_sampleRate = 44100.0f;

    float m_thresholdLin = 1.0f;   // linear threshold (0 dB default)
    float m_ratio = 1.0f;
    float m_slope = 0.0f;
    float m_kneeDb = 0.0f;
    float m_makeupLin = 1.0f;
};

// ============================================================================
// Brick-wall Lookahead Limiter
// ============================================================================

class ZLimiter
{
public:
    ZLimiter() = default;

    /** Configure for a sample rate and lookahead time.
     *  @param sampleRate     Sample rate in Hz.
     *  @param lookaheadMs    Lookahead time in ms (typical: 1–5 ms).
     *  @param maxChannels    Maximum number of channels to process.
     */
    void prepare(double sampleRate, float lookaheadMs, int maxChannels)
    {
        m_sampleRate = static_cast<float>(sampleRate);
        m_lookaheadSamples = static_cast<int>(lookaheadMs * 0.001f * m_sampleRate);
        if (m_lookaheadSamples < 1) m_lookaheadSamples = 1;
        m_maxChannels = maxChannels;

        m_delayBuffer.assign(m_maxChannels, std::vector<float>(m_lookaheadSamples, 0.0f));
        m_writePos.assign(maxChannels, 0);
        m_holdSamples = 0;
        m_gainReduction = 1.0f;
        m_targetReduction = 1.0f;
    }

    /** Set ceiling in dB (typical: -1 to 0 dBFS). */
    void setCeiling(float db) { m_ceilingLin = dbToLin(db); }

    /** Set release time in milliseconds. */
    void setRelease(float ms)
    {
        m_releaseCoeff = std::exp(-1.0f / (ms * 0.001f * m_sampleRate));
    }

    /** Reset internal state. */
    void reset()
    {
        for (auto& buf : m_delayBuffer)
            std::fill(buf.begin(), buf.end(), 0.0f);
        std::fill(m_writePos.begin(), m_writePos.end(), 0);
        m_holdSamples = 0;
        m_gainReduction = 1.0f;
        m_targetReduction = 1.0f;
    }

    /** Process a single sample for one channel.
     *  Channels must be processed in lockstep (synchronized per-sample across channels).
     *  @param input         Input sample (linear amplitude).
     *  @param channelIndex  Channel index (0-based).
     *  @param outGainReductionDb  Optional: gain reduction in dB.
     *  @return Limited sample.
     */
    float processSample(float input, int channelIndex, float* outGainReductionDb = nullptr)
    {
        assert(channelIndex >= 0 && channelIndex < m_maxChannels);

        // Lookahead: determine required gain reduction for this sample's peak
        const float absInput = std::abs(input);
        if (absInput * m_gainReduction > m_ceilingLin)
        {
            const float neededGain = m_ceilingLin / (absInput + 1e-30f);
            m_targetReduction = std::min(m_targetReduction, neededGain);
            m_holdSamples = m_lookaheadSamples;
        }

        // Read delayed sample and apply gain reduction
        auto& buf = m_delayBuffer[channelIndex];
        const float delayed = buf[m_writePos[channelIndex]];

        // Write current input to delay line
        buf[m_writePos[channelIndex]] = input;
        m_writePos[channelIndex] = (m_writePos[channelIndex] + 1) % m_lookaheadSamples;

        // Smooth gain reduction — hold counter is per-frame, not per-channel
        if (m_holdSamples > 0)
        {
            if (channelIndex == 0)
                m_holdSamples--;
            m_gainReduction = m_targetReduction;
        }
        else
        {
            m_gainReduction += m_releaseCoeff * (1.0f - m_gainReduction);
            m_targetReduction = 1.0f;
        }

        const float gainReductionDb = linToDb(m_gainReduction);
        if (outGainReductionDb)
            *outGainReductionDb = -gainReductionDb;

        return delayed * m_gainReduction;
    }

    /** Returns the lookahead latency in samples. */
    int getLatencySamples() const { return m_lookaheadSamples; }

private:
    static float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }
    static float linToDb(float lin) { return 20.0f * std::log10(lin); }

    float m_sampleRate = 44100.0f;
    int m_lookaheadSamples = 22; // ~0.5 ms at 44.1 kHz
    int m_maxChannels = 2;

    std::vector<std::vector<float>> m_delayBuffer;
    std::vector<int> m_writePos;
    int m_holdSamples = 0;
    float m_gainReduction = 1.0f;
    float m_targetReduction = 1.0f;
    float m_releaseCoeff = 0.0f;
    float m_ceilingLin = dbToLin(-1.0f);
};

// ============================================================================
// Noise Gate — Downward expander with hysteresis
// ============================================================================

class ZGate
{
public:
    ZGate() = default;

    /** Configure for a sample rate. Call in prepareToPlay. */
    void prepare(double sampleRate)
    {
        m_envelope.prepare(sampleRate);
        m_sampleRate = static_cast<float>(sampleRate);
    }

    /** Set threshold in dB (typical: -80 to 0). */
    void setThreshold(float db) { m_thresholdLin = dbToLin(db); }

    /** Set range in dB (how much attenuation when gate is closed, typical: -80 to 0). */
    void setRange(float db) { m_rangeLin = dbToLin(db); }

    /** Set attack time in milliseconds (how fast gate opens). */
    void setAttack(float ms)
    {
        m_attackCoeff = std::exp(-1.0f / (ms * 0.001f * m_sampleRate));
    }

    /** Set release/hold time in milliseconds (how fast gate closes). */
    void setRelease(float ms)
    {
        m_releaseCoeff = std::exp(-1.0f / (ms * 0.001f * m_sampleRate));
    }

    /** Set hysteresis in dB (prevents chatter around threshold). */
    void setHysteresis(float db) { m_hysteresisDb = db; }

    /** Set RMS window in ms for detection. */
    void setRmsWindow(float ms) { m_envelope.setRmsWindow(ms); }

    /** Reset internal state. */
    void reset()
    {
        m_envelope.reset();
        m_gainState = 1.0f;
    }

    /** Process a single sample.
     *  @param input              Input sample.
     *  @param outGainReductionDb Optional: gain reduction applied in dB.
     *  @return Gated sample.
     */
    float processSample(float input, float* outGainReductionDb = nullptr)
    {
        const float levelDb = linToDb(m_envelope.process(input) + 1e-30f);

        // Hysteresis: open threshold is higher than close threshold
        const float openThreshold = m_thresholdDb();
        const float closeThreshold = m_thresholdDb() - m_hysteresisDb;

        const bool shouldClose = levelDb < closeThreshold;
        const bool shouldOpen = levelDb > openThreshold;

        // Hold state (simple: only switch on clear crossing)
        if (shouldOpen)
            m_gateOpen = true;
        else if (shouldClose)
            m_gateOpen = false;

        const float targetGain = m_gateOpen ? 1.0f : m_rangeLin;
        const float coeff = (targetGain > m_gainState) ? m_attackCoeff : m_releaseCoeff;
        m_gainState = targetGain + coeff * (m_gainState - targetGain);

        if (m_gainState < 1e-30f && m_gainState > -1e-30f)
            m_gainState = 0.0f;

        if (outGainReductionDb)
            *outGainReductionDb = linToDb(m_gainState);

        return input * m_gainState;
    }

private:
    float m_thresholdDb() const { return linToDb(m_thresholdLin); }
    static float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }
    static float linToDb(float lin) { return 20.0f * std::log10(lin); }

    ZEnvelopeFollower m_envelope;
    float m_sampleRate = 44100.0f;

    float m_thresholdLin = dbToLin(-40.0f);   // -40 dB default threshold
    float m_rangeLin = dbToLin(-80.0f);        // -80 dB default range
    float m_hysteresisDb = 3.0f;
    float m_attackCoeff = 0.0f;
    float m_releaseCoeff = 0.0f;
    float m_gainState = 1.0f;
    bool m_gateOpen = true;
};
