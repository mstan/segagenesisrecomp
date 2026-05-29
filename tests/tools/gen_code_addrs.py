"""
gen_code_addrs.py — emit the full set of INSTRUCTION-START addresses from an
AS .lst (every offset whose first assembled emit is a mnemonic, not a data
directive). One hex address per line, --offset applied.

This is the disasm's "is this address code?" oracle. The recompiler loads it
via game.toml `code_addrs_file` and uses it to gate boundary-split / dispatch-
seed promotion: an extern target that lands on a known DATA address is never
promoted to a function entry. That kills the data-as-code false-positive class
(e.g. Eni_Decomp_Masks, sine tables) without per-address blacklisting.

Run (lock-on halves):
  python gen_code_addrs.py skdisasm/sonic3k.lst --max-addr 0x200000 --offset 0          >  sonic3k.code_addrs.txt
  python gen_code_addrs.py skdisasm/s3.lst      --max-addr 0x200000 --offset 0x200000   >> sonic3k.code_addrs.txt
"""
from __future__ import annotations
import argparse
import re
import sys

EMIT_RE = re.compile(r"^\s*\d+/\s+([0-9A-Fa-f]+)\s*:\s+"
                     r"([0-9A-Fa-f]{2,8}(?:\s+[0-9A-Fa-f]{2,8})*)\s+(\S+)")
DATA_RE = re.compile(r"^(?:dc\.[bwl]|dcb\.[bwl]|ds\.[bwl]|"
                     r"binclude|incbin|even|align|cnop|org)\b", re.IGNORECASE)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("lst")
    ap.add_argument("--max-addr", type=lambda s: int(s, 0), default=0x200000,
                    help="RAW cap, before --offset (default 0x200000)")
    ap.add_argument("--offset", type=lambda s: int(s, 0), default=0)
    args = ap.parse_args()

    code: set[int] = set()
    data: set[int] = set()
    for line in open(args.lst, encoding="utf-8", errors="ignore"):
        m = EMIT_RE.match(line)          # main-listing (non-Z80) emit rows only
        if not m:
            continue
        a = int(m.group(1), 16)
        if a >= args.max_addr or a in code or a in data:
            continue
        (data if DATA_RE.match(m.group(3)) else code).add(a)

    out = sys.stdout
    for a in sorted(code):
        out.write(f"{a + args.offset:06X}\n")
    print(f"# {len(code)} code addrs, {len(data)} data addrs, offset 0x{args.offset:X}",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
