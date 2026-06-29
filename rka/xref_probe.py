import capstone, struct
data = open("rka.bin","rb").read(); N=len(data)
md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)
def rd32(a): return struct.unpack(">I", data[a:a+4])[0]
def dec(a):
    for ins in md.disasm(data[a:a+8], a): return ins
    return None

MISSES = [0x5FE4,0x444E,0x51F4,0x3C44,0x688C,0x80C2,0x4478,0x0C74,
          0x4B94,0x18EC,0x1902,0x1918,0x192E,0x5F76,0x1A2E]

# build index of every aligned/unaligned position whose long == miss addr
for m in MISSES:
    needle = struct.pack(">I", m)            # 00 MM MM MM (top byte 0)
    needle3 = struct.pack(">I", m)[1:]       # MM MM MM (3-byte, for embedded)
    refs = []
    start = 0
    while True:
        i = data.find(needle, start)
        if i < 0: break
        refs.append(i); start = i+1
    # decode a couple instructions before to see the producer
    info = []
    for r in refs[:6]:
        # is r the operand of move.l #imm or pea/lea? check insn starting 2 bytes before
        ctx = ""
        for back in (2,4,6):
            ins = dec(r-back)
            if ins and ins.address + ins.size > r:
                ctx = f"{ins.mnemonic} {ins.op_str} @${ins.address:06X}"
                break
        info.append(f"${r:06X}({ctx or 'data?'})")
    print(f"${m:06X}: {len(refs)} long-refs  " + "  ".join(info))
