import wave, struct, numpy as np, os

def analyze(f, path):
    w = wave.open(path, 'rb')
    n = w.getnframes()
    nch = w.getnchannels()
    sr = w.getframerate()
    frames = w.readframes(n)
    a = np.frombuffer(frames, dtype=np.int16).astype(np.float32)/32768.0
    a = a.reshape(-1, nch)
    peak = np.abs(a).max()
    rms = np.sqrt((a**2).mean(axis=0))
    dur = n/sr
    print(f"{f}: sr={sr} ch={nch} dur={dur:.2f}s peak={peak:.3f} rmsL={rms[0]:.4f} rmsR={rms[1]:.4f}")

base = 'artifacts/ab-sources/zscorch'
for f in ['guitar_dry.wav','guitar_proc.wav','crunch_dry.wav','crunch_proc.wav',
          'hook_dry.wav','hook_proc.wav','strings_dry.wav','strings_proc.wav',
          'chorus_dry.wav','chorus_proc.wav']:
    p = os.path.join(base, f)
    if os.path.exists(p):
        analyze(f, p)
