"""Read-only, explicit-address inspection of private character donor evidence."""
import argparse
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("rom", type=Path)
p.add_argument("start", type=lambda s: int(s, 0))
p.add_argument("length", type=lambda s: int(s, 0))
p.add_argument("--animations", action="store_true")
args = p.parse_args()
data = args.rom.read_bytes()
start, end = args.start, args.start + args.length
if not 0 <= start < end <= len(data):
    p.error("range outside input image")
if args.animations:
    count = int.from_bytes(data[start:start + 2], "big") // 2
    if not 0 < count <= 128:
        p.error("invalid animation pointer count")
    for i in range(count):
        address = start + int.from_bytes(data[start + i * 2:start + i * 2 + 2], "big")
        print(f"{i:02x} @{address:06x}: {data[address:address + args.length].hex(' ')}")
else:
    import capstone
    decoder = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)
    for insn in decoder.disasm(data[start:end], start):
        print(f"{insn.address:06x}: {insn.mnemonic:10} {insn.op_str}")
