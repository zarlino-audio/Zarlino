#pragma once

#include <JuceHeader.h> // bridge to JUCE; only this header includes JUCE
#include "ZColour.h"
#include "ZRectangle.h"
#include <cmath>

//==============================================================================
// ZColourGradient — linear or radial gradient
//==============================================================================
struct ZColourGradient {
    ZColour startColour;
    float startX = 0.0f, startY = 0.0f;
    ZColour endColour;
    float endX = 0.0f, endY = 0.0f;
    bool isRadial = false;

    ZColourGradient() = default;
    ZColourGradient(ZColour sc, float sx, float sy, ZColour ec, float ex, float ey, bool radial = false)
        : startColour(sc), startX(sx), startY(sy), endColour(ec), endX(ex), endY(ey), isRadial(radial) {}
};

//==============================================================================
// ZPathStrokeType
//==============================================================================
enum class ZStrokeJoint { Mitered, Curved, Beveled };

struct ZPathStrokeType {
    float thickness = 1.0f;
    ZStrokeJoint jointStyle = ZStrokeJoint::Curved;

    ZPathStrokeType() = default;
    explicit ZPathStrokeType(float t, ZStrokeJoint j = ZStrokeJoint::Curved)
        : thickness(t), jointStyle(j) {}
};

//==============================================================================
// ZPath — geometric path
//==============================================================================
class ZPath {
public:
    ZPath() = default;

    void clear() { m_path.clear(); }

    void addEllipse(const ZRectangle& bounds) {
        m_path.addEllipse(bounds.toJuce());
    }
    void addEllipse(float x, float y, float w, float h) {
        m_path.addEllipse(x, y, w, h);
    }
    
    void addCentredArc(float cx, float cy, float rx, float ry,
                       float rotationRadians, float startAngle, float endAngle,
                       bool useCircularEllipse) {
        m_path.addCentredArc(cx, cy, rx, ry, rotationRadians,
                            startAngle, endAngle, useCircularEllipse);
    }

    void startNewSubPath(float x, float y) { m_path.startNewSubPath(x, y); }
    void lineTo(float x, float y) { m_path.lineTo(x, y); }
    void closeSubPath() { m_path.closeSubPath(); }
    void quadraticTo(float cpx, float cpy, float x, float y) { m_path.quadraticTo(cpx, cpy, x, y); }
    void cubicTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y) {
        m_path.cubicTo(cp1x, cp1y, cp2x, cp2y, x, y);
    }

    // Create stroked version (for drop shadows etc.)
    ZPath createStrokedPath(const ZPathStrokeType& strokeType) const;

    // Internal access (only for bridge code)
    const juce::Path& getJucePath() const { return m_path; }
    juce::Path& getJucePath() { return m_path; }

private:
    juce::Path m_path;
};

//==============================================================================
// ZDropShadow  
//==============================================================================
struct ZDropShadow {
    ZColour colour;
    int radius = 0;
    float offsetX = 0.0f, offsetY = 0.0f;

    ZDropShadow() = default;
    ZDropShadow(ZColour c, int r, float ox = 0.0f, float oy = 0.0f)
        : colour(c), radius(r), offsetX(ox), offsetY(oy) {}
};

//==============================================================================
// ZJustification — text alignment
//==============================================================================
enum class ZJustification {
    centred,
    centredTop,
    centredBottom,
    centredLeft,
    centredRight,
    topLeft,
    topRight,
    bottomLeft,
    bottomRight
};

//==============================================================================
// ZFont
//==============================================================================
struct ZFont {
    float height = 12.0f;
    bool bold = false;
    bool italic = false;

    ZFont() = default;

    ZFont withHeight(float h) const {
        ZFont f = *this; f.height = h; return f;
    }

    ZFont withBold(bool b = true) const {
        ZFont f = *this; f.bold = b; return f;
    }

    ZFont withItalic(bool i = true) const {
        ZFont f = *this; f.italic = i; return f;
    }

};

//==============================================================================
// ZGraphics — the main drawing context
//==============================================================================
class ZGraphics {
public:
    // Construct wrapping an existing juce::Graphics
    explicit ZGraphics(juce::Graphics& g) : m_g(g) {}

    // --- Colour ---
    void setColour(ZColour c) { m_g.setColour(c.toJuce()); }
    void setGradientFill(const ZColourGradient& grad);

    // --- Filled shapes ---
    void fillAll(ZColour colour) { m_g.fillAll(colour.toJuce()); }
    void fillRoundedRectangle(const ZRectangle& bounds, float cornerRadius);
    void fillEllipse(const ZRectangle& bounds);
    void fillEllipse(float x, float y, float w, float h);
    void fillPath(const ZPath& path);

    // --- Stroked shapes ---
    void drawRoundedRectangle(const ZRectangle& bounds, float cornerRadius, float lineThickness);
    void drawEllipse(const ZRectangle& bounds, float lineThickness);
    void drawEllipse(float x, float y, float w, float h, float lineThickness);
    void drawLine(float x1, float y1, float x2, float y2, float lineThickness);
    void strokePath(const ZPath& path, const ZPathStrokeType& strokeType);

    // --- Text ---
    void setFont(const ZFont& font);
    void drawText(const char* text, const ZRectangle& bounds, ZJustification justification, bool useEllipsesIfTooBig = false);

    // --- Drop shadow ---
    void drawDropShadow(const ZDropShadow& shadow, const ZPath& path);

    // --- State ---
    void saveState() { m_g.saveState(); }
    void restoreState() { m_g.restoreState(); }
    void reduceClipRegion(const ZRectangle& bounds);
    
    // --- Access underlying (temporary bridge) ---
    juce::Graphics& getJuceGraphics() { return m_g; }

private:
    juce::Graphics& m_g;
};

//==============================================================================
// INLINE IMPLEMENTATIONS
//==============================================================================

inline void ZGraphics::setGradientFill(const ZColourGradient& grad) {
    juce::ColourGradient jg(
        grad.startColour.toJuce(), grad.startX, grad.startY,
        grad.endColour.toJuce(), grad.endX, grad.endY,
        grad.isRadial);
    m_g.setGradientFill(jg);
}

inline void ZGraphics::fillRoundedRectangle(const ZRectangle& bounds, float cornerRadius) {
    m_g.fillRoundedRectangle(bounds.toJuce(), cornerRadius);
}

inline void ZGraphics::fillEllipse(const ZRectangle& bounds) {
    m_g.fillEllipse(bounds.toJuce());
}

inline void ZGraphics::fillEllipse(float x, float y, float w, float h) {
    m_g.fillEllipse(x, y, w, h);
}

inline void ZGraphics::fillPath(const ZPath& path) {
    m_g.fillPath(path.getJucePath());
}

inline void ZGraphics::drawRoundedRectangle(const ZRectangle& bounds, float cornerRadius, float lineThickness) {
    m_g.drawRoundedRectangle(bounds.toJuce(), cornerRadius, lineThickness);
}

inline void ZGraphics::drawEllipse(float x, float y, float w, float h, float lineThickness) {
    m_g.drawEllipse(x, y, w, h, lineThickness);
}

inline void ZGraphics::drawEllipse(const ZRectangle& bounds, float lineThickness) {
    m_g.drawEllipse(bounds.toJuce(), lineThickness);
}

inline void ZGraphics::drawLine(float x1, float y1, float x2, float y2, float lineThickness) {
    m_g.drawLine(x1, y1, x2, y2, lineThickness);
}

inline void ZGraphics::strokePath(const ZPath& path, const ZPathStrokeType& strokeType) {
    juce::PathStrokeType jst(strokeType.thickness);
    switch (strokeType.jointStyle) {
        case ZStrokeJoint::Curved:  jst = juce::PathStrokeType(strokeType.thickness, juce::PathStrokeType::curved); break;
        case ZStrokeJoint::Beveled: jst = juce::PathStrokeType(strokeType.thickness, juce::PathStrokeType::beveled); break;
        case ZStrokeJoint::Mitered: jst = juce::PathStrokeType(strokeType.thickness, juce::PathStrokeType::mitered); break;
    }
    m_g.strokePath(path.getJucePath(), jst);
}

inline void ZGraphics::setFont(const ZFont& font) {
    int flags = juce::Font::plain;
    if (font.bold) flags |= juce::Font::bold;
    if (font.italic) flags |= juce::Font::italic;
    m_g.setFont(juce::Font(juce::FontOptions{}).withHeight(font.height).withStyle(flags));
}

inline void ZGraphics::drawText(const char* text, const ZRectangle& bounds, ZJustification justification, bool useEllipses) {
    juce::Justification j = juce::Justification::centred;
    switch (justification) {
        case ZJustification::centred:       j = juce::Justification::centred; break;
        case ZJustification::centredTop:    j = juce::Justification::centredTop; break;
        case ZJustification::centredBottom: j = juce::Justification::centredBottom; break;
        case ZJustification::centredLeft:   j = juce::Justification::centredLeft; break;
        case ZJustification::centredRight:  j = juce::Justification::centredRight; break;
        case ZJustification::topLeft:       j = juce::Justification::topLeft; break;
        case ZJustification::topRight:      j = juce::Justification::topRight; break;
        case ZJustification::bottomLeft:    j = juce::Justification::bottomLeft; break;
        case ZJustification::bottomRight:   j = juce::Justification::bottomRight; break;
        default: j = juce::Justification::centred; break;
    }
    m_g.drawText(text, bounds.toJuce(), j, useEllipses);
}

inline void ZGraphics::drawDropShadow(const ZDropShadow& shadow, const ZPath& path) {
    juce::DropShadow ds(shadow.colour.toJuce(), shadow.radius, 
                       juce::Point<int>((int)std::lround(shadow.offsetX), (int)std::lround(shadow.offsetY)));
    ds.drawForPath(m_g, path.getJucePath());
}

inline void ZGraphics::reduceClipRegion(const ZRectangle& bounds) {
    m_g.reduceClipRegion(bounds.toJuce().toNearestInt());
}

inline ZPath ZPath::createStrokedPath(const ZPathStrokeType& strokeType) const {
    ZPath result;
    juce::PathStrokeType jst(strokeType.thickness);
    switch (strokeType.jointStyle) {
        case ZStrokeJoint::Curved:  jst = juce::PathStrokeType(strokeType.thickness, juce::PathStrokeType::curved); break;
        case ZStrokeJoint::Beveled: jst = juce::PathStrokeType(strokeType.thickness, juce::PathStrokeType::beveled); break;
        case ZStrokeJoint::Mitered: jst = juce::PathStrokeType(strokeType.thickness, juce::PathStrokeType::mitered); break;
    }
    jst.createStrokedPath(result.getJucePath(), m_path);
    return result;
}
