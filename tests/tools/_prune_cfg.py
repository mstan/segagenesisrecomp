"""
Rewrite a game.cfg, dropping every `extra_func <addr>` line whose
address is already covered by the auto-extracted side files
(sonic1.disasm_seeds.txt / sonic1.disasm_subs.txt /
sonic1.disasm_jumptables.txt). Also drops out-of-ROM "garbage"
addresses like log-corruption artifacts.

Preserves:
  - Every non-extra_func directive (output_prefix, annotations,
    extra_func_file, jump_table_file, blacklist, vblank_yield, etc.)
  - Comment lines and blank lines in their original positions.
  - extra_func entries the extractors don't cover (residuals).

Run from segagenesisrecomp/:
    python tests/tools/_prune_cfg.py sonicthehedgehog/game.cfg \
        sonic1 \
        sonicthehedgehog/game.cfg.pruned

Compare with:
    diff -u sonicthehedgehog/game.cfg sonicthehedgehog/game.cfg.pruned
"""
from __future__ import annotations
import re
import sys
from pathlib import Path

EXTRA_FUNC_RE = re.compile(r"^(\s*)extra_func\s+(?:0x)?([0-9A-Fa-f]+)\b")

def load_addrs(path: Path) -> set[int]:
    if not path.exists():
        return set()
    out: set[int] = set()
    pat = re.compile(r"^\s*extra_func\s+(?:0x)?([0-9A-Fa-f]+)\b")
    for line in path.read_text(encoding="utf-8").splitlines():
        m = pat.match(line)
        if m:
            try: out.add(int(m.group(1), 16))
            except: pass
    return out

def main() -> int:
    if len(sys.argv) < 4:
        print("Usage: _prune_cfg.py <game.cfg> <prefix> <out.cfg>", file=sys.stderr)
        return 2
    cfg_path = Path(sys.argv[1])
    prefix = sys.argv[2]
    out_path = Path(sys.argv[3])

    cfg_dir = cfg_path.parent
    auto = (
        load_addrs(cfg_dir / f"{prefix}.disasm_seeds.txt") |
        load_addrs(cfg_dir / f"{prefix}.disasm_subs.txt") |
        load_addrs(cfg_dir / f"{prefix}.disasm_jumptables.txt")
    )

    in_lines = cfg_path.read_text(encoding="utf-8").splitlines()
    out_lines: list[str] = []
    dropped_covered = 0
    dropped_garbage = 0
    kept_residual = 0
    for line in in_lines:
        m = EXTRA_FUNC_RE.match(line)
        if not m:
            out_lines.append(line)
            continue
        addr = int(m.group(2), 16)
        # Drop garbage addresses (out of 4 MB Genesis cart range).
        if addr >= 0x400000:
            dropped_garbage += 1
            continue
        # Drop entries the auto-extractors cover.
        if addr in auto:
            dropped_covered += 1
            continue
        # Keep residuals — preserve the original line verbatim.
        out_lines.append(line)
        kept_residual += 1

    out_path.write_text("\n".join(out_lines) + "\n", encoding="utf-8")
    print(f"prune: dropped {dropped_covered} covered + {dropped_garbage} garbage; "
          f"kept {kept_residual} residual extra_func + non-extra_func lines -> {out_path}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
