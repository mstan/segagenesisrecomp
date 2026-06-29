"""Characterize indirect-dispatch idioms in a Genesis ROM (literal oracle).

Finds:
  A) JMP  (d8,PC,Xn)            opcode 0x4EFB   -> pcrel/bra table
  B) JSR  (d8,PC,Xn)            opcode 0x4EBB
  C) movea.l (d8,PC,Xn),aN  +  jmp/jsr (aN)     two-step long-pointer table
Resolves each table (longs / pcrel16 / bra-ladder) and lists targets.
"""
import capstone, struct, sys

ROM = sys.argv[1] if len(sys.argv) > 1 else "rka.bin"
data = open(ROM, "rb").read()
N = len(data)
md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)
md.detail = True

MISSES = {0x5FE4,0x444E,0x51F4,0x3C44,0x688C,0x80C2,0x4478,0x0C74,
          0x4B94,0x18EC,0x1902,0x1918,0x192E,0x5F76,0x1A2E}

def rd16(a): return struct.unpack(">H", data[a:a+2])[0]
def rd32(a): return struct.unpack(">I", data[a:a+4])[0]

def decode1(a):
    for ins in md.disasm(data[a:a+8], a):
        return ins
    return None

def legal(a):
    """does addr a decode as a plausible instruction (even, in-rom, decodes)?"""
    if a & 1 or a >= N: return False
    return decode1(a) is not None

def walk_long_table(base, maxN=16):
    """enumerate absolute-long pointers until one is implausible."""
    out = []
    for i in range(maxN):
        ea = base + i*4
        if ea+4 > N: break
        t = rd32(ea) & 0xFFFFFF
        if t & 1 or t >= N or t == 0: break
        if decode1(t) is None: break
        out.append(t)
    return out

def walk_pcrel16(base, maxN=64):
    """enumerate signed-16 offsets from base (target = base + off)."""
    out = []
    for i in range(maxN):
        ea = base + i*2
        if ea+2 > N: break
        off = rd16(ea)
        if off >= 0x8000: off -= 0x10000
        t = (base + off) & 0xFFFFFF
        if t & 1 or t >= N or t == 0: break
        if decode1(t) is None: break
        out.append(t)
    return out

def walk_bra_ladder(base, maxN=64):
    """run of same-width bra.s(0x6xxx,2B) / bra.w(0x6000,4B) trampolines."""
    if base+2 > N: return []
    w0 = rd16(base)
    if w0 == 0x6000: stride = 4
    elif 0x6000 < w0 <= 0x60FF: stride = 2
    else: return []
    out = []
    for i in range(maxN):
        s = base + i*stride
        if s+stride > N: break
        ww = rd16(s)
        if stride == 4 and ww != 0x6000: break
        if stride == 2 and not (0x6000 < ww <= 0x60FF): break
        out.append(s)
    return out

twostep = []   # (movea_pc, base, areg, call_pc, call_mnem, targets)
single  = []   # (jmp_pc, base, targets, kind)

a = 0x200
while a < N-2:
    w = rd16(a)
    # --- two-step: movea.l (d8,PC,Xn),aN  == 0x2{A*2}7B ---
    if (w & 0xF1FF) == 0x207B:           # movea.l <ea>,aN with ea mode (d8,PC,Xn)
        areg = (w >> 9) & 7
        ext = rd16(a+2)
        d8 = ext & 0xFF
        if d8 >= 0x80: d8 -= 0x100
        base = a + 2 + d8
        # look for jmp/jsr (aN) within next ~12 bytes, same areg, no reload of aN
        p = a + 4
        found = None
        for _ in range(6):
            if p+2 > N: break
            ins = decode1(p)
            if ins is None: break
            ww = rd16(p)
            # jmp (aN) = 0x4ED0|reg ; jsr (aN) = 0x4E90|reg
            if ww == (0x4ED0 | areg): found = (p, "jmp"); break
            if ww == (0x4E90 | areg): found = (p, "jsr"); break
            # if this insn writes aN again, abort
            p += ins.size
        if found and 0 < base < N:
            tg = walk_long_table(base)
            if tg:
                twostep.append((a, base, areg, found[0], found[1], tg))
    # --- single-step JMP/JSR (d8,PC,Xn) ---
    if w in (0x4EFB, 0x4EBB):
        ext = rd16(a+2); d8 = ext & 0xFF
        if d8 >= 0x80: d8 -= 0x100
        base = a + 2 + d8
        # try bra-ladder, then pcrel16, then long table
        kind, tg = None, []
        bl = walk_bra_ladder(base)
        pr = walk_pcrel16(base)
        lt = walk_long_table(base)
        if len(bl) >= 2:   kind, tg = "bra",  bl
        elif len(pr) >= 2: kind, tg = "pcrel16", pr
        elif len(lt) >= 2: kind, tg = "long", lt
        single.append((a, base, tg, kind, "JMP" if w==0x4EFB else "JSR"))
    a += 2

allcov = set()
print(f"### ROM={ROM} size={N:#x}")
print(f"\n### TWO-STEP movea.l(d8,PC,Xn),aN + jmp/jsr(aN):  {len(twostep)} sites")
for (mpc, base, areg, cpc, cmn, tg) in twostep:
    hits = [t for t in tg if t in MISSES]
    allcov |= set(tg)
    flag = f"  <== covers misses {[hex(h) for h in hits]}" if hits else ""
    print(f"  movea@${mpc:06X} table=${base:06X} {cmn}(a{areg})@${cpc:06X} "
          f"-> {len(tg)} entries {[hex(t) for t in tg]}{flag}")

print(f"\n### SINGLE-STEP JMP/JSR(d8,PC,Xn):  {len(single)} sites")
for (jpc, base, tg, kind, mn) in single:
    hits = [t for t in tg if t in MISSES]
    if tg: allcov |= set(tg)
    flag = f"  <== covers misses {[hex(h) for h in hits]}" if hits else ""
    print(f"  {mn}@${jpc:06X} table=${base:06X} kind={kind} -> {[hex(t) for t in tg]}{flag}")

print(f"\n### MISS COVERAGE")
covered = sorted(m for m in MISSES if m in allcov)
missing = sorted(m for m in MISSES if m not in allcov)
print(f"  covered by some table: {[hex(x) for x in covered]}")
print(f"  NOT covered:           {[hex(x) for x in missing]}")
