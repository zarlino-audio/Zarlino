#pragma once

#include <vector>
#include <cstddef>
#include <cassert>
#include <algorithm>
#include <cstring>

// ZBuffer: lightweight header-only replacement for juce::AudioBuffer<float> for Phase 1 Step 1
// Backed by std::vector<std::vector<float>> for owned storage, and supports a non-owning view
// over external float** channel pointers.

class ZBuffer
{
public:
    // Empty
    ZBuffer() noexcept
        : m_numChannels(0), m_numSamples(0), m_ownsData(true), m_externalData(nullptr)
    {
    }

    // Owned allocation
    ZBuffer(int numChannels, int numSamples)
        : ZBuffer()
    {
        setSize(numChannels, numSamples, false, false, false);
    }

    // Construct as a view over external float** data (non-owning) or copy-in if ownsData==true
    // externalData may be nullptr when numChannels==0
    ZBuffer(float** externalData, int numChannels, int numSamples, bool ownsData = false)
        : m_numChannels(0), m_numSamples(0), m_ownsData(true), m_externalData(nullptr)
    {
        if (numChannels <= 0 || numSamples < 0)
            return;

        if (ownsData)
        {
            setSize(numChannels, numSamples, false, false, false);
            if (externalData)
            {
                for (int ch = 0; ch < m_numChannels; ++ch)
                {
                    const float* src = externalData[ch];
                    if (src)
                        std::memcpy(m_data[ch].data(), src, static_cast<std::size_t>(m_numSamples) * sizeof(float));
                    else
                        std::memset(m_data[ch].data(), 0, static_cast<std::size_t>(m_numSamples) * sizeof(float));
                }
            }
        }
        else
        {
            // Non-owning view
            m_numChannels = numChannels;
            m_numSamples = numSamples;
            m_ownsData = false;
            m_externalData = externalData;
        }
    }

    ~ZBuffer() = default;

    // No copy
    ZBuffer(const ZBuffer&) = delete;
    ZBuffer& operator=(const ZBuffer&) = delete;

    // Move allowed
    ZBuffer(ZBuffer&&) = default;
    ZBuffer& operator=(ZBuffer&&) = default;

    // Size
    int getNumSamples() const noexcept { return m_numSamples; }
    int getNumChannels() const noexcept { return m_numChannels; }

    // Resizes the buffer. Semantics inspired by juce::AudioBuffer::setSize
    // keepExisting: keep old data where possible
    // clearExtraSpace: zero any newly allocated/unused space
    // avoidReallocation: try to reuse existing allocations when possible
    void setSize(int numChannels, int numSamples, bool keepExisting = false, bool clearExtraSpace = false, bool avoidReallocation = false)
    {
        assert(numChannels >= 0 && numSamples >= 0);

        if (!m_ownsData)
        {
            // Switching from a non-owning view to owned storage
            // If caller wants to keepExisting we copy from external pointers
            std::vector<std::vector<float>> newData;
            newData.resize(numChannels);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                newData[ch].assign(static_cast<std::size_t>(numSamples), 0.0f);
                if (keepExisting && m_externalData && ch < m_numChannels && m_externalData[ch])
                {
                    const int copySamples = std::min(m_numSamples, numSamples);
                    std::memcpy(newData[ch].data(), m_externalData[ch], static_cast<std::size_t>(copySamples) * sizeof(float));
                }
            }

            m_data.swap(newData);
            m_ownsData = true;
            m_externalData = nullptr;
            m_numChannels = numChannels;
            m_numSamples = numSamples;
            return;
        }

        // We own data below
        if (avoidReallocation)
        {
            // Try to reuse capacity: if current channels >= requested and each channel capacity >= requested samples
            bool canReuse = (static_cast<int>(m_data.size()) >= numChannels);
            if (canReuse)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    if (static_cast<int>(m_data[ch].capacity()) < numSamples)
                    {
                        canReuse = false;
                        break;
                    }
                }
            }

            if (canReuse)
            {
                // adjust sizes but keep capacity
                m_data.resize(numChannels);
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    if (static_cast<int>(m_data[ch].size()) < numSamples)
                    {
                        // expand size and optionally clear extra
                        const int oldSize = static_cast<int>(m_data[ch].size());
                        m_data[ch].resize(numSamples);
                        if (clearExtraSpace && numSamples > oldSize)
                            std::fill(m_data[ch].begin() + oldSize, m_data[ch].end(), 0.0f);
                    }
                    else
                    {
                        // shrink size
                        if (!keepExisting)
                            std::fill(m_data[ch].begin(), m_data[ch].begin() + numSamples, 0.0f);
                        m_data[ch].resize(numSamples);
                    }
                }

                m_numChannels = numChannels;
                m_numSamples = numSamples;
                return;
            }
        }

        // General case: allocate new storage
        std::vector<std::vector<float>> newData;
        newData.resize(numChannels);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            newData[ch].assign(static_cast<std::size_t>(numSamples), 0.0f);
            if (keepExisting && ch < m_numChannels && ch < static_cast<int>(m_data.size()))
            {
                const int copySamples = std::min(m_numSamples, numSamples);
                if (copySamples > 0)
                    std::memcpy(newData[ch].data(), m_data[ch].data(), static_cast<std::size_t>(copySamples) * sizeof(float));
            }
        }

        m_data.swap(newData);
        m_numChannels = numChannels;
        m_numSamples = numSamples;
    }

    // Data access
    float* getWritePointer(int channel) noexcept
    {
        if (channel < 0 || channel >= m_numChannels)
            return nullptr;

        if (!m_ownsData)
            return (m_externalData ? m_externalData[channel] : nullptr);

        return m_data[channel].data();
    }

    const float* getReadPointer(int channel) const noexcept
    {
        if (channel < 0 || channel >= m_numChannels)
            return nullptr;

        if (!m_ownsData)
            return (m_externalData ? m_externalData[channel] : nullptr);

        return m_data[channel].data();
    }

    float getSample(int channel, int sampleIndex) const noexcept
    {
        assert(channel >= 0 && channel < m_numChannels);
        assert(sampleIndex >= 0 && sampleIndex < m_numSamples);
        const float* ptr = getReadPointer(channel);
        if (!ptr || sampleIndex < 0 || sampleIndex >= m_numSamples)
            return 0.0f;
        return ptr[sampleIndex];
    }

    // Clearing
    void clear()
    {
        if (!m_ownsData)
        {
            if (m_externalData)
            {
                for (int ch = 0; ch < m_numChannels; ++ch)
                {
                    float* p = m_externalData[ch];
                    if (p)
                        std::memset(p, 0, static_cast<std::size_t>(m_numSamples) * sizeof(float));
                }
            }
            return;
        }

        for (int ch = 0; ch < m_numChannels; ++ch)
            std::fill(m_data[ch].begin(), m_data[ch].end(), 0.0f);
    }

    void clear(int channel, int startSample, int numSamples)
    {
        assert(channel >= 0 && channel < m_numChannels);
        if (startSample < 0)
            startSample = 0;
        if (numSamples <= 0)
            return;
        const int end = std::min(m_numSamples, startSample + numSamples);
        if (startSample >= end)
            return;

        if (!m_ownsData)
        {
            if (m_externalData && m_externalData[channel])
            {
                float* p = m_externalData[channel] + startSample;
                std::memset(p, 0, static_cast<std::size_t>(end - startSample) * sizeof(float));
            }
            return;
        }

        std::fill(m_data[channel].begin() + startSample, m_data[channel].begin() + end, 0.0f);
    }

    // Copying
    void makeCopyOf(const ZBuffer& other, bool avoidReallocation = false)
    {
        if (&other == this)
            return;

        if (!other.m_ownsData)
        {
            // Other is a view; still copy its contents
        }

        if (avoidReallocation)
        {
            // Try to reuse capacity
            if (m_ownsData && static_cast<int>(m_data.size()) >= other.m_numChannels)
            {
                bool canReuse = true;
                for (int ch = 0; ch < other.m_numChannels; ++ch)
                {
                    if (static_cast<int>(m_data[ch].capacity()) < other.m_numSamples)
                    {
                        canReuse = false;
                        break;
                    }
                }

                if (canReuse)
                {
                    m_data.resize(other.m_numChannels);
                    for (int ch = 0; ch < other.m_numChannels; ++ch)
                    {
                        m_data[ch].resize(other.m_numSamples);
                        const float* src = other.getReadPointer(ch);
                        if (src)
                            std::memcpy(m_data[ch].data(), src, static_cast<std::size_t>(other.m_numSamples) * sizeof(float));
                        else
                            std::memset(m_data[ch].data(), 0, static_cast<std::size_t>(other.m_numSamples) * sizeof(float));
                    }

                    m_numChannels = other.m_numChannels;
                    m_numSamples = other.m_numSamples;
                    m_ownsData = true;
                    m_externalData = nullptr;
                    return;
                }
            }
        }

        // Fallback: allocate fresh
        std::vector<std::vector<float>> newData;
        newData.resize(other.m_numChannels);
        for (int ch = 0; ch < other.m_numChannels; ++ch)
        {
            newData[ch].assign(static_cast<std::size_t>(other.m_numSamples), 0.0f);
            const float* src = other.getReadPointer(ch);
            if (src)
                std::memcpy(newData[ch].data(), src, static_cast<std::size_t>(other.m_numSamples) * sizeof(float));
        }

        m_data.swap(newData);
        m_numChannels = other.m_numChannels;
        m_numSamples = other.m_numSamples;
        m_ownsData = true;
        m_externalData = nullptr;
    }

    // Copy a region from another ZBuffer into this one
    void copyFrom(int destChannel, int destStartSample, const ZBuffer& source,
                  int sourceChannel, int sourceStartSample, int numSamples)
    {
        if (numSamples <= 0)
            return;

        if (destChannel < 0 || destChannel >= m_numChannels)
            return;
        if (sourceChannel < 0 || sourceChannel >= source.m_numChannels)
            return;

        // Clamp ranges
        int dstStart = std::max(0, destStartSample);
        int srcStart = std::max(0, sourceStartSample);
        int dstEnd = std::min(m_numSamples, dstStart + numSamples);
        int srcEnd = std::min(source.m_numSamples, srcStart + numSamples);
        int actualNum = std::min(dstEnd - dstStart, srcEnd - srcStart);
        if (actualNum <= 0)
            return;

        // Get pointers
        float* dstPtr = getWritePointer(destChannel);
        const float* srcPtr = source.getReadPointer(sourceChannel);
        if (!dstPtr || !srcPtr)
            return;

        // Offset pointers
        dstPtr += dstStart;
        srcPtr += srcStart;

        // If both buffers point to the exact same memory region, use memmove
        if (dstPtr == srcPtr)
            return; // nothing to do

        // If they overlap in memory use memmove, else memcpy
        // We can't easily detect overlap across different containers reliably; use memmove for safety
        std::memmove(dstPtr, srcPtr, static_cast<std::size_t>(actualNum) * sizeof(float));
    }

    // Return the maximum absolute value in range
    float getMagnitude(int channel, int startSample, int numSamples) const
    {
        if (channel < 0 || channel >= m_numChannels)
            return 0.0f;
        if (numSamples <= 0)
            return 0.0f;

        int start = std::max(0, startSample);
        int end = std::min(m_numSamples, start + numSamples);
        if (start >= end)
            return 0.0f;

        const float* ptr = getReadPointer(channel);
        if (!ptr)
            return 0.0f;

        float maxAbs = 0.0f;
        for (int i = start; i < end; ++i)
        {
            float v = ptr[i];
            float a = (v < 0.0f) ? -v : v;
            if (a > maxAbs)
                maxAbs = a;
        }
        return maxAbs;
    }

private:
    int m_numChannels;
    int m_numSamples;

    // When owning data, m_data stores per-channel vectors. When non-owning, m_externalData points to float*[]
    bool m_ownsData;
    float** m_externalData;
    std::vector<std::vector<float>> m_data;
};
