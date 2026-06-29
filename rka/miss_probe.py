"""Root-cause a single dispatch miss: how is TARGET reached, and why did
function discovery miss it? Literal oracle (capstone over raw rka.bin)."""
import capstone, struct, sys

ROM = "rka.bin"
data = open(ROM, "rb").read(); N = len(data)
md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)

TARGET = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0x23D6

def rd16(a): return struct.unpack(">H", data[a:a+2])[0]
def rd32(a): return struct.unpack(">I", data[a:a+4])[0]
def dec1(a):
    for ins in md.disasm(data[a:a+8], a): return ins
    return None

print(f"### TARGET ${TARGET:06X}")
print("### disasm at target")
for ins in md.disasm(data[TARGET:TARGET+32], TARGET):
    print(f"  ${ins.address:06X}: {ins.bytes.hex():<14} {ins.mnemonic} {ins.op_str}")

# 1) absolute-long pointer table entries == TARGET (00 xx xx xx)
print("\n### long-pointer (abs.l) refs  (dword == target)")
needle = struct.pack(">I", TARGET)
start = 0; longrefs = []
while True:
    i = data.find(needle, start)
    if i < 0: break
    longrefs.append(i); start = i + 1
for r in longrefs[:30]:
    print(f"  @${r:06X}  (table slot? neighbors: "
          f"[-4]={rd32(r-4):08X} [+4]={rd32(r+4):08X})")
print(f"  total long-refs: {len(longrefs)}")

# 2) pcrel16 table entries: a word W at addr A such that A + (int16)W == TARGET,
#    AND some JMP/JSR (d8,PC,Xn) has a base near A.
print("\n### pcrel16 table-slot candidates  (A + int16(word) == target)")
hits = []
for A in range(max(0, TARGET-0x4000), min(N-2, TARGET+0x4000), 2):
    w = rd16(A); off = w - 0x10000 if w >= 0x8000 else w
    if (A + off) == TARGET:
        hits.append(A)
for A in hits[:30]:
    print(f"  slot @${A:06X} word={rd16(A):04X}")
print(f"  total pcrel16 slot candidates near target: {len(hits)}")

# 3) two-step dispatch tables (movea.l (d8,PC,Xn),aN; jmp/jsr(aN)) whose long
#    walk would include TARGET — scan whole rom for the movea opcode.
print("\n### two-step movea.l(d8,PC,Xn),aN sites whose table covers target")
a = 0x200
found = 0
while a < N-4:
    w = rd16(a)
    if (w & 0xF1FF) == 0x207B:           # movea.l <ea>,aN, ea=(d8,PC,Xn)
        areg = (w >> 9) & 7
        d8 = rd16(a+2) & 0xFF
        if d8 >= 0x80: d8 -= 0x100
        base = a + 2 + d8
        # walk longs with leading-skip tolerance (mirror jt_enumerate_long)
        targets = []; valid_seen = 0; lead = 0
        for i in range(64):
            ea = base + i*4
            if ea+4 > N: break
            raw = rd32(ea)
            ok = raw != 0 and not (raw & 1) and raw < N and dec1(raw) is not None
            if not ok:
                if valid_seen: break
                if i >= 4: break
                continue
            valid_seen += 1; targets.append(raw)
        if TARGET in targets:
            found += 1
            print(f"  movea@${a:06X} table=${base:06X} a{areg} -> idx "
                  f"{targets.index(TARGET)} of {len(targets)}: {[hex(t) for t in targets]}")
    a += 2
if not found:
    print("  (none)")
