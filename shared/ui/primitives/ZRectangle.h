#pragma once
#include <algorithm>
#include <cmath>

struct ZRectangle {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

    ZRectangle() = default;
    ZRectangle(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}

    // Derived positions (match JUCE naming)
    float getX() const { return x; }
    float getY() const { return y; }
    float getWidth() const { return w; }
    float getHeight() const { return h; }
    float getRight() const { return x + w; }
    float getBottom() const { return y + h; }
    float getCentreX() const { return x + w * 0.5f; }
    float getCentreY() const { return y + h * 0.5f; }

    bool isEmpty() const { return w <= 0.0f || h <= 0.0f; }

    // Transforms (return new, don't mutate)
    ZRectangle translated(float dx, float dy) const {
        return ZRectangle(x + dx, y + dy, w, h);
    }

    ZRectangle expanded(float amount) const {
        return ZRectangle(x - amount, y - amount, w + amount * 2.0f, h + amount * 2.0f);
    }

    ZRectangle reduced(float amount) const {
        return expanded(-amount);
    }

    ZRectangle withX(float newX) const { return ZRectangle(newX, y, w, h); }
    ZRectangle withY(float newY) const { return ZRectangle(x, newY, w, h); }
    ZRectangle withWidth(float newW) const { return ZRectangle(x, y, newW, h); }
    ZRectangle withHeight(float newH) const { return ZRectangle(x, y, w, newH); }

    // Convert to/from JUCE
    juce::Rectangle<float> toJuce() const { return juce::Rectangle<float>(x, y, w, h); }
    static ZRectangle fromJuce(const juce::Rectangle<float>& r) { 
        return ZRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight()); 
    }
};
