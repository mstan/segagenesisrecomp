#!/usr/bin/env python3
"""inspect_jmp_base.py — decode the bytes at a JMP-table base address.

Useful for triaging `INTERIOR_UNRESOLVED` entries from
`<game>_dispatch_audit.log`. For each candidate base, this prints:

  - The raw 16 bytes as offset-table entries (word-as-signed-offset and
    resolved target = base + offset) — useful for spotting offset tables.
  - The raw 16 bytes as 68K opcodes (first word + estimated length) —
    useful for spotting Duff's-device-style uniform sequences (e.g.,
    16 copies of $22C0 = move.l d0,(a1)+).

Usage:
  python tools/inspect_jmp_base.py <rom.bin> <base_hex> [count]

  python tools/inspect_jmp_base.py s2disasm/s2.bin 0x03AD1A 16
"""
from __future__ import annotations
import struct, sys

def main(argv):
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    rom_path = argv[0]
    base = int(argv[1], 0)
    n = int(argv[2]) if len(argv) > 2 else 16

    with open(rom_path, "rb") as f:
        f.seek(base)
        raw = f.read(n * 2)
    words = struct.unpack(f">{n}H", raw)

    print(f"# {n} words at 0x{base:06X} (raw, offset-table, opcode-bytes):")
    print()
    print(f"  {'idx':>4}  {'word':>6}  {'as offset':>11}  {'+base':>8}   raw")
    for i, w in enumerate(words):
        off = w if w < 0x8000 else w - 0x10000
        target = (base + off) & 0xFFFFFF
        print(f"  +{i*2:>3}  0x{w:04X}  {off:+11d}  0x{target:06X}   {raw[i*2]:02X} {raw[i*2+1]:02X}")
    print()
    # Same opcode across many words = Duff's device signature.
    op = words[0]
    same = sum(1 for w in words if w == op)
    if same >= 8:
        print(f"# {same}/{n} words have opcode 0x{op:04X} — likely DUFF'S DEVICE")
        print(f"# (uniform instruction sequence; JMP (PC,Dn.W) dispatches into it)")
    # Most decode to function-entry-like addresses = offset table.
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
