#pragma once

#include <vector>
#include <complex>
#include <cmath>
#include <cassert>

class ZFFT {
public:
    explicit ZFFT(int order)
        : order_(order), size_(1 << order), twiddles_((size_ / 2))
    {
        assert(order >= 0);
        const float twoPi = 2.0f * std::acos(-1.0f);
        for (int k = 0; k < size_ / 2; ++k) {
            float angle = -twoPi * k / float(size_);
            twiddles_[k] = std::complex<float>(std::cos(angle), std::sin(angle));
        }
        buffer_.resize(size_);
    }

    int getSize() const { return size_; }

    void performFrequencyOnlyForwardTransform(float* data)
    {
        assert(data != nullptr);
        // Input: data[0..size_-1] real samples (time domain)
        // Output: data[0..size_-1] magnitude spectrum in packed form
        // Copy real input into complex buffer
        for (int i = 0; i < size_; ++i)
            buffer_[i] = std::complex<float>(data[i], 0.0f);

        fftInPlace(false);

        // Write magnitudes into data[0..size_-1]
        // We store magnitudes for bins 0..size_/2 at indexes 0..size_/2
        const int half = size_ / 2;
        data[0] = std::abs(buffer_[0]);
        for (int k = 1; k < half; ++k)
            data[k] = std::abs(buffer_[k]);
        data[half] = std::abs(buffer_[half]);
        // Zero the remaining slots for cleanliness
        for (int k = half + 1; k < size_; ++k)
            data[k] = 0.0f;
    }

    void performRealOnlyForwardTransform(float* data)
    {
        assert(data != nullptr);
        // Input: data[0..size_-1] real samples
        // Output: data[0..size_*2-1] packed complex (JUCE format)
        // Copy real input into complex buffer
        for (int i = 0; i < size_; ++i)
            buffer_[i] = std::complex<float>(data[i], 0.0f);

        fftInPlace(false);

        const int half = size_ / 2;
        // DC
        data[0] = buffer_[0].real();
        data[1] = buffer_[0].imag(); // should be ~0
        // bins 1..half-1
        for (int k = 1; k < half; ++k) {
            data[2 * k]     = buffer_[k].real();
            data[2 * k + 1] = buffer_[k].imag();
        }
        // Nyquist
        data[2 * half]     = buffer_[half].real();
        data[2 * half + 1] = buffer_[half].imag(); // should be ~0
        // The rest of the array (if any) is left untouched; caller expects size*2 floats
    }

    void performRealOnlyInverseTransform(float* data)
    {
        assert(data != nullptr);
        // Input: data[0..size_*2-1] packed complex (JUCE format)
        // Output: data[0..size_-1] real samples (time domain)
        const int half = size_ / 2;
        // Reconstruct complex spectrum
        for (int k = 0; k <= half; ++k) {
            float re = data[2 * k];
            float im = data[2 * k + 1];
            buffer_[k] = std::complex<float>(re, im);
        }
        // Use Hermitian symmetry to fill k=half+1..size_-1
        for (int k = half + 1; k < size_; ++k) {
            // X[k] = conj(X[N-k])
            buffer_[k] = std::conj(buffer_[size_ - k]);
        }

        fftInPlace(true);

        // After inverse, divide by N is already performed in fftInPlace
        for (int i = 0; i < size_; ++i)
            data[i] = buffer_[i].real();
    }

private:
    int order_;
    int size_;
    std::vector<std::complex<float>> twiddles_;
    std::vector<std::complex<float>> buffer_;

    void bitReversePermute()
    {
        unsigned int n = static_cast<unsigned int>(size_);
        unsigned int j = 0;
        for (unsigned int i = 1; i < n - 1; ++i) {
            unsigned int bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap(buffer_[i], buffer_[j]);
        }
    }

    void fftInPlace(bool inverse)
    {
        bitReversePermute();
        const unsigned int n = static_cast<unsigned int>(size_);
        for (unsigned int len = 2; len <= n; len <<= 1) {
            unsigned int half = len >> 1;
            unsigned int step = n / len;
            for (unsigned int i = 0; i < n; i += len) {
                for (unsigned int j = 0; j < half; ++j) {
                    std::complex<float> w = twiddles_[j * step];
                    if (inverse)
                        w = std::conj(w);
                    std::complex<float> u = buffer_[i + j];
                    std::complex<float> v = buffer_[i + j + half] * w;
                    buffer_[i + j] = u + v;
                    buffer_[i + j + half] = u - v;
                }
            }
        }
        if (inverse) {
            // scale
            for (unsigned int i = 0; i < n; ++i)
                buffer_[i] /= static_cast<float>(n);
        }
    }
};
