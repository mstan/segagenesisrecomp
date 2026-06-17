#!/usr/bin/env python3
"""audit_runner_purity.py — scan shared runner code for per-game leakage.

The shared runner (`segagenesisrecomp/runner/`) is supposed to be
game-agnostic: per-game data flows through `g_game_spec` (function hooks)
and `g_game_layout` (WRAM addresses) only. This script greps for patterns
that violate that contract:

  - Genesis WRAM literal addresses: `0xFF[0-9A-F]{4}`, `0x00FF[0-9A-F]{4}`,
    `$FF[0-9A-F]{4}` (68K disasm syntax in comments).
  - Decimal equivalents of known per-game addresses (vetted list).
  - Per-game function names harvested from each game's spec file.
  - Per-game identifier substrings (`nemesis`, `nem_dec`, `plc_buffer`,
    `dynamic_object_ram`, `kosinski`, `enigma`, `saxman`).

Per-game files no longer live in `runner/` after the runner-promotion
refactor; they're under `sonicthehedgehog/` and `sonicthehedgehog2/`.

Hits are reported, not failed. The audit is a manual pre-commit gate
when touching shared runner code.

There is no GitHub Actions / cloud CI for this project (the build needs a
ROM that can't be checked in upstream). All checks are local.

Usage:
  python tools/audit_runner_purity.py [--root PATH] [--verbose]

Default root is `runner/` inside the submodule.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

# Files inside runner/ that would be excluded from the audit if any
# per-game code lived there. Empty after the runner-promotion refactor
# moved sonic1_spec.c, sonic_extras.{c,h}, sonic1_hybrid_table.c, and
# sonic2_hybrid_table.c into their per-game directories.
PER_GAME_FILES_EXCLUDE: set[str] = set()

# Files we never want to scan (artifacts, third-party, generated).
# "deps" excludes the vendored RmlUi/FreeType launcher dependency tree
# (runner/launcher/deps) — permissive third-party, not our runner code.
PATH_EXCLUDE_DIRS = {"build", "external", "audio/external", "deps"}

# File extensions we scan.
SCAN_EXTS = {".c", ".cc", ".cpp", ".h", ".hh", ".hpp"}

# Vetted list of decimal equivalents for known per-game WRAM addresses.
# We don't flag arbitrary decimals (would have too many false positives);
# only those that match a Sonic-1 / Sonic-2 / Sonic-3K WRAM constant.
VETTED_DECIMAL_HITS = {
    16774144: "$FFF600 (Game_Mode)",
    16776192: "$FFFE00 (initial_ssp / stacks)",
    16776202: "$FFFE0A",
    16776204: "$FFFE0C (Vint_runcount)",
    16774186: "$FFF62A (S1 vint_routine)",
    16755200: "$FFAA00 (S1 NemDec code table)",
    16764928: "$FFD000 (S1 player_object / dynamic_object_ram)",
    16756736: "$FFB000 (S2 player_object)",
    16769024: "$FFEC00",
}

# Substrings that indicate per-game compression / data formats which
# should be parameterized through `[memory_regions]` or per-game hooks.
PER_GAME_IDENT_SUBSTRINGS = (
    "nemesis", "nem_dec", "nem_read", "nem_entry", "nem_zero", "nem_rom",
    "kosinski", "kosinski_moduled",
    "saxman",
    "enigma",
    "plc_buffer", "plc_pending",   # plc_pending is in g_game_layout, OK; plc_buffer literal is not
    "dynamic_object_ram",
)

# Regexes for hex / disasm-syntax literals.
RE_HEX_8 = re.compile(r"\b0x00FF[0-9A-Fa-f]{4}\b")
RE_HEX_4 = re.compile(r"\b0xFF[0-9A-Fa-f]{4}\b")
RE_DOLLAR = re.compile(r"\$FF[0-9A-Fa-f]{4}\b")
# Plain decimal — we look up against VETTED_DECIMAL_HITS only.
RE_DECIMAL = re.compile(r"\b1677[0-9]{4}\b")

# Function-name pattern. We harvest per-game names from spec files and
# also flag any free-standing func_NNNNNN identifier in shared code (since
# those should all route through `g_game_spec.call_*` callbacks, not be
# named directly).
RE_FUNC_NAME = re.compile(r"\bfunc_[0-9A-Fa-f]{4,8}\b")


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def harvest_func_names(spec_paths: list[Path]) -> set[str]:
    """Extract `func_XXXX` names referenced from per-game spec files."""
    names: set[str] = set()
    for p in spec_paths:
        if not p.is_file():
            continue
        try:
            text = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for m in RE_FUNC_NAME.finditer(text):
            names.add(m.group(0))
    return names


def should_scan(path: Path, root: Path) -> bool:
    if path.suffix.lower() not in SCAN_EXTS:
        return False
    rel = path.relative_to(root)
    parts = set(rel.parts)
    if parts & PATH_EXCLUDE_DIRS:
        return False
    if path.name in PER_GAME_FILES_EXCLUDE:
        return False
    return True


def scan_file(path: Path, per_game_funcs: set[str]) -> list[tuple[int, str, str]]:
    """Returns list of (line_no, kind, snippet) hits."""
    hits: list[tuple[int, str, str]] = []
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return hits

    for line_no, line in enumerate(text.splitlines(), start=1):
        # Skip comment-only lines that are clearly documenting addresses
        # rather than executing on them. (We still flag $FF... in inline
        # comments because those are usually code-adjacent claims.)
        stripped = line.lstrip()
        is_pure_comment = stripped.startswith(("//", "*", "/*"))
        snippet = line.rstrip()

        # Lines that already route through g_game_layout / g_game_spec are
        # correct usages — substring matches like "plc_pending_addr" inside
        # `g_game_layout.plc_pending_addr` are noise.
        if "g_game_layout." in line or "g_game_spec." in line:
            # Still flag literal hex / dollar literals on the same line
            # (e.g., a line that reads g_game_layout AND hardcodes a
            # backup address), but skip the ident-substring scan.
            skip_ident = True
        else:
            skip_ident = False

        for m in RE_HEX_8.finditer(line):
            hits.append((line_no, "hex8", f"{m.group(0)}  -- {snippet}"))
        for m in RE_HEX_4.finditer(line):
            # 0xFFxxxx is also legitimately Genesis I/O ($A11100 etc.) —
            # the audit complains only about WRAM range $FF0000-$FFFFFF.
            val = int(m.group(0), 16)
            if 0xFF0000 <= val <= 0xFFFFFF:
                hits.append((line_no, "hex4_wram", f"{m.group(0)}  -- {snippet}"))
        for m in RE_DOLLAR.finditer(line):
            hits.append((line_no, "dollar_wram", f"{m.group(0)}  -- {snippet}"))
        for m in RE_DECIMAL.finditer(line):
            try:
                v = int(m.group(0))
            except ValueError:
                continue
            if v in VETTED_DECIMAL_HITS:
                hits.append((line_no, "decimal_wram",
                             f"{m.group(0)} = {VETTED_DECIMAL_HITS[v]}  -- {snippet}"))

        # Per-game function names in shared runner = anti-pattern (Wave 1
        # already migrated glue.c to g_game_spec; remaining references are
        # stale comments or new bugs).
        for m in RE_FUNC_NAME.finditer(line):
            name = m.group(0)
            if name in per_game_funcs:
                tag = "per_game_func_comment" if is_pure_comment else "per_game_func"
                hits.append((line_no, tag, f"{name}  -- {snippet}"))

        # Per-game identifier substrings (case-insensitive).
        if not skip_ident:
            low = line.lower()
            for sub in PER_GAME_IDENT_SUBSTRINGS:
                if sub in low:
                    hits.append((line_no, f"per_game_ident:{sub}", snippet))

    return hits


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--root", type=Path, default=None,
                   help="Shared runner directory to audit (default: "
                        "runner/ inside this file's parent submodule).")
    p.add_argument("--verbose", action="store_true",
                   help="Print every hit with full snippet.")
    args = p.parse_args(argv)

    here = Path(__file__).resolve().parent          # segagenesisrecomp/tools
    submodule_root = here.parent                    # segagenesisrecomp/
    runner_root = args.root or (submodule_root / "runner")

    if not runner_root.is_dir():
        print(f"[audit] runner root not found: {runner_root}", file=sys.stderr)
        return 2

    # Harvest per-game function names from the spec files we know about.
    spec_paths = [
        submodule_root / "sonicthehedgehog"  / "sonic1_spec.c",
        submodule_root / "sonicthehedgehog2" / "sonic2_spec.c",
    ]
    per_game_funcs = harvest_func_names(spec_paths)

    # Walk the shared runner directory.
    files_scanned = 0
    total_hits = 0
    by_kind: dict[str, int] = {}
    by_file: dict[Path, list[tuple[int, str, str]]] = {}

    for root, dirs, files in os.walk(runner_root):
        root_path = Path(root)
        # Prune excluded subdirs in-place.
        dirs[:] = [d for d in dirs if d not in PATH_EXCLUDE_DIRS]
        for name in files:
            path = root_path / name
            if not should_scan(path, runner_root):
                continue
            files_scanned += 1
            hits = scan_file(path, per_game_funcs)
            if hits:
                by_file[path] = hits
                total_hits += len(hits)
                for _, kind, _ in hits:
                    by_kind[kind] = by_kind.get(kind, 0) + 1

    # Report.
    rel = lambda p: p.relative_to(release_root) if release_root in p.parents else p

    print(f"audit_runner_purity.py — scanning {runner_root}")
    print(f"  files scanned:  {files_scanned}")
    print(f"  per-game funcs harvested: {len(per_game_funcs)}")
    print(f"  total hits:     {total_hits}")
    print()

    if not by_file:
        print("PASS — no hits.")
        return 0

    print("Hits by kind:")
    for kind in sorted(by_kind):
        print(f"  {kind:30s} {by_kind[kind]:5d}")
    print()

    if args.verbose:
        for path in sorted(by_file):
            print(f"--- {rel(path)} ---")
            for line_no, kind, snippet in by_file[path]:
                print(f"  {line_no:5d}  [{kind}]  {snippet}")
            print()
    else:
        print("Hits by file (top 20; pass --verbose for full detail):")
        for path, hits in sorted(by_file.items(), key=lambda kv: -len(kv[1]))[:20]:
            print(f"  {len(hits):5d}  {rel(path)}")

    print()
    print("These hits are the agreed cleanup baseline (Wave 4 fixes).")
    print("Audit is non-fatal in Wave 0A; flips to exit-1 hard-fail post-Wave 4.")
    print("Run manually before committing changes to shared runner code.")
    # Non-fatal exit. Wave 4+ will flip this.
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
