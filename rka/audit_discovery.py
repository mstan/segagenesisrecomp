#!/usr/bin/env python3
"""Detector audit mode (Step 2) — run the discovery detectors WITHOUT changing
codegen and classify every candidate target against the runtime oracle.

This is the safety checkpoint ChatGPT prescribed: "run detectors without
changing codegen ... inspect damage without causing damage." It re-runs the
two detector classes the reverted patch touched —

  (A) JSR/JMP (d8,PC,Xn.W) self-relative WORD-offset call tables
  (B) two-step LONG-pointer dispatch tables (movea.l (d8,pc,Xn),aN; jmp (aN)
      and  lea tbl,aN; movea.l (aN,Xn),aP; jmp (aP))   [the $23D8 idiom]

faithfully to recompiler/src/function_finder.c (raw ROM math mirrors the C;
capstone provides the legal-decode gate + function instruction boundaries),
then for each proposed candidate emits the classification ChatGPT specified:
runtime_observed / inside_existing_function / instruction_boundary /
table_bound_source -> the acceptance-ladder class + would_make_function /
would_create_interior_label / would_defer.

The point: show that the REAL targets are runtime-observed while the over-scan
(esp. raising the long-table cap globally) produces garbage the oracle rejects.

Inputs (defaults assume CWD = segagenesisrecomp/rka):
  rka.bin, rka_funcs.txt (baseline discovered entries),
  rka_executed_pcs.txt + rka_ram_targets.txt (runtime oracle).
Output: rka_discovery_audit.csv + a stdout per-detector summary.
"""
import argparse, struct, csv, collections, sys
import capstone

# ---- ROM access ----------------------------------------------------------
def load_rom(path):
    return open(path, "rb").read()

def s16(rom, a):  return struct.unpack(">h", rom[a:a+2])[0]
def r16(rom, a):  return struct.unpack(">H", rom[a:a+2])[0]
def r32(rom, a):  return struct.unpack(">I", rom[a:a+4])[0]

MD = capstone.Cs(capstone.CS_ARCH_M68K,
                 capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)

def legal_decode(rom, t):
    """True if a legal MC68000 instruction decodes at t (the jt gate)."""
    if t & 1 or t + 2 > len(rom):
        return False
    try:
        next(MD.disasm(rom[t:t+10], t))
        return True
    except StopIteration:
        return False

# ---- baseline function spans + instruction-boundary set ------------------
def load_funcs(path):
    out = []
    for line in open(path):
        line = line.strip()
        if line and not line.startswith("#"):
            out.append(int(line, 16))
    return sorted(set(out))

def build_spans(rom, funcs):
    """Linear-decode each function to get its instruction-boundary set and a
    [start,end) span. Stops at a terminator or the next function start."""
    fset = set(funcs)
    boundaries = set()
    spans = []   # (start, end)
    TERM = {"rts", "rte", "rtr", "jmp", "bra"}
    for i, f in enumerate(funcs):
        nxt = funcs[i+1] if i+1 < len(funcs) else len(rom)
        pc = f
        guard = 0
        while pc < nxt and guard < 8000:
            guard += 1
            try:
                ins = next(MD.disasm(rom[pc:pc+10], pc))
            except StopIteration:
                break
            boundaries.add(pc)
            mnem = ins.mnemonic.lower().split('.')[0]
            pc += ins.size if ins.size else 2
            if mnem in TERM:           # unconditional flow leaves: end of run
                break
            if pc in fset:             # ran into the next function
                break
        spans.append((f, pc))
    spans.sort()
    return boundaries, spans

def inside_function(spans, target):
    """Binary-search spans for one containing target; return its start or None."""
    lo, hi = 0, len(spans)
    while lo < hi:
        mid = (lo + hi) // 2
        s, e = spans[mid]
        if target < s:   hi = mid
        elif target >= e: lo = mid + 1
        else:            return s
    return None

# ---- runtime oracle -------------------------------------------------------
def load_addr_set(path):
    out = set()
    try:
        for line in open(path):
            line = line.strip()
            if line and not line.startswith("#"):
                out.add(int(line, 16))
    except FileNotFoundError:
        pass
    return out

# ---- detectors (faithful to function_finder.c) ---------------------------
PCRELW_WINDOW = 0x800
JT_LONG_LEAD_SKIP = 4
AUDIT_LONG_MAX = 1024   # expose over-scan past the old cap of 64

def find_index_bound(rom, site_pc):
    """Look backward up to ~12 insns from a dispatch site for a nearby index
    bound (cmpi.w #N,dX / andi.w #N,dX / and.w #N). Returns N or None — the
    'table_bound_source' proof ChatGPT requires before trusting >cap entries."""
    # decode a window ending at site_pc by scanning forward from site-0x40
    start = max(0, site_pc - 0x40)
    addrs = []
    pc = start
    while pc < site_pc:
        try:
            ins = next(MD.disasm(rom[pc:pc+10], pc))
        except StopIteration:
            pc += 2; continue
        addrs.append((pc, ins))
        pc += ins.size if ins.size else 2
    for pc, ins in reversed(addrs[-12:]):
        m = ins.mnemonic.lower()
        op = ins.op_str.lower()
        if m.startswith("cmpi") and "#" in op:
            try:
                imm = op.split("#")[1].split(",")[0]
                return int(imm, 16) if imm.startswith("0x") else int(imm, 0)
            except ValueError:
                pass
        if m.startswith("andi") or m.startswith("and"):
            if "#" in op:
                try:
                    imm = op.split("#")[1].split(",")[0]
                    v = int(imm, 16) if imm.startswith("0x") else int(imm, 0)
                    return v   # mask bound: index <= v
                except ValueError:
                    pass
    return None

def detect_jsr_word_tables(rom, boundaries):
    """(A) jmp/jsr (d8,PC,Xn.W) word-offset call tables. Yields candidates.
    Source-instruction anchored (ChatGPT rule #1): only fire at a source_pc
    that is a known instruction boundary, never a blind ROM-byte match."""
    romsz = len(rom)
    cands = []
    for pc in sorted(boundaries):
        if pc + 2 > romsz:
            continue
        w = r16(rom, pc)
        is_jsr = (w == 0x4EBB)
        is_jmp = (w == 0x4EFB)
        if not (is_jsr or is_jmp):
            continue
        ext_at = pc + 2
        if ext_at + 1 >= romsz:
            continue
        d8 = r16(rom, ext_at) & 0xFF
        if d8 >= 0x80:
            d8 -= 0x100
        base = (ext_at + d8) & 0xFFFFFF
        bound = find_index_bound(rom, pc)
        for i in range(256):
            a = base + i * 2
            if a + 1 >= romsz:
                break
            off = s16(rom, a)
            target = (base + off) & 0xFFFFFF
            dist = abs(target - base)
            if dist > PCRELW_WINDOW: break
            if target & 1:            break
            if target >= romsz:       break
            if not legal_decode(rom, target): break
            cands.append(dict(detector=("JSR" if is_jsr else "JMP") + "_PC_INDEX_WORD",
                              source_pc=pc, logical_source_pc=pc, base=base,
                              index=i, target=target,
                              bound=("proven:%d" % bound) if bound is not None else "none"))
    return cands

def _walk_long_table(rom, base, source_pc, logical_src, bound):
    """jt_enumerate_long-faithful walk, but uncapped (AUDIT_LONG_MAX) to expose
    over-scan. Yields candidates tagged with index + whether past old cap 64."""
    romsz = len(rom)
    out = []
    valid_seen = 0
    for i in range(AUDIT_LONG_MAX):
        ea = base + i * 4
        if ea + 4 > romsz:
            break
        raw = r32(rom, ea) & 0xFFFFFFFF
        ok = (raw != 0) and not (raw & 1) and (raw < romsz)
        if ok and not legal_decode(rom, raw):
            ok = False
        if not ok:
            if valid_seen:        break
            if i >= JT_LONG_LEAD_SKIP: break
            continue
        valid_seen += 1
        out.append(dict(detector="LONG_PTR_TABLE", source_pc=source_pc,
                        logical_source_pc=logical_src, base=base, index=i,
                        target=raw & 0xFFFFFF,
                        bound=("proven:%d" % bound) if bound is not None else "none"))
    return out

def detect_long_tables(rom, boundaries):
    """(B) two-step long-pointer dispatch tables. Two site forms, mirroring
    function_finder.c's MN_MOVEA(pc-idx) and MN_LEA paths. Source-instruction
    anchored: the movea.l / lea site must be a known instruction boundary."""
    romsz = len(rom)
    cands = []
    seen_bases = set()
    for pc in sorted(boundaries):
        if pc + 2 > romsz:
            continue
        w = r16(rom, pc)
        # Form 1: movea.l (d8,pc,Xn),aN  (opcode 0x20..0x2E with ea 0x7B)
        if (w & 0xF1FF) == 0x207B:       # movea.l (d8,pc,Xn.?),aN
            ext_at = pc + 2
            if ext_at + 1 >= romsz: continue
            d8 = r16(rom, ext_at) & 0xFF
            if d8 >= 0x80: d8 -= 0x100
            base = (ext_at + d8) & 0xFFFFFF
            if base in seen_bases: continue
            # require a jmp/jsr (aN) shortly after (we don't track aN precisely
            # here; the strong long gate rejects non-tables anyway)
            seen_bases.add(base)
            bound = find_index_bound(rom, pc)
            cands += _walk_long_table(rom, base, pc, pc, bound)
        # Form 2: lea abs.l,aN (0x41F9|aN<<9) or lea (d16,pc),aN (0x41FA|aN<<9)
        if (w & 0xF1FF) == 0x41F9 and pc + 6 <= romsz:      # lea abs.l,aN
            tbl = r32(rom, pc + 2) & 0xFFFFFF
            base, src = _resolve_lea_table(rom, pc, pc + 6, tbl)
            if base is not None and base not in seen_bases:
                seen_bases.add(base)
                bound = find_index_bound(rom, src)
                cands += _walk_long_table(rom, base, src, pc, bound)
        if (w & 0xF1FF) == 0x41FA and pc + 4 <= romsz:      # lea (d16,pc),aN
            tbl = (pc + 2 + s16(rom, pc + 2)) & 0xFFFFFF
            base, src = _resolve_lea_table(rom, pc, pc + 4, tbl)
            if base is not None and base not in seen_bases:
                seen_bases.add(base)
                bound = find_index_bound(rom, src)
                cands += _walk_long_table(rom, base, src, pc, bound)
    return cands

def _resolve_lea_table(rom, lea_pc, after, tbl_base):
    """After a `lea tbl,aN`, scan straight-line for `movea.l (aN,Xn),aP` to get
    the real table base = tbl_base + disp, and confirm a `jmp/jsr (aP)` follows.
    Returns (base, dispatch_site_pc) or (None,None). Mirrors the C MN_LEA scan."""
    romsz = len(rom)
    pc = after
    for _ in range(8):
        if pc + 2 > romsz: break
        try:
            ins = next(MD.disasm(rom[pc:pc+10], pc))
        except StopIteration:
            break
        w = r16(rom, pc)
        # movea.l (aN,Xn.W),aP : opcode 0x2070 | (aP<<9) | aN ; needs ext word
        if (w & 0xF1F8) == 0x2070 and pc + 4 <= romsz:
            disp = r16(rom, pc + 2) & 0xFF
            if disp >= 0x80: disp -= 0x100
            base = (tbl_base + disp) & 0xFFFFFF
            # confirm jmp/jsr (aP) within a few insns
            jpc = pc + (ins.size if ins.size else 4)
            for _ in range(4):
                if jpc + 2 > romsz: break
                jw = r16(rom, jpc)
                if (jw & 0xFFF8) in (0x4ED0, 0x4E90):   # jmp/jsr (aP)
                    return base, jpc
                try:
                    ji = next(MD.disasm(rom[jpc:jpc+10], jpc))
                except StopIteration:
                    break
                jpc += ji.size if ji.size else 2
            return None, None
        pc += ins.size if ins.size else 2
    return None, None

# ---- classification (ChatGPT's acceptance ladder) ------------------------
def classify(c, rom_exec, ram_exec, boundaries, spans):
    t = c["target"]
    region = "RAM" if t >= 0xFF0000 else "ROM"
    observed = (t in ram_exec) if region == "RAM" else (t in rom_exec)
    fstart = inside_function(spans, t) if region == "ROM" else None
    inside = fstart is not None
    boundary = t in boundaries
    proven = c["bound"].startswith("proven")

    if region == "RAM":
        cls = "RamRuntimeTarget"          # -> interpreter, never static fn
    elif observed and inside and boundary:
        cls = "ObservedInteriorEntry"
    elif observed and inside and not boundary:
        cls = "ObservedMidInstruction"    # suspicious: observed yet not a boundary
    elif observed and not inside:
        cls = "ObservedFunctionEntry"
    elif inside and boundary:
        cls = "StaticInteriorEntry"
    elif inside and not boundary:
        cls = "RejectedMidInstruction"    # data/mid-insn promoted -> the regression
    elif proven and c["index"] < 64:
        cls = "StaticBoundedCandidate"
    elif proven:
        cls = "StaticBoundedCandidate"
    else:
        cls = "DeferredCandidate"

    would_fn   = cls == "ObservedFunctionEntry"
    would_int  = cls in ("ObservedInteriorEntry", "StaticInteriorEntry")
    would_def  = cls in ("DeferredCandidate", "StaticBoundedCandidate",
                         "RamRuntimeTarget", "RejectedMidInstruction",
                         "ObservedMidInstruction")
    return dict(region=region, runtime_observed=int(observed),
                inside_existing_function=int(inside),
                instruction_boundary=int(boundary),
                classification=cls, would_make_function=int(would_fn),
                would_create_interior_label=int(would_int),
                would_defer=int(would_def))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default="rka.bin")
    ap.add_argument("--funcs", default="rka_funcs.txt")
    ap.add_argument("--exec-pcs", default="rka_executed_pcs.txt")
    ap.add_argument("--ram-pcs", default="rka_ram_targets.txt")
    ap.add_argument("--out", default="rka_discovery_audit.csv")
    args = ap.parse_args()

    rom = load_rom(args.rom)
    funcs = load_funcs(args.funcs)
    rom_exec = load_addr_set(args.exec_pcs)
    ram_exec = load_addr_set(args.ram_pcs)
    boundaries, spans = build_spans(rom, funcs)
    print(f"baseline: {len(funcs)} funcs, {len(boundaries)} instruction "
          f"boundaries; oracle: {len(rom_exec)} ROM + {len(ram_exec)} RAM PCs")

    cands = detect_jsr_word_tables(rom, boundaries) + detect_long_tables(rom, boundaries)
    rows = []
    for c in cands:
        cls = classify(c, rom_exec, ram_exec, boundaries, spans)
        rows.append({**c, **cls})

    fields = ["detector", "source_pc", "logical_source_pc", "base", "index",
              "target", "bound", "region", "runtime_observed",
              "inside_existing_function", "instruction_boundary",
              "classification", "would_make_function",
              "would_create_interior_label", "would_defer"]
    with open(args.out, "w", newline="") as f:
        wri = csv.DictWriter(f, fieldnames=fields)
        wri.writeheader()
        for r in rows:
            r2 = dict(r)
            for k in ("source_pc", "logical_source_pc", "base", "target"):
                r2[k] = f"{r[k]:06X}"
            wri.writerow(r2)
    print(f"wrote {len(rows)} candidates -> {args.out}\n")

    # Per-detector summary
    by_det = collections.defaultdict(lambda: collections.Counter())
    for r in rows:
        d = by_det[r["detector"]]
        d["candidates"] += 1
        d["observed"] += r["runtime_observed"]
        d["would_fn"] += r["would_make_function"]
        d["would_int"] += r["would_create_interior_label"]
        d["would_def"] += r["would_defer"]
        d[r["classification"]] += 1
    for det, c in sorted(by_det.items()):
        print(f"[{det}] candidates={c['candidates']} observed={c['observed']} "
              f"-> fn={c['would_fn']} interior={c['would_int']} defer={c['would_def']}")
        for cls in sorted(k for k in c if k[0].isupper()):
            print(f"      {cls:24} {c[cls]}")

    # Over-scan exposure: long-table candidates past the old cap of 64.
    past = [r for r in rows if r["detector"] == "LONG_PTR_TABLE" and r["index"] >= 64]
    if past:
        obs = sum(r["runtime_observed"] for r in past)
        defer = sum(r["would_defer"] for r in past)
        print(f"\nLONG_PTR_TABLE entries past old cap 64: {len(past)} "
              f"({obs} runtime-observed, {defer} would-defer/reject) "
              f"-- proves cap must be a PROVEN per-table bound, not a global raise")


if __name__ == "__main__":
    main()
