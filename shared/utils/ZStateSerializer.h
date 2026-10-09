#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include <string>
#include <stdexcept>
#include <cassert>
#include <algorithm>

// Lightweight parameter blob for state save/load
// Replaces juce::ValueTree for DAW preset serialization
class ZStateSerializer {
public:
    // Write state: version + parameter count + per-parameter (id, value) pairs
    // Returns binary blob suitable for DAW preset storage
    static std::vector<uint8_t> serialize(
        int version,
        const std::vector<std::pair<std::string, float>>& params)
    {
        const size_t totalSize = requiredSize(params);
        std::vector<uint8_t> buf;
        buf.reserve(totalSize);

        writeInt32(buf, static_cast<int32_t>(version));
        writeInt32(buf, static_cast<int32_t>(params.size()));

        for (const auto& p : params) {
            const std::string& id = p.first;
            const float value = p.second;
            // length includes null terminator
            int32_t idLen = static_cast<int32_t>(id.size()) + 1;
            writeInt32(buf, idLen);
            // copy id bytes including terminating null
            buf.insert(buf.end(), id.c_str(), id.c_str() + idLen);
            writeFloat(buf, value);
        }

        assert(buf.size() == totalSize);
        return buf;
    }

    // Read state: returns (version, param_map)
    // Throws std::runtime_error on corrupt data
    struct State {
        int version;
        std::vector<std::pair<std::string, float>> params;
    };

    static State deserialize(const uint8_t* data, size_t size)
    {
        if (data == nullptr && size != 0)
            throw std::runtime_error("ZStateSerializer::deserialize: null data pointer");

        const uint8_t* ptr = data;
        const uint8_t* end = data + size;

        State s;
        s.version = readInt32(ptr, end);

        int32_t paramCount = readInt32(ptr, end);
        if (paramCount < 0)
            throw std::runtime_error("ZStateSerializer::deserialize: negative parameter count");

        s.params.reserve(static_cast<size_t>(paramCount));
        for (int32_t i = 0; i < paramCount; ++i) {
            int32_t idLen = readInt32(ptr, end);
            if (idLen <= 0)
                throw std::runtime_error("ZStateSerializer::deserialize: invalid id length");

            // ensure enough bytes for id
            if ((size_t)(end - ptr) < static_cast<size_t>(idLen))
                throw std::runtime_error("ZStateSerializer::deserialize: truncated id string");

            // ensure null terminator present
            if (ptr[idLen - 1] != '\0')
                throw std::runtime_error("ZStateSerializer::deserialize: id string not null-terminated");

            std::string id(reinterpret_cast<const char*>(ptr), static_cast<size_t>(idLen - 1));
            ptr += idLen;

            float value = readFloat(ptr, end);

            s.params.emplace_back(std::move(id), value);
        }

        // There may be trailing bytes (future extensions) — that's acceptable. But ensure we didn't read past end.
        return s;
    }

    // Convenience: write directly to a pre-allocated buffer (for processBlock-like usage)
    // Returns number of bytes written
    static size_t serializeTo(
        uint8_t* dest, size_t destSize,
        int version,
        const std::vector<std::pair<std::string, float>>& params)
    {
        if (dest == nullptr && destSize != 0)
            throw std::runtime_error("ZStateSerializer::serializeTo: null dest pointer");

        const size_t need = requiredSize(params);
        if (destSize < need)
            throw std::runtime_error("ZStateSerializer::serializeTo: destination buffer too small");

        uint8_t* ptr = dest;
        uint8_t* end = dest + destSize;

        // write version
        writeInt32ToBuffer(ptr, end, static_cast<int32_t>(version));
        // write count
        writeInt32ToBuffer(ptr, end, static_cast<int32_t>(params.size()));

        for (const auto& p : params) {
            const std::string& id = p.first;
            const float value = p.second;
            int32_t idLen = static_cast<int32_t>(id.size()) + 1;
            writeInt32ToBuffer(ptr, end, idLen);
            // write id bytes including null
            if ((size_t)(end - ptr) < static_cast<size_t>(idLen))
                throw std::runtime_error("ZStateSerializer::serializeTo: unexpected buffer overrun while writing id");
            std::memcpy(ptr, id.c_str(), static_cast<size_t>(idLen));
            ptr += idLen;
            writeFloatToBuffer(ptr, end, value);
        }

        return static_cast<size_t>(ptr - dest);
    }

private:
    static size_t requiredSize(const std::vector<std::pair<std::string, float>>& params)
    {
        // header: 4 bytes version + 4 bytes count
        size_t total = 8;
        for (const auto& p : params) {
            size_t idLen = p.first.size() + 1; // include null
            total += 4; // id length field
            total += idLen; // id bytes
            total += 4; // float
        }
        return total;
    }

    // Helper: write int32 little-endian to vector
    static void writeInt32(std::vector<uint8_t>& buf, int32_t v)
    {
        uint32_t uv = static_cast<uint32_t>(v);
        buf.push_back(static_cast<uint8_t>(uv & 0xFF));
        buf.push_back(static_cast<uint8_t>((uv >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>((uv >> 16) & 0xFF));
        buf.push_back(static_cast<uint8_t>((uv >> 24) & 0xFF));
    }

    static int32_t readInt32(const uint8_t*& ptr, const uint8_t* end)
    {
        if ((size_t)(end - ptr) < 4)
            throw std::runtime_error("ZStateSerializer::readInt32: buffer underrun");
        uint32_t b0 = ptr[0];
        uint32_t b1 = ptr[1];
        uint32_t b2 = ptr[2];
        uint32_t b3 = ptr[3];
        ptr += 4;
        uint32_t uv = (b0) | (b1 << 8) | (b2 << 16) | (b3 << 24);
        return static_cast<int32_t>(uv);
    }

    // Helper: write float to vector (as IEEE-754 little-endian)
    static void writeFloat(std::vector<uint8_t>& buf, float v)
    {
        static_assert(sizeof(float) == 4, "float must be 32-bit");
        uint32_t uv;
        std::memcpy(&uv, &v, sizeof(float));
        writeInt32(buf, static_cast<int32_t>(uv));
    }

    static float readFloat(const uint8_t*& ptr, const uint8_t* end)
    {
        int32_t iv = readInt32(ptr, end);
        uint32_t uv = static_cast<uint32_t>(iv);
        float v;
        std::memcpy(&v, &uv, sizeof(float));
        return v;
    }

    // Buffer-writing helpers
    static void writeInt32ToBuffer(uint8_t*& ptr, const uint8_t* end, int32_t v)
    {
        if ((size_t)(end - ptr) < 4)
            throw std::runtime_error("ZStateSerializer::writeInt32ToBuffer: buffer overflow");
        uint32_t uv = static_cast<uint32_t>(v);
        ptr[0] = static_cast<uint8_t>(uv & 0xFF);
        ptr[1] = static_cast<uint8_t>((uv >> 8) & 0xFF);
        ptr[2] = static_cast<uint8_t>((uv >> 16) & 0xFF);
        ptr[3] = static_cast<uint8_t>((uv >> 24) & 0xFF);
        ptr += 4;
    }

    static void writeFloatToBuffer(uint8_t*& ptr, const uint8_t* end, float v)
    {
        static_assert(sizeof(float) == 4, "float must be 32-bit");
        if ((size_t)(end - ptr) < 4)
            throw std::runtime_error("ZStateSerializer::writeFloatToBuffer: buffer overflow");
        uint32_t uv;
        std::memcpy(&uv, &v, sizeof(float));
        ptr[0] = static_cast<uint8_t>(uv & 0xFF);
        ptr[1] = static_cast<uint8_t>((uv >> 8) & 0xFF);
        ptr[2] = static_cast<uint8_t>((uv >> 16) & 0xFF);
        ptr[3] = static_cast<uint8_t>((uv >> 24) & 0xFF);
        ptr += 4;
    }
};
