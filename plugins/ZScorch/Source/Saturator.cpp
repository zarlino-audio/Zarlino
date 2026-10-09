#include "Saturator.h"

//==============================================================================
// ZScorch v2.0 saturation curves — voicing notes
// ---------------------------------------------------------------------------
// Every mode is built from stages of the smooth sigmoid
//     s(x) = x / sqrt(1 + x^2)          (C-infinity, |s| < 1, s'(0) = 1)
// with an optional bias b that shifts the knee asymmetrically:
//     stage(x) = s(k*g*x + b) - s(b)     (still C-infinity, passes through 0)
// and analytic antiderivative
//     S(x) = sqrt(1 + x^2),   S' = s   ->   F_stage = S(k*g*x + b)/(k*g) - s(b)*x
//
// Why this family sounds "analog" rather than textbook:
//   * C-infinity and slope-continuous everywhere — no clipping corners, so no
//     buzz at the knee (the classic textbook hard clipper's giveaway).
//   * Unity slope at the origin after normalisation — quiet passages pass
//     untouched, no low-level fizz ("digititis").
//   * Asymmetry via bias rather than a separate negative branch — 2nd harmonic
//     appears smoothly, exactly like a single-ended tube stage.
//   * Two-stage voicings (Tape / Transistor) model magnetic/BJT behaviour: a
//     soft knee that hands over to a firmer one, so harmonics arrive
//     progressively instead of all at once.
//   * Wavefold uses a true sine fold but with drive-scaled phase and amplitude
//     compensation so low drive is a gentle lift rather than instant chaos.
//
// Anti-aliasing: first-order antiderivative anti-aliasing (ADAA-1) integrates
// the curve exactly across each sample interval and falls back to the midpoint
// value when the input barely moves.  This removes the harsh fold-back
// "crackle" of the old design without the CPU/latency cost of 4x oversampling.
//==============================================================================

namespace
{
    constexpr float kMinDrive   = 0.0f;
    constexpr float kMaxDrive   = 2.0f;
    constexpr float kAdaaEps    = 1.0e-4f;
    constexpr float kInputClamp = 64.0f;

    inline float stageValue (float x, float u, float b) noexcept
    {
        // s(u*x + b) - s(b)
        return zscorch_curves::sigmoid (u * x + b) - zscorch_curves::sigmoid (b);
    }

    inline float stageAnti (float x, float u, float b) noexcept
    {
        // int stage dx = S(u*x + b)/u - s(b)*x
        return zscorch_curves::sigmoidS (u * x + b) / u
             - zscorch_curves::sigmoid (b) * x;
    }

    // Hostile-input guard: NaN/Inf become silence (NaN fails both comparisons),
    // hot-but-finite values are clamped into the curve's working range.
    inline float sanitise (float x) noexcept
    {
        if (! (x > -1.0e30f && x < 1.0e30f))
            return 0.0f;
        return juce::jlimit (-kInputClamp, kInputClamp, x);
    }
}

//==============================================================================
Saturator::CurveConsts Saturator::makeConsts (float drive, SaturationMode mode) noexcept
{
    CurveConsts c;
    c.mode = static_cast<int> (mode);

    const float d = juce::jlimit (kMinDrive, kMaxDrive, drive);
    const float dRoot = std::sqrt (d);

    // Progressive drive law: gentle at the low end, aggressive when pushed.
    const float g = 1.0f + 3.0f * d * dRoot;
    c.g = g;

    // Stage-2 characteristics per mode (weight + scale of the firmer knee).
    float a2w = 0.0f;        // raw stage-2 weight (relative to stage 1)
    float k2base = 1.0f;     // stage-2 scale once fully engaged

    switch (mode)
    {
        case SaturationMode::Tube:
            // Single asymmetric stage: 2nd-harmonic warmth, 3rd as it saturates.
            c.b1 = 0.30f;
            break;

        case SaturationMode::Tape:
            // Two-stage magnetic knee: soft onset hands over to a firmer knee.
            c.b1 = 0.10f;  c.b2 = 0.10f;  a2w = 0.32f;  k2base = 4.0f;
            break;

        case SaturationMode::Transistor:
            // Firmer knee and slightly more 2nd stage weight: cut-through edge.
            c.b1 = 0.05f;  c.b2 = 0.05f;  a2w = 0.45f;  k2base = 4.5f;
            break;

        case SaturationMode::Germanium:
            // Deep bias (strong 2nd harmonic) + warm upper stage.
            c.b1 = 0.50f;  c.b2 = 0.18f;  a2w = 0.35f;  k2base = 2.2f;
            break;

        case SaturationMode::Diode:
            // Weak-asymmetric conduction knee: buzzy, musical, mid-forward.
            c.b1 = 0.62f;  c.b2 = 0.30f;  a2w = 0.20f;  k2base = 3.0f;
            break;

        case SaturationMode::Wavefold:
            // Drive-scaled phase; amplitude compensated so the fold stays musical.
            c.omega = 1.0f + 5.5f * d * dRoot;
            c.foldA = 1.0f / (1.0f + 0.55f * (c.omega - 1.0f));
            {
                const float slope = c.foldA * c.omega;
                c.norm = slope > 1.0e-6f ? (1.0f + 0.9f * d) / slope : 1.0f;
            }
            return c;
    }

    // Two-stage weighting: the weights sum to one while the stages coincide
    // (k2 == 1), so a gentle drive is a single smooth stage and the second knee
    // only appears as the drive rises — the "progressive" character.
    c.a2 = a2w / (1.0f + a2w);
    c.a1 = 1.0f - c.a2;
    c.k2 = 1.0f + (k2base - 1.0f) * juce::jmin (1.0f, 2.0f * d);

    // Level compensation: tiny-signal gain is exactly unity at drive 0 and rises
    // only mildly with drive (a <= +6 dB upward compression, the classic analog
    // feel) while hot signals compress into the knee instead of collapsing.
    const float slopeRaw = g * (c.a1 * zscorch_curves::sigmoidDeriv (c.b1)
                                + c.a2 * c.k2 * zscorch_curves::sigmoidDeriv (c.b2));
    const float boost = juce::jmin (std::pow (g, 0.4f), 2.0f);
    c.norm = slopeRaw > 1.0e-6f ? boost / slopeRaw : 1.0f;

    return c;
}

//==============================================================================
float Saturator::curveF (const CurveConsts& c, float x) noexcept
{
    x = sanitise (x);

    switch (static_cast<SaturationMode> (c.mode))
    {
        case SaturationMode::Tube:
            return stageValue (x, c.g, c.b1);

        case SaturationMode::Wavefold:
            return c.foldA * std::sin (c.omega * x);

        default:
            return c.a1 * stageValue (x, c.g, c.b1)
                 + c.a2 * stageValue (x, c.k2 * c.g, c.b2);
    }
}

float Saturator::curveF1 (const CurveConsts& c, float x) noexcept
{
    x = sanitise (x);

    switch (static_cast<SaturationMode> (c.mode))
    {
        case SaturationMode::Tube:
            return stageAnti (x, c.g, c.b1);

        case SaturationMode::Wavefold:
            return -(c.foldA / c.omega) * std::cos (c.omega * x);

        default:
            return c.a1 * stageAnti (x, c.g, c.b1)
                 + c.a2 * stageAnti (x, c.k2 * c.g, c.b2);
    }
}

float Saturator::curveDirect (const CurveConsts& c, float x) noexcept
{
    // Plain curve evaluation (used by the HQ 4x path, where the oversampling
    // filters already suppress aliasing and the exact curve shape is wanted).
    return c.norm * curveF (c, x);
}

//==============================================================================
float Saturator::evaluateCurve (SaturationMode mode, float drive, float x) noexcept
{
    const auto c = makeConsts (drive, mode);
    return c.norm * curveF (c, x);
}

//==============================================================================
void Saturator::prepare (double sampleRate, int blockSize, int channels)
{
    numChannels = channels;
    maxBlockSize = blockSize;
    currentSampleRate = sampleRate;

    // HQ oversampler — allocated once here (never on the audio thread), used
    // only when the user enables the HQ toggle.
    if (blockSize > 0 && channels > 0)
    {
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            static_cast<size_t> (channels), 4,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
            true, true);
        oversampling->initProcessing (static_cast<size_t> (blockSize));
    }
    else
    {
        oversampling.reset();
    }

    // Harmonic-reduction shelf — prepared for the HQ (4x) rate; coefficients are
    // recomputed per operating rate in updateHarmReduction().
    lpFilter.prepare ({ sampleRate * 4.0, static_cast<juce::uint32> (juce::jmax (1, blockSize * 4)),
                        static_cast<juce::uint32> (juce::jmax (1, channels)) });

    reset();
}

void Saturator::reset()
{
    if (oversampling)
        oversampling->reset();
    lpFilter.reset();
    for (auto& p : prevIn)
        p = 0.0f;
    m_cachedHarmReduction = -1.0f;
    m_cachedSampleRate = 0.0;
}

int Saturator::getLatencySamples() const noexcept
{
    if (force4xOS && oversampling)
        return static_cast<int> (oversampling->getLatencyInSamples());
    return 0;
}

//==============================================================================
void Saturator::updateHarmReduction (float harmReduction)
{
    const float rateMul = force4xOS ? 4.0f : 1.0f;
    const float rate = static_cast<float> (currentSampleRate) * rateMul;

    if (harmReduction == m_cachedHarmReduction && rate == m_cachedSampleRate)
        return;

    m_cachedHarmReduction = harmReduction;
    m_cachedSampleRate = rate;

    // 0 = open, airy top end (only ~1 dB of smoothing); 1 = dark, tape-like veil.
    // Deliberately gentle: the shelf shapes the top octave only, so presence and
    // air survive even at maximum harmonic reduction.
    const float reductionDb = -(1.0f + juce::jlimit (0.0f, 1.0f, harmReduction) * 9.0f);

    *lpFilter.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        static_cast<double> (rate), 10000.0f, 0.707f,
        juce::Decibels::decibelsToGain (reductionDb));
}

//==============================================================================
void Saturator::process (juce::AudioBuffer<float>& buffer,
                         float drive, SaturationMode mode, float harmReduction)
{
    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (buffer.getNumChannels(), kMaxChannels);

    if (n <= 0 || chans <= 0)
        return;

    // Sanitise hostile input (NaN/Inf) so it can never poison the ADAA state.
    for (int ch = 0; ch < chans; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
            if (! (data[i] > -1.0e30f && data[i] < 1.0e30f))
                data[i] = 0.0f;
    }

    // Zero drive is a bit-exact bypass; keep the ADAA history fresh so that
    // re-engaging drive does not produce a discontinuity.
    if (drive <= kAdaaEps)
    {
        for (int ch = 0; ch < chans; ++ch)
            prevIn[ch] = buffer.getSample (ch, n - 1);
        return;
    }

    const auto c = makeConsts (drive, mode);
    updateHarmReduction (harmReduction);

    if (force4xOS && oversampling != nullptr)
    {
        // ---- HQ path: 4x oversampling + exact curve ----
        // Sanitise first: the oversampling filters must never see NaN/Inf.
        for (int ch = 0; ch < chans; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                if (! (d[i] > -1.0e30f && d[i] < 1.0e30f))
                    d[i] = 0.0f;
        }

        juce::dsp::AudioBlock<float> block (buffer);
        auto osBlock = oversampling->processSamplesUp (block);

        const int osN = static_cast<int> (osBlock.getNumSamples());
        const int osCh = juce::jmin (static_cast<int> (osBlock.getNumChannels()), chans);

        for (int ch = 0; ch < osCh; ++ch)
        {
            auto* data = osBlock.getChannelPointer (static_cast<size_t> (ch));
            for (int i = 0; i < osN; ++i)
                data[i] = curveDirect (c, data[i]);
        }

        juce::dsp::ProcessContextReplacing<float> ctx (osBlock);
        lpFilter.process (ctx);
        oversampling->processSamplesDown (block);

        // Keep the ADAA history in sync with the (post-oversampling) signal.
        for (int ch = 0; ch < chans; ++ch)
            prevIn[ch] = buffer.getSample (ch, n - 1);

        return;
    }

    // ---- Default path: ADAA-1 at base rate (no latency, low CPU) ----
    for (int ch = 0; ch < chans; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        float prev = prevIn[ch];

        for (int i = 0; i < n; ++i)
        {
            float x = data[i];
            if (! (x > -1.0e30f && x < 1.0e30f))   // NaN/Inf guard (cheap: 2 compares)
                x = 0.0f;

            const float delta = x - prev;

            float y;
            if (std::abs (delta) > kAdaaEps)
                y = (curveF1 (c, x) - curveF1 (c, prev)) / delta;
            else
                y = curveF (c, 0.5f * (x + prev));   // midpoint fallback

            data[i] = c.norm * y;
            prev = x;
        }

        prevIn[ch] = prev;
    }

    juce::dsp::AudioBlock<float> shapedBlock (buffer);
    juce::dsp::ProcessContextReplacing<float> shapedContext (shapedBlock);
    lpFilter.process (shapedContext);
}
