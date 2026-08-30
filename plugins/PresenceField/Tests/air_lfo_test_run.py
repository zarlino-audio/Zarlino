"""Runner: verifies PresenceField Air LFO coefficient-stepping zipper/click risk."""
from pf_model import *
from pathlib import Path

OUT = Path("plugins/PresenceField/Tests/renders"); OUT.mkdir(parents=True, exist_ok=True)
DUR = 2.0  # two full LFO cycles (LFO period ~0.67s at speed 1.5)

def write_wav(path, sig, sr):
    s=np.int16(np.clip(sig*0.9,-1,1)*32767)
    with wave.open(str(path),'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(s.tobytes())

results=[]
print("=== PresenceField Air LFO zipper/click test ===")
print("Conditions: Air Drift Speed=1.5, Stereo Width=200%, Presence Field=100%")
print("Air Q=3.5 +3dB peak, airHz=12000+sin*1500*1.0 (=10500-13500Hz)\n")
for kind in ['1k','10k','12k','sweep']:
    x,ahz=render(DUR,kind)
    st=process(x,ahz,'stepped')   # shipped: 256-sample coeff snap
    cl=process(x,ahz,'per_sample')# reference: no coefficient stepping
    sl=process(x,ahz,'slew')      # fix candidate: slewed centre
    rms,pk,db,dbsp=meas([st,cl],kind)
    rms2,pk2,db2,dbsp2=meas([sl,cl],kind)
    # mask: only consider artifact energy in the air band (8-20k) vs signal
    verdict="PASS" if dbsp < -70 else ("MARGINAL" if dbsp < -55 else "FAIL")
    results.append((kind,dbsp,dbsp2,verdict))
    write_wav(OUT/f"air_{kind}_stepped.wav",st,SR)
    write_wav(OUT/f"air_{kind}_per_sample.wav",cl,SR)
    write_wav(OUT/f"air_{kind}_slewed.wav",sl,SR)
    print(f"[{kind:6s}] signal RMS={sig_to_db(np.sqrt(np.mean(st*st))):6.1f} dBFS | "
          f"artifact(stepped) rel={dbsp:6.1f} dB | artifact(slewed) rel={dbsp2:6.1f} dB -> {verdict}")

print("\nVERDICT SUMMARY")
for k,db,db2,v in results:
    print(f"  {k:6s}: stepped artifact {db:6.1f} dB vs signal (slewed fix: {db2:6.1f} dB) -> {v}")
w=1 if any(v!='PASS' for _,_,_,v in results) else 0
print("RESULT:", "FAIL (audible zipper/click present)" if w else "PASS (no audible artifact)")
print("WAVs:", [f.name for f in sorted(OUT.glob('*.wav'))])
