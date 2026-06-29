#!/usr/bin/env python3
"""Literal-oracle triage: for each interior-label miss address, find the
producing dispatch site in the ROM and classify its shape.

Shapes:
  PCIDX_WORD  jmp/jsr (d8,PC,Xn.W) over a dc.w self-relative offset table
  PCIDX_DUFF  jmp/jsr (d8,PC,Xn.W) into a uniform instruction sequence
  AREG_IND    jmp/jsr (aN) / (d16,aN) / (d8,aN,Xn) -- two-step, table in aN
"""
import sys, struct, collections

rom = open('rka.bin','rb').read()
ROMSZ = len(rom)

def r16(a): return struct.unpack('>H', rom[a:a+2])[0]
def s16(a): return struct.unpack('>h', rom[a:a+2])[0]
def r32(a): return struct.unpack('>I', rom[a:a+4])[0]

funcs = sorted(int(l,16) for l in open('rka_funcs.txt') if l.strip() and not l.startswith('#'))
funcset = set(funcs)

# distinct miss addresses
misses = []
seen = set()
for l in open('interior_label_misses.log'):
    l=l.strip()
    if not l.startswith('addr='): continue
    a = int(l.split()[0].split('=')[1],16)
    if a not in seen:
        seen.add(a); misses.append(a)

import capstone
md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)

def func_end(entry):
    """next function start after entry, or +0x2000 cap."""
    end = entry + 0x2000
    for f in funcs:
        if f > entry and f < end: end = f
    return min(end, ROMSZ)

# Collect every PC-indexed dispatch and its enumerated target set.
# site -> (kind, mnem, base, [targets])
sites = []
def enumerate_pcidx(jmp_pc, extw_addr, is_jsr):
    """jmp/jsr (d8,PC,Xn.W): brief ext word at extw_addr."""
    ext = r16(extw_addr)
    d8 = ext & 0xFF
    if d8 >= 0x80: d8 -= 0x100
    base = (extw_addr + d8) & 0xFFFFFF
    mnem = 'jsr' if is_jsr else 'jmp'
    # word-offset table: dc.w (target-base); plausible window [jmp_pc-0x200, base+0x2000]
    wtargets=[]
    lo, hi = 0, ROMSZ
    for i in range(64):
        a = base + i*2
        if a+1 >= ROMSZ: break
        off = s16(a)
        t = (base + off) & 0xFFFFFF
        if t & 1 or t >= ROMSZ: break
        # gate: target must decode as a legal insn
        try:
            ins = next(md.disasm(rom[t:t+8], t))
        except StopIteration:
            break
        wtargets.append(t)
    # Duff: uniform 2-byte opcode run at base
    dtargets=[]
    if base+1 < ROMSZ:
        op0 = r16(base)
        for i in range(64):
            a = base+i*2
            if a+1>=ROMSZ: break
            if r16(a)!=op0: break
            dtargets.append(a)
    if len(dtargets) >= 8:
        sites.append(('PCIDX_DUFF', mnem, jmp_pc, base, dtargets))
    # word table is plausible if first few entries land in a tight window
    if len(wtargets) >= 1:
        sites.append(('PCIDX_WORD', mnem, jmp_pc, base, wtargets))

areg_sites = []  # (mnem, pc, ea_str, func)
for entry in funcs:
    end = func_end(entry)
    pc = entry
    guard = 0
    while pc < end and guard < 4000:
        guard += 1
        w = r16(pc) if pc+1 < ROMSZ else 0
        # decode length via capstone
        try:
            ins = next(md.disasm(rom[pc:pc+12], pc))
        except StopIteration:
            break
        ln = ins.size
        # PC-indexed jmp/jsr
        if w == 0x4EFB:   # jmp (d8,pc,Xn)
            enumerate_pcidx(pc, pc+2, False)
        elif w == 0x4EBB: # jsr (d8,pc,Xn)
            enumerate_pcidx(pc, pc+2, True)
        elif 0x4ED0 <= w <= 0x4EF7 and (w & 0xFFF8) in (0x4ED0,0x4EE8,0x4EF0):
            areg_sites.append(('jmp', pc, ins.op_str, entry))
        elif 0x4E90 <= w <= 0x4EB7 and (w & 0xFFF8) in (0x4E90,0x4EA8,0x4EB0):
            areg_sites.append(('jsr', pc, ins.op_str, entry))
        # terminators end the linear walk
        mnem = ins.mnemonic.lower()
        if mnem in ('rts','rte','rtr','jmp','bra') and not mnem.startswith('b'):
            if mnem in ('rts','rte','rtr'): break
        if mnem == 'rts' or mnem=='rte' or mnem=='rtr': break
        pc += ln if ln>0 else 2

# invert: miss -> producing sites
def containing_func(a):
    c=None
    for f in funcs:
        if f<=a: c=f
        else: break
    return c

print(f"{len(misses)} distinct miss addrs; {len(funcs)} funcs; "
      f"{len(sites)} PC-indexed dispatch sites; {len(areg_sites)} areg-indirect sites\n")

classcount = collections.Counter()
for m in misses:
    hits=[]
    for kind,mnem,jpc,base,tgts in sites:
        if m in tgts:
            hits.append((kind,mnem,jpc,base,len(tgts)))
    cf = containing_func(m)
    if hits:
        # prefer the most specific (word table whose base is closest below m)
        k = hits[0][0]
        classcount[k]+=1
        h = hits[0]
        print(f"miss ${m:06X} (in func ${cf:06X}): {h[0]} via {h[1]} @${h[2]:06X} base=${h[3]:06X} ntgts={h[4]}"
              + (f"  [+{len(hits)-1} more]" if len(hits)>1 else ""))
    else:
        # check if it's in RAM
        if m >= 0xFF0000:
            classcount['RAM_TARGET']+=1
            print(f"miss ${m:06X}: RAM target (computed jmp into WRAM-resident code)")
        else:
            classcount['UNRESOLVED']+=1
            cfs = f"${cf:06X}" if cf is not None else "?"
            print(f"miss ${m:06X} (in func {cfs}): NO static PC-indexed producer found "
                  f"-> likely areg-indirect / two-step")

print("\n=== class histogram ===")
for k,v in classcount.most_common():
    print(f"  {k:14} {v}")
