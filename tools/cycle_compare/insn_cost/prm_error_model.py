def popcount(x): return bin(x & 0xFFFF).count('1')

# ---- clown/PRM base: initial 4 cycles + action add ----
BASE = 4

def mulu_cost(src):
    return BASE + 34 + 2*popcount(src)   # EA reg =0

def muls_total_ops(src):
    s = (src << 1) & 0x1FFFF
    t10 = bin(((s ^ (s<<1)) & (0xAAAA<<1)) & 0x3FFFF).count('1')
    t01 = bin(((s ^ (s>>1)) & (0xAAAA>>1))).count('1')
    return t10 + t01

def muls_cost(src):
    return BASE + 34 + 2*muls_total_ops(src)

def divu_cost(dest, src):
    src &= 0xFFFF; dest &= 0xFFFFFFFF
    if src == 0: return BASE + 6  # trap-ish; ignore
    c = BASE + 6
    asrc = src; adst = dest
    if asrc >= (adst >> 16):
        c += 66
        wd = adst; sh = (asrc & 0xFFFF) << 16
        for i in range(15):
            hb = (wd & 0x80000000) != 0
            wd = (wd << 1) & 0xFFFFFFFF
            if not hb:
                c += 2
                if wd < sh:
                    c += 2
                    continue
            wd = (wd - sh) & 0xFFFFFFFF
    # else overflow: just c (BASE+6)
    return c

def divs_cost(dest, src):
    src16 = src & 0xFFFF; dest &= 0xFFFFFFFF
    if src16 == 0: return BASE+6
    sn = (src16 & 0x8000)!=0
    dn = (dest & 0x80000000)!=0
    rn = sn != dn
    asrc = (0 - ((src16-0x10000) if sn else src16)) & 0xFFFFFFFF if sn else src16
    asrc = ((-(src16-0x10000)) if sn else src16) & 0xFFFFFFFF
    adst = ((-(dest-0x100000000)) if dn else dest) & 0xFFFFFFFF
    c = BASE + 6
    c += 8 if dn else 6
    if asrc >= (adst>>16):
        aq = adst // asrc
        c += 104
        if sn: c += 2
        elif dn: c += 4
        c += (15 - bin(aq>>1).count('1')) * 2
    return c

def shift_cost_word(count): return BASE + 2 + 2*count
def shift_cost_long(count): return BASE + 2 + 2 + 2*count

# ---------- MULU ----------
costs = [mulu_cost(s) for s in range(65536)]
import statistics
print("MULU.W: probe(src=0x5555)=%d  PRM range %d..%d  mean=%.2f" % (
    mulu_cost(0x5555), min(costs), max(costs), statistics.mean(costs)))
mae = statistics.mean(abs(54-c) for c in costs)
print("   probe=54; mean|err|=%.2f  signed mean err=%.2f  worst |err|=%d" % (
    mae, statistics.mean(54-c for c in costs), max(abs(54-c) for c in costs)))

# ---------- MULS ----------
costs = [muls_cost(s) for s in range(65536)]
p = muls_cost(0x5555)
print("MULS.W: probe(src=0x5555)=%d  PRM range %d..%d  mean=%.2f" % (
    p, min(costs), max(costs), statistics.mean(costs)))
print("   mean|err|=%.2f  signed mean err=%.2f  worst |err|=%d" % (
    statistics.mean(abs(p-c) for c in costs),
    statistics.mean(p-c for c in costs),
    max(abs(p-c) for c in costs)))

# ---------- DIVU ----------  dest=0x00005555 in probe
import random
random.seed(1)
samp = [(random.randint(0,0xFFFFFFFF), random.randint(1,0xFFFF)) for _ in range(200000)]
dc = [divu_cost(d,s) for d,s in samp]
probe = divu_cost(0x00005555, 0x5555)
print("DIVU.W: probe(dest=0x5555,src=0x5555)=%d  sampled range %d..%d  mean=%.2f" % (
    probe, min(dc), max(dc), statistics.mean(dc)))
print("   mean|err|=%.2f signed mean err=%.2f worst|err|=%d" % (
    statistics.mean(abs(probe-c) for c in dc),
    statistics.mean(probe-c for c in dc),
    max(abs(probe-c) for c in dc)))

# ---------- DIVS ----------
dc = [divs_cost(d,s) for d,s in samp]
probe = divs_cost(0x00005555, 0x5555)
print("DIVS.W: probe(dest=0x5555,src=0x5555)=%d  sampled range %d..%d  mean=%.2f" % (
    probe, min(dc), max(dc), statistics.mean(dc)))
print("   mean|err|=%.2f signed mean err=%.2f worst|err|=%d" % (
    statistics.mean(abs(probe-c) for c in dc),
    statistics.mean(probe-c for c in dc),
    max(abs(probe-c) for c in dc)))

# ---------- register shift ----------
print("REG-SHIFT.W: probe count if creg in d0-d6 (0x5555&63=%d) -> cost %d ; if d7 (=4) -> cost %d" % (
    0x5555&63, shift_cost_word(0x5555&63), shift_cost_word(4)))
print("   PRM word range count 0..63 -> %d..%d" % (shift_cost_word(0), shift_cost_word(63)))
print("REG-SHIFT.L: probe d0-d6 -> %d ; d7 -> %d ; range %d..%d" % (
    shift_cost_long(21), shift_cost_long(4), shift_cost_long(0), shift_cost_long(63)))

print("\n--- DIV restricted to LEGITIMATE (non-overflow) divisions (realistic) ---")
def divu_overflow(dest,src):
    src&=0xFFFF
    if src==0: return True
    return not (src >= (dest>>16))   # overflow if src < dest>>16
nz=[(d,s) for d,s in samp if not divu_overflow(d,s)]
dc=[divu_cost(d,s) for d,s in nz]
probe=divu_cost(0x5555,0x5555)
print("DIVU non-ovf: n=%d probe=%d range %d..%d mean=%.2f mean|err|=%.2f signed=%.2f worst=%d"%(
 len(nz),probe,min(dc),max(dc),sum(dc)/len(dc),
 sum(abs(probe-c) for c in dc)/len(dc), sum(probe-c for c in dc)/len(dc), max(abs(probe-c) for c in dc)))

def divs_ovf(dest,src):
    s=src&0xFFFF
    if s==0: return True
    sn=(s&0x8000)!=0; dn=(dest&0x80000000)!=0; rn=sn!=dn
    asrc=((-(s-0x10000)) if sn else s)&0xFFFFFFFF
    adst=((-(dest-0x100000000)) if dn else dest)&0xFFFFFFFF
    if asrc < (adst>>16): return True
    return (adst//asrc) > (0x8000 if rn else 0x7FFF)
nz=[(d,s) for d,s in samp if not divs_ovf(d,s)]
dc=[divs_cost(d,s) for d,s in nz]
probe=divs_cost(0x5555,0x5555)
print("DIVS non-ovf: n=%d probe=%d range %d..%d mean=%.2f mean|err|=%.2f signed=%.2f worst=%d"%(
 len(nz),probe,min(dc),max(dc),sum(dc)/len(dc),
 sum(abs(probe-c) for c in dc)/len(dc), sum(probe-c for c in dc)/len(dc), max(abs(probe-c) for c in dc)))
