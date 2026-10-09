import io, sys

path = r'C:/Users/khrit/Desktop/plugin/plugins/ZScorch/Source/PluginProcessor.cpp'
with io.open(path, 'r', encoding='utf-8', newline='') as f:
    data = f.read()

bad = '} \u201c'  # could be the curly-quote U+201D
# Find the broken line: it starts after the limiter struct's '};' and the namespace '}'
# followed by the em-dash comment fragment.
import re
m = re.search(r'\}\s*[\u2014\u201c\u201d\u2013\u00d7]\s*the real, anti-aliased DSP implementation\.', data)
if not m:
    print('ERR: broken fragment not found via regex')
    sys.exit(1)
print('found broken fragment at char', m.start())
print('preceding context:', repr(data[m.start()-60:m.start()]))
replacement = '}\n\n//==============================================================================\n// ZScorchAudioProcessor::Engine - the real, anti-aliased DSP implementation.'
data = data[:m.start()] + replacement + data[m.end():]

with io.open(path, 'w', encoding='utf-8', newline='') as f:
    f.write(data)
print('FIXED')
