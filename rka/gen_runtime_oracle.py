#!/usr/bin/env python3
"""Decode the oracle's always-on executed-PC coverage dump (ECOV) into the
discovery runtime-oracle artifacts.

The oracle build (RKARecomp_oracle) accumulates, over a whole session, a
non-evicting bitmap of every instruction-fetch PC and writes it via
  RKARecomp_oracle.exe rka.bin --no-launcher --turbo --max-frames N \
      --exec-coverage-out rka_exec_cov.bin

This is the *guaranteed-code positive set* (a candidate target whose bit is set
is definitely code — ChatGPT's runtime acceptance signal) plus the RAM-execution
set that flags RAM-resident computed-jump targets which must route to the
interpreter, never a static function.

Outputs:
  rka_executed_pcs.txt   sorted hex ROM instruction-start addresses
  rka_ram_targets.txt    sorted hex WRAM instruction-start addresses

Also reports overlap with the discovered function list and whether the known
baseline dispatch/interior-label miss targets are runtime-confirmed.
"""
import argparse, struct, sys

# Baseline 4000-frame attract misses (from triage_misses.py / DEVELOPMENT.md).
KNOWN_MISSES = {
    0x005F2E: "JSR word-table @ $5F12",
    0x005F42: "JSR word-table @ $5F12",
    0x00C7A6: "JSR word-table @ $C74C",
    0x02FE20: "$23D8 object table idx 83 (beyond cap 64)",
    0x012F8A: "$23D8 object table idx 101 (beyond cap 64)",
    0x008992: "1-entry long pointer @ $898E",
    0xFFB1F2: "RAM-resident computed-jump target",
}


def decode_ecov(path):
    """Return (rom_window, set(rom_pcs), set(ram_pcs), stats dict)."""
    with open(path, "rb") as f:
        blob = f.read()
    if blob[:4] != b"ECOV":
        sys.exit(f"{path}: bad magic {blob[:4]!r} (expected b'ECOV')")
    (version, rom_window, ram_window, rom_bytes, ram_bytes,
     rom_hits, ram_hits, other) = struct.unpack_from("<IIIIIQQQ", blob, 4)
    off = 4 + struct.calcsize("<IIIIIQQQ")
    rom_bm = blob[off:off + rom_bytes]
    ram_bm = blob[off + rom_bytes:off + rom_bytes + ram_bytes]

    def bits_to_addrs(bm, base):
        out = set()
        for byte_i, byte in enumerate(bm):
            if not byte:
                continue
            for b in range(8):
                if byte & (1 << b):
                    out.add(base + ((byte_i * 8 + b) * 2))
        return out

    rom_pcs = bits_to_addrs(rom_bm, 0x000000)
    ram_pcs = bits_to_addrs(ram_bm, 0xFF0000)
    stats = dict(version=version, rom_window=rom_window, ram_window=ram_window,
                 rom_hits=rom_hits, ram_hits=ram_hits, other=other)
    return rom_window, rom_pcs, ram_pcs, stats


def load_funcs(path):
    funcs = set()
    try:
        with open(path) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#"):
                    funcs.add(int(line, 16))
    except FileNotFoundError:
        return None
    return funcs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cov", default="rka_exec_cov.bin")
    ap.add_argument("--funcs", default="rka_funcs.txt",
                    help="discovered function entries (one hex/line)")
    ap.add_argument("--out-pcs", default="rka_executed_pcs.txt")
    ap.add_argument("--out-ram", default="rka_ram_targets.txt")
    args = ap.parse_args()

    rom_window, rom_pcs, ram_pcs, stats = decode_ecov(args.cov)

    with open(args.out_pcs, "w") as f:
        f.write("# executed ROM instruction-start PCs (oracle runtime oracle)\n")
        for a in sorted(rom_pcs):
            f.write(f"{a:06X}\n")
    with open(args.out_ram, "w") as f:
        f.write("# executed WRAM instruction-start PCs (RAM-resident code)\n")
        for a in sorted(ram_pcs):
            f.write(f"{a:06X}\n")

    print(f"ECOV v{stats['version']}: rom_window={stats['rom_window']:#x} "
          f"rom_hits={stats['rom_hits']} ram_hits={stats['ram_hits']} "
          f"other={stats['other']}")
    print(f"executed: {len(rom_pcs)} ROM PCs -> {args.out_pcs}; "
          f"{len(ram_pcs)} WRAM PCs -> {args.out_ram}")

    funcs = load_funcs(args.funcs)
    if funcs is not None:
        entries_run = funcs & rom_pcs
        print(f"\ndiscovered functions: {len(funcs)}; "
              f"observed executing (entry PC in set): {len(entries_run)} "
              f"({100*len(entries_run)//max(1,len(funcs))}%)")
        # Executed PCs that are NOT a discovered function entry are either
        # interior instruction boundaries of known funcs or undiscovered code.
        non_entry = len(rom_pcs - funcs)
        print(f"executed ROM PCs that are not a discovered entry: {non_entry} "
              f"(interior boundaries + undiscovered code)")

    print("\nbaseline miss targets - runtime-confirmed as real code?")
    for addr in sorted(KNOWN_MISSES):
        if addr >= 0xFF0000:
            hit = addr in ram_pcs
            region = "RAM"
        else:
            hit = addr in rom_pcs
            region = "ROM"
        mark = "YES" if hit else "no "
        print(f"  ${addr:06X} [{region}] observed={mark}  {KNOWN_MISSES[addr]}")


if __name__ == "__main__":
    main()
