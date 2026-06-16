"""
rederive_extras.py — remap the hardcoded CODE-address lists in a game.toml
([functions].extra and blacklist) across a disassembly edit that shifts code
addresses (e.g. a widescreen reassembly).

Why: Sonic 1 (and the S3 family) carry residual dispatch targets in
[functions].extra that the seeds/subs/jumptables extractors can't identify —
many are INTERIOR addresses with no label of their own. When the disasm is
patched, every address after the edit shifts, so these hardcodes become wrong.
This tool anchors each address to (nearest global code label, byte offset) in
the OLD listing and resolves that label in the NEW listing, so an interior
target follows its containing function to the new address. RAM addresses
([ram_layout], *_addr) never shift and are left untouched.

Usage:
  python rederive_extras.py --old OLD.lst --new NEW.lst --game game.toml [--write]
  (omit --write for a dry run that prints the remap to stderr)
"""
from __future__ import annotations
import argparse, re, bisect, sys

# .lst format (AS V1.42):  "[(N)]   NNNN/   HEXOFFS : [bytes] [source]"
# Heavily-included disasms (Sonic 1) prefix lines from include files with the
# include nesting level "(1) ", "(2) ", ... — must be tolerated or every
# label/emit inside an include (i.e. most of the code) is missed.
LST_PREFIX = r"^\s*(?:\(\d+\)\s*)?\d+/\s*([0-9A-Fa-f]+)\s*:\s+"
LABEL_RE = re.compile(
    LST_PREFIX +
    r"(?:\(MACRO\)\s*)?(?:=\$[0-9A-Fa-f]+\s+)?"
    r"(\.?[A-Za-z_][A-Za-z0-9_.]*):")
EMIT_RE = re.compile(
    LST_PREFIX +
    r"([0-9A-Fa-f]{2,8}(?:\s+[0-9A-Fa-f]{2,8})*)\s+(\S+)")
DATA_RE = re.compile(r"^(?:dc\.[bwl]|dcb\.[bwl]|ds\.[bwl]|binclude|incbin|"
                     r"even|align|cnop|org)$", re.IGNORECASE)


def scan(path: str, max_addr: int) -> dict[int, tuple[str, bool]]:
    """{addr: (label, is_code)} for every labeled offset. is_code = the first
    assembled emit at that offset is a mnemonic (not a data directive)."""
    labels: dict[int, str] = {}
    code: set[int] = set()
    data: set[int] = set()
    with open(path, "rb") as f:
        raw = f.read().replace(b"\x00", b"")   # tolerate UTF-16 / null padding
    for line in raw.decode("utf-8", "ignore").splitlines():
        m = LABEL_RE.match(line)
        if m:
            a = int(m.group(1), 16)
            if a < max_addr:
                labels.setdefault(a, m.group(2))
            continue
        e = EMIT_RE.match(line)
        if not e:
            continue
        a = int(e.group(1), 16)
        if a in code or a in data:
            continue
        (data if DATA_RE.match(e.group(3)) else code).add(a)
    return {a: (n, a in code) for a, n in labels.items()}


def globals_only(labels: dict[int, tuple[str, bool]]):
    def is_local(n: str) -> bool:
        return n.startswith(".") or n[0] in "+-"
    return {a: n for a, (n, is_code) in labels.items()
            if is_code and not is_local(n)}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--old", required=True, help="listing BEFORE the edit")
    ap.add_argument("--new", required=True, help="listing AFTER the edit")
    ap.add_argument("--game", required=True, help="game.toml to remap")
    ap.add_argument("--max-addr", type=lambda x: int(x, 0), default=0x80000,
                    help="ROM size cap (default 0x80000 = 512KB Sonic 1)")
    ap.add_argument("--write", action="store_true",
                    help="rewrite game.toml in place (default: dry run)")
    args = ap.parse_args()

    old_g = sorted(globals_only(scan(args.old, args.max_addr)).items())  # [(addr,name)]
    old_addrs = [a for a, _ in old_g]
    new_g = globals_only(scan(args.new, args.max_addr))
    new_name2addr: dict[str, int] = {}
    for a in sorted(new_g):                 # first occurrence wins (stable)
        new_name2addr.setdefault(new_g[a], a)

    def remap(addr: int):
        i = bisect.bisect_right(old_addrs, addr) - 1
        if i < 0:
            return None, None, None
        laddr, lname = old_g[i]
        off = addr - laddr
        na = new_name2addr.get(lname)
        if na is None:
            return None, lname, off
        return na + off, lname, off

    lines = open(args.game, encoding="utf-8").read().splitlines()
    out, in_code_block, n_ok, n_warn = [], False, 0, 0
    ADDR_LINE = re.compile(r"^(\s*)0x([0-9A-Fa-f]+)(\s*,.*)?$")
    for line in lines:
        s = line.strip()
        if re.match(r"^(extra|blacklist)\s*=\s*\[", s):
            in_code_block = True; out.append(line); continue
        if in_code_block and s.startswith("]"):
            in_code_block = False; out.append(line); continue
        if in_code_block:
            m = ADDR_LINE.match(line)
            if m:
                old = int(m.group(2), 16)
                new, lname, off = remap(old)
                if new is None:
                    n_warn += 1
                    sys.stderr.write(f"WARN  0x{old:06X}  no remap "
                                     f"(label={lname})  -> kept\n")
                    out.append(line)
                else:
                    n_ok += 1
                    sys.stderr.write(f"  0x{old:06X} -> 0x{new:06X}  "
                                     f"[{lname}+0x{off:X}]\n")
                    out.append(f"{m.group(1)}0x{new:06X}{m.group(3) or ''}")
                continue
        out.append(line)

    sys.stderr.write(f"\nremapped {n_ok} addresses, {n_warn} warnings\n")
    if args.write:
        open(args.game, "w", encoding="utf-8").write("\n".join(out) + "\n")
        sys.stderr.write(f"wrote {args.game}\n")
    else:
        sys.stderr.write("(dry run; pass --write to apply)\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
