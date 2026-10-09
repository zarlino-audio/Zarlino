# Zarlino UI Primitives — Graphics Abstraction Layer

## Overview

The `shared/ui/primitives/` directory provides a rendering abstraction that allows UI code
to work without directly depending on JUCE's painting API. Only `ZGraphics.h` includes
`<JuceHeader.h>` — the other headers (`ZColour.h`, `ZRectangle.h`) are pure value types
with no JUCE dependency.

**Bridge strategy:** All types provide `toJuce()` / `fromJuce()` conversion methods that
assume JUCE is available in the including translation unit. When we eventually swap the
backend, we implement the methods directly and remove the JUCE header include.

---

## Types

### ZColour (`ZColour.h`)
- Pure value type wrapping AARRGGBB `uint32_t`
- Factory: `fromARGB(a, r, g, b)`, named helpers (`white()`, `transparentBlack()`)
- Accessors: `getAlpha()`, `getRed()`, `getGreen()`, `getBlue()`
- Modifiers (immutable): `withAlpha(f)`, `withBrightness(f)`, `withSaturation(f)`
- Bridge: `toJuce()` → `juce::Colour`, `fromJuce(c)` ← `juce::Colour`

### ZRectangle (`ZRectangle.h`)
- Pure value type: `{ float x, y, w, h }`
- Read: `getX/Y/Width/Height/Right/Bottom/CentreX/CentreY`, `isEmpty()`
- Transforms (immutable): `translated(dx, dy)`, `expanded(amount)`, `reduced(amount)`, `withX/Y/Width/Height(v)`
- Bridge: `toJuce()` → `juce::Rectangle<float>`, `fromJuce(r)` ← `juce::Rectangle<float>`

### ZGraphics (`ZGraphics.h`)
Central drawing context. Wraps `juce::Graphics&` today; will be backend-agnostic later.

**Also defines these companion types (all in ZGraphics.h):**

- **ZColourGradient** — linear/radial gradient parameters
- **ZPath** — geometric path wrapping `juce::Path` (ellipses, arcs, lines, bezier curves)
- **ZPathStrokeType** — stroke thickness + joint style (Mitered/Curved/Beveled)
- **ZDropShadow** — colour, radius, offset for path-based drop shadows
- **ZFont** — height + bold/italic flags, functional builder pattern
- **ZJustification** — text alignment (centred, topLeft, bottomRight, etc.)

**ZGraphics drawing methods:**
- `setColour(c)`, `setGradientFill(grad)` — fill options
- `fillRoundedRectangle(bounds, radius)`, `fillEllipse(bounds)`, `fillPath(path)`
- `drawRoundedRectangle(bounds, radius, thickness)`, `drawEllipse(x, y, w, h, thick)`, `drawLine(x1, y1, x2, y2, thick)`, `strokePath(path, strokeType)`
- `setFont(font)`, `drawText(text, bounds, justification, useEllipses)`
- `drawDropShadow(shadow, path)`
- `saveState()`, `restoreState()`, `reduceClipRegion(bounds)`
- `getJuceGraphics()` — escape hatch to underlying `juce::Graphics&`

---

## Migration Pattern: Converting `juce::Graphics` Paint Code

### Before (JUCE-dependent)
```cpp
g.setColour(juce::Colour(0xFF00FF00));  // green
g.fillRoundedRectangle(bounds, 4.0f);
g.setFont(juce::Font(14.0f, juce::Font::bold));
g.drawText("Hello", bounds, juce::Justification::centred, false);
```

### After (ZGraphics bridge)
```cpp
ZGraphics zg(g);
zg.setColour(ZColour::fromARGB(0xFF, 0x00, 0xFF, 0x00));
zg.fillRoundedRectangle(rect, 4.0f);
zg.setFont(ZFont{}.withHeight(14.0f).withBold(true));
zg.drawText("Hello", rect, ZJustification::centred, false);
```

### Colour Migration from KriTheme
```cpp
// KriTheme stores constexpr uint32_t ARGB values:
// static constexpr uint32_t kKnobTrack = 0xFF00FF00;

// Old: juce::Colour(KriTheme::kKnobTrack)
// New: ZColour(KriTheme::kKnobTrack)
```

---

## Files Modified by Phase 3

| File | Status | Notes |
|------|--------|-------|
| `shared/ui/theme/KriTheme.h` | JUCE-free | Removed `#include <JuceHeader.h>` — just constexpr uint32_t |
| `shared/ui/primitives/ZColour.h` | NEW | Pure value type, no JUCE include |
| `shared/ui/primitives/ZRectangle.h` | NEW | Pure value type, no JUCE include |
| `shared/ui/primitives/ZGraphics.h` | NEW | Bridge file — only this includes JUCE |

---

## Next Steps: Porting Editors (`port-editors`)

Files still using raw JUCE graphics directly:
- `shared/ui/KriPaint.h` — 4 static paint methods (background, knob track, shadow)
- `shared/ui/KriKnob.h` — custom rotary slider widget
- `plugins/ZTame/Source/PluginEditor.h` + `.cpp` — 1000+ lines, 4 custom components
- `plugins/ZScorch/Source/PluginEditor.*`
- `plugins/RACK_AI/Source/PluginEditor.*` + `UIComponents/*`

**Approach:** Start with `KriPaint.h` (simplest — static functions), then `KriKnob.h`,
then port editors one at a time starting with the simplest (MyFirstEQ has essentially no UI).

---

## Limitations

- **ZColour** uses simple linear scaling for brightness/saturation — not perceptual
  (matches JUCE's approximation, not CIELAB-correct)
- **ZFont** is minimal — no typeface selection, no kerning hints
- **ZPath** wraps `juce::Path` entirely — a non-JUCE backend needs its own path implementation
- **ZGraphics** holds a reference to `juce::Graphics` — not self-owning, not movable
- No image/pixel manipulation API yet
