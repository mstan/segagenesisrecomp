"""
wav_vint_align_diff.py — VINT-ALIGNED audio A/B between two static-recompiled
builds whose attract demos run at slightly different frame rates.

A single cross-correlation offset can't align the two WAVs because the
frame-rate offset *varies* across the run (V-int/title-timer drift). But both
builds are deterministic static recompiles running the SAME attract demo, so
they pass through the SAME vint_runcount ($FFFE0C) values — just at different
wall frames. The --framelog of each build records `fcnt=` (vint) per frame, and
the WAV is ~constant samples/frame, so we can build a vint->sample map for each
dump and resample BOTH onto a common vint axis. Then audio at vint V on side A
is compared to audio at vint V on side B — same game-logic moment, content
matched.

Usage:
  python tools/wav_vint_align_diff.py A.wav A.flog B.wav B.flog [--lo-khz 2 --hi-khz 6]

Reports, per matched vint window: overall RMS and 2-6 kHz (SFX-band) RMS for each
side, their ratio, and an aggregate of where one side is systematically quieter.
"""
from __future__ import annotations
import sys, re, wave, argparse
import numpy as np
from scipy import signal

FLOG_RE = re.compile(r"^F(\d+)\b.*?fcnt=([0-9A-Fa-f]{8})")


def load_wav_mono(path):
    with wave.open(path, "rb") as w:
        ch, sw, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    assert sw == 2, f"{path}: expected 16-bit"
    a = np.frombuffer(raw, dtype="<i2").astype(np.float32)
    if ch == 2:
        a = a.reshape(-1, 2).mean(axis=1)
    return a / 32768.0, rate


def frame_to_vint(flog):
    """Ordered list of (frame, vint) from a --framelog. Frames are sequential
    from 0; vint is the longword fcnt (wraps handled by treating as unsigned)."""
    out = []
    with open(flog) as f:
        for ln in f:
            m = FLOG_RE.match(ln)
            if m:
                out.append((int(m.group(1)), int(m.group(2), 16)))
    out.sort()
    return out


def vint_to_sample(flog, wav_n):
    """Map vint -> first WAV sample index for that vint, assuming ~constant
    samples/frame over the captured run."""
    fv = frame_to_vint(flog)
    if not fv:
        raise SystemExit(f"{flog}: no 'F#### ... fcnt=' lines parsed")
    max_frame = fv[-1][0]
    spf = wav_n / (max_frame + 1)          # samples per frame
    v2s = {}
    for frame, vint in fv:
        if vint not in v2s:                # first frame this vint appears
            v2s[vint] = int(frame * spf)
    return v2s, spf


def band_rms(sig, rate, lo, hi):
    sos = signal.butter(4, [lo, hi], btype="band", fs=rate, output="sos")
    return signal.sosfiltfilt(sos, sig)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a_wav"); ap.add_argument("a_flog")
    ap.add_argument("b_wav"); ap.add_argument("b_flog")
    ap.add_argument("--lo-khz", type=float, default=2.0)
    ap.add_argument("--hi-khz", type=float, default=6.0)
    ap.add_argument("--label-a", default="A(native)")
    ap.add_argument("--label-b", default="B(goldref)")
    args = ap.parse_args()

    a, ra = load_wav_mono(args.a_wav)
    b, rb = load_wav_mono(args.b_wav)
    assert ra == rb, "sample-rate mismatch"
    av2s, aspf = vint_to_sample(args.a_flog, len(a))
    bv2s, bspf = vint_to_sample(args.b_flog, len(b))
    print(f"[align] {args.label_a}: {len(a)} samp, {aspf:.1f} samp/frame, vints {min(av2s)}..{max(av2s)}")
    print(f"[align] {args.label_b}: {len(b)} samp, {bspf:.1f} samp/frame, vints {min(bv2s)}..{max(bv2s)}")

    # Pre-filter both to the SFX band once.
    lo, hi = args.lo_khz * 1000, args.hi_khz * 1000
    a_band = band_rms(a, ra, lo, hi)
    b_band = band_rms(b, rb, lo, hi)

    common = sorted(set(av2s) & set(bv2s))
    common = [v for v in common if v > 0]
    if len(common) < 8:
        print(f"[align] only {len(common)} common vints — framelogs barely overlap?")
    # Compare per a window of W vints (so each window has enough samples).
    W = 6
    chunk = int(np.median([aspf, bspf])) * W   # samples per W-vint window
    rows = []
    for i in range(0, len(common) - W, W):
        v0 = common[i]
        sa, sb = av2s[v0], bv2s[v0]
        if sa + chunk > len(a) or sb + chunk > len(b):
            continue
        ar = np.sqrt(np.mean(a[sa:sa+chunk] ** 2))
        br = np.sqrt(np.mean(b[sb:sb+chunk] ** 2))
        afb = np.sqrt(np.mean(a_band[sa:sa+chunk] ** 2))
        bfb = np.sqrt(np.mean(b_band[sb:sb+chunk] ** 2))
        rows.append((v0, ar, br, afb, bfb))

    if not rows:
        print("[align] no comparable windows."); return 2

    import numpy as _np
    A = _np.array(rows)
    vints, ar, br, afb, bfb = A[:,0], A[:,1], A[:,2], A[:,3], A[:,4]
    print(f"\n=== {len(rows)} vint-aligned windows ({W} vints each), SFX band {args.lo_khz:.0f}-{args.hi_khz:.0f} kHz ===")
    print(f"overall RMS:  {args.label_a} mean={ar.mean():.4f}   {args.label_b} mean={br.mean():.4f}   ratio A/B={ar.mean()/ (br.mean()+1e-9):.3f}")
    print(f"SFX-band RMS: {args.label_a} mean={afb.mean():.4f}   {args.label_b} mean={bfb.mean():.4f}   ratio A/B={afb.mean()/(bfb.mean()+1e-9):.3f}")
    # envelope correlation (content-match sanity): if aligned right + same content, high.
    def corr(x,y):
        x=x-x.mean(); y=y-y.mean(); d=_np.sqrt((x*x).sum()*(y*y).sum()); return float((x*y).sum()/d) if d>0 else 0.0
    print(f"overall-RMS envelope corr (A vs B over vint): {corr(ar,br):.3f}   SFX-band corr: {corr(afb,bfb):.3f}")

    # Windows where the REFERENCE genuinely has SFX-band energy but native is
    # much quieter — the real "faint/missing SFX" moments (ignore both-silent).
    thresh = float(_np.percentile(bfb, 75))   # "B has real SFX here"
    ratio = afb / (bfb + 1e-6)
    cand = [k for k in range(len(rows)) if bfb[k] >= thresh]
    cand.sort(key=lambda k: ratio[k])
    print(f"\ntop 12 vint windows where {args.label_b} has strong SFX (>= p75={thresh:.4f}) "
          f"but {args.label_a} is much quieter:")
    print(f"  {'vint':>6} {'A_sfx':>9} {'B_sfx':>9} {'A/B':>7}")
    for k in cand[:12]:
        print(f"  {int(vints[k]):>6} {afb[k]:>9.4f} {bfb[k]:>9.4f} {ratio[k]:>7.3f}")
    # How many strong-SFX windows is native <50% / <25% of the reference?
    strong = _np.array([k for k in range(len(rows)) if bfb[k] >= thresh])
    if len(strong):
        r = ratio[strong]
        print(f"\nof {len(strong)} strong-SFX windows: {(r<0.5).sum()} have native <50% of ref, "
              f"{(r<0.25).sum()} <25%  (median A/B={_np.median(r):.3f})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
