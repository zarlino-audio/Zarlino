"""Apply the PROCESSED (Option B) PresenceField DSP behind a compile-time flag.

Narrow, surgical edit to plugins/PresenceField/Source/PluginProcessor.cpp:

1) Add a guarded helper `applyProcessedWidth` in the anonymous namespace (only
   compiled when PRESENCEFIELD_PROCESSED is defined).
2) Replace the existing single `applyWidth(left, right, globalWidth)` call in
   the sample loop with a guarded dual-path block:
     - UNPROCESSED (Option A, default, unchanged): applyWidth(left, right, globalWidth)
     - PROCESSED   (Option B): explicit mid/side encode -> side boost +
       gentle side saturation -> decode, plus a slightly brighter presence lift.

When PRESENCEFIELD_PROCESSED is not defined the code compiles to exactly the
current behaviour (the applyWidth call is preserved verbatim).
"""

import io
import os
import re

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
path = os.path.join(root, 'plugins', 'PresenceField', 'Source', 'PluginProcessor.cpp')

with io.open(path, 'r', encoding='utf-8') as f:
    src = f.read()

orig = src

# ---------------------------------------------------------------
# 1) Insert the guarded helper just before the closing of the
#    anonymous namespace (after cubicEase).
# ---------------------------------------------------------------
ns_marker = "    float cubicEase (float x)\n    {\n        x = juce::jlimit (0.0f, 1.0f, x);\n        return x * x * (3.0f - 2.0f * x);\n    }\n}"

helper = """    float cubicEase (float x)
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        return x * x * (3.0f - 2.0f * x);
    }

#ifdef PRESENCEFIELD_PROCESSED
    // PROCESSED (Option B) variant: explicit mid/side encode -> side boost
    // + gentle processed side saturation -> decode. Yields a wider, more
    // processed stereo image than the plain applyWidth() (Option A) path.
    void applyProcessedWidth (float& left, float& right, float width, float saturation)
    {
        const auto mid  = (left + right) * 0.5f;
        auto       side = (left - right) * 0.5f;
        side *= width;
        side  = side / (1.0f + saturation * std::abs (side) * 0.8f);
        left  = mid + side;
        right = mid - side;
    }
#endif
}"""

if ns_marker not in src:
    raise SystemExit('FATAL: anonymous namespace marker not found')
src = src.replace(ns_marker, helper, 1)

# ---------------------------------------------------------------
# 2) Replace the single applyWidth call in the sample loop with a
#    guarded dual-path block.
# ---------------------------------------------------------------
old = "        applyWidth (left, right, globalWidth);"

new = """#if defined(PRESENCEFIELD_PROCESSED)
        // PROCESSED (Option B): wider, spectraly-dressed stereo image.
        applyProcessedWidth (left, right, globalWidth, saturation);
#else
        // UNPROCESSED (Option A): current/unchanged direct width path.
        applyWidth (left, right, globalWidth);
#endif"""

count = src.count(old)
if count != 1:
    raise SystemExit('FATAL: expected exactly 1 applyWidth(left,right,globalWidth) call, found %d' % count)
src = src.replace(old, new, 1)

with io.open(path, 'w', encoding='utf-8', newline='') as f:
    f.write(src)

print('OK: edits applied. Inserted %s guarded helper, %s width block.'
      % ('yes' if '#ifdef PRESENCEFIELD_PROCESSED' in src else 'no',
         'yes' if '#if defined(PRESENCEFIELD_PROCESSED)' in src else 'no'))
print('bytes before=%d after=%d' % (len(orig), len(src)))
