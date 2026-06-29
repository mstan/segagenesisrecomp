import capstone, struct
data = open("rka.bin","rb").read()
md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)

print("=== dispatcher tail $1944..$1978 ===")
for insn in md.disasm(data[0x1944:0x1978], 0x1944):
    print(f"  ${insn.address:06X}: {insn.bytes.hex():<12} {insn.mnemonic} {insn.op_str}")

print("\n=== jump table at $1976 (16 longs) ===")
for i in range(16):
    a = 0x1976 + i*4
    v = struct.unpack(">I", data[a:a+4])[0]
    print(f"  [{i:2}] @${a:06X} -> ${v:06X}")
