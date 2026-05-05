"""
gen_disasm_labels.py — extract EVERY labeled address from an AS .lst
listing as an extra-function entry candidate, emitted as TOML.

Why: the assembled .lst is the ground truth — it pairs each symbol
with its byte-offset in the ROM as decided by the assembler itself.
That covers static JSR/BSR targets, jumptable-entry labels, per-routine
sub-labels, and inlined object bodies. No heuristic auto-walker needed.
The recompiler's function finder discards labels that don't decode as
instructions; we additionally pre-filter labels whose first assembled
line at that offset is a data directive.

Run:
    python gen_disasm_labels.py s2disasm/s2.lst > sonic2.discovery.toml

Then in game.toml reference the file:
    [game]
    discovery_files = ["sonic2.discovery.toml"]

.lst format (AS V1.42):
       NNNN/   HEXOFFS :                     LabelName:
where HEXOFFS is the absolute ROM byte offset.
"""
from __future__ import annotations
import argparse
import re
import sys

# Disasm labels include curly quotes / em-dashes inherited from comments;
# force UTF-8 stdout so redirection to a TOML file produces clean bytes.
try: sys.stdout.reconfigure(encoding="utf-8", newline="\n")
except Exception: pass

LABEL_RE = re.compile(
    r"^\s*\d+/\s*([0-9A-Fa-f]+)\s*:\s+"   # lineno / hex_offset :
    r"(?:\(MACRO\)\s*)?"                    # optional (MACRO) tag
    r"(?:=\$[0-9A-Fa-f]+\s+)?"              # equ-style assignments aren't labels
    r"([A-Za-z_][A-Za-z0-9_.]*):"          # label terminated by colon
)
# A line that shows the assembler emitted bytes at this address. Two shapes:
#   "    1234/   ABCD : 46FC 2300           \tmove ..."  (instruction)
#   "    1235/   ABD0 : 0008                       dc.w ..."  (data)
EMIT_RE = re.compile(
    r"^\s*\d+/\s*([0-9A-Fa-f]+)\s*:\s+"
    r"([0-9A-Fa-f]{2,8}(?:\s+[0-9A-Fa-f]{2,8})*)\s+"  # one or more hex byte words
    r"(\S+)"                                            # the directive / mnemonic
)
DATA_DIRECTIVE_RE = re.compile(r"^(?:dc\.[bwl]|dcb\.[bwl]|ds\.[bwl]|"
                               r"binclude|incbin|even|align|cnop|org)$",
                               re.IGNORECASE)


def scan_lst(path: str, max_addr: int):
    """Return {addr: (label, is_code)} for every offset that has a label.

    is_code = True iff the FIRST assembler-emitted line at this offset is
    an instruction (mnemonic). False if it's a data directive.
    Labels with no following emit (pure equ symbols, end-of-file) are
    treated as is_code=False (recompiler can't translate them anyway).
    """
    labels: dict[int, str] = {}
    code_offsets: set[int] = set()
    data_offsets: set[int] = set()
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = LABEL_RE.match(line)
            if m:
                addr = int(m.group(1), 16)
                if addr < max_addr:
                    labels.setdefault(addr, m.group(2))
                continue
            e = EMIT_RE.match(line)
            if not e: continue
            addr = int(e.group(1), 16)
            if addr in code_offsets or addr in data_offsets:
                continue  # only the FIRST emit per offset decides
            directive = e.group(3)
            if DATA_DIRECTIVE_RE.match(directive):
                data_offsets.add(addr)
            else:
                code_offsets.add(addr)
    return {addr: (name, addr in code_offsets)
            for addr, name in labels.items()}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("lst", help="path to AS-emitted .lst file")
    ap.add_argument("--max-addr", type=lambda x: int(x, 0), default=0x100000,
                    help="cap to ROM size (default 0x100000 = 1 MB Sonic 2)")
    ap.add_argument("--name-filter", default=None,
                    help="optional regex to skip labels (e.g. '^(byte|word)_')")
    args = ap.parse_args()

    skip_re = re.compile(args.name_filter) if args.name_filter else None
    labels = scan_lst(args.lst, args.max_addr)
    code_labels = {a: n for a, (n, is_code) in labels.items() if is_code}
    if skip_re:
        code_labels = {a: n for a, n in code_labels.items() if not skip_re.search(n)}
    skipped_data = sum(1 for _, is_code in labels.values() if not is_code)

    print("# Auto-extracted from AS .lst by gen_disasm_labels.py.")
    print("# Every labeled CODE byte-offset in the ROM is emitted as an")
    print("# additional function-entry candidate. Labels at addresses whose")
    print("# first assembled line is a data directive (dc.b/w/l, ds.b/w/l,")
    print("# binclude, etc.) are filtered - emitting those would let the")
    print("# recompiler decode data bytes as 68K instructions and synthesize")
    print("# calls into garbage addresses.")
    print(f"# Source: {args.lst}")
    print(f"# Code labels emitted: {len(code_labels)}")
    print(f"# Data labels skipped: {skipped_data}")
    print()
    print("[functions]")
    print("extra = [")
    for addr in sorted(code_labels):
        print(f"    0x{addr:06X},   # {code_labels[addr]}")
    print("]")
    return 0

if __name__ == "__main__":
    sys.exit(main())
