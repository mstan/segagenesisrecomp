#!/usr/bin/env python3
"""
gen_annotations_csv.py — extract every named code label from an AS .lst
listing into the CSV format the recompiler's annotations loader expects.

Reads:   <disasm>/<game>.lst   (AS listing)
         <code_addresses.txt>  (gen_l1_fixtures.py side output)
Writes:  <out>.csv             (addr_hex,name,notes)

The recompiler reads this CSV in `annotations.c` and uses it to:
  * name generated C functions (`func_PauseGame` instead of `func_0013A8`)
  * emit a doc comment header above each function

CSV format (matches annotations.c):
    addr_hex,name,notes
    0013A8,PauseGame,

Filtering:
  KEEP — every label whose address is in code_addresses.txt
  SKIP — labels matching loc_HEX, locret_HEX, sub_HEX, byte_HEX, ...
         (interior branch targets / data labels)
  SKIP — leading-dot local labels (`.loop`, `.end`)
  SKIP — equate-only lines (where the listing shows `=$...` for bytes)
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# Label line in the MAIN listing. The leading anchor deliberately does NOT
# accept the `(N)` pass-prefix that AS puts on macro / Z80-sub-listing lines:
# those lines (e.g. `(1) 470/ 38 : (MACRO) zVInt:`) belong to the embedded
# Z80 sound driver, a different CPU with its own address space. Importing
# them as 68K names would mislabel 68K offsets that happen to collide with
# Z80 addresses. The `^\s*\d+/` anchor matches only main 68K-listing lines.
LBL_RE = re.compile(
    r"^\s*\d+/\s+([0-9A-Fa-f]+)\s*:\s+([A-Za-z_.][\w.]*):"
)
# Skip equate-listing lines: AS shows `=$VALUE` after the colon.
EQUATE_RE = re.compile(
    r"^\s*\d+/\s+[0-9A-Fa-f]+\s*:\s+=\$"
)
# An emit line: an address with assembled bytes. Used to classify each
# offset as code (first emit is an instruction) vs data (first emit is a
# data directive) directly from the .lst — same heuristic as
# gen_disasm_labels.py — so a runtime --code-addrs fixture is optional.
EMIT_RE = re.compile(
    r"^\s*\d+/\s+([0-9A-Fa-f]+)\s*:\s+"
    r"([0-9A-Fa-f]{2,8}(?:\s+[0-9A-Fa-f]{2,8})*)\s+"
    r"(\S+)"
)
DATA_DIRECTIVE_RE = re.compile(r"^(?:dc\.[bwl]|dcb\.[bwl]|ds\.[bwl]|"
                               r"binclude|incbin|even|align|cnop|org)$",
                               re.IGNORECASE)


def derive_code_addrs(lst_text: str, max_addr: int) -> set[int]:
    """Code-address set from the .lst itself: every offset whose FIRST
    assembled emit is an instruction (not a data directive). Mirrors
    gen_disasm_labels.scan_lst's classification but kept local so this
    tool stays runnable standalone. Only main 68K-listing lines count."""
    code: set[int] = set()
    data: set[int] = set()
    for line in lst_text.splitlines():
        e = EMIT_RE.match(line)
        if not e:
            continue
        a = int(e.group(1), 16)
        if a >= max_addr or a in code or a in data:
            continue
        if DATA_DIRECTIVE_RE.match(e.group(3)):
            data.add(a)
        else:
            code.add(a)
    return code
# Names we exclude (interior labels, anonymous data labels).
INTERIOR_NAME_RE = re.compile(
    r"^("
    r"loc_[0-9A-Fa-f]+|"
    r"locret_[0-9A-Fa-f]+|"
    r"j_[0-9A-Fa-f]+|"
    r"sub_[0-9A-Fa-f]+|"
    r"byte_[0-9A-Fa-f]+|word_[0-9A-Fa-f]+|off_[0-9A-Fa-f]+|"
    r"\..*"
    r")$",
    re.IGNORECASE,
)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--lst", type=Path, required=True,
                    help="AS .lst listing produced by the disasm build")
    ap.add_argument("--code-addrs", type=Path, default=None,
                    help="optional code_addresses.txt fixture (from "
                         "gen_l1_fixtures.py). If omitted, code addresses are "
                         "derived from the .lst itself (first-emit-is-"
                         "instruction heuristic) — no runtime fixture needed.")
    ap.add_argument("--offset", type=lambda x: int(x, 0), default=0,
                    help="absolute offset added to every emitted address "
                         "(default 0). Use for lock-on / multi-ROM builds "
                         "(e.g. Sonic 3 locked onto S&K maps to 0x200000).")
    ap.add_argument("--max-addr", type=lambda x: int(x, 0), default=0x400000,
                    help="cap on RAW .lst offset, before --offset "
                         "(default 0x400000).")
    ap.add_argument("-o", "--output", type=Path, required=True,
                    help="output annotations CSV")
    ap.add_argument("--header", type=str, default=None,
                    help="override the file's header comment")
    args = ap.parse_args()

    if not args.lst.exists():
        print(f"missing: {args.lst}", file=sys.stderr); return 2

    lst_text = args.lst.read_text(encoding="utf-8", errors="replace")

    if args.code_addrs is not None:
        if not args.code_addrs.exists():
            print(f"missing: {args.code_addrs}", file=sys.stderr); return 2
        code_addrs: set[int] = set()
        for line in args.code_addrs.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#"): continue
            try: code_addrs.add(int(line, 16))
            except ValueError: pass
    else:
        # Fixture-free: classify code vs data straight from the .lst.
        code_addrs = derive_code_addrs(lst_text, args.max_addr)

    addr_names: dict[int, list[str]] = {}
    skipped_interior = 0
    skipped_equate = 0
    skipped_data = 0
    for line in lst_text.splitlines():
        if EQUATE_RE.match(line):
            skipped_equate += 1
            continue
        m = LBL_RE.match(line)
        if not m: continue
        try: a = int(m.group(1), 16)
        except ValueError: continue
        if a >= args.max_addr:
            continue
        name = m.group(2)
        if INTERIOR_NAME_RE.match(name):
            skipped_interior += 1
            continue
        if a not in code_addrs:
            skipped_data += 1
            continue
        names = addr_names.setdefault(a, [])
        if name not in names:
            names.append(name)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    header = args.header if args.header is not None else (
        "# annotations CSV — auto-generated by tests/tools/gen_annotations_csv.py\n"
        "# Format: addr_hex,name,notes\n"
        "# notes column collects aliasing labels at the same address.\n"
    )
    n_written = 0
    with args.output.open("w", encoding="utf-8", newline="\n") as f:
        f.write(header)
        for a in sorted(addr_names):
            names = addr_names[a]
            primary = names[0]
            notes = " / ".join(names[1:]) if len(names) > 1 else ""
            f.write(f"{a + args.offset:06X},{primary},{notes}\n")
            n_written += 1

    print(
        f"gen_annotations_csv: {n_written} (addr,name) entries -> {args.output}\n"
        f"  skipped: interior-name={skipped_interior}, equate={skipped_equate}, "
        f"data-not-code={skipped_data}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
