"""
chip_stream_diff.py — diff the FM/PSG register-WRITE STREAM between the native
(own-backend) and oracle (clownmdemu) builds.

The parity audit proved the Z80 sound driver + its 8KB RAM are identical between
the two builds, so they should issue the SAME FM/PSG writes. This tool checks
that directly, and is the decisive split for the audio bug:

  * Streams match in VALUE+ORDER  -> the writes are identical; the audible
    difference is the synth CORE (ymfm vs clownmdemu FM) and/or the per-scanline
    FM/PSG advance CADENCE. (See ISSUES.md (3b) write-collapse.)
  * Streams DIFFER in value/order -> the own backend drops/reorders/duplicates
    writes; the bug is upstream (Z80 timing, bus, per-scanline batching).
  * Streams match in value but the LINE timing differs -> writes land at
    different raster points within the frame (own per-scanline batch vs
    clownmdemu interleave) — the cadence suspect.

Input: two chip_ring.txt dumps (lines "f=N sl=L FM  pP $VV" / "f=N sl=L PSG $VV")
captured from each build at the same --snd-dump-frame N.

Usage:
  python tools/chip_stream_diff.py native_chip_ring.txt oracle_chip_ring.txt
"""
from __future__ import annotations
import re, sys
from collections import defaultdict

FM_RE  = re.compile(r"f=(\d+)\s+sl=(\d+)\s+FM\s+p(\d+)\s+\$([0-9A-Fa-f]+)")
PSG_RE = re.compile(r"f=(\d+)\s+sl=(\d+)\s+PSG\s+\$([0-9A-Fa-f]+)")


def load(path):
    """frame -> list of (line, kind, port, val) in capture order. kind 'F'/'P'."""
    frames = defaultdict(list)
    fm = psg = 0
    with open(path) as f:
        for ln in f:
            m = FM_RE.search(ln)
            if m:
                fr, sl, p, v = int(m[1]), int(m[2]), int(m[3]), int(m[4], 16)
                frames[fr].append((sl, "F", p, v)); fm += 1; continue
            m = PSG_RE.search(ln)
            if m:
                fr, sl, v = int(m[1]), int(m[2]), int(m[3], 16)
                frames[fr].append((sl, "P", 0, v)); psg += 1
    return frames, fm, psg


def main():
    if len(sys.argv) < 3:
        print(__doc__); return 2
    npath, opath = sys.argv[1], sys.argv[2]
    nf, n_fm, n_psg = load(npath)
    of, o_fm, o_psg = load(opath)
    print(f"native ({npath}): {len(nf)} frames, {n_fm} FM + {n_psg} PSG writes")
    print(f"oracle ({opath}): {len(of)} frames, {o_fm} FM + {o_psg} PSG writes")

    common = sorted(set(nf) & set(of))
    if not common:
        print("\nNO COMMON FRAMES — dumps don't overlap. Capture both at the same "
              "--snd-dump-frame N (both reach frame N; native faster, just wait).")
        return 2
    print(f"\ncommon frames: {len(common)}  [{common[0]}..{common[-1]}]")

    # Per-frame comparison of the ordered (kind,port,val) sequence (timing-agnostic).
    val_match = val_diff = 0
    count_mismatch = 0
    first_diffs = []
    line_abs_sum = 0; line_n = 0
    diff_frames = []
    for fr in common:
        seq_n = [(k, p, v) for (_, k, p, v) in nf[fr]]
        seq_o = [(k, p, v) for (_, k, p, v) in of[fr]]
        if seq_n == seq_o:
            val_match += 1
            # value-identical: measure line-timing skew write-by-write
            for (ln_, _, _, _), (lo_, _, _, _) in zip(nf[fr], of[fr]):
                line_abs_sum += abs(ln_ - lo_); line_n += 1
        else:
            val_diff += 1
            diff_frames.append(fr)
            if len(seq_n) != len(seq_o):
                count_mismatch += 1
            # first differing index
            for i, (a, b) in enumerate(zip(seq_n, seq_o)):
                if a != b:
                    first_diffs.append((fr, i, a, b, len(seq_n), len(seq_o)))
                    break
            else:
                first_diffs.append((fr, min(len(seq_n), len(seq_o)),
                                    None, None, len(seq_n), len(seq_o)))

    print("\n" + "=" * 70)
    print("CHIP-STREAM VERDICT (native own-backend vs oracle clownmdemu)")
    print("=" * 70)
    pct = 100.0 * val_match / len(common)
    print(f"frames with IDENTICAL write value+order : {val_match}/{len(common)} ({pct:.1f}%)")
    print(f"frames that DIFFER                       : {val_diff}"
          f"  (of which {count_mismatch} have a different write COUNT)")
    if line_n:
        print(f"line-timing skew on value-identical frames: mean |Δline| = "
              f"{line_abs_sum/line_n:.2f} scanlines over {line_n} writes")

    if val_diff:
        print(f"\nfirst-divergence per differing frame (showing up to 20):")
        print(f"  {'frame':>6} {'idx':>5} {'native(k,p,v)':>16} {'oracle(k,p,v)':>16}"
              f"  {'nN':>5} {'oN':>5}")
        for fr, i, a, b, ln_, lo_ in first_diffs[:20]:
            print(f"  {fr:>6} {i:>5} {str(a):>16} {str(b):>16}  {ln_:>5} {lo_:>5}")
        # per-frame write-count delta distribution
        deltas = defaultdict(int)
        for fr in diff_frames:
            deltas[len(nf[fr]) - len(of[fr])] += 1
        print(f"\n  per-frame (native_writes - oracle_writes) histogram:")
        for d in sorted(deltas):
            print(f"    delta={d:+5d} : {deltas[d]} frames")

    print("\nINTERPRETATION:")
    if val_diff == 0:
        print("  Streams are VALUE+ORDER IDENTICAL. The writes the chips receive match")
        print("  the clownmdemu reference exactly -> the audible difference is the synth")
        print("  CORE (ymfm vs clownmdemu FM) and/or per-scanline advance CADENCE, NOT a")
        print("  dropped/reordered write. Check line-timing skew above; pursue ISSUES.md")
        print("  (3b) per-scanline write-collapse + the FM core floor.")
    else:
        print(f"  Streams DIVERGE on {val_diff} frames -> the own backend is not issuing the")
        print("  same FM/PSG writes as clownmdemu despite identical driver state. Bug is")
        print("  upstream in the own backend (Z80 step cadence / bus port routing /")
        print("  per-scanline batching). Inspect the first-divergence rows above.")
    return 0 if val_diff == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
