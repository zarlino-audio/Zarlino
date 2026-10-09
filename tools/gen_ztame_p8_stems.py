#!/usr/bin/env python3
# Generate ZTame P8 test stems (deterministic).
import math, os, wave
import numpy as np
SR = 44100

def tone(f, dur, amp=1.0, ph=0.0):
    t = np.arange(int(dur*SR))/SR
    return amp*np.sin(2*np.pi*f*t+ph)

def noise(dur, seed=42):
    return np.random.RandomState(seed).randn(int(dur*SR))

def genv(n, a_s, r_s):
    a, r = int(a_s*SR), int(r_s*SR)
    env = np.ones(n)
    if n > a + r:
        env[:a] = np.linspace(0,1,a)
        env[-r:] *= np.linspace(1,0,r)
    return env

def norm(x, pk=-1.0):
    p = np.max(np.abs(x))
    if p < 1e-12:
        return x
    return x*(10.0**(pk/20.0)/p)

def write_wav(path, left, right):
    if len(left) != len(right):
        raise ValueError('stereo channels length mismatch')
    inter = np.empty((len(left),2), dtype=np.float32)
    inter[:,0], inter[:,1] = left, right
    pcm = (inter*32767.0).astype(np.int16)
    w = wave.open(path,'wb')
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes(pcm.tobytes()); w.close()
    return len(left)/SR

def stem1_ringing(dur):
    n = int(dur*SR)
    rng = np.random.RandomState(777)
    bed = noise(dur,777) + tone(110.0,dur,amp=0.22) + tone(220.0,dur,amp=0.10)
    ring = np.zeros(n)
    for f in (980,1250,1900,2530,3100,3850,4400):
        ring += tone(f,dur,amp=1.0,ph=rng.uniform(0,math.tau))
    q = np.fft.rfft(bed)
    freqs = np.fft.rfftfreq(n,1.0/SR)
    q[freqs > 8000.0] *= 0.2
    bed_lp = np.fft.irfft(q,n)
    sig = 0.55*bed_lp + 0.45*ring
    sig *= genv(n,0.02,0.02)
    return norm(sig)

def pluck(freq, dur, hroll=0.60):
    t = np.arange(int(dur*SR))/SR
    nh = int(9000.0/freq) + 1
    sig = np.zeros_like(t)
    for k in range(1,nh):
        fk = freq*k*(1+0.0001*k)
        tau = max(0.35, 2.5/k)
        a = hroll**k
        sig += a*(np.sin(2*np.pi*fk*t + k*0.13) + 0.5*np.exp(-t/tau)*np.sin(2*np.pi*fk*t))
    sig /= (1 + nh//4)
    return sig

def stem2_plucked(dur):
    n = int(dur*SR)
    out = np.zeros(n)
    notes = [(196.0,0.0),(246.9,1.1),(293.7,2.2),(329.6,3.3),(392.0,4.4),(493.9,5.6)]
    for f,t0 in notes:
        s0 = int(t0*SR)
        s1 = min(s0 + int(2.0*SR), n)
        seg = pluck(f,(s1-s0)/SR) * genv(s1-s0,0.003,0.08)
        out[s0:s1] += seg
    out *= genv(n,0.02,0.05)
    return norm(out)

def stem3_drums_pad(dur):
    n = int(dur*SR)
    t = np.arange(n)/SR
    out = np.zeros(n)
    pad = tone(147.0,dur,amp=0.5)+tone(220.0,dur,amp=0.35)+tone(880.0,dur,amp=0.10)+tone(1760.0,dur,amp=0.05)
    pad *= (0.65 + 0.35*np.sin(2*np.pi*0.25*t))
    out += 0.5*pad
    def kick(t0):
        s0 = int(t0*SR)
        if s0 >= n:
            return
        s1 = min(s0+int(0.6*SR), n)
        kt = np.arange(s1-s0)/SR
        out[s0:s1] += 0.9*np.sin(2*np.pi*55.0*kt)*np.exp(-kt/0.09)
    def snare(t0):
        s0 = int(t0*SR)
        if s0 >= n:
            return
        s1 = min(s0+int(0.35*SR), n)
        kt = np.arange(s1-s0)/SR
        nz = noise(0.35, seed=int(t0*100)+9000)[:s1-s0]
        out[s0:s1] += 0.8*nz*np.exp(-kt/0.03)
    def hat(t0):
        s0 = int(t0*SR)
        if s0 >= n:
            return
        s1 = min(s0+int(0.12*SR), n)
        kt = np.arange(s1-s0)/SR
        nz = noise(0.12, seed=int(t0*100)+5000)[:s1-s0]
        out[s0:s1] += 0.55*nz*np.exp(-kt/0.015)
    for b in np.arange(0.0, dur-0.6, 1.0):
        kick(float(b))
    for b in np.arange(0.5, dur-0.35, 1.0):
        snare(float(b))
    for b in np.arange(0.25, dur-0.12, 0.5):
        hat(float(b))
    out *= genv(n,0.02,0.03)
    return norm(out)

def main():
    ap = __import__(chr(97)+chr(114)+chr(103)+chr(112)+chr(97)+chr(114)+chr(115)+chr(101)).ArgumentParser(description='ZTame P8 stems')
    ap.add_argument('--out', default='artifacts/ztame-p8/stems')
    ap.add_argument('--dur', type=float, default=8.0)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    stems = {
        'resonant-ringing-1-4k-over-broadband.wav': stem1_ringing(args.dur),
        'plucked-string-upper-harmonics.wav': stem2_plucked(args.dur),
        'mix-drums-plus-sustained-pad.wav': stem3_drums_pad(args.dur),
    }
