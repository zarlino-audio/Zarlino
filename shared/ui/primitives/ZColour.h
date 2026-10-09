#pragma once
#include <cstdint>

struct ZColour {
    uint32_t argb = 0xFF000000u; // AARRGGBB

    ZColour() = default;
    explicit constexpr ZColour(uint32_t argb_) : argb(argb_) {}

    // Construct from ARGB bytes
    static constexpr ZColour fromARGB(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
        return ZColour((uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b));
    }

    // Named colors used across plugins
    static constexpr ZColour transparentBlack() { return ZColour(0x00000000); }
    static constexpr ZColour white() { return ZColour(0xFFFFFFFF); }

    // Component access
    uint8_t getAlpha() const { return (argb >> 24) & 0xFF; }
    uint8_t getRed()   const { return (argb >> 16) & 0xFF; }
    uint8_t getGreen() const { return (argb >> 8)  & 0xFF; }
    uint8_t getBlue()  const { return argb & 0xFF; }

    // Modifiers (return new colour, don't mutate)
    ZColour withAlpha(float alpha) const {
        uint8_t a = static_cast<uint8_t>(alpha * 255.0f + 0.5f);
        return ZColour((uint32_t(a) << 24) | (argb & 0x00FFFFFF));
    }

    ZColour withBrightness(float brightness) const {
        // brightness 0..1. Keep alpha. 
        // Simple linear scaling — matches JUCE's perceptual approximation
        float r = getRed() / 255.0f * brightness;
        float g = getGreen() / 255.0f * brightness;
        float b = getBlue() / 255.0f * brightness;
        return fromARGB(getAlpha(), 
            static_cast<uint8_t>(r * 255.0f + 0.5f), 
            static_cast<uint8_t>(g * 255.0f + 0.5f), 
            static_cast<uint8_t>(b * 255.0f + 0.5f));
    }

    ZColour withSaturation(float saturation) const {
        // saturation 0..1. Desaturate by blending toward gray.
        float gray = (getRed() + getGreen() + getBlue()) / (3.0f * 255.0f);
        float r = gray + (getRed()/255.0f - gray) * saturation;
        float g = gray + (getGreen()/255.0f - gray) * saturation;  
        float b = gray + (getBlue()/255.0f - gray) * saturation;
        return fromARGB(getAlpha(),
            static_cast<uint8_t>(r * 255.0f + 0.5f),
            static_cast<uint8_t>(g * 255.0f + 0.5f),
            static_cast<uint8_t>(b * 255.0f + 0.5f));
    }

    // Convert to/from juce::Colour for bridge code
    // Note: ZColour.h purposely does not include JUCE headers. These helpers
    // assume the including translation unit has JUCE available (ZGraphics.h does).
    juce::Colour toJuce() const { return juce::Colour(argb); }
    static ZColour fromJuce(const juce::Colour& c) { return ZColour(c.getARGB()); }

    bool operator==(const ZColour& o) const { return argb == o.argb; }
    bool operator!=(const ZColour& o) const { return argb != o.argb; }
};
