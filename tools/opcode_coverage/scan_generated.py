#!/usr/bin/env python3
"""
scan_generated.py - Axis-1 (68000 instruction semantics) REAL-GAME EXPOSURE scan.

Static analysis only. Greps the *generated* C translation units of each game
for the exact non-real markers the recompiler's code_generator.c emits when it
fails to produce real instruction semantics, then counts/groups/locates them.

This turns "Axis-1 coverage" from prose into numbers: how many sites in the
shipped generated C hit a stub / MN_OTHER / comment-only / mis-emit path, broken
down by marker kind and (for MN_OTHER) by opcode word, with example locations.

It does NOT modify any recompiler/runner/generated source. Read-only.

Marker provenance (all from recompiler/src/code_generator.c, verified by reading):
  - "unimplemented opcode $XXXX @ $YYYYYY"  -> MN_OTHER / default case (CGD_MN_OTHER)
       The opcode word the decoder could not classify; addr is the ROM addr.
  - "TODO: dynamic JSR/BSR EA m/r"          -> dynamic JSR/BSR mode unsupported
  - "TODO: dynamic JMP mode m/r"            -> dynamic JMP mode unsupported
  - "BRA with no target"                    -> branch decoded without a target
  - "Bcc no target"                         -> branch decoded without a target
  - "cannot store to EA 7/r"                -> store to invalid mode-7 EA (immediate/PC-rel)
  - "cannot store to mode m"                -> store to invalid EA mode
  - "cannot take addr of mode m"            -> LEA/PEA/addr-of on non-addressable EA
  - "unknown EA addr 7/r"                   -> addr-of on unknown mode-7 sub-form

NOTE: of the above, only the first six are routed through codegen_diag (the
--fail-on-unsupported gate). "cannot take addr of mode" and "unknown EA addr"
are comment-only and escape that gate; this scanner catches them anyway.

Usage:
  python scan_generated.py                 # auto-discover all games under the worktree
  python scan_generated.py --root <dir>    # worktree root (default: 3 levels up)
  python scan_generated.py --json out.json # also write machine-readable JSON
"""
import argparse
import json
import os
import re
import sys

# (marker-kind, compiled regex). Each regex may capture opcode/addr/mode/reg.
MARKERS = [
    ("MN_OTHER (unimplemented opcode)",
     re.compile(r"/\* unimplemented opcode \$([0-9A-Fa-f]{4}) @ \$([0-9A-Fa-f]{6}) \*/")),
    ("dynamic JSR/BSR unsupported",
     re.compile(r"/\* TODO: dynamic JSR/BSR EA (\d+)/(\d+) \*/")),
    ("dynamic JMP unsupported",
     re.compile(r"/\* TODO: dynamic JMP mode (\d+)/(\d+) \*/")),
    ("BRA without target",
     re.compile(r"/\* BRA with no target \*/")),
    ("Bcc without target",
     re.compile(r"/\* Bcc no target \*/")),
    ("invalid store EA (mode 7)",
     re.compile(r"/\* cannot store to EA 7/(\d+) \*/")),
    ("invalid store EA (mode)",
     re.compile(r"/\* cannot store to mode (\d+) \*/")),
    ("cannot take addr (mode)",
     re.compile(r"/\* cannot take addr of mode (\d+) \*/")),
    ("unknown EA addr (mode 7)",
     re.compile(r"/\* unknown EA addr 7/(\d+) \*/")),
    # Load-side EA fallbacks (comment-only; NOT routed through codegen_diag).
    ("load unknown EA (mode 7)",
     re.compile(r"0 /\* unknown EA 7/(\d+) \*/")),
    ("load unknown EA (mode)",
     re.compile(r"0 /\* unknown mode (\d+) \*/")),
    ("MOVEM unknown EA",
     re.compile(r"0 /\* MOVEM unknown EA \*/")),
]

# Generated TU basenames per game prefix. We glob for *_full.c (the main body)
# and *_dispatch.c (call_by_address table) under each game's generated/ dir.
GAMES = [
    ("Sonic 1 (S1)",        "sonicthehedgehog"),
    ("Sonic 2 (S2)",        "sonicthehedgehog2"),
    ("Sonic 3 alone (S3)",  "sonic3"),
    ("Sonic 3&K combined",  "sonic3k"),
    ("S&K alone",           "sandk"),
]

ADDR_COMMENT = re.compile(r"/\* \$([0-9A-Fa-f]{6}) \*/")


def is_ascii_word(opcode_hex):
    """Heuristic: a 16-bit opcode whose both bytes are printable ASCII is
    almost certainly ROM text/data decoded as code, not a real instruction."""
    v = int(opcode_hex, 16)
    hi, lo = (v >> 8) & 0xFF, v & 0xFF
    def pr(b):
        return 0x20 <= b <= 0x7E
    return pr(hi) and pr(lo)


def scan_file(path):
    """Return dict: kind -> list of records. Each record carries line number,
    captured fields, and the nearest preceding '/* $addr */' context."""
    results = {k: [] for k, _ in MARKERS}
    last_addr = None
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for lineno, line in enumerate(fh, 1):
            m_addr = ADDR_COMMENT.search(line)
            if m_addr:
                last_addr = m_addr.group(1).upper()
            for kind, rx in MARKERS:
                m = rx.search(line)
                if not m:
                    continue
                rec = {"line": lineno, "groups": [g for g in m.groups()]}
                # MN_OTHER carries its own opcode+addr inline.
                if kind.startswith("MN_OTHER"):
                    rec["opcode"] = m.group(1).upper()
                    rec["addr"] = m.group(2).upper()
                else:
                    rec["addr"] = last_addr
                results[kind].append(rec)
    return results


def find_generated(root, prefix):
    gdir = os.path.join(root, prefix, "generated")
    if not os.path.isdir(gdir):
        return []
    out = []
    for fn in sorted(os.listdir(gdir)):
        if fn.endswith(".c") and (fn.endswith("_full.c") or fn.endswith("_dispatch.c")):
            out.append(os.path.join(gdir, fn))
    return out


def addr_region(addr):
    """Group an address by its top byte (256KB-ish bucket) for clustering."""
    if not addr:
        return "??"
    return addr[:2] + "xxxx"


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    default_root = os.path.abspath(os.path.join(here, "..", ".."))
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=default_root,
                    help="worktree root (default: %(default)s)")
    ap.add_argument("--json", default=None, help="write JSON report to this path")
    ap.add_argument("--examples", type=int, default=5,
                    help="example locations to print per group (default 5)")
    args = ap.parse_args()

    report = {"root": args.root, "games": {}}
    grand_total = 0

    for label, prefix in GAMES:
        files = find_generated(args.root, prefix)
        if not files:
            continue
        game = {"prefix": prefix, "files": [], "by_kind": {}, "total": 0,
                "mn_other_by_opcode": {}, "mn_other_ascii_sites": 0,
                "mn_other_nonascii_sites": 0, "by_region": {}}
        merged = {k: [] for k, _ in MARKERS}
        for path in files:
            res = scan_file(path)
            rel = os.path.relpath(path, args.root).replace("\\", "/")
            fcount = sum(len(v) for v in res.values())
            game["files"].append({"path": rel, "marker_sites": fcount})
            for k in merged:
                for rec in res[k]:
                    rec["file"] = rel
                    merged[k].append(rec)

        total = 0
        for kind, _ in MARKERS:
            recs = merged[kind]
            if not recs:
                continue
            total += len(recs)
            game["by_kind"][kind] = len(recs)
            # region clustering
            for rec in recs:
                reg = addr_region(rec.get("addr"))
                game["by_region"][reg] = game["by_region"].get(reg, 0) + 1
            # MN_OTHER opcode-word breakdown + ASCII heuristic
            if kind.startswith("MN_OTHER"):
                for rec in recs:
                    op = rec["opcode"]
                    game["mn_other_by_opcode"][op] = game["mn_other_by_opcode"].get(op, 0) + 1
                    if is_ascii_word(op):
                        game["mn_other_ascii_sites"] += 1
                    else:
                        game["mn_other_nonascii_sites"] += 1
        game["total"] = total
        game["_examples"] = {}
        for kind, _ in MARKERS:
            recs = merged[kind]
            if recs:
                game["_examples"][kind] = recs[:args.examples]
        report["games"][label] = game
        grand_total += total

    report["grand_total"] = grand_total

    # ---- human-readable output ----
    print("=" * 72)
    print("Axis-1 REAL-GAME EXPOSURE: non-real instruction sites in generated C")
    print("=" * 72)
    for label, prefix in GAMES:
        if label not in report["games"]:
            continue
        g = report["games"][label]
        print(f"\n### {label}   [{prefix}/generated]   TOTAL non-real sites: {g['total']}")
        for f in g["files"]:
            print(f"    {f['path']}: {f['marker_sites']} sites")
        if g["total"] == 0:
            print("    -> CLEAN: zero stub / MN_OTHER / comment-only / mis-emit sites.")
            continue
        print("    by marker kind:")
        for kind in g["by_kind"]:
            print(f"      {kind:38s} {g['by_kind'][kind]:6d}")
        if g["mn_other_by_opcode"]:
            print(f"    MN_OTHER ASCII-looking (data-as-code) sites : {g['mn_other_ascii_sites']}")
            print(f"    MN_OTHER non-ASCII sites                    : {g['mn_other_nonascii_sites']}")
            top = sorted(g["mn_other_by_opcode"].items(), key=lambda x: -x[1])[:12]
            print("    top MN_OTHER opcode words (count) [A=ascii]:")
            for op, c in top:
                a = "A" if is_ascii_word(op) else " "
                print(f"      ${op} {a} : {c}")
        print("    address-region clustering (top byte):")
        for reg, c in sorted(g["by_region"].items(), key=lambda x: -x[1]):
            print(f"      {reg}: {c}")
        print(f"    examples (up to {args.examples} per kind):")
        for kind, recs in g["_examples"].items():
            for rec in recs:
                extra = f"opcode ${rec.get('opcode')}" if rec.get("opcode") else ""
                print(f"      [{kind}] {rec['file']}:{rec['line']} "
                      f"addr=${rec.get('addr')} {extra}")

    print("\n" + "=" * 72)
    print(f"GRAND TOTAL non-real sites across all games: {grand_total}")
    print("=" * 72)

    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump(report, fh, indent=2)
        print(f"\nJSON written to {args.json}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
