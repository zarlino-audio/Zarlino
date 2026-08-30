"""PresenceField Air LFO zipper/click numeric hearing test.
Replicates the EXACT shipped algorithm. Outputs measured verdict + WAVs."""
import numpy as np, wave

SR=44100.0; BLOCK=256; TWO_PI=2.0*np.pi
AIR_BASE=12000.0; AIR_SPAN=1500.0; SHAPE=1.0; SPEED=1.5; WIDTH=2.0
AIR_Q=3.5; AIR_GAIN_DB=3.0

def peak(f):
    g=10.0**(AIR_GAIN_DB/20.0); A=np.sqrt(g); w0=TWO_PI*f/SR
    al=np.sin(w0)/(2.0*AIR_Q); cw=np.cos(w0); a0=1.0+al/A
    return np.array([(1.0+al*A)/a0, (-2.0*cw)/a0, (1.0-al*A)/a0, (-2.0*cw)/a0, (1.0-al/A)/a0])

class Bi:
    def __init__(s): s.d1=0.0; s.d2=0.0; s.c=None
    def set(s,c): s.c=c
    def proc(s,x):
        b0,b1,b2,a1,a2=s.c
        y=b0*x+s.d1; s.d1=b1*x-a1*y+s.d2; s.d2=b2*x-a2*y; return y

def process(x, ahz, mode):
    n=len(x); out=np.empty(n); f=Bi(); step=0
    if mode=='slew':
        tau=300.0; alpha=1.0-np.exp(-1.0/tau); sm=ahz[0]
        for i in range(n):
            sm+=(ahz[i]-sm)*alpha; f.set(peak(sm)); out[i]=f.proc(x[i])
        return out
    for i in range(n):
        if mode=='stepped':
            if step==0: f.set(peak(ahz[i]))
        else: f.set(peak(ahz[i]))
        out[i]=f.proc(x[i]); step=(step+1)%BLOCK
    return out

def render(dur, kind):
    n=int(SR*dur); t=np.arange(n)/SR
    if kind=='1k': x=0.5*np.sin(TWO_PI*1000*t)
    elif kind=='10k': x=0.5*np.sin(TWO_PI*10000*t)
    elif kind=='12k': x=0.5*np.sin(TWO_PI*12000*t)
    else:
        f0=250.0; f1=18000.0; k=np.log(f1/f0)
        x=0.6*np.sin(TWO_PI*dur*f0*(np.exp(t/dur*k)-1.0)/k*SR/SR)
    lfo=TWO_PI*SPEED/SR; ph=np.arange(n)*lfo
    ahz=AIR_BASE+np.sin(ph)*AIR_SPAN*SHAPE
    return x, ahz

def meas(a, label):
    art=a[0]-a[1]
    rms=np.sqrt(np.mean(art*art)); peak_a=np.max(np.abs(art))
    sig_rms=np.sqrt(np.mean(a[0]*a[0]))
    db=rms_to_db(rms); dbsp=db-sig_to_db(sig_rms)
    return rms,peak_a,db,dbsp

def rms_to_db(r): return 20.0*np.log10(r+1e-12)
def sig_to_db(r): return 20.0*np.log10(r+1e-12)
