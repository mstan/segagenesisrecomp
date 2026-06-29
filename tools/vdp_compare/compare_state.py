#!/usr/bin/env python3
"""
compare_state.py - compare our recomp's VDP state surfaces against the BlastEm
oracle at a chosen sync point. Normalises CRAM/VSRAM encodings so the two
backends are apples-to-apples:

  CRAM  : Genesis colour word masked to 0x0EEE (our genesis_vdp.c stores exactly
          value&0x0EEE; BlastEm stores the raw word -> mask it the same).
  VSRAM : vertical scroll masked to 0x03FF (our side stores value&0x03FF).
  VRAM  : raw 64KB bytes, directly comparable.

Inputs:
  --ours   <prefix>  reads <prefix>.vram.bin, <prefix>.cram.json, <prefix>.vsram.json
  --oracle <prefix>  reads <prefix>.vram.bin, <prefix>.cram.bin,  <prefix>.vsram.bin
"""
import sys, json, struct

def argval(flag):
    return sys.argv[sys.argv.index(flag) + 1]

def load_ours(p):
    vram = open(p + ".vram.bin", "rb").read()
    cram = json.load(open(p + ".cram.json"))
    vsram = json.load(open(p + ".vsram.json"))
    return vram, cram, vsram

def load_oracle(p):
    vram = open(p + ".vram.bin", "rb").read()
    cb = open(p + ".cram.bin", "rb").read()
    cram = list(struct.unpack("<%dH" % (len(cb)//2), cb))
    vb = open(p + ".vsram.bin", "rb").read()
    vsram = list(struct.unpack("<%dH" % (len(vb)//2), vb))
    return vram, cram, vsram

def main():
    op = argval("--ours"); rp = argval("--oracle")
    ov, oc, ovs = load_ours(op)
    rv, rc, rvs = load_oracle(rp)

    print("=== VRAM (64KB raw bytes) ===")
    n = min(len(ov), len(rv))
    diffs = [i for i in range(n) if ov[i] != rv[i]]
    print(f"  size ours={len(ov)} oracle={len(rv)}  differing bytes={len(diffs)} / {n}  "
          f"({100.0*(n-len(diffs))/n:.4f}% identical)")
    if diffs:
        # structural: which 32-byte tiles differ (tile = 32 bytes of 4bpp 8x8)
        tiles = sorted({d // 32 for d in diffs})
        print(f"  differing tiles (32B units): {len(tiles)}  first few: {tiles[:12]}")
        print(f"  byte-range of diffs: 0x{diffs[0]:04X}..0x{diffs[-1]:04X}")

    print("=== CRAM (64 palette entries, masked 0x0EEE) ===")
    oc_m = [(x & 0x0EEE) for x in oc[:64]]
    rc_m = [(x & 0x0EEE) for x in rc[:64]]
    cdiff = [i for i in range(64) if oc_m[i] != rc_m[i]]
    print(f"  differing entries={len(cdiff)} / 64  ({100.0*(64-len(cdiff))/64:.2f}% identical)")
    for i in cdiff[:16]:
        print(f"    [{i:2d}] ours=0x{oc_m[i]:04X} oracle=0x{rc_m[i]:04X}")

    print("=== VSRAM (vertical scroll, masked 0x03FF) ===")
    m = min(len(ovs), len(rvs), 40)
    ovs_m = [(x & 0x03FF) for x in ovs[:m]]
    rvs_m = [(x & 0x03FF) for x in rvs[:m]]
    vdiff = [i for i in range(m) if ovs_m[i] != rvs_m[i]]
    print(f"  compared {m} entries  differing={len(vdiff)}  ({100.0*(m-len(vdiff))/m:.2f}% identical)")
    for i in vdiff[:16]:
        print(f"    [{i:2d}] ours=0x{ovs_m[i]:04X} oracle=0x{rvs_m[i]:04X}")

if __name__ == "__main__":
    main()
