"""
oracle_parity_audit.py — comprehensive native(own-backend)-vs-oracle(clownmdemu)
per-subsystem parity audit for the AGPL-free runtime.

WHY THIS EXISTS
---------------
divergence_diff.py free-runs both binaries and diffs their frame rings where the
two CURRENTLY overlap. But vint_runcount is monotonic and native runs ~2x the
oracle's speed, so the two 600-frame rings only ever hold the SAME vint during
early boot (vint < ~600). It can never reach a deep attract-demo vint (~3600+),
which is exactly where the Sonic 1 audio regression reproduces.

This tool exploits determinism instead of live overlap: the attract demo takes
NO input, so native-at-vint-N and oracle-at-vint-N are the SAME logical state
regardless of wall-clock. We capture each side's full FrameRecord at a set of
target vints INDEPENDENTLY (each side polls its own ring and grabs the vint
before it evicts), then diff OFFLINE, per subsystem, across all targets. No
cross-process pause/step, no shared-window race — pure always-on-ring queries.

COMPARABILITY (what the AGPL refactor swapped, and whether we can diff it)
-------------------------------------------------------------------------
  z80 regs   superzazu vs clownz80      SAME ISA  -> COMPARABLE (sound driver!)
  z80 ram    g_machine.bus.z80_ram      identical -> COMPARABLE (SMPS work RAM)
  wram       g_ram vs m68k.ram          identical -> COMPARABLE (68K game state)
  vdp regs   GVDP decode vs VDP_State   hw-defined-> COMPARABLE (semantic fields)
  vram       raw bytes                  identical -> COMPARABLE (rendering)
  cram/vsram raw words                  identical -> COMPARABLE
  m68k regs  recompiled vs interpreter  frame-aligned but different PC within the
                                        frame -> ADVISORY (reported, low trust)
  fm/psg     ymfm/sn76489 vs FM/PSG_St  NOT byte-comparable -> SKIPPED here; use
                                        the chip_ring register-stream tap instead.

USAGE
-----
  # both binaries already running with debug.ini (any ports):
  python tools/oracle_parity_audit.py --native-port 4390 --oracle-port 4381 \
      --vints 60,240,1000,2000,3600,3700,3800,3900

  # default vints span boot -> attract-demo entry.
"""

from __future__ import annotations
import argparse, json, socket, sys, time, threading


class DebugClient:
    def __init__(self, host, port, label):
        self.label = label
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((host, port))
        self.sock.settimeout(60.0)
        self.buf = b""
        self._id = 0

    def cmd(self, name, **fields):
        self._id += 1
        msg = {"id": self._id, "cmd": name}
        msg.update(fields)
        self.sock.sendall((json.dumps(msg) + "\n").encode())
        while b"\n" not in self.buf:
            chunk = self.sock.recv(1 << 20)
            if not chunk:
                raise RuntimeError(f"{self.label}: connection closed")
            self.buf += chunk
        line, _, self.buf = self.buf.partition(b"\n")
        return json.loads(line.decode())

    def close(self):
        try: self.sock.close()
        except Exception: pass


def vint_map(c: DebugClient):
    """Map vint_runcount -> wall_frame for the CURRENT ring window."""
    fi = c.cmd("frame_info")
    if not fi.get("ok"):
        raise RuntimeError(f"{c.label}: frame_info failed: {fi}")
    lo, hi = fi["oldest_frame"], fi["current_frame"]
    out = {}
    f = lo
    while f <= hi:
        end = min(f + 599, hi)
        r = c.cmd("frame_timeseries", field="wram32[FE0C]", **{"from": f, "to": end})
        for i, v in enumerate(r.get("values") or []):
            if v is not None:
                out[int(v)] = f + i
        f = end + 1
    return out, lo, hi


def capture_targets(c: DebugClient, targets, timeout=240.0, poll=0.25):
    """Independently capture full FrameRecords at each target vint as the binary
    runs through them. Returns {vint: record}. Tolerates eviction by polling
    fast; reports any target that slid past before capture."""
    want = set(targets)
    got = {}
    missed = set()
    t0 = time.time()
    last_cur = -1
    while want and time.time() - t0 < timeout:
        vm, lo_f, hi_f = vint_map(c)
        cur_vint = max(vm) if vm else 0
        lo_vint = min(vm) if vm else 0
        last_cur = cur_vint
        for V in sorted(want):
            if V in vm:                              # present in ring right now
                rec = c.cmd("get_frame", frame=vm[V], include="all")
                if rec.get("ok"):
                    got[V] = rec
                    want.discard(V)
            elif V < lo_vint:                        # evicted before we caught it
                missed.add(V); want.discard(V)
        if not want:
            break
        time.sleep(poll)
    for V in want:
        missed.add(V)
    if missed:
        print(f"  [{c.label}] MISSED vints (evicted/timeout, last cur={last_cur}): "
              f"{sorted(missed)}")
    return got


# ---- per-subsystem diffs ------------------------------------------------------

def _hexbytes(a, b):
    """byte offsets where two equal-ish hex strings differ."""
    n = min(len(a), len(b))
    return [i // 2 for i in range(0, n - 1, 2) if a[i:i+2] != b[i:i+2]]


def diff_record(o, n):
    """Return {subsystem: summary_dict}. summary has 'n' (diff count) and
    'sample' (list of lines)."""
    res = {}

    # --- WRAM (comparable) — page histogram + samples
    ow, nw = (o.get("wram") or "").lower(), (n.get("wram") or "").lower()
    if ow and nw:
        d = _hexbytes(ow, nw)
        if d:
            from collections import Counter
            pages = Counter(off >> 8 for off in d)
            sample = [f"$FF{pg:02X}xx: {ct}B e.g [{next(x for x in d if x>>8==pg):04X}] "
                      f"o={ow[next(x for x in d if x>>8==pg)*2:next(x for x in d if x>>8==pg)*2+2]} "
                      f"n={nw[next(x for x in d if x>>8==pg)*2:next(x for x in d if x>>8==pg)*2+2]}"
                      for pg, ct in sorted(pages.items(), key=lambda x: -x[1])[:8]]
            res["wram"] = {"n": len(d), "sample": sample}

    # --- VRAM (comparable)
    ov, nv = o.get("vdp") or {}, n.get("vdp") or {}
    ovr, nvr = (ov.get("vram") or "").lower(), (nv.get("vram") or "").lower()
    if ovr and nvr:
        d = _hexbytes(ovr, nvr)
        if d:
            res["vram"] = {"n": len(d),
                           "sample": [f"[{off:04X}] o={ovr[off*2:off*2+2]} n={nvr[off*2:off*2+2]}"
                                      for off in d[:8]]}

    # --- VDP semantic scalars.
    # Exclude fields whose own-backend decode doesn't share clownmdemu's exact
    # semantics (structural, constant across frames — not state divergence):
    #   hscroll_mask/access_code/access_increment  (different encodings)
    # and known intra-frame phase fields (snapshot lands at a different raster
    # point / mid-DMA): in_vblank + the DMA-in-flight quartet.
    VDP_IGNORE = {"vram", "cram", "vsram", "hscroll_mask", "access_code",
                  "access_increment", "in_vblank", "dma_len", "dma_mode",
                  "dma_src", "access_addr"}
    sc = []
    for k in sorted(set(ov) | set(nv)):
        if k in VDP_IGNORE: continue
        if ov.get(k) != nv.get(k):
            sc.append(f"{k}: o={ov.get(k)!r} n={nv.get(k)!r}")
    if sc:
        res["vdp_regs"] = {"n": len(sc), "sample": sc[:12]}

    # --- CRAM / VSRAM int arrays. VSRAM only has 40 hardware words (H40); the
    # own backend stores 40, clownmdemu reports 64, so compare only [0..39].
    for fld in ("cram", "vsram"):
        oa, na = ov.get(fld), nv.get(fld)
        if oa and na and oa != na:
            m = min(len(oa), len(na), 40 if fld == "vsram" else 64)
            d = [i for i in range(m) if oa[i] != na[i]]
            res[f"vdp_{fld}"] = {"n": len(d),
                                 "sample": [f"[{i:02X}] o=0x{oa[i]:04X} n=0x{na[i]:04X}" for i in d[:10]]}

    # --- Z80 regs (comparable, sound driver)
    oz, nz = o.get("z80") or {}, n.get("z80") or {}
    zr = []
    for k in sorted(set(oz) | set(nz)):
        if k == "ram": continue
        if oz.get(k) != nz.get(k):
            zr.append(f"{k}: o={oz.get(k)!r} n={nz.get(k)!r}")
    if zr:
        res["z80_regs"] = {"n": len(zr), "sample": zr[:16]}

    # --- Z80 RAM (comparable, SMPS work RAM) — only if both present as hex
    ozr, nzr = oz.get("ram"), nz.get("ram")
    if isinstance(ozr, str) and isinstance(nzr, str):
        a, b = ozr.lower(), nzr.lower()
        d = _hexbytes(a, b)
        if d:
            res["z80_ram"] = {"n": len(d),
                              "sample": [f"[{off:04X}] o={a[off*2:off*2+2]} n={b[off*2:off*2+2]}" for off in d[:10]]}

    # --- M68K (advisory — recompiled vs interpreter, different PC within frame)
    om, nm = o.get("m68k") or {}, n.get("m68k") or {}
    o_d, o_a = om.get("D") or [0]*8, om.get("A") or [0]*8
    oracle_blank = (not any(o_d) and not any(o_a) and not om.get("SR"))
    if not oracle_blank:
        mr = []
        nd, na = nm.get("D") or [0]*8, nm.get("A") or [0]*8
        for i in range(8):
            if o_d[i] != nd[i]: mr.append(f"D{i}: o=0x{o_d[i]:08X} n=0x{nd[i]:08X}")
        for i in range(8):
            if o_a[i] != na[i]: mr.append(f"A{i}: o=0x{o_a[i]:08X} n=0x{na[i]:08X}")
        for k in ("SR",):
            if om.get(k) != nm.get(k): mr.append(f"{k}: o=0x{om.get(k,0):04X} n=0x{nm.get(k,0):04X}")
        if mr:
            res["m68k_ADVISORY"] = {"n": len(mr), "sample": mr[:8]}

    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--native-port", type=int, default=4390)
    ap.add_argument("--oracle-port", type=int, default=4381)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--vints", default="60,240,600,1000,2000,3000,3600,3700,3800,3900",
                    help="comma-separated target vint_runcount values to compare")
    ap.add_argument("--cluster", type=int, default=3,
                    help="consecutive vints captured per target, to separate stable "
                         "(real) divergence from transient intra-frame phase skew")
    ap.add_argument("--timeout", type=float, default=300.0)
    args = ap.parse_args()

    base = sorted(int(x) for x in args.vints.split(",") if x.strip())
    # Expand each target into a short cluster of consecutive vints. A subsystem
    # that diverges in EVERY frame of a cluster is a real divergence; one that
    # diverges in only some frames is intra-frame phase skew (the snapshot lands
    # at a different raster point / mid-DMA), not a genuine state difference.
    cluster = sorted({V + k for V in base for k in range(args.cluster)})
    print(f"[audit] targets={base}  cluster={args.cluster} -> capturing vints {cluster}")

    # Capture both sides CONCURRENTLY so the fast native core doesn't evict the
    # shallow vints before we grab them. Each thread polls its own always-on ring.
    results = {}
    def grab(port, label):
        c = DebugClient(args.host, port, label)
        try: results[label] = capture_targets(c, cluster, args.timeout)
        finally: c.close()
    th = [threading.Thread(target=grab, args=(args.oracle_port, "oracle")),
          threading.Thread(target=grab, args=(args.native_port, "native"))]
    for t in th: t.start()
    for t in th: t.join()
    o_recs, n_recs = results.get("oracle", {}), results.get("native", {})
    print(f"[audit] oracle captured {sorted(o_recs)}")
    print(f"[audit] native captured {sorted(n_recs)}")

    # For each base target, diff every cluster frame present on BOTH sides and
    # take the per-subsystem MIN diff count across the cluster (the irreducible
    # floor that survives the best-aligned frame = real divergence).
    SUBS = ["wram", "z80_regs", "z80_ram", "vdp_regs", "vdp_cram", "vdp_vsram",
            "vram", "m68k_ADVISORY"]
    agg = {}   # sub -> {"targets":[(V,floor)], "worst":(n,V,sampleframe)}
    audited = []
    for V in base:
        frames = [V + k for k in range(args.cluster) if V + k in o_recs and V + k in n_recs]
        if not frames:
            continue
        audited.append(V)
        per = {f: diff_record(o_recs[f], n_recs[f]) for f in frames}
        for sub in SUBS:
            counts = [(per[f].get(sub, {}).get("n", 0), f) for f in frames]
            floor_n, floor_f = min(counts)            # best-aligned frame
            if floor_n > 0:                            # survives best alignment => real
                a = agg.setdefault(sub, {"targets": [], "worst": (0, V, floor_f)})
                a["targets"].append((V, floor_n))
                if floor_n > a["worst"][0]:
                    a["worst"] = (floor_n, V, floor_f)

    if not audited:
        print("[audit] no vints captured from BOTH sides — relaunch both and retry.")
        return 2

    print("\n" + "=" * 74)
    print("PER-SUBSYSTEM PARITY (native own-backend vs oracle clownmdemu)")
    print(f"audited targets: {audited}   (phase skew cancelled via {args.cluster}-vint clusters)")
    print("=" * 74)
    for sub in SUBS:
        if sub not in agg:
            continue
        a = agg[sub]
        wn, wv, wf = a["worst"]
        print(f"\n### {sub}: REAL divergence at {len(a['targets'])}/{len(audited)} targets "
              f"(worst floor={wn} diffs near vint {wv})")
        print(f"    per-target floor: {a['targets']}")
        for line in diff_record(o_recs[wf], n_recs[wf]).get(sub, {}).get("sample", []):
            print(f"      {line}")

    clean = [s for s in SUBS if s not in agg and s != "m68k_ADVISORY"]
    print(f"\n[audit] AT PARITY across all clusters (phase skew removed): {clean or 'none'}")
    print("[audit] fm/psg not diffed (not byte-comparable) — chip_ring stream is the audio comparable (Phase 2).")
    return 0 if not agg else 1


if __name__ == "__main__":
    sys.exit(main())
