import wave, numpy as np, os

def load(path):
    w=wave.open(path,'rb'); n=w.getnframes(); sr=w.getframerate()
    a=np.frombuffer(w.readframes(n),dtype=np.int16).astype(np.float32)/32768.0
    return a.reshape(-1,w.getnchannels()),sr

# Verify the verse "before" has real dynamic musical content (envelope varies, no silence/constant)
a,sr = load('artifacts/ab-sources/zscorch/ab-review/SET3_edited_before_verse.wav')
m = a.mean(axis=1)
# amplitude envelope in 0.5s windows -> should vary over time
hop=int(sr*0.5); env=[np.sqrt(np.mean(m[i:i+hop]**2)) for i in range(0,len(m)-hop,hop)]
print("Verse BEFORE - RMS envelope over time (should rise/fall like a phrase):")
print("  ", [f"{e:.3f}" for e in env])
print(f"  min={min(env):.4f} max={max(env):.4f} spike_ratio={max(env)/max(min(env),1e-6):.1f}")

a2,sr2 = load('artifacts/ab-sources/zscorch/ab-review/SET3_edited_after_verse.wav')
m2=a2.mean(axis=1); env2=[np.sqrt(np.mean(m2[i:i+hop]**2)) for i in range(0,len(m2)-hop,hop)]
print("Verse AFTER - RMS envelope:")
print("  ", [f"{e:.3f}" for e in env2])

# confirm dry != proc (they are genuinely different files/content)
diff = np.sqrt(np.mean((m-m2)**2))
print(f"Dry-vs-Proced RMS diff (verse): {diff:.4f}")
