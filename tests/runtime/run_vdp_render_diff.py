#!/usr/bin/env python3
"""Build and run the VDP renderer differential harness.

The baseline renderer is extracted from Git by ref into build/runtime-validation
so post-commit runs do not accidentally compare the candidate with itself.
"""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


SYMBOLS = [
    "gvdp_init",
    "gvdp_reset",
    "gvdp_write_data",
    "gvdp_write_control",
    "gvdp_read_data",
    "gvdp_read_control",
    "gvdp_read_hv_counter",
    "gvdp_consume_68k_stall",
    "g_gvdp_events",
    "g_gvdp_event_seq",
    "gvdp_screen_width",
    "gvdp_screen_height",
    "gvdp_display_enabled",
    "gvdp_interlace_double",
    "gvdp_output_height",
    "gvdp_set_ws_extra",
    "gvdp_set_ws_canvas",
    "gvdp_set_ws_bar_black",
    "gvdp_active_width",
    "gvdp_set_bgdiag",
    "gvdp_render_scanline",
    "gvdp_begin_scanline",
]


def run(args: list[str], cwd: Path) -> None:
    print("+ " + " ".join(args))
    subprocess.run(args, cwd=cwd, check=True)


def defs(prefix: str) -> list[str]:
    return [f"-D{s}={prefix}_{s}" for s in SYMBOLS]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--gcc", default=r"C:\msys64\mingw64\bin\gcc.exe")
    parser.add_argument("--baseline-ref", default="997bd1f1c754f38dd598318661b2a52553d33cc0")
    parser.add_argument("--baseline-source", type=Path)
    parser.add_argument("--candidate-source", type=Path)
    ns = parser.parse_args()

    root = ns.root.resolve()
    out = root / "build" / "runtime-validation"
    out.mkdir(parents=True, exist_ok=True)
    baseline = ns.baseline_source
    if baseline is None:
        baseline = out / "genesis_vdp_baseline.c"
        data = subprocess.check_output(
            ["git", "-C", str(root), "show", f"{ns.baseline_ref}:runner/video/genesis_vdp.c"]
        )
        baseline.write_bytes(data)
    else:
        baseline = baseline.resolve(strict=True)

    candidate = ns.candidate_source or (root / "runner" / "video" / "genesis_vdp.c")
    candidate = candidate.resolve(strict=True)
    include = root / "runner" / "video"
    test = root / "tests" / "runtime" / "vdp_render_diff_test.c"
    head_obj = out / "genesis_vdp_head.o"
    cand_obj = out / "genesis_vdp_cand.o"
    exe = out / "vdp_render_diff_test.exe"
    common = [ns.gcc, "-std=c11", "-O2", "-DNDEBUG", "-Wall", "-Wextra", "-Werror", "-I", str(include)]

    run(common + defs("head") + ["-c", str(baseline), "-o", str(head_obj)], root)
    run(common + defs("cand") + ["-c", str(candidate), "-o", str(cand_obj)], root)
    run(common + [str(test), str(head_obj), str(cand_obj), "-o", str(exe)], root)
    run([str(exe)], root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
