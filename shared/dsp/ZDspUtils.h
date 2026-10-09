#pragma once

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstring>

//==============================================================================
// Lightweight, header-only DSP utilities to replace a small subset of JUCE
// utilities in a JUCE-free way. Intended for inclusion from shared code.

namespace zdsp {

// Linear gain to decibels. minusInfinityDb is returned for gain <= 0.
inline float gainToDecibels(float gain, float minusInfinityDb = -100.0f) noexcept
{
    return (gain > 0.0f) ? std::max(minusInfinityDb, 20.0f * std::log10(gain)) : minusInfinityDb;
}

// Decibels to linear gain
inline float decibelsToGain(float decibels, float minusInfinityDb = -100.0f) noexcept
{
    return (decibels > minusInfinityDb) ? std::pow(10.0f, decibels * 0.05f) : 0.0f;
}

//==============================================================================
// Clamping utilities

template <typename T>
inline T clamp(T value, T minVal, T maxVal) noexcept
{
    return (value < minVal) ? minVal : (value > maxVal) ? maxVal : value;
}

// juce::jlimit equivalent
template <typename T>
inline T jlimit(T lower, T upper, T value) noexcept
{
    return (value < lower) ? lower : (value > upper) ? upper : value;
}

//==============================================================================
// Min / Max equivalents

template <typename T>
inline T jmin(T a, T b) noexcept { return (a < b) ? a : b; }

template <typename T>
inline T jmax(T a, T b) noexcept { return (a > b) ? a : b; }

//==============================================================================
// Rounding

inline int roundToInt(float x) noexcept { return static_cast<int>(std::lround(x)); }

//==============================================================================
// Float vector operations

inline void clearFloatVector(float* data, int numSamples) noexcept
{
    if (data == nullptr || numSamples <= 0)
        return;
    std::memset(data, 0, static_cast<std::size_t>(numSamples) * sizeof(float));
}

//==============================================================================
// Denormal prevention — sets SSE flush-to-zero and denormals-are-zero
// Equivalent to juce::ScopedNoDenormals (RAII)

#if defined(__SSE__) || defined(_MSC_VER)
    #include <xmmintrin.h>
#endif

class ScopedNoDenormals
{
public:
    ScopedNoDenormals() noexcept
    {
    #if defined(__SSE__) || defined(_MSC_VER)
        m_oldMXCSR = _mm_getcsr();
        unsigned int newMXCSR = m_oldMXCSR;
        newMXCSR |= 0x8000u; // FTZ
        newMXCSR |= 0x0040u; // DAZ
        _mm_setcsr(newMXCSR);
    #else
        m_oldMXCSR = 0u;
    #endif
    }

    ~ScopedNoDenormals() noexcept
    {
    #if defined(__SSE__) || defined(_MSC_VER)
        _mm_setcsr(m_oldMXCSR);
    #endif
    }

    ScopedNoDenormals(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;

private:
    unsigned int m_oldMXCSR = 0u;
};

//==============================================================================
// Math constants

template <typename T>
struct MathConstants {
    static constexpr T pi = static_cast<T>(3.14159265358979323846);
    static constexpr T twoPi = static_cast<T>(6.28318530717958647692);
    static constexpr T halfPi = static_cast<T>(1.57079632679489661923);
};

//==============================================================================
// ignoreUnused — suppresses "unused variable" warnings

template <typename... Args>
inline void ignoreUnused(Args&&...) noexcept {}

} // namespace zdsp
