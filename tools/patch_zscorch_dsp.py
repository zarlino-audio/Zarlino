import io, sys

path = r'C:/Users/khrit/Desktop/plugin/plugins/ZScorch/Source/PluginProcessor.cpp'
with io.open(path, 'r', encoding='utf-8') as f:
    src = f.read()

errors = 0

# ---------- Edit 1: genuine 4x oversampling ----------
if 'enum { osFactor = 2 };' in src:
    src = src.replace('enum { osFactor = 2 };', 'enum { osFactor = 4 };')
    print('OK  Edit1 osFactor 2 -> 4')
else:
    print('ERR Edit1 osFactor marker not found'); errors += 1

# ---------- Edit 2: replace CeilingLimiter struct ----------
start_marker = '    // 1 ms-lookahead ceiling limiter with arctan soft-knee.\n    struct CeilingLimiter\n'
end_marker = '    };\n}\n\n//==============================================================================\n// ZScorchAudioProcessor::Engine'

si = src.find(start_marker)
ei = src.find(end_marker)
if si < 0 or ei < 0 or ei <= si:
    print('ERR Edit2 limiter block boundaries not found si=%d ei=%d' % (si, ei)); errors += 1
else:
    new_limiter = r'''    // 1 ms-lookahead ceiling limiter with a smoothed peak-hold envelope.
    // The one-pole peak-hold (fast attack ~1 ms, medium release ~80 ms) removes the
    // instantaneous per-sample gain-reduction steps that produced zipper / crackle
    // artefacts under the multiband saturation -> dry/wet -> limiter chain.
    struct CeilingLimiter
    {
        int lookaheadSamples = 48;
        juce::AudioBuffer<float> delayBuf;
        int writePos = 0;
        float env = 1.0f;
        float attackCoef = 0.2f;
        float releaseCoef = 0.01f;

        void prepare (const juce::dsp::ProcessSpec& spec)
        {
            lookaheadSamples = juce::jmax (1, (int) (spec.sampleRate * 0.001));
            // Fast attack (reach ~1ms), medium release (~80ms).
            attackCoef  = 1.0f - std::exp (-1.0f / (0.001f * (float) spec.sampleRate));
            releaseCoef = 1.0f - std::exp (-1.0f / (0.080f * (float) spec.sampleRate));
            delayBuf.setSize ((int) spec.numChannels,
                              (int) spec.maximumBlockSize + lookaheadSamples * 2,
                              false, false, true);
            delayBuf.clear ();
            writePos = 0;
            env = 1.0f;
        }
        void reset ()
        {
            delayBuf.clear ();
            writePos = 0;
            env = 1.0f;
        }

        void process (juce::AudioBuffer<float>& buffer, float ceilingDb, float& gainReduction)
        {
            const int n = buffer.getNumSamples ();
            const int chans = buffer.getNumChannels ();
            const float ceiling = dbToGain (ceilingDb);
            const int dSize = delayBuf.getNumSamples ();
            gainReduction = 0.0f;

            for (int ch = 0; ch < chans; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                auto* delay = delayBuf.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                {
                    delay[writePos] = data[i];
                    const int readPos = (writePos + lookaheadSamples) % dSize;
                    const float incoming = delay[readPos];

                    // Instantaneous required gain reduction (1.0 = none).
                    float inst = 1.0f;
                    const float a = std::abs (incoming);
                    if (a > ceiling && a > 1e-6f)
                        inst = ceiling / a;

                    // Smooth the gain-reduction envelope (fast attack, slow release).
                    if (inst < env)
                        env = env + attackCoef * (inst - env);
                    else
                        env = env + releaseCoef * (inst - env);
                    env = juce::jlimit (0.001f, 1.0f, env);

                    // Apply smoothed gain with an arctan soft-knee final safety.
                    float out = delay[readPos] * env;
                    out = std::tanh (out / (ceiling + 1e-6f)) * ceiling;
                    data[i] = out;

                    if (env < 1.0f)
                        gainReduction = juce::jmax (gainReduction, 20.0f * std::log10 (env));
                    writePos = (writePos + 1) % dSize;
                }
            }
        }
    };
}'''
    src = src[:si] + new_limiter + src[ei+len(end_marker):]
    print('OK  Edit2 CeilingLimiter replaced with smoothed peak-hold')

# ---------- Edit 3: add master DC blocker member ----------
old_member = '    // Lookahead limiter at the end.\n    CeilingLimiter limiter;\n'
if old_member in src:
    new_member = ('    // Lookahead limiter at the end.\n    CeilingLimiter limiter;\n'
                  '    // Master DC blocker: removes residual DC/hum summed by the per-band\n'
                  '    // saturators at recombination so it never contaminates the limiter.\n'
                  '    std::array<DCBlocker, 2> masterDc;\n')
    src = src.replace(old_member, new_member, 1)
    print('OK  Edit3 master DCBlocker member added')
else:
    print('ERR Edit3 masterDc member marker not found'); errors += 1

# ---------- Edit 4: reset masterDc in reset() ----------
old_reset = '''        limiter.reset ();
        for (auto& b : bands)
        {'''
if old_reset in src:
    src = src.replace(old_reset, '''        limiter.reset ();
        for (auto& d : masterDc) d.reset ();
        for (auto& b : bands)
        {''', 1)
    print('OK  Edit4 masterDc reset added')
else:
    print('ERR Edit4 reset marker not found'); errors += 1

# ---------- Edit 5: pipeline master DC blocker before limiter in Engine::process ----------
old_proc = '''        // Ceiling limiter.
        gainRed = 0.0f;
        limiter.process (buffer, ceilingDb, gainRed);'''
if old_proc in src:
    new_proc = '''        // Master DC block (kills residual hum/DC before the limiter).
        for (int ch = 0; ch < chans; ++ch)
        {
            auto* i = buffer.getWritePointer (ch);
            auto& dc = masterDc[ch];
            for (int s = 0; s < n; ++s)
                i[s] = dc.process (i[s]);
        }

        // Ceiling limiter.
        gainRed = 0.0f;
        limiter.process (buffer, ceilingDb, gainRed);'''
    src = src.replace(old_proc, new_proc, 1)
    print('OK  Edit5 master DC blocker inserted before limiter')
else:
    print('ERR Edit5 limiter call marker not found'); errors += 1

if errors:
    print('ABORT: %d edit(s) failed; file NOT saved' % errors)
    sys.exit(1)

with io.open(path, 'w', encoding='utf-8') as f:
    f.write(src)
print('DONE: plugin DSP patched and saved')
