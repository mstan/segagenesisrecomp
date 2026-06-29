#!/usr/bin/env python3
"""zone_smoke.py — per-zone visual regression check.

Launches a recomp binary with a `.input` script + `--hash-frames N`,
captures every `[FBHASH] frame=... hash=0x...` line printed to stderr,
and diffs the resulting sequence against a baseline JSON. The runner's
own `EXIT N` directive in the .input file ends the process cleanly.

This is the visual counterpart to boot_smoke.py:
  boot_smoke.py — WRAM hash at a single frame (state regression)
  zone_smoke.py — framebuffer hashes at many frames (visual regression)

Sample baseline JSON:

  {
    "tool": "zone_smoke.py",
    "version": 1,
    "game": "sonic2",
    "input_script": "tools/smoke_enter_level_run_right.input",
    "hash_frames": 60,
    "fbhashes": [
      {"frame": 60,   "hash": "0x1234567890ABCDEF"},
      {"frame": 120,  "hash": "0xFEDCBA0987654321"},
      ...
    ]
  }

Usage:

  # First run — write the baseline:
  python tools/zone_smoke.py --game sonic2 \
      --input tools/smoke_enter_level_run_right.input \
      --write-baseline

  # Subsequent runs — assert against baseline:
  python tools/zone_smoke.py --game sonic2 \
      --input tools/smoke_enter_level_run_right.input

  # Diagnostic — keep the runner's stderr around:
  python tools/zone_smoke.py --game sonic2 \
      --input tools/smoke_enter_level_run_right.input \
      --keep-log

Exit codes:
  0 — match (or --write-baseline succeeded)
  1 — divergence vs baseline (hash differs, or sequence length differs)
  2 — runner didn't start, exited non-zero unexpectedly, or no FBHASH lines
  3 — no baseline file present (use --write-baseline first)
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
SUBMODULE_ROOT = SCRIPT_DIR.parent  # segagenesisrecomp/ (the engine repo)

# The game release repos sit beside the engine; the exact nesting differs by
# checkout (engine-top-level + junctions vs submodule-under-release-repo), so
# resolve the exe against several candidate workspace roots rather than assume
# one. The smoke harness runs from inside the exe's directory so it picks up
# debug.ini / annotations / ROM the way the user does when they double-click.
CANDIDATE_ROOTS = [
    SUBMODULE_ROOT.parent,          # engine-top-level layout: F:/Projects/segagenesisrecomp
    SUBMODULE_ROOT.parent.parent,   # submodule-under-release-repo layout
]

GAMES = {
    "sonic1": {"repo": "SonicTheHedgehogRecomp",  "exe_name": "SonicTheHedgehogRecomp.exe",  "rom_name": "sonic.bin",  "game_dir": "sonicthehedgehog"},
    "sonic2": {"repo": "SonicTheHedgehog2Recomp", "exe_name": "SonicTheHedgehog2Recomp.exe", "rom_name": "sonic2.bin", "game_dir": "sonicthehedgehog2"},
    "rka":    {"repo": "RocketKnightAdventuresRecomp", "exe_name": "RKARecomp.exe", "rom_name": "rka.bin", "game_dir": "rka"},
}


def resolve_exe(game: str, override: str | None) -> Path:
    if override:
        return Path(override).resolve()
    cfg = GAMES[game]
    tried = []
    for root in CANDIDATE_ROOTS:
        cand = root / cfg["repo"] / "build" / "Release" / cfg["exe_name"]
        tried.append(cand)
        if cand.is_file():
            return cand
    # Return the first candidate so the caller's not-found error lists a path.
    raise FileNotFoundError(
        "runner exe not found; tried:\n  " + "\n  ".join(str(t) for t in tried)
        + "\nbuild the project first, or pass --exe <path>"
    )

# `[FBHASH] frame=12 w=320 h=224 hash=0x1234567890ABCDEF`
FBHASH_RE = re.compile(
    r"^\[FBHASH\]\s+frame=(\d+)\s+w=\d+\s+h=\d+\s+hash=0x([0-9A-Fa-f]+)\s*$"
)


def run_smoke(game: str, input_script, hash_frames: int,
              max_frames: int, timeout: float, keep_log: bool,
              exe_override=None):
    cfg = GAMES[game]
    exe: Path = resolve_exe(game, exe_override)
    rom = exe.parent / cfg["rom_name"]
    if not rom.is_file():
        raise FileNotFoundError(
            f"ROM {cfg['rom_name']} not next to exe at {rom}"
        )

    args = [str(exe), cfg["rom_name"], "--hash-frames", str(hash_frames)]
    if input_script is not None:
        args += ["--input-script", str(input_script.resolve())]
    else:
        # No scripted flow (e.g. a boot/title golden test): run headless and
        # rely on --max-frames to end the run. --no-launcher + --turbo keep it
        # deterministic and unattended.
        args += ["--no-launcher", "--turbo"]
    if max_frames > 0:
        args += ["--max-frames", str(max_frames)]

    print(f"[zone_smoke] launching: {' '.join(args[1:])}")
    print(f"[zone_smoke] cwd: {exe.parent}")

    try:
        proc = subprocess.run(
            args,
            cwd=str(exe.parent),
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as e:
        raise RuntimeError(
            f"runner did not exit within {timeout}s — the .input script "
            f"may be missing an EXIT directive, or --max-frames is unset"
        ) from e

    stderr = proc.stderr or ""
    stdout = proc.stdout or ""
    if keep_log:
        log_path = input_script.with_suffix(".smoke.log")
        log_path.write_text(
            f"=== ARGS ===\n{' '.join(args)}\n\n"
            f"=== STDOUT ===\n{stdout}\n\n"
            f"=== STDERR ===\n{stderr}\n\n"
            f"=== EXIT CODE ===\n{proc.returncode}\n"
        )
        print(f"[zone_smoke] wrote runner log: {log_path}")

    fbhashes = []
    for line in stderr.splitlines():
        m = FBHASH_RE.match(line.strip())
        if m:
            fbhashes.append({
                "frame": int(m.group(1)),
                "hash": f"0x{m.group(2).upper()}",
            })

    if proc.returncode not in (0, None) and proc.returncode != 0:
        # The .input script exits 0 by convention; non-zero from the runner
        # itself (e.g. ROM load failure) is fatal regardless of what we saw.
        print(
            f"[zone_smoke] WARNING: runner exited with code {proc.returncode}",
            file=sys.stderr,
        )
        if not fbhashes:
            raise RuntimeError(
                f"runner exited {proc.returncode} with no FBHASH output; "
                f"see stderr above"
            )

    return fbhashes, proc.returncode


def diff_fbhashes(baseline, current):
    diffs = []
    b = baseline
    c = current
    if len(b) != len(c):
        diffs.append(
            f"sequence length: baseline {len(b)} fbhashes, captured {len(c)}"
        )
    for i, (be, ce) in enumerate(zip(b, c)):
        if be.get("frame") != ce.get("frame"):
            diffs.append(
                f"#{i}: frame {be.get('frame')} -> {ce.get('frame')}"
            )
        elif be.get("hash") != ce.get("hash"):
            diffs.append(
                f"frame {be['frame']}: hash {be['hash']} -> {ce['hash']}"
            )
    return diffs


def main(argv):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--game", choices=sorted(GAMES), required=True)
    p.add_argument("--input", default=None,
                   help="path to .input script (relative to repo or absolute). "
                        "Omit for a no-input boot/title golden test (requires "
                        "--max-frames).")
    p.add_argument("--baseline", default=None,
                   help="baseline JSON path (default: <input>.smoke_baseline.json)")
    p.add_argument("--write-baseline", action="store_true",
                   help="capture and write as the new baseline")
    p.add_argument("--hash-frames", type=int, default=60,
                   help="--hash-frames N passed to the runner (default 60)")
    p.add_argument("--max-frames", type=int, default=0,
                   help="--max-frames N safety cap; 0 relies on the script's EXIT")
    p.add_argument("--timeout", type=float, default=300.0,
                   help="seconds to wait for the runner to exit (default 300)")
    p.add_argument("--keep-log", action="store_true",
                   help="save runner stdout/stderr to <input>.smoke.log")
    p.add_argument("--exe", default=None,
                   help="explicit path to the runner exe (overrides auto-resolve)")
    args = p.parse_args(argv)

    if args.input is not None:
        input_path = Path(args.input).resolve()
        if not input_path.is_file():
            print(f"[zone_smoke] input script not found: {input_path}", file=sys.stderr)
            return 2
    else:
        input_path = None
        if args.max_frames <= 0:
            print("[zone_smoke] --max-frames is required when --input is omitted "
                  "(no EXIT directive to end the run)", file=sys.stderr)
            return 2

    if args.baseline:
        baseline_path = Path(args.baseline).resolve()
    elif input_path is not None:
        baseline_path = input_path.with_suffix(".smoke_baseline.json")
    else:
        game_dir = SUBMODULE_ROOT / GAMES[args.game]["game_dir"]
        baseline_path = game_dir / f"{args.game}_boot_smoke_baseline.json"

    try:
        fbhashes, rc = run_smoke(
            args.game, input_path, args.hash_frames, args.max_frames,
            args.timeout, args.keep_log, args.exe,
        )
    except (FileNotFoundError, RuntimeError) as e:
        print(f"[zone_smoke] {e}", file=sys.stderr)
        return 2

    if not fbhashes:
        print(
            "[zone_smoke] no [FBHASH] lines captured — was --hash-frames "
            "honored by the runner?",
            file=sys.stderr,
        )
        return 2

    print(f"[zone_smoke] captured {len(fbhashes)} fbhashes "
          f"(frame range: {fbhashes[0]['frame']}..{fbhashes[-1]['frame']})")

    snap = {
        "tool": "zone_smoke.py",
        "version": 1,
        "game": args.game,
        "input_script": input_path.name if input_path is not None else "(boot)",
        "hash_frames": args.hash_frames,
        "fbhashes": fbhashes,
    }

    if args.write_baseline:
        baseline_path.write_text(json.dumps(snap, indent=2) + "\n")
        print(f"[zone_smoke] wrote baseline: {baseline_path}")
        return 0

    if not baseline_path.is_file():
        print(
            f"[zone_smoke] no baseline at {baseline_path}; "
            f"run with --write-baseline first",
            file=sys.stderr,
        )
        return 3

    baseline = json.loads(baseline_path.read_text())
    diffs = diff_fbhashes(baseline.get("fbhashes", []), fbhashes)
    if not diffs:
        print(f"[zone_smoke] OK — {len(fbhashes)} fbhashes match {baseline_path.name}")
        return 0

    print(
        f"[zone_smoke] DIVERGENCE — {len(diffs)} difference(s):",
        file=sys.stderr,
    )
    for d in diffs:
        print(f"  {d}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
