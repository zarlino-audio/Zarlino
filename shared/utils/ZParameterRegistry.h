#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <functional>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cmath>
#include <mutex>

// Parameter types
enum class ZParamType { Float, Bool };

// Normalisable range (replaces juce::NormalisableRange<float>)
struct ZNormalisableRange {
    float min = 0.0f;
    float max = 1.0f;
    float step = 0.0f; // 0 means continuous
    float skew = 1.0f; // 1 means linear

    float snapToLegalValue(float v) const {
        if (v != v) return min; // NaN guard
        if (v < min) v = min;
        if (v > max) v = max;
        if (step > 0.0f) {
            float steps = std::round((v - min) / step);
            v = min + steps * step;
            // after snapping, clamp again
            if (v < min) v = min;
            if (v > max) v = max;
        }
        return v;
    }

    // Convert raw value (min..max) to 0..1 considering skew
    float convertTo0to1(float v) const {
        if (max <= min) return 0.0f;
        float proportion = (v - min) / (max - min);
        if (proportion < 0.0f) proportion = 0.0f;
        if (proportion > 1.0f) proportion = 1.0f;
        if (skew != 1.0f && skew > 0.0f) {
            // JUCE uses pow to implement skewing: out = pow(in, 1.0/skew)
            proportion = std::pow(proportion, 1.0f / skew);
        }
        return proportion;
    }

    // Convert 0..1 to raw value (min..max) considering skew
    float convertFrom0to1(float v) const {
        float norm = v;
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        if (skew != 1.0f && skew > 0.0f) {
            norm = std::pow(norm, skew);
        }
        float raw = min + (max - min) * norm;
        if (step > 0.0f)
            raw = snapToLegalValue(raw);
        return raw;
    }
};

// Individual parameter definition
struct ZParamDef {
    std::string id;       // e.g. "lift"
    std::string name;     // e.g. "Lift"
    ZParamType type = ZParamType::Float;
    ZNormalisableRange range; // ignored for Bool
    float defaultFloat = 0.0f;   // default value (raw units)
    bool defaultBool = false;
};

// The registry
class ZParameterRegistry {
public:
    using ParamChangeCallback = std::function<void(float newValue)>;

private:
    struct Param; // forward

public:
    // Lightweight parameter interface expected by tests
    struct RangedParam {
        RangedParam(ZParameterRegistry* r, Param* p) : registry(r), param(p) {}
        void setValue(float normalized) {
            if (registry && param) registry->setValue(param->def.id, normalized);
        }
        void sendValueChangedMessageToListeners() {
            if (!registry || !param) return;
            float raw = param->atomicValue.load();
            std::lock_guard<std::mutex> lock(registry->listenerMutex_);
            for (auto& l : param->listeners) if (l) l(raw);
        }
        float getValue() const {
            if (!param) return 0.0f;
            return param->atomicValue.load();
        }
        const std::string& getID() const { return param->def.id; }
    private:
        ZParameterRegistry* registry = nullptr;
        Param* param = nullptr;
    };

private:
    // Internal per-param storage
    struct Param {
        ZParamDef def;
        std::atomic<float> atomicValue{0.0f};
        std::vector<ParamChangeCallback> listeners;
        std::unique_ptr<RangedParam> paramInterface;
    };

public:
    // Constructor — takes a list of parameter definitions
    ZParameterRegistry(const std::vector<ZParamDef>& params) {
        m_params.reserve(params.size());
        for (const auto& d : params) {
            auto p = std::make_unique<Param>();
            p->def = d;
            float init = 0.0f;
            if (d.type == ZParamType::Float) {
                // defaultFloat is expected to be in raw units (min..max)
                init = d.range.snapToLegalValue(d.defaultFloat);
            } else {
                init = d.defaultBool ? 1.0f : 0.0f;
            }
            p->atomicValue.store(init);
            p->paramInterface = std::make_unique<RangedParam>(this, p.get());
            m_params.push_back(std::move(p));
        }
    }

    // Get atomic pointer for audio-thread reads (same as APVTS)
    std::atomic<float>* getRawParameterValue(const std::string& id) {
        int idx = findParamIndex(id);
        if (idx < 0) return nullptr;
        return &m_params[idx]->atomicValue;
    }

    // UI callback registration
    void addListener(const std::string& id, ParamChangeCallback cb) {
        std::lock_guard<std::mutex> lock(listenerMutex_);
        int idx = findParamIndex(id);
        if (idx < 0) return;
        m_params[idx]->listeners.push_back(std::move(cb));
    }

    // remove all listeners for id
    void removeListener(const std::string& id) {
        std::lock_guard<std::mutex> lock(listenerMutex_);
        int idx = findParamIndex(id);
        if (idx < 0) return;
        m_params[idx]->listeners.clear();
    }

    // Programmatic value setting (triggers callbacks)
    // normalizedValue is 0..1 for floats, >=0.5 means true for bools
    void setValue(const std::string& id, float normalizedValue) {
        int idx = findParamIndex(id);
        if (idx < 0) return;
        auto& param = *m_params[idx];
        float newRaw = 0.0f;
        if (param.def.type == ZParamType::Float) {
            if (normalizedValue < 0.0f) normalizedValue = 0.0f;
            if (normalizedValue > 1.0f) normalizedValue = 1.0f;
            newRaw = param.def.range.convertFrom0to1(normalizedValue);
        } else {
            newRaw = (normalizedValue >= 0.5f) ? 1.0f : 0.0f;
        }
        param.atomicValue.store(newRaw);
        // notify listeners (UI thread expectation)
        {
            std::lock_guard<std::mutex> lock(listenerMutex_);
            for (auto& l : param.listeners) {
                if (l) l(newRaw);
            }
        }
    }

    // Get raw value (actual units for floats, 0/1 for bools)
    float getValue(const std::string& id) const {
        int idx = findParamIndex(id);
        if (idx < 0) return 0.0f;
        return m_params[idx]->atomicValue.load();
    }

    // State serialization helpers
    // Writes version (int32) + param count (int32) + [id length (int32), id string, value (float)] pairs
    std::vector<uint8_t> serializeState() const {
        const int32_t version = 1;
        int32_t count = static_cast<int32_t>(m_params.size());
        // compute size
        size_t size = sizeof(int32_t) + sizeof(int32_t);
        for (const auto& p : m_params) {
            int32_t idlen = static_cast<int32_t>(p->def.id.size());
            size += sizeof(int32_t) + idlen + sizeof(float);
        }
        std::vector<uint8_t> out;
        out.resize(size);
        size_t offset = 0;
        auto writeBytes = [&](const void* src, size_t n) {
            std::memcpy(out.data() + offset, src, n);
            offset += n;
        };
        writeBytes(&version, sizeof(version));
        writeBytes(&count, sizeof(count));
        for (const auto& p : m_params) {
            int32_t idlen = static_cast<int32_t>(p->def.id.size());
            writeBytes(&idlen, sizeof(idlen));
            if (idlen > 0) writeBytes(p->def.id.data(), idlen);
            float val = p->atomicValue.load();
            writeBytes(&val, sizeof(val));
        }
        assert(offset == size);
        return out;
    }

    void deserializeState(const uint8_t* data, size_t size) {
        if (!data || size < sizeof(int32_t) * 2) return;
        size_t offset = 0;
        auto readBytes = [&](void* dst, size_t n) -> bool {
            if (offset + n > size) return false;
            std::memcpy(dst, data + offset, n);
            offset += n;
            return true;
        };
        int32_t version = 0;
        if (!readBytes(&version, sizeof(version))) return;
        (void)version; // reserved for future use
        int32_t count = 0;
        if (!readBytes(&count, sizeof(count))) return;
        for (int32_t i = 0; i < count; ++i) {
            int32_t idlen = 0;
            if (!readBytes(&idlen, sizeof(idlen))) return;
            if (idlen < 0 || idlen > 10000) return; // sanity
            std::string id;
            if (idlen > 0) {
                id.resize(idlen);
                if (!readBytes(&id[0], idlen)) return;
            }
            float val = 0.0f;
            if (!readBytes(&val, sizeof(val))) return;
            // find param and set raw value
            int idx = findParamIndex(id);
            if (idx < 0) continue; // skip unknown params
            auto& param = *m_params[idx];
            float newVal = val;
            if (param.def.type == ZParamType::Float) {
                newVal = param.def.range.snapToLegalValue(newVal);
            } else {
                newVal = (newVal >= 0.5f) ? 1.0f : 0.0f;
            }
            float oldVal = param.atomicValue.load();
            param.atomicValue.store(newVal);
            // notify listeners about the new value
            {
                std::lock_guard<std::mutex> lock(listenerMutex_);
                for (auto& l : param.listeners) {
                    if (l) l(newVal);
                }
            }
        }
    }

    // Parameter count
    int getNumParams() const { return static_cast<int>(m_params.size()); }

    // Return nullptr if not found
    RangedParam* getParameter(const std::string& id) {
        int idx = findParamIndex(id);
        if (idx < 0) return nullptr;
        return m_params[idx]->paramInterface.get();
    }

private:
    std::vector<std::unique_ptr<Param>> m_params;
    mutable std::mutex listenerMutex_;

    int findParamIndex(const std::string& id) const {
        for (size_t i = 0; i < m_params.size(); ++i) {
            if (m_params[i]->def.id == id) return static_cast<int>(i);
        }
        return -1;
    }
};
