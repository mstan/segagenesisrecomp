#!/usr/bin/env python3
"""
wav_boop_scan.py - offline audio-artifact scanner for runner --wav captures.

Independent of the live [BOOP] detector (runner/audio/observability.c):
different thresholds, different criteria, so it cross-checks rather than
repeats the in-process logic. Works on any capture from any game/backend.

Modes (combinable):
  scan  : single-WAV anomaly scan
            - click scan: per-window peak |sample delta| vs global
              percentile baseline AND absolute floor
            - energy scan: per-window RMS bursts that appear and vanish
              within a few windows (boop-shaped: short foreign tone)
  diff  : two-WAV sample-exact comparison (deterministic runs should be
          bit-identical; any divergence is localized and frame-mapped)

Frame mapping is proportional: frame = sample_index * total_frames /
total_samples. Pass --frames with the run's --max-frames value.

Usage:
  python wav_boop_scan.py scan  capture.wav --frames 15000
  python wav_boop_scan.py diff  a.wav b.wav --frames 15000
"""
import argparse
import struct
import sys


def read_wav(path):
    """Minimal RIFF reader -> (rate, channels, int16 interleaved bytes)."""
    with open(path, "rb") as f:
        riff = f.read(12)
        if riff[:4] != b"RIFF" or riff[8:12] != b"WAVE":
            sys.exit(f"{path}: not a RIFF/WAVE file")
        rate = channels = bits = None
        data = None
        while True:
            hdr = f.read(8)
            if len(hdr) < 8:
                break
            cid, size = hdr[:4], struct.unpack("<I", hdr[4:])[0]
            if cid == b"fmt ":
                fmt = f.read(size)
                channels, rate = struct.unpack("<HI", fmt[2:8])
                bits = struct.unpack("<H", fmt[14:16])[0]
            elif cid == b"data":
                # Header may carry a stale/zero size (interrupted run):
                # chunk-read to EOF rather than trusting the declared size.
                chunks = []
                while True:
                    c = f.read(1 << 24)
                    if not c:
                        break
                    chunks.append(c)
                data = b"".join(chunks)
                if size and len(data) > size:
                    data = data[:size]
                break
            else:
                f.seek(size, 1)
        if data is None or rate is None:
            sys.exit(f"{path}: missing fmt/data chunk")
        if bits != 16:
            sys.exit(f"{path}: expected 16-bit PCM, got {bits}")
        return rate, channels, data


def to_frames_per_window(n_samples, channels, total_frames):
    return n_samples / channels / max(total_frames, 1)


def scan(path, total_frames, win_frames=1):
    import array
    rate, channels, data = read_wav(path)
    samples = array.array("h")
    samples.frombytes(data[: len(data) // 2 * 2])
    n = len(samples) // channels  # interleaved frame count
    spf = n / max(total_frames, 1)  # samples (frames) per wall frame
    print(f"{path}: rate={rate} ch={channels} pcm_frames={n} "
          f"wall_frames={total_frames} samples/frame={spf:.1f}")

    win = max(1, int(spf * win_frames))
    n_win = n // win

    # Pass 1: per-window peak |delta| (max over channels) and RMS energy.
    peak = [0] * n_win
    energy = [0.0] * n_win
    prev = [0] * channels
    for w in range(n_win):
        base = w * win * channels
        pk = 0
        acc = 0
        for i in range(win):
            for c in range(channels):
                s = samples[base + i * channels + c]
                d = s - prev[c]
                if d < 0:
                    d = -d
                if d > pk:
                    pk = d
                acc += s * s
                prev[c] = s
        peak[w] = pk
        energy[w] = (acc / (win * channels)) ** 0.5
    # Baselines: global percentiles (robust to music structure).
    sp = sorted(peak)
    se = sorted(energy)
    p50, p95, p999 = sp[len(sp)//2], sp[int(len(sp)*0.95)], sp[int(len(sp)*0.999)]
    e50, e95 = se[len(se)//2], se[int(len(se)*0.95)]
    print(f"  peak|d|: p50={p50} p95={p95} p99.9={p999}   "
          f"rms: p50={e50:.0f} p95={e95:.0f}")

    findings = []
    # Click criterion: way above the 95th percentile and absolutely large.
    click_floor = max(4 * p95, 8000)
    for w, pk in enumerate(peak):
        if pk > click_floor:
            findings.append((w, "click", f"peak|d|={pk} (p95={p95})"))
    # Boop criterion: short energy burst — window RMS far above both
    # neighbors a few windows away (foreign tone appears then vanishes).
    for w in range(4, n_win - 4):
        e = energy[w]
        if e < max(3 * e95, 500):
            continue
        before = max(energy[w - 4], energy[w - 3])
        after = max(energy[w + 3], energy[w + 4])
        if e > 4 * max(before, 1.0) and e > 4 * max(after, 1.0):
            findings.append((w, "burst", f"rms={e:.0f} before={before:.0f} after={after:.0f}"))

    if not findings:
        print("  CLEAN: no clicks or isolated bursts found")
    for w, kind, detail in findings[:200]:
        frame = w * win_frames
        t = w * win / rate
        print(f"  [{kind.upper()}] wall_frame~{frame} t={t:.2f}s  {detail}")
    if len(findings) > 200:
        print(f"  ... {len(findings) - 200} more")
    return findings


def diff(path_a, path_b, total_frames):
    rate_a, ch_a, a = read_wav(path_a)
    rate_b, ch_b, b = read_wav(path_b)
    if (rate_a, ch_a) != (rate_b, ch_b):
        sys.exit(f"format mismatch: {rate_a}/{ch_a} vs {rate_b}/{ch_b}")
    n = min(len(a), len(b))
    if len(a) != len(b):
        print(f"LENGTH MISMATCH: {len(a)} vs {len(b)} bytes "
              f"(comparing first {n})")
    if a[:n] == b[:n]:
        print(f"IDENTICAL: {n} bytes ({n // 4 // max(rate_a,1)}s) — "
              f"runs are sample-exact deterministic")
        return []
    # Localize differing regions, map to wall frames.
    spf_bytes = (n / max(total_frames, 1))  # bytes per wall frame
    regions = []
    i = 0
    chunk = 1 << 16
    while i < n:
        ca, cb = a[i:i+chunk], b[i:i+chunk]
        if ca != cb:
            j = next(k for k in range(len(ca)) if ca[k] != cb[k])
            start = i + j
            # extend to end of differing run (coarse: chunk granularity)
            end = start
            while end < n and a[end:end+chunk] != b[end:end+chunk]:
                end += chunk
            regions.append((start, min(end, n)))
            i = end
        else:
            i += chunk
    print(f"{len(regions)} differing region(s):")
    for start, end in regions[:50]:
        f0 = int(start / spf_bytes)
        f1 = int(end / spf_bytes)
        print(f"  bytes [{start}, {end})  wall_frames ~[{f0}, {f1}]")
    return regions


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["scan", "diff"])
    ap.add_argument("wav", nargs="+")
    ap.add_argument("--frames", type=int, required=True,
                    help="wall frames in the capture (--max-frames value)")
    args = ap.parse_args()
    if args.mode == "scan":
        for p in args.wav:
            scan(p, args.frames)
    else:
        if len(args.wav) != 2:
            sys.exit("diff mode needs exactly two WAVs")
        diff(args.wav[0], args.wav[1], args.frames)


if __name__ == "__main__":
    main()
