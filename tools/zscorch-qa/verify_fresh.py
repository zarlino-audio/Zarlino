import wave, numpy as np, os

def analyze(path):
    w = wave.open(path, 'rb')
    n = w.getnframes(); nch = w.getnchannels(); sr = w.getframerate()
    a = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float32)/32768.0
    a = a.reshape(-1, nch)
    peak = np.abs(a).max(); rms = np.sqrt((a**2).mean(axis=0))
    dur = n/sr
    # spectral flatness (tonal check): 0=tonal, 1=noise
    m = a.mean(axis=1)
    seg = m[len(m)//3:len(m)//3+sr*3]
    ww = np.hanning(len(seg)); spec = np.abs(np.fft.rfft(seg*ww)); eps=1e-10
    flat = np.exp(np.mean(np.log(spec+eps)))/(np.mean(spec+eps))
    print(f"{os.path.basename(path)}: sr={sr} ch={nch} dur={dur:.2f}s peak={peak:.3f} rmsL={rms[0]:.4f} rmsR={rms[1]:.4f} flatness={flat:.4f}")

base='artifacts/ab-sources/zscorch'
for f in ['guitar_dry_fresh.wav','guitar_proc_fresh.wav','verse_dry_fresh.wav','verse_proc_fresh.wav']:
    analyze(os.path.join(base,f))
