#!/usr/bin/env python3
"""Convert the Tier-3 pre-resample mixer dump (GENESIS_AUDIO_PREDRC) to listenable
WAVs, so the boop can be localized to the MIXER (present pre-DRC) vs the DRC
RESAMPLER (only post-DRC), and to FM vs PSG. Uses a SIMPLE linear resample (NOT
the DRC), so a boop surviving here is a mixer artifact, not a DRC one."""
import sys, wave, numpy as np
pfx = sys.argv[1] if len(sys.argv) > 1 else "predrc"
FM_RATE, PSG_RATE, OUT = 53320, 223920, 48000
def resample(x, src, dst):
    n = int(len(x) * dst / src)
    return np.interp(np.linspace(0, len(x), n, endpoint=False), np.arange(len(x)), x)
def wav(path, data, rate, ch):
    d = np.clip(data, -32768, 32767).astype("<i2")
    w = wave.open(path, "wb"); w.setnchannels(ch); w.setsampwidth(2); w.setframerate(rate)
    w.writeframes(d.tobytes()); w.close()
    print(f"  wrote {path}  ({len(d)//ch} frames @ {rate}Hz)")
fm = np.fromfile(f"{pfx}.fm.s16", dtype="<i2").astype(np.float32).reshape(-1,2)
psg = np.fromfile(f"{pfx}.psg.s16", dtype="<i2").astype(np.float32)
# FM-only (its native rate, playable)
wav(f"{pfx}_fm.wav", fm.reshape(-1), FM_RATE, 2)
# PSG-only downsampled to 48k mono
psg48 = resample(psg, PSG_RATE, OUT)
wav(f"{pfx}_psg.wav", psg48, OUT, 1)
# Combined mixer output through a SIMPLE resampler (both -> 48k stereo, summed)
fmL = resample(fm[:,0], FM_RATE, OUT); fmR = resample(fm[:,1], FM_RATE, OUT)
n = min(len(fmL), len(psg48))
mixL = fmL[:n] + psg48[:n]; mixR = fmR[:n] + psg48[:n]
mix = np.empty(n*2); mix[0::2]=mixL; mix[1::2]=mixR
wav(f"{pfx}_mix.wav", mix, OUT, 2)
print("Listen: if the boop is in *_mix.wav (or *_fm/_psg), it's the MIXER; if only in the shipped/DRC output, it's the RESAMPLER.")
