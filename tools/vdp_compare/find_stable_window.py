#!/usr/bin/env python3
"""
find_stable_window.py - read a BlastEm <prefix>.vdphash.csv (frame, vram, cram,
vsram FNV-1a-64 hashes, one row per frame) and report the longest run of
consecutive frames whose VRAM+VSRAM hashes are identical (a provably static
picture, modulo palette rotation in CRAM). Use this to pick a sync point.

Usage: python find_stable_window.py <prefix>.vdphash.csv [--min-run N]
"""
import sys, csv

def main():
    path = sys.argv[1]
    min_run = 8
    if "--min-run" in sys.argv:
        min_run = int(sys.argv[sys.argv.index("--min-run") + 1])
    rows = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            rows.append((int(r["frame"]), r["vram_fnv1a64"], r["cram_fnv1a64"], r["vsram_fnv1a64"]))
    if not rows:
        print("no rows"); return
    # group by (vram,vsram) identical runs
    runs = []
    s = 0
    for i in range(1, len(rows) + 1):
        if i == len(rows) or (rows[i][1], rows[i][3]) != (rows[s][1], rows[s][3]):
            runs.append((rows[s][0], rows[i-1][0], i - s,
                         rows[s][1] != rows[s][1],  # placeholder
                         len({r[2] for r in rows[s:i]})))  # distinct cram hashes in run
            s = i
    runs.sort(key=lambda x: -x[2])
    print(f"{len(rows)} frames, {len(runs)} VRAM+VSRAM-stable runs")
    print(f"{'start':>7} {'end':>7} {'len':>5} {'cram_states':>11}")
    for st, en, ln, _, ncram in runs[:15]:
        if ln >= min_run:
            mid = (st + en) // 2
            print(f"{st:>7} {en:>7} {ln:>5} {ncram:>11}   midframe={mid}")

if __name__ == "__main__":
    main()
