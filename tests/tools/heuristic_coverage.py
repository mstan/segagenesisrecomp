"""
heuristic_coverage.py — exercise the function-finder against disasm ground truth.

The "over-document then back out" exercise:
  T_func  = disasm global code labels (the real function-entry set).
  T_code  = every address the disasm assembles as an INSTRUCTION (first emit
            at that offset is a mnemonic, not a data directive). The "is this
            address even code?" oracle.
  H       = function set discovered by the recompiler with NO disasm seeds
            (pure heuristic): --dump-functions output of a run using
            game_pure_heuristic.toml.
  F_full  = function set with all disasm seeds: --dump-functions output of a
            run using game.toml.

Reports:
  H \\ T_code   — pure-heuristic FALSE POSITIVES (found non-code; e.g. data
                  tables decoded as functions). The headline quality metric:
                  improving the heuristic must NOT grow this.
  T_func \\ H   — function entries the heuristic MISSES (must stay seeded from
                  the disasm). Shrinking this = a better heuristic.
  F_full \\ T_code — false positives that survive into the REAL build (the
                  ones polluting generated C; root-cause these first).

Usage (from sonic3k/):
  python ../tests/tools/heuristic_coverage.py \\
      --lst skdisasm/sonic3k.lst --lst-offset 0 \\
      --lst skdisasm/s3.lst      --lst-offset 0x200000 \\
      --max-addr 0x200000 \\
      --pure  funcs_pure.txt \\
      --full  funcs_full.txt \\
      --fp-out fp_full.txt
"""
from __future__ import annotations
import argparse
import re
from pathlib import Path

LABEL_RE = re.compile(r"^\s*\d+/\s+([0-9A-Fa-f]+)\s*:\s+(\.?[A-Za-z_][\w.]*):")
EMIT_RE  = re.compile(r"^\s*\d+/\s+([0-9A-Fa-f]+)\s*:\s+"
                      r"([0-9A-Fa-f]{2,8}(?:\s+[0-9A-Fa-f]{2,8})*)\s+(\S+)")
DATA_RE  = re.compile(r"^(?:dc\.[bwl]|dcb\.[bwl]|ds\.[bwl]|"
                      r"binclude|incbin|even|align|cnop|org)\b", re.IGNORECASE)


def scan_lst(path: str, max_addr: int, offset: int):
    """Return (code_addrs, func_labels) in absolute (offset-applied) space.
    code_addrs: offsets whose first emit is an instruction.
    func_labels: global (non-dotted) code labels = function-entry candidates."""
    code: set[int] = set()
    data: set[int] = set()
    label_at: dict[int, str] = {}
    for line in open(path, encoding="utf-8", errors="ignore"):
        m = LABEL_RE.match(line)
        if m:
            a = int(m.group(1), 16)
            if a < max_addr:
                label_at.setdefault(a, m.group(2))
            continue
        e = EMIT_RE.match(line)
        if not e:
            continue
        a = int(e.group(1), 16)
        if a >= max_addr or a in code or a in data:
            continue
        (data if DATA_RE.match(e.group(3)) else code).add(a)
    code_abs = {a + offset for a in code}
    # function-entry labels = global (non-dotted) labels at code addresses
    func_abs = {a + offset for a, n in label_at.items()
                if a in code and not n.startswith(".")}
    return code_abs, func_abs


def load_addrs(path: str) -> set[int]:
    out: set[int] = set()
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        try:
            out.add(int(line, 16))
        except ValueError:
            pass
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--lst", action="append", required=True, help="AS .lst (repeatable)")
    ap.add_argument("--lst-offset", action="append", default=None,
                    help="offset per --lst (repeatable, parallel to --lst)")
    ap.add_argument("--max-addr", type=lambda s: int(s, 0), default=0x200000,
                    help="per-half RAW cap (default 0x200000)")
    ap.add_argument("--pure", required=True, help="pure-heuristic --dump-functions output (H)")
    ap.add_argument("--full", default=None, help="disasm-seeded --dump-functions output (F_full)")
    ap.add_argument("--fp-out", default=None, help="write F_full\\T_code FP addresses here")
    ap.add_argument("--miss-out", default=None, help="write T_func\\H heuristic-miss addresses here")
    args = ap.parse_args()

    offsets = [int(x, 0) for x in (args.lst_offset or ["0"] * len(args.lst))]
    T_code: set[int] = set()
    T_func: set[int] = set()
    for lst, off in zip(args.lst, offsets):
        c, fn = scan_lst(lst, args.max_addr, off)
        T_code |= c
        T_func |= fn

    H = load_addrs(args.pure)
    print(f"T_code (disasm instruction addrs): {len(T_code)}")
    print(f"T_func (disasm function labels)  : {len(T_func)}")
    print(f"H      (pure-heuristic funcs)     : {len(H)}")
    print()
    h_fp   = H - T_code
    h_hit  = H & T_func
    h_miss = T_func - H
    print(f"H & T_func  (heuristic finds real func entry): {len(h_hit)}")
    print(f"T_func \\ H  (heuristic MISSES; stay seeded)  : {len(h_miss)}")
    print(f"H \\ T_code  (pure-heuristic FALSE POSITIVES) : {len(h_fp)}")
    if h_fp:
        print("  sample FP addrs:", " ".join(f"{a:06X}" for a in sorted(h_fp)[:20]))

    if args.full:
        F = load_addrs(args.full)
        f_fp = F - T_code
        print()
        print(f"F_full (disasm-seeded funcs)      : {len(F)}")
        print(f"F_full \\ T_code (FPs in REAL build): {len(f_fp)}")
        if f_fp:
            print("  sample:", " ".join(f"{a:06X}" for a in sorted(f_fp)[:20]))
        if args.fp_out:
            Path(args.fp_out).write_text(
                "".join(f"{a:06X}\n" for a in sorted(f_fp)), encoding="utf-8")
            print(f"  wrote {len(f_fp)} FP addrs -> {args.fp_out}")

    if args.miss_out:
        Path(args.miss_out).write_text(
            "".join(f"{a:06X}\n" for a in sorted(h_miss)), encoding="utf-8")
        print(f"wrote {len(h_miss)} heuristic-miss addrs -> {args.miss_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
