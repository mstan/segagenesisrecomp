import capstone
data = open("rka.bin","rb").read()
md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)
def dump(s,e,label):
    print(f"\n===== {label} ${s:06X}..${e:06X} =====")
    for ins in md.disasm(data[s:e], s):
        print(f"  ${ins.address:06X}: {ins.bytes.hex():<12} {ins.mnemonic} {ins.op_str}")
dump(0x208, 0x208+0x70, "entry/reset")
dump(0x5E0, 0x770, "abs.l trampoline region (callers of 5FE4/444E/...)")
