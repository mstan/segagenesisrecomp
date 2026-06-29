"""Find the code that addresses a jump-table near BASE: scan for lea/pea/movea
that reference any address in [LO,HI], and disasm the dispatcher around them."""
import capstone, struct, sys
data = open("rka.bin","rb").read(); N=len(data)
md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)
md.detail = True

LO = int(sys.argv[1],16) if len(sys.argv)>1 else 0x23C0
HI = int(sys.argv[2],16) if len(sys.argv)>2 else 0x2420

def rd16(a): return struct.unpack(">H", data[a:a+2])[0]
def rd32(a): return struct.unpack(">I", data[a:a+4])[0]

# Disasm a region preceding the table to see the dispatcher.
print(f"### disasm $2380..$23D6 (dispatcher candidate before table)")
for ins in md.disasm(data[0x2380:0x23D6], 0x2380):
    print(f"  ${ins.address:06X}: {ins.bytes.hex():<14} {ins.mnemonic} {ins.op_str}")

# Whole-ROM scan: any instruction whose decoded operand string references an
# absolute or pc-relative address in [LO,HI].
print(f"\n### instructions referencing any addr in [${LO:06X},${HI:06X}]")
a = 0x200; hits=0
while a < N-2 and hits < 60:
    for ins in md.disasm(data[a:a+12], a):
        op = ins.op_str
        # crude: look for hex constants in op_str that fall in range
        import re
        for tok in re.findall(r'0x[0-9a-fA-F]+', op):
            v = int(tok,16)
            if LO <= v <= HI:
                print(f"  ${ins.address:06X}: {ins.mnemonic:<8} {op}")
                hits += 1
        break
    a += 2
print(f"  total: {hits}")
