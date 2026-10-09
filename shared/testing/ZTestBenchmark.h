#pragma once

#include <cstdio>
#include <vector>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <string>

// ZTestBenchmark: CPU profiling utility for DSP functions.
// Measures min/mean/max/p99 CPU time in microseconds over N iterations.
// Designed for audio-thread-appropriate measurement — does not allocate
// during measurement phase.

class ZTestBenchmark
{
public:
    /** Run a benchmark on a DSP function. The function receives
     *  an iteration index (for parametrized tests).
     *
     *  @param fn          Callable: void(int iteration).
     *  @param iterations  Number of times to call fn.
     *  @param warmupRuns  Number of warm-up calls (not measured).
     *  @return Aggregate timing in microseconds.
     */
    template <typename F>
    static Results measure(F&& fn, int iterations = 1000, int warmupRuns = 10)
    {
        // Warm-up (cache, branch predictor)
        for (int i = 0; i < warmupRuns; ++i)
            fn(i);

        std::vector<double> times;
        times.reserve(iterations);

        using Clock = std::chrono::high_resolution_clock;
        for (int i = 0; i < iterations; ++i)
        {
            auto t0 = Clock::now();
            fn(i);
            auto t1 = Clock::now();
            double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
            times.push_back(us);
        }

        return computeResults(times);
    }

    /** Measure multiple calls in a block (for processBlock-style measurement).
     *  This is useful when the test needs to measure the aggregate cost of
     *  processing an entire audio block.
     */
    template <typename F>
    static Results measureBlock(F&& fn, int iterations = 1000)
    {
        return measure(fn, iterations, 10);
    }

    struct Results
    {
        double minUs = 0.0;
        double meanUs = 0.0;
        double maxUs = 0.0;
        double p99Us = 0.0;
        double stdDevUs = 0.0;
        int totalIterations = 0;

        /** Check if this benchmark passes a budget (in microseconds). */
        bool passesBudget(double budgetUs) const
        {
            return maxUs <= budgetUs && meanUs <= budgetUs * 0.8;
        }

        /** Format as human-readable string. */
        std::string toString() const
        {
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "min=%.2f us  mean=%.2f us  max=%.2f us  p99=%.2f us  std=%.2f us  n=%d",
                     minUs, meanUs, maxUs, p99Us, stdDevUs, totalIterations);
            return std::string(buf);
        }
    };

private:
    static Results computeResults(std::vector<double>& times)
    {
        Results r;
        r.totalIterations = static_cast<int>(times.size());
        if (times.empty()) return r;

        std::sort(times.begin(), times.end());

        r.minUs = times.front();
        r.maxUs = times.back();

        double sum = 0.0;
        for (double t : times) sum += t;
        r.meanUs = sum / static_cast<double>(times.size());

        // P99
        size_t p99Idx = static_cast<size_t>(times.size() * 0.99);
        if (p99Idx >= times.size()) p99Idx = times.size() - 1;
        r.p99Us = times[p99Idx];

        // Standard deviation
        double sqSum = 0.0;
        for (double t : times)
        {
            double d = t - r.meanUs;
            sqSum += d * d;
        }
        r.stdDevUs = std::sqrt(sqSum / static_cast<double>(times.size()));

        return r;
    }
};

// ============================================================================
// CPU Budget Assertion Helpers
// ============================================================================

// ZScorch budget: ≤0.8% of 2.33ms buffer (512 samples @ 44.1 kHz) ≈ 18.6 us
// Typical budget for a single plug-in: ≤2% for all DSP = 46 us
namespace ZBenchmarkBudgets
{
    // Budgets in microseconds per 512-sample block @ 44.1 kHz
    // Block duration: 512 / 44100 = 11.61 ms. 1% CPU = 116.1 us.
    constexpr double kLightProcessor  = 250.0;   // ~2.15% CPU — simple EQ, gate
    constexpr double kMediumProcessor = 500.0;   // ~4.3% CPU — compressor, saturator
    constexpr double kHeavyProcessor  = 1000.0;  // ~8.6% CPU — FFT-based, reverb
    constexpr double kCritical        = 93.0;    // ~0.8% CPU — ZScorch per-band budget

    // Convert CPU percentage to budget in microseconds
    constexpr double percentToUs(double pct)
    {
        return pct * 116.1; // 1% CPU = 116.1 us for 512-sample block @ 44.1 kHz
    }
}
