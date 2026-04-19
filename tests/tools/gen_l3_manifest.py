#!/usr/bin/env python3
"""
gen_l3_manifest.py — emit an ordered list of `func_XXXXXX` addresses for
the L3 oracle to validate, leaves first.

A leaf function is one whose generated C body contains no calls to any
other recompiled function — i.e., no `func_XXXXXX()`, no
`call_by_address(...)`. These are the safest starting point: their
correctness depends only on the recompiler emitting correct code for
the instructions they contain, not on transitive behavior.

Subsequent (non-leaf) tiers are then ordered topologically: a function
appears after every function it calls. With leaves validated first, a
non-leaf failure can be attributed to that function alone (its callees
are already known-good).

Reads:   segagenesisrecomp/sonicthehedgehog/generated/sonic_full.c
Writes:  segagenesisrecomp/tests/fixtures/sonic1/l3/manifest.txt

Manifest format: one func address per line (hex, no prefix), grouped
into tiers separated by `# tier N` headers.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

SUB_ROOT  = Path(__file__).resolve().parents[2]
FULL_C    = SUB_ROOT / "sonicthehedgehog" / "generated" / "sonic_full.c"
OUT_PATH  = SUB_ROOT / "tests" / "fixtures" / "sonic1" / "l3" / "manifest.txt"

FUNC_DEF_RE = re.compile(r"^void (func_([0-9A-Fa-f]+))\(void\)\s*\{", re.MULTILINE)
CALL_RE     = re.compile(r"\bfunc_([0-9A-Fa-f]+)\s*\(\s*\)")
DYNDISP_RE  = re.compile(r"\bcall_by_address\s*\(")

def parse_function_bodies(src: str) -> dict[int, set[int]]:
    """For each defined function, return the set of other-function
    addresses it directly calls (via func_XXXXXX()). Functions that use
    call_by_address(..) are flagged with a sentinel callee 'DYN' so we
    can demote them out of the leaf tier (we don't know which dispatch
    target they hit at runtime)."""
    bodies: dict[int, set[int]] = {}
    has_dyndisp: dict[int, bool] = {}
    # Walk function defs in order, slice each body until next top-level
    # `void func_XXXXXX(void) {` or end of file.
    starts: list[tuple[int, int]] = []  # (file_offset, addr)
    for m in FUNC_DEF_RE.finditer(src):
        starts.append((m.start(), int(m.group(2), 16)))
    starts.append((len(src), 0))
    for i in range(len(starts) - 1):
        body_start, addr = starts[i]
        body_end, _ = starts[i + 1]
        body = src[body_start:body_end]
        callees = {int(g, 16) for g in CALL_RE.findall(body)}
        callees.discard(addr)  # don't count self-calls
        bodies[addr] = callees
        has_dyndisp[addr] = bool(DYNDISP_RE.search(body))
    return bodies, has_dyndisp

def main() -> int:
    if not FULL_C.exists():
        print(f"sonic_full.c not found: {FULL_C}", file=sys.stderr)
        return 2
    src = FULL_C.read_text(encoding="utf-8")
    bodies, has_dyndisp = parse_function_bodies(src)
    all_funcs = set(bodies)

    # Tier 0 (leaves): no static func_X() calls AND no call_by_address.
    tiered: dict[int, list[int]] = {}
    placed: set[int] = set()
    tier_idx = 0
    while True:
        tier_set = set()
        for f in all_funcs - placed:
            if has_dyndisp.get(f):
                continue  # callees unknown; defer to a separate tier
            callees = bodies[f] & all_funcs
            if callees.issubset(placed):
                tier_set.add(f)
        if not tier_set:
            break
        tiered[tier_idx] = sorted(tier_set)
        placed |= tier_set
        tier_idx += 1

    # Final tier: functions with dynamic dispatch (call_by_address).
    dyndisp_tier = sorted(f for f in all_funcs - placed)
    if dyndisp_tier:
        tiered[tier_idx] = dyndisp_tier
        tier_dyndisp = tier_idx
    else:
        tier_dyndisp = -1

    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    with OUT_PATH.open("w", encoding="utf-8", newline="\n") as f:
        f.write("# L3 oracle manifest — addresses to validate, leaves first.\n")
        f.write(f"# Total: {len(all_funcs)} functions across {len(tiered)} tiers.\n\n")
        for ti in sorted(tiered):
            label = " (dynamic-dispatch)" if ti == tier_dyndisp else ""
            f.write(f"# tier {ti}: {len(tiered[ti])} func(s){label}\n")
            for a in tiered[ti]:
                f.write(f"{a:06X}\n")
            f.write("\n")

    print(f"l3-manifest: {len(all_funcs)} functions, tier sizes: "
          f"{[len(tiered[t]) for t in sorted(tiered)]}")
    print(f"  -> {OUT_PATH}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
