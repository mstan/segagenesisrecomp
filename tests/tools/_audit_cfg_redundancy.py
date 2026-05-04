"""
Audit which extra_func entries in a game.cfg are redundant — already
covered by gen_disasm_seeds.txt (static JSR/BSR walk),
gen_disasm_subs.txt (named subroutine extraction), or
gen_disasm_jumptables.txt (jump-table extraction).

Reports:
  - covered: count + sample of cfg entries the auto-extraction already finds
  - residual: every cfg entry NOT found by any extractor
  - garbage: cfg entries with addresses outside ROM (likely log corruption)

Run from repo root:
    python tests/tools/_audit_cfg_redundancy.py sonicthehedgehog/game.cfg
"""
from __future__ import annotations
import re
import sys
from pathlib import Path

CFG_RE = re.compile(r"^\s*extra_func\s+(?:0x)?([0-9A-Fa-f]+)\b")

def load_addrs(path: Path, prefix: bool = False) -> set[int]:
    if not path or not path.exists():
        return set()
    out: set[int] = set()
    pat = (re.compile(r"^\s*extra_func\s+(?:0x)?([0-9A-Fa-f]+)\b") if prefix
           else re.compile(r"^\s*extra_func\s+([0-9A-Fa-f]+)\b"))
    for line in path.read_text(encoding="utf-8").splitlines():
        m = pat.match(line)
        if m:
            try: out.add(int(m.group(1), 16))
            except: pass
    return out

def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: _audit_cfg_redundancy.py <game.cfg>", file=sys.stderr)
        return 2
    cfg = Path(sys.argv[1])
    if not cfg.exists():
        print(f"missing: {cfg}", file=sys.stderr); return 2
    cfg_dir = cfg.parent

    # Parse cfg for in-band extra_func entries (line-by-line, with comments).
    cfg_entries: list[tuple[int, int, str]] = []  # (line_no, addr, comment)
    cfg_addrs: set[int] = set()
    for ln, line in enumerate(cfg.read_text(encoding="utf-8").splitlines(), 1):
        m = CFG_RE.match(line)
        if not m: continue
        try: addr = int(m.group(1), 16)
        except: continue
        comment = ""
        if "#" in line:
            comment = line[line.index("#"):].rstrip()
        cfg_entries.append((ln, addr, comment))
        cfg_addrs.add(addr)

    # Load auto-extracted addresses from the side files referenced by the cfg.
    seeds_path = cfg_dir / "sonic1.disasm_seeds.txt"
    subs_path  = cfg_dir / "sonic1.disasm_subs.txt"
    jt_path    = cfg_dir / "sonic1.disasm_jumptables.txt"

    seeds = load_addrs(seeds_path)
    subs  = load_addrs(subs_path)
    jt    = load_addrs(jt_path)
    auto  = seeds | subs | jt

    # Classify
    covered_seeds = sorted(cfg_addrs & seeds)
    covered_subs  = sorted(cfg_addrs & subs - seeds)
    covered_jt    = sorted(cfg_addrs & jt - seeds - subs)
    residual      = sorted(cfg_addrs - auto)
    garbage       = [a for a in cfg_addrs if a >= 0x400000]

    print(f"cfg:         {len(cfg_addrs)} unique extra_func addresses")
    print(f"  seeds:     {len(seeds)} addresses (gen_disasm_seeds.txt)")
    print(f"  subs:      {len(subs)} addresses (gen_disasm_subs.txt)")
    print(f"  jt:        {len(jt)} addresses (gen_disasm_jumptables.txt)")
    print(f"  auto-union:{len(auto)} addresses")
    print()
    print(f"covered by seeds:        {len(covered_seeds):4d}")
    print(f"covered by subs (only):  {len(covered_subs):4d}")
    print(f"covered by jumptables:   {len(covered_jt):4d}  <-- new bra extractor")
    print(f"residual (cfg only):     {len(residual):4d}")
    print(f"garbage (out-of-rom):    {len(garbage):4d}")
    print()

    if covered_jt:
        print("Sample covered-by-jumptables entries (cfg can drop these):")
        for a in covered_jt[:10]:
            print(f"  ${a:06X}")
        if len(covered_jt) > 10:
            print(f"  ... and {len(covered_jt)-10} more")
        print()

    if garbage:
        print(f"Garbage addresses (out of 4MB ROM range):")
        for a in garbage:
            print(f"  ${a:08X}")
        print()

    if residual:
        print(f"Residual ({len(residual)} entries — neither auto-extractor catches these).")
        print(f"Full list:")
        for a in residual:
            for ln, addr, comment in cfg_entries:
                if addr == a:
                    print(f"  ${a:06X}   line {ln:4d}   {comment}")
                    break
    return 0

if __name__ == "__main__":
    sys.exit(main())
