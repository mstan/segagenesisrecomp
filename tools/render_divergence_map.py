#!/usr/bin/env python3
"""TRUE render-differential measurement: continuous divergence map between the
recomp's ACTUAL FM render (mixer-driven, GENESIS_AUDIO_PREDRC .fm.s16) and a
CANONICAL render of the SAME write stream (synth_replay ours_fm.wav — push-order
+ stamp-advance, NO mixer sort/clamp/pairguard/spread). Peaks = where the mixer's
SCHEDULE diverges from canonical, over the whole run. Not jump-specific."""
import sys, wave, numpy as np
def read_wav(p):
    w=wave.open(p,"rb"); ch=w.getnchannels(); r=w.getframerate()
    d=np.frombuffer(w.readframes(w.getnframes()),dtype="<i2").astype(np.float32)
    w.close()
    if ch==2: d=d.reshape(-1,2).mean(1)
    return d,r
actual = np.fromfile(sys.argv[1],dtype="<i2").astype(np.float32).reshape(-1,2).mean(1); ar=53320
canon,cr = read_wav(sys.argv[2])
# resample both to a common 48k grid (envelope comparison is rate/phase tolerant)
def rs(x,src,dst): n=int(len(x)*dst/src); return np.interp(np.linspace(0,len(x),n,endpoint=False),np.arange(len(x)),x)
OUT=48000; a=rs(actual,ar,OUT); c=rs(canon,cr,OUT)
n=min(len(a),len(c)); a=a[:n]; c=c[:n]
W=int(OUT*0.05)  # 50ms windows
nb=n//W
def env(x): return np.array([np.sqrt(np.mean(x[i*W:(i+1)*W]**2)) for i in range(nb)])
ea,ec=env(a),env(c)
# divergence per window = RMS of (actual-canonical) envelope-normalized; also raw env delta
diff_env=np.abs(ea-ec)
print(f"windows={nb} ({nb*0.05:.0f}s)  actual_rms(med)={np.median(ea):.0f}  canon_rms(med)={np.median(ec):.0f}")
print(f"envelope |actual-canon|: median={np.median(diff_env):.0f}  p95={np.percentile(diff_env,95):.0f}  max={diff_env.max():.0f}")
order=np.argsort(diff_env)[::-1][:15]
print("top 15 divergence windows (mixer-schedule vs canonical):")
print("   t(s)   |dEnv|   actEnv  canEnv   ratio")
for i in sorted(order):
    t=i*0.05; ratio=ea[i]/(ec[i]+1) 
    print(f"  {t:6.2f}   {diff_env[i]:6.0f}   {ea[i]:6.0f}  {ec[i]:6.0f}   {ratio:5.2f}")
# is the divergence FLAT (mixer clean -> boop is DRC) or PEAKED (mixer diverges)?
print(f"\nVERDICT: {'PEAKED - mixer schedule diverges from canonical (boop likely here)' if diff_env.max() > 4*np.median(diff_env)+50 else 'FLAT - mixer render ~= canonical => boop is downstream (DRC resampler)'}")
