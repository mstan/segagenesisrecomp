#!/usr/bin/env python3
"""
cycle_delta_compare.py - Axis-2 (cycle/timing) Delta-anchor comparator.

Validates the recomp's per-instruction cycle accounting against die-accurate
BlastEm, OFFSET-INDEPENDENTLY. Absolute cycle origins differ between the two
binaries; we compare the Delta-cycles BETWEEN consecutive *same* chip-register
writes, which cancels the offset (the PSX cycle_compare.py method).

Cross-comparable anchor = FM/PSG register WRITES. Both backends run the same
deterministic boot+title program and emit the same (chip,port,value) write
sequence; the cycle GAP between consecutive writes reflects the CPU work done
in between.

CYCLE DOMAIN RECONCILIATION (both sides are 68000 MASTER cycles, scale = 1):

  recomp  chip_ring.txt  "mc=" :
     68K-origin writes  -> g_audio_cycle_counter*7 + g_68k_stamp_rebase
                           (68K cycles this wall-frame * 7  ==> master cycles)
     Z80-origin writes  -> machine_z80_stamp()  (master cycles, raster cursor)
     => PER-FRAME master cycles (resets each wall frame, range 0..~896040).
        Reconstruct monotonic time with the "wf=" wall-frame counter.

  BlastEm  .writes.bin  "abs_cycle" :
     m68k/YM/PSG share one master clock; oracle_abs_cycle() folds every
     sync_components rebase deduction back in => MONOTONIC master cycles.
     YM writes are tagged with ym->current_cycle (quantized UP to the next YM
     sample boundary, ~ clock_inc master cycles of jitter); PSG with psg->cycles.

  Scale factor recomp->blastem = 1.0 (both master cycles). The only conversions
  are: (a) fold recomp per-frame mc into monotonic via wf, (b) tolerate the YM
  sample-boundary quantization jitter by working on DELTAS over many cycles.

NTSC frame = 3420 master cycles/line * 262 lines = 896040 master cycles.
"""
import sys, os, struct, argparse
import numpy as np
from difflib import SequenceMatcher

NTSC_FRAME_MC = 3420 * 262   # 896040

# ----------------------------------------------------------------------------
def parse_recomp(path):
    """Parse chip_ring.txt -> structured arrays, in capture (chronological) order.
    Lines:
      f=99 sl=225 FM  p0 $2A mc=771120 wf=161 pcz=$00C5
      f=322 sl=181 PSG $C0 mc=620802 wf=... pcz=...
    """
    keys = []   # (chip, port, value)
    mc   = []   # per-frame master cycles
    wf   = []   # wall frame
    sl   = []
    vint = []
    pcz  = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split()
            # parts[0]=f=.. parts[1]=sl=.. parts[2]=FM/PSG
            d = {}
            try:
                d['f']  = int(parts[0][2:])
                d['sl'] = int(parts[1][3:])
            except (ValueError, IndexError):
                continue
            chip_tok = parts[2]
            if chip_tok == 'FM':
                # FM  pN $XX mc=.. wf=.. pcz=..
                port = int(parts[3][1:])           # pN -> N
                val  = int(parts[4][1:], 16)       # $XX
                chip = 0
                rest = parts[5:]
            elif chip_tok == 'PSG':
                # PSG $XX mc=.. wf=.. pcz=..
                port = 0
                val  = int(parts[3][1:], 16)
                chip = 1
                rest = parts[4:]
            else:
                continue
            mcv = wfv = pczv = 0
            for tok in rest:
                if tok.startswith('mc='):
                    mcv = int(tok[3:])
                elif tok.startswith('wf='):
                    wfv = int(tok[3:])
                elif tok.startswith('pcz=$'):
                    pczv = int(tok[5:], 16)
            keys.append((chip, port, val))
            mc.append(mcv); wf.append(wfv); sl.append(d['sl'])
            vint.append(d['f']); pcz.append(pczv)
    return {
        'keys': keys,
        'mc': np.array(mc, dtype=np.int64),
        'wf': np.array(wf, dtype=np.int64),
        'sl': np.array(sl, dtype=np.int64),
        'vint': np.array(vint, dtype=np.int64),
        'pcz': np.array(pcz, dtype=np.int64),
    }

def parse_blastem_writes(path):
    """Parse <prefix>.writes.bin -> structured arrays (chronological).
    header(32): magic[8] 'BLEMOWRT', u32 version, u32 tap(2), u32 entry_size(16),
                u32 reserved, u64 total_written
    entry(16):  u64 abs_cycle; u8 chip; u8 port; u8 value; u8 pad; u32 pad
    """
    with open(path, 'rb') as f:
        hdr = f.read(32)
        data = f.read()
    magic = hdr[:8]
    version, tap, esz, _ = struct.unpack('<IIII', hdr[8:24])
    total = struct.unpack('<Q', hdr[24:32])[0]
    assert magic == b'BLEMOWRT', f"bad magic {magic!r}"
    assert esz == 16, f"unexpected entry size {esz}"
    n = len(data) // esz
    keys = []
    abs  = np.empty(n, dtype=np.int64)
    for i in range(n):
        ac, chip, port, val = struct.unpack_from('<QBBB', data, i*16)
        abs[i] = ac
        keys.append((chip, port, val))
    return {'keys': keys, 'abs': abs, 'total': total}

# ----------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--recomp', default='chip_ring.txt')
    ap.add_argument('--blastem', default='blastem.writes.bin')
    ap.add_argument('--worst', type=int, default=25, help='how many worst intervals to print')
    args = ap.parse_args()

    print("=== Axis-2 Delta-anchor cycle comparator ===")
    R = parse_recomp(args.recomp)
    B = parse_blastem_writes(args.blastem)
    nr, nb = len(R['keys']), len(B['keys'])
    print(f"recomp  writes : {nr:>8}  (wall frames {R['wf'].min()}..{R['wf'].max()}, "
          f"vint {R['vint'].min()}..{R['vint'].max()})")
    print(f"blastem writes : {nb:>8}  (total produced {B['total']})")

    # composition
    def compo(keys):
        from collections import Counter
        c = Counter((k[0], k[1]) for k in keys)
        return {f"{'FM' if cp==0 else 'PSG'}p{pt}": n for (cp, pt), n in sorted(c.items())}
    print(f"recomp  by chip/port : {compo(R['keys'])}")
    print(f"blastem by chip/port : {compo(B['keys'])}")

    # --- ALIGN the two (chip,port,value) token sequences -----------------
    # difflib finds the matching blocks (longest-common-subsequence based);
    # robust to the recomp stream being a sub-window of blastem's (boot skipped).
    print("\n[align] running SequenceMatcher (this can take ~1 min)...")
    sm = SequenceMatcher(None, R['keys'], B['keys'], autojunk=False)
    blocks = sm.get_matching_blocks()   # list of (i, j, size)
    matched = sum(bk.size for bk in blocks)
    print(f"[align] matched {matched}/{nr} recomp writes "
          f"({100.0*matched/max(1,nr):.1f}% of recomp; "
          f"{100.0*matched/max(1,nb):.1f}% of blastem) across "
          f"{sum(1 for b in blocks if b.size>0)} blocks")
    print(f"[align] NOTE low recomp%% just means recomp's window has more writes "
          f"than blastem's {nb}; the % of blastem matched is the alignment quality.")
    big = sorted([b for b in blocks if b.size > 0], key=lambda b: -b.size)[:5]
    print(f"[align] largest blocks (size @ recomp_i->blastem_j): "
          + ", ".join(f"{b.size}@{b.a}->{b.b}" for b in big))

    # recomp monotonic abs (master cycles) = wf*FRAME + mc  (for cross-frame ctx)
    r_abs_mono = R['wf'] * NTSC_FRAME_MC + R['mc']

    # --- gross frame-Delta CALIBRATION ----------------------------------
    # For matched anchors, take blastem abs at the FIRST matched anchor of each
    # recomp wall frame; consecutive diffs = blastem master-cycles per recomp
    # wall frame. Should be ~ NTSC_FRAME_MC (896040) if pacing is calibrated.
    first_anchor = {}   # wf -> (b_abs)
    for bk in blocks:
        for k in range(bk.size):
            ri, bj = bk.a + k, bk.b + k
            w = int(R['wf'][ri])
            if w not in first_anchor:
                first_anchor[w] = int(B['abs'][bj])
    wfs = sorted(first_anchor)
    frame_mc = np.array([first_anchor[wfs[i+1]] - first_anchor[wfs[i]]
                         for i in range(len(wfs)-1)
                         if wfs[i+1] == wfs[i] + 1], dtype=np.float64)
    print(f"\n[calib] gross frame Delta (blastem master-cycles per recomp wall-frame):")
    if frame_mc.size:
        print(f"        n={frame_mc.size}  median={np.median(frame_mc):.0f}  "
              f"mean={frame_mc.mean():.0f}  expected(NTSC)={NTSC_FRAME_MC}  "
              f"ratio_to_expected={np.median(frame_mc)/NTSC_FRAME_MC:.5f}")
    else:
        print("        (no consecutive matched wall frames)")

    # --- per-interval Delta-ratio (INTRA-FRAME matched consecutive pairs) ---
    # dr = recomp mc-delta (same wf => exact, no frame-period assumption)
    # db = blastem abs-delta. ratio = dr/db. ~1.0 = accurate pacing.
    intra = []   # (ratio, dr, db, wf, sl, vint, key_i, pcz)
    cross = []   # cross-frame intervals (V-int handler / vblank wait)
    for bk in blocks:
        for k in range(bk.size - 1):
            ri, bj = bk.a + k, bk.b + k
            ri2, bj2 = ri + 1, bj + 1
            db = int(B['abs'][bj2] - B['abs'][bj])
            same_frame = (R['wf'][ri] == R['wf'][ri2])
            if same_frame:
                dr = int(R['mc'][ri2] - R['mc'][ri])
            else:
                dr = int(r_abs_mono[ri2] - r_abs_mono[ri])
            rec = (dr, db, int(R['wf'][ri]), int(R['sl'][ri]), int(R['vint'][ri]),
                   R['keys'][ri], int(R['pcz'][ri]))
            if same_frame:
                intra.append(rec)
            else:
                cross.append(rec)

    ia = np.array([(r[0], r[1]) for r in intra], dtype=np.float64)
    # keep intervals where both deltas are strictly positive (drop quantization
    # zero/neg jitter from mixed 68K/Z80 stamping + YM sample-boundary rounding)
    mask = (ia[:,0] > 0) & (ia[:,1] > 0)
    dr = ia[mask,0]; db = ia[mask,1]
    ratio = dr / db
    print(f"\n[delta] intra-frame matched consecutive intervals: "
          f"{ia.shape[0]} total, {mask.sum()} usable (dr>0 & db>0)")
    print(f"[delta] cross-frame intervals (V-int/vblank, reported separately): {len(cross)}")

    if ratio.size:
        # aggregate (offset-free) pacing ratio = sum(dr)/sum(db)
        agg = dr.sum() / db.sum()
        pct = np.percentile(ratio, [1,5,25,50,75,95,99])
        print(f"\n=== HEADLINE Axis-2 result (recomp / BlastEm Delta-cycle) ===")
        print(f"  aggregate pacing ratio  sum(dr)/sum(db) = {agg:.5f}   (1.0 = perfect)")
        print(f"  median per-interval ratio              = {np.median(ratio):.5f}")
        print(f"  mean   per-interval ratio              = {ratio.mean():.5f}")
        print(f"  percentiles  1/5/25/50/75/95/99 = "
              + " ".join(f"{p:.3f}" for p in pct))
        # histogram of log2 ratio
        lr = np.log2(ratio)
        edges = [-4,-2,-1,-0.5,-0.25,-0.1,0.1,0.25,0.5,1,2,4]
        hist, _ = np.histogram(lr, bins=[-1e9]+edges+[1e9])
        labels = ["<-4","-4..-2","-2..-1","-1..-.5","-.5..-.25","-.25..-.1",
                  "-.1..+.1","+.1..+.25","+.25..+.5","+.5..1","1..2","2..4",">4"]
        print(f"  log2(ratio) histogram (|<.1| octave = within ~7% of perfect):")
        for lbl, h in zip(labels, hist):
            print(f"      {lbl:>10} : {h:>7}  {'#'*int(60*h/max(1,hist.max()))}")

    # --- DISTINCT divergence MODES (grouped) -----------------------------
    # The per-interval ratios are discrete (DAC/music have fixed cadences), so
    # raw "worst N" is dominated by one repeated mode. Group by
    # (writer 68K/Z80, next-write key, dr, db) -> count; rank by total impact
    # = count * |log2 ratio|. This surfaces the real candidate cost errors.
    if ratio.size:
        from collections import defaultdict
        recs = [intra[i] for i in np.where(mask)[0]]
        modes = defaultdict(lambda: [0, None])  # key -> [count, sample-rec]
        for dr_i, db_i, wf_i, sl_i, vint_i, key_i, pcz_i in recs:
            origin = '68K' if pcz_i == 0xFFFF else 'Z80'
            gk = (origin, key_i, dr_i, db_i)
            modes[gk][0] += 1
            modes[gk][1] = (wf_i, sl_i, vint_i, pcz_i)
        mode_rows = []
        for (origin, key_i, dr_i, db_i), (cnt, samp) in modes.items():
            r = dr_i/db_i
            impact = cnt * abs(np.log2(r))
            mode_rows.append((impact, r, dr_i, db_i, cnt, origin, key_i, samp))
        mode_rows.sort(key=lambda x: -x[0])
        print(f"\n=== distinct divergence MODES (ranked by impact = count*|log2 ratio|) ===")
        print(f"  {'ratio':>7} {'dr':>6} {'db':>6} {'count':>7}  org  next-write   sample(wf/sl/vint/pcz)")
        for impact, r, dr_i, db_i, cnt, origin, key_i, samp in mode_rows[:args.worst]:
            chip = 'FM ' if key_i[0]==0 else 'PSG'
            wf_i, sl_i, vint_i, pcz_i = samp
            pcztxt = f"${pcz_i:04X}" if pcz_i != 0xFFFF else "68K"
            print(f"  {r:>7.3f} {dr_i:>6} {db_i:>6} {cnt:>7}  {origin}  "
                  f"{chip}p{key_i[1]} ${key_i[2]:02X}  {wf_i}/{sl_i}/{vint_i}/{pcztxt}")

        # split aggregate pacing by writer origin (68K cost vs Z80 cadence)
        org = np.array([0 if intra[i][6]==0xFFFF else 1 for i in np.where(mask)[0]])
        for oi, oname in ((0,'68K-origin (FM/music driver: 68K cycle cost)'),
                          (1,'Z80-origin (DAC/PSG playback cadence)')):
            sel = org == oi
            if sel.sum():
                print(f"  [{oname}] n={sel.sum()} "
                      f"agg ratio sum(dr)/sum(db)={dr[sel].sum()/db[sel].sum():.5f} "
                      f"median={np.median(ratio[sel]):.5f}")

    # --- cross-frame interval summary (V-int handler cost) -----------------
    if cross:
        ca = np.array([(r[0], r[1]) for r in cross], dtype=np.float64)
        cm = (ca[:,0] > 0) & (ca[:,1] > 0)
        if cm.sum():
            cr = ca[cm,0]/ca[cm,1]
            print(f"\n[cross-frame] {cm.sum()} usable; median ratio={np.median(cr):.5f} "
                  f"mean={cr.mean():.5f}  (these span the V-int handler + vblank wait)")

if __name__ == '__main__':
    main()
