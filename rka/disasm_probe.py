import capstone, sys

ROM = "rka.bin"
data = open(ROM, "rb").read()
md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)

MISSES = [0x5FE4,0x444E,0x51F4,0x3C44,0x688C,0x80C2,0x4478,0x0C74,
          0x4B94,0x18EC,0x1902,0x1918,0x192E,0x5F76,0x1A2E]
MS = set(MISSES)

def dump(start, end, label=""):
    print(f"\n===== {label}  ${start:06X}..${end:06X} =====")
    code = data[start:end]
    for insn in md.disasm(code, start):
        mark = "  <== MISS TARGET" if insn.address in MS else ""
        print(f"  ${insn.address:06X}: {insn.bytes.hex():<12} {insn.mnemonic} {insn.op_str}{mark}")

# windows around clustered/representative misses
dump(0x18C0, 0x1960, "cluster stride-0x16 (18EC/1902/1918/192E)")
dump(0x1A00, 0x1A60, "1A2E")
dump(0x5F40, 0x6020, "5F76 / 5FE4")
dump(0x4420, 0x44A0, "444E / 4478")
dump(0x3C20, 0x3C80, "3C44")
