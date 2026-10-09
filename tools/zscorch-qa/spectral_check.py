import wave, numpy as np, os

def load(path):
    w = wave.open(path, 'rb')
    n = w.getnframes()
    sr = w.getframerate()
    a = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float32)/32768.0
    return a.reshape(-1, w.getnchannels()), sr

def spectral_profile(a, sr):
    # average mono
    m = a.mean(axis=1)
    # use a middle section
    start = len(m)//3
    seg = m[start:start+int(sr*3)]
    win = np.hanning(len(seg))
    spec = np.abs(np.fft.rfft(seg*win))
    freqs = np.fft.rfftfreq(len(seg), 1/sr)
    # energy in bands
    bands = {
        'sub (20-60)': (20,60), 'bass (60-250)': (60,250),
        'low-mid (250-500)': (250,500), 'mid (500-2k)': (500,2000),
        'high-mid (2k-6k)': (2000,6000), 'presence (6k-12k)': (6000,12000),
        'air (12k-20k)': (12000,20000)
    }
    tot = np.sum(spec)
    print("  Band energy distribution (fraction of total):")
    for name,(lo,hi) in bands.items():
        m_band = (freqs>=lo)&(freqs<hi)
        e = np.sum(spec[m_band])
        bar = '#'*int(e/tot*60)
        print(f"    {name:22s} {e/tot*100:6.2f}% {bar}")

for f in ['guitar_dry.wav','guitar_proc.wav','crunch_dry.wav','crunch_proc.wav','strings_dry.wav','strings_proc.wav']:
    p = os.path.join('artifacts/ab-sources/zscorch', f)
    a, sr = load(p)
    print(f"\n=== {f} (sr={sr}) ===")
    # zero-crossing rate / tonal check
    m = a.mean(axis=1)
    zcr = np.mean(np.abs(np.diff(np.sign(m))))
    # spectral flatness
    seg = m[len(m)//3:len(m)//3+sr*3]
    spec = np.abs(np.fft.rfft(seg*np.hanning(len(seg))))
    eps = 1e-10
    flat = np.exp(np.mean(np.log(spec+eps)))/(np.mean(spec+eps))
    print(f"  ZCR~{zcr:.2f}  spectral_flatness~{flat:.5f} (0=tonal, 1=noise)")
    spectral_profile(a, sr)
