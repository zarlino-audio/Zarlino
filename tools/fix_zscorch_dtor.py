import io, sys

# Inline definition of ~ZScorchAudioProcessor in the .cpp after Engine is complete,
# and a declaration only in the .h. This lets unique_ptr<Engine> be destroyed in
# the TU where Engine is fully defined (Fixes 'incomplete type' in OfflineRender).

# --- 1. header: declaration only ---
hpath = r'C:/Users/khrit/Desktop/plugin/plugins/ZScorch/Source/PluginProcessor.h'
with io.open(hpath, 'r', encoding='utf-8', newline='') as f:
    h = f.read()

old = 'ZScorchAudioProcessor();\n    ~ZScorchAudioProcessor() override = default;'
new = 'ZScorchAudioProcessor();\n    ~ZScorchAudioProcessor() override;'
if old in h:
    h = h.replace(old, new, 1)
    print('H: made dtor a declaration')
else:
    print('H: WARN exact dtor line not found; trying alternate')
    alt_old = '~ZScorchAudioProcessor() override = default;'
    if alt_old in h:
        h = h.replace(alt_old, '~ZScorchAudioProcessor() override;', 1)
        print('H: made dtor a declaration (alt match)')
    else:
        print('H: ERROR cannot find dtor'); sys.exit(1)

with io.open(hpath, 'w', encoding='utf-8', newline='') as f:
    f.write(h)

# --- 2. cpp: define dtor right after the Engine struct definition ---
cppath = r'C:/Users/khrit/Desktop/plugin/plugins/ZScorch/Source/PluginProcessor.cpp'
with io.open(cppath, 'r', encoding='utf-8', newline='') as f:
    c = f.read()

# The Engine struct ends with the '};' right before 'const char* ZScorchAudioProcessor::satParamId'
marker = "//==============================================================================\nconst char* ZScorchAudioProcessor::satParamId (const char* base, int band)"
if marker in c:
    insert = "//==============================================================================\n// Out-of-line destructor: completes unique_ptr<Engine> where Engine is fully defined.\nZScorchAudioProcessor::~ZScorchAudioProcessor() = default;\n\n//==============================================================================\nconst char* ZScorchAudioProcessor::satParamId (const char* base, int band)"
    c = c.replace(marker, insert, 1)
    print('C: inserted out-of-line dtor definition')
else:
    print('C: ERROR satParamId marker not found'); sys.exit(1)

with io.open(cppath, 'w', encoding='utf-8', newline='') as f:
    f.write(c)

print('OK both files patched')
