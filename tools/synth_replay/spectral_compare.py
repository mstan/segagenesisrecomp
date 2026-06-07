#!/usr/bin/env python3
# [CHIP-TRACE] TEMP — spectral parity of ours vs theirs (per-stream).
# Compares average magnitude spectra of paired WAVs (phase-invariant), reports
# band energy ratios + log-spectral distance, to localize timbre/level errors.
import sys, wave, numpy as np

def load(p):
    w = wave.open(p, 'rb'); n = w.getnframes(); sr = w.getframerate()
    d = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float64)
    w.close()
    if w.getnchannels() == 2: d = d.reshape(-1, 2).mean(axis=1)
    return d, sr

def avg_spectrum(x, sr, nfft=4096):
    if len(x) < nfft: return None, None
    hop = nfft // 2; win = np.hanning(nfft)
    acc = np.zeros(nfft // 2 + 1); cnt = 0
    for s in range(0, len(x) - nfft, hop):
        seg = x[s:s+nfft] * win
        acc += np.abs(np.fft.rfft(seg)); cnt += 1
    acc /= max(cnt, 1)
    freqs = np.fft.rfftfreq(nfft, 1.0/sr)
    return acc, freqs

def report(name, ap, bp):
    oa, fr = avg_spectrum(*load(ap))
    ob, _  = avg_spectrum(*load(bp))
    n = min(len(oa), len(ob)); oa, ob, fr = oa[:n], ob[:n], fr[:n]
    eps = 1.0
    # band energy (ours/theirs) in octave-ish bands
    print(f"== {name}: ours={ap}  theirs={bp} ==")
    bands = [(0,500),(500,1000),(1000,2000),(2000,4000),(4000,8000),(8000,16000),(16000,30000)]
    print(f"  {'band(Hz)':>14} {'oursE':>10} {'theirsE':>10} {'ratio o/t':>10}")
    for lo,hi in bands:
        m = (fr>=lo)&(fr<hi)
        if not m.any(): continue
        eo = float(np.sqrt((oa[m]**2).mean())); et = float(np.sqrt((ob[m]**2).mean()))
        print(f"  {lo:6d}-{hi:6d} {eo:10.0f} {et:10.0f} {eo/(et+eps):10.2f}")
    lsd = float(np.sqrt(np.mean((20*np.log10((oa+eps)/(ob+eps)))**2)))
    print(f"  log-spectral distance = {lsd:.2f} dB  (0 = identical timbre)")

if __name__ == "__main__":
    import os
    d = sys.argv[1] if len(sys.argv) > 1 else "."
    report("FM",  os.path.join(d,"ours_fm.wav"),  os.path.join(d,"theirs_fm.wav"))
    report("PSG", os.path.join(d,"ours_psg.wav"), os.path.join(d,"theirs_psg.wav"))
