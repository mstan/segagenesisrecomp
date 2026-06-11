#!/usr/bin/env python3
"""
wav_band_compare.py — banded spectral A/B between two runner --wav captures
(native vs oracle), the offline comparator behind the S1 "GHZ bass boop" hunt.

Method (matches the original ad-hoc analysis that found the boop):
  1. Load both WAVs mono (int16 scale, no normalization).
  2. Align: shift side B by --align-frames wall frames (B = A + K). With
     --auto-align, pick K in [-60, 60] maximizing full-band envelope
     correlation, then report it.
  3. Decimate 8x (anti-aliased) -> ~27965 Hz.
  4. STFT: 4096-pt FFT, 2048 hop, Hann window (~146 ms per window).
  5. 8 log-spaced bands across [60, 12000] Hz; per window per band,
     energy = sqrt(mean |X|^2) over the band's bins.
  6. Flag windows where A > RATIO * B + FLOOR in any band (defaults 3.0 /
     50000 — the thresholds that flagged the original wf 1132/1240/1348/1456
     train). Window positions are reported in wall frames (sample-rate
     proportional, --fps frames per second of capture, default 60).

Usage:
  python wav_band_compare.py native.wav oracle.wav --align-frames 13
  python wav_band_compare.py a.wav b.wav --auto-align [--ratio 3 --floor 50000]
"""
from __future__ import annotations
import argparse, sys, wave
import numpy as np


def load_wav_mono(path):
    with wave.open(path, "rb") as w:
        ch, sw, rate, n = (w.getnchannels(), w.getsampwidth(),
                           w.getframerate(), w.getnframes())
        raw = w.readframes(n)
    assert sw == 2, f"{path}: expected 16-bit PCM"
    a = np.frombuffer(raw, dtype="<i2").astype(np.float64)
    if ch == 2:
        a = a.reshape(-1, 2).mean(axis=1)
    return a, rate


def decimate8(x):
    try:
        from scipy import signal
        return signal.decimate(x, 8, ftype="fir", zero_phase=True)
    except ImportError:
        # Fallback: simple 8-sample boxcar average (mild aliasing; fine for
        # the <2 kHz bands this tool exists to compare).
        n = len(x) // 8 * 8
        return x[:n].reshape(-1, 8).mean(axis=1)


def band_energies(x, rate, nfft=4096, hop=2048, nbands=8, flo=60.0, fhi=12000.0):
    """-> (energies[window, band], window_center_sample_at_decimated_rate)"""
    win = np.hanning(nfft)
    edges = np.geomspace(flo, min(fhi, rate / 2 * 0.99), nbands + 1)
    freqs = np.fft.rfftfreq(nfft, 1.0 / rate)
    masks = [(freqs >= edges[i]) & (freqs < edges[i + 1]) for i in range(nbands)]
    rows, centers = [], []
    for s in range(0, len(x) - nfft, hop):
        X = np.abs(np.fft.rfft(x[s:s + nfft] * win))
        rows.append([np.sqrt((X[m] ** 2).mean()) if m.any() else 0.0 for m in masks])
        centers.append(s + nfft // 2)
    return np.array(rows), np.array(centers), edges


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wav_a", help="side A (native)")
    ap.add_argument("wav_b", help="side B (oracle / reference)")
    ap.add_argument("--align-frames", type=int, default=0,
                    help="B's content leads A by this many wall frames (B = A + K)")
    ap.add_argument("--auto-align", action="store_true",
                    help="search K in [-60,60] by full-band envelope correlation")
    ap.add_argument("--ratio", type=float, default=3.0)
    ap.add_argument("--floor", type=float, default=50000.0)
    ap.add_argument("--fps", type=float, default=60.0)
    ap.add_argument("--drift-windows", type=int, default=0, metavar="J",
                    help="tolerate alignment drift: compare each A window "
                         "against the MAX of B windows within +/-J (two "
                         "deterministic-but-independent runs drift apart over "
                         "time; a global offset can't track it)")
    args = ap.parse_args()

    a, ra = load_wav_mono(args.wav_a)
    b, rb = load_wav_mono(args.wav_b)
    assert ra == rb, f"sample-rate mismatch: {ra} vs {rb}"
    spf = ra / args.fps  # samples per wall frame

    k = args.align_frames
    if args.auto_align:
        # frame-granular envelope on the raw streams
        nfa, nfb = int(len(a) // spf), int(len(b) // spf)
        ea = np.array([np.sqrt((a[int(i*spf):int((i+1)*spf)] ** 2).mean()) for i in range(nfa)])
        eb = np.array([np.sqrt((b[int(i*spf):int((i+1)*spf)] ** 2).mean()) for i in range(nfb)])
        best, bestc = 0, -2.0
        for kk in range(-60, 61):
            lo_a, lo_b = max(0, -kk), max(0, kk)
            n = min(len(ea) - lo_a, len(eb) - lo_b)
            if n < 100:
                continue
            c = np.corrcoef(ea[lo_a:lo_a + n], eb[lo_b:lo_b + n])[0, 1]
            if c > bestc:
                best, bestc = kk, c
        k = best
        print(f"auto-align: B = A + {k} frames (envelope corr {bestc:.4f})")

    # apply alignment by trimming whole samples
    off = int(round(k * spf))
    if off >= 0:
        b = b[off:]
    else:
        a = a[-off:]
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]

    da, db = decimate8(a), decimate8(b)
    rdec = ra / 8.0
    ea, centers, edges = band_energies(da, rdec)
    eb, _, _ = band_energies(db, rdec)
    nw = min(len(ea), len(eb))
    ea, eb, centers = ea[:nw], eb[:nw], centers[:nw]
    wf = centers * 8.0 / spf  # window center in side-A wall frames

    print(f"{nw} windows of {4096/rdec*1000:.0f} ms; bands (Hz): "
          + " ".join(f"{edges[i]:.0f}-{edges[i+1]:.0f}" for i in range(8)))
    print(f"== per-band totals (A/B ratio over full run) ==")
    for i in range(8):
        ta, tb = ea[:, i].sum(), eb[:, i].sum()
        print(f"  {edges[i]:7.0f}-{edges[i+1]:7.0f} Hz: A={ta:12.0f} B={tb:12.0f} "
              f"ratio={ta/(tb+1):.2f}")

    j = args.drift_windows
    flags = []
    for w in range(nw):
        lo, hi = max(0, w - j), min(nw, w + j + 1)
        for i in range(8):
            ref = eb[lo:hi, i].max()
            if ea[w, i] > args.ratio * ref + args.floor:
                flags.append((w, i, ref))
    print(f"== flags (A > {args.ratio:.1f}*B + {args.floor:.0f}"
          + (f", B = max within +/-{j} windows" if j else "") + ") ==")
    if not flags:
        print("  NONE")
    else:
        for w, i, ref in flags:
            print(f"  wf~{wf[w]:6.0f}  band {edges[i]:.0f}-{edges[i+1]:.0f} Hz  "
                  f"A={ea[w, i]:.0f} B={ref:.0f} ratio={ea[w, i]/(ref+1):.2f}")
    print(f"{len(flags)} flagged window-bands")
    return 0 if not flags else 2


if __name__ == "__main__":
    sys.exit(main())
