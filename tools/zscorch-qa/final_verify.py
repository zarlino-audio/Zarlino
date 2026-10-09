import wave, numpy as np, os

def analyzef(path):
    w = wave.open(path, 'rb')
    n = w.getnframes(); nch = w.getnchannels(); sr = w.getframerate()
    a = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float32)/32768.0
    a = a.reshape(-1, nch)
    peak = np.abs(a).max(); rms = np.sqrt((a**2).mean(axis=0)); dur = n/sr
    m = a.mean(axis=1)
    seg = m[len(m)//3:len(m)//3+sr*3]
    ww = np.hanning(len(seg)); spec = np.abs(np.fft.rfft(seg*ww)); eps=1e-10
    flat = np.exp(np.mean(np.log(spec+eps)))/(np.mean(spec+eps))
    # inter-channel correlation (should vary with real music, not be 1.0)
    c = np.corrcoef(a[:,0], a[:,1])[0,1]
    return dict(n=n,nch=nch,sr=sr,dur=dur,peak=peak,rms=rms,flat=flat,icc=c)

base='artifacts/ab-sources/zscorch/ab-review'
files = ['SET1_source_before_guitar.wav','SET1_source_after_guitar.wav',
         'SET2_render_before.wav','SET2_render_after.wav',
         'SET3_edited_before_verse.wav','SET3_edited_after_verse.wav']
print(f"{'file':32s} {'sr':6s}{'ch':3s}{'dur':6s}{'peak':7s}{'rmsL':8s}{'rmsR':8s}{'flat':7s}{'ICC':6s}")
ok=True
for f in files:
    p=os.path.join(base,f)
    if not os.path.exists(p):
        print(f, "MISSING"); ok=False; continue
    d=analyzef(p)
    print(f"{f:32s} {d['sr']:<6}{d['nch']:<3}{d['dur']:<6.2f}{d['peak']:<7.3f}{d['rms'][0]:<8.4f}{d['rms'][1]:<8.4f}{d['flat']:<7.4f}{d['icc']:<6.3f}")
    # criteria checks
    if d['sr']!=44100: print(f"   !! NOT 44.1k"); ok=False
    if d['nch']!=2: print(f"   !! NOT stereo"); ok=False
    if not (5<=d['dur']<=10): print(f"   !! dur out of 5-10s range"); ok=False
    if d['flat']>0.55: print(f"   !! looks noise-like"); ok=False
print("\nALL CRITERIA PASS" if ok else "\nSOME CRITERIA FAILED")
