#pragma once

#include <JuceHeader.h>

//==============================================================================
/** Six saturation modes, each with a distinct, musically voiced harmonic
    fingerprint.

    v2.0 curves are hand-voiced transfer functions rather than textbook
    waveshapers: every curve is C1-continuous (no kinks, no dead zones), unity
    slope at the origin (small signals pass untouched), and injects harmonics
    progressively as drive rises. They are paired with antiderivative
    anti-aliasing (ADAA) so the aliasing floor stays low without paying for
    oversampling filters.
*/
enum class SaturationMode
{
    Tube,        // Asymmetric soft knee — 2nd-harmonic warmth, 3rd at drive
    Tape,        // Two-stage magnetic knee — 3rd-rich, silky top end
    Transistor,  // Firm two-stage knee — edge and bite without fizz
    Germanium,   // Deep asymmetric bias — vintage 2nd-harmonic glow
    Diode,       // Weakly asymmetric conduction knee — buzzy but musical
    Wavefold     // Progressive sine fold — creative harmonics, ADAA smoothed
};

//==============================================================================
/**
    Saturator — multi-mode harmonic saturator.

    v2.0 signal path (default, low CPU):
        input → antiderivative anti-aliasing (ADAA-1) waveshaper
              → harmonic-reduction shelf

    Optional HQ path ("Oversample 4x" toggle):
        input → 4x oversampling → waveshaper (same curves, ADAA)
              → harmonic-reduction shelf → downsample

    Zero drive is a bit-exact bypass. The default path adds no latency; the HQ
    path reports the oversampler's latency via getLatencySamples().
*/
class Saturator
{
public:
    Saturator() = default;

    /** Create the optional HQ oversampler and filters. Call in prepareToPlay. */
    void prepare (double sampleRate, int blockSize, int numChannels);

    /** Release resources. */
    void reset();

    /** Returns the latency introduced by the optional HQ oversampling stage. */
    int getLatencySamples() const noexcept;

    /**
        Process a buffer of audio in-place.
        @param buffer         Audio buffer to process (all channels).
        @param drive          Drive amount (0.0–1.0 nominal, clamped to 2.0).
        @param mode           Saturation mode selection.
        @param harmReduction  Post-shaping high-frequency roll-off (0.0–1.0).
    */
    void process (juce::AudioBuffer<float>& buffer,
                  float drive, SaturationMode mode, float harmReduction);

    /**
        Enable the HQ (4x oversampled) signal path.  Default is off — the ADAA
        path is already low-aliasing and uses a fraction of the CPU.
    */
    void setForce4xOversampling (bool force) noexcept { force4xOS = force; }

    /** Current HQ state (UI/processor convenience). */
    bool isHQEnabled() const noexcept { return force4xOS; }

    /**
        Evaluate the voiced transfer curve directly (no ADAA, no filtering).
        Normalised to unity small-signal slope, exactly as the DSP uses it.
        Intended for transfer-curve displays and for DSP correctness tests.
    */
    static float evaluateCurve (SaturationMode mode, float drive, float x) noexcept;

private:
    //==================================================================
    // Musical curve set — see Saturator.cpp for the voicing notes.
    struct CurveConsts
    {
        int   mode   = 0;
        float g      = 1.0f;   // pre-gain (drive)
        float norm   = 1.0f;   // post gain: unity small-signal slope at drive 0
        float a1     = 1.0f;   // stage-1 weight
        float b1     = 0.0f;   // stage-1 bias
        float b2     = 0.0f;   // stage-2 bias
        float k2     = 1.0f;   // stage-2 scale
        float a2     = 0.0f;   // stage-2 weight
        float omega  = 1.0f;   // wavefold phase scale
        float foldA  = 1.0f;   // wavefold amplitude
    };

    static CurveConsts makeConsts (float drive, SaturationMode mode) noexcept;

    /** Curves f(x) and antiderivatives F(x), both in drive-scaled space. */
    static float curveF (const CurveConsts& c, float x) noexcept;
    static float curveF1 (const CurveConsts& c, float x) noexcept;

    /** Plain (non-ADAA) evaluation used for the HQ 4x path. */
    static float curveDirect (const CurveConsts& c, float x) noexcept;

    void updateHarmReduction (float harmReduction);

    // Optional HQ oversampling (allocated in prepare(), used only when enabled).
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    // Harmonic-reduction shelf.
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> lpFilter;

    // ADAA state (previous input per channel, drive-scaled handled inside).
    static constexpr int kMaxChannels = 8;
    float prevIn[kMaxChannels] = {};

    int numChannels = 0;
    int maxBlockSize = 0;
    double currentSampleRate = 44100.0;
    bool force4xOS = false;

    // Cached harm-reduction coefficients (avoid heap allocation on the audio thread).
    float m_cachedHarmReduction = -1.0f;
    float m_cachedSampleRate = 0.0f;
};

//==============================================================================
// Inline curve math (header-inline so tests can exercise it directly).
//   s(x)  = x / sqrt(1 + x^2)          : smooth saturating sigmoid, |s| < 1
//   S(x)  = sqrt(1 + x^2)              : S' = s,  used as the antiderivative
//
// A "stage" of a curve is  A * [ s(k*g*x + b) - s(b) ],
// whose antiderivative is  (A / (k*g)) * S(k*g*x + b) - s(b) * x.
//==============================================================================
namespace zscorch_curves
{
    inline float sigmoid (float x) noexcept
    {
        return x / std::sqrt (1.0f + x * x);
    }

    inline float sigmoidS (float x) noexcept
    {
        return std::sqrt (1.0f + x * x);
    }

    inline float sigmoidDeriv (float x) noexcept
    {
        const float d = 1.0f + x * x;
        return 1.0f / (d * std::sqrt (d));
    }
}
