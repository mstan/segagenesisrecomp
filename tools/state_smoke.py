#!/usr/bin/env python3
"""state_smoke.py — pacing-invariant boot/visual regression guard.

The older boot_smoke/zone_smoke harnesses sample [FBHASH] at FIXED wall frames
(`--hash-frames N`). That makes them drift-fragile: any codegen/timing change
that shifts how many frames the game takes to reach a state slides content past
the sample points, so a content-identical run reports a false DIVERGENCE
(see GENESIS_ACCURACY_BURNDOWN.md backlog item 8 and feedback_smoke_harness_unreliable).

This harness instead anchors to GAME STATE. The runner's `--hash-on-mode` emits a
`[MODEHASH] seq=N mode=0xMM frame=F ... hash=0x..` line on every Game_Mode
TRANSITION. The transition *sequence* is deterministic; only its wall-frame
timing drifts. We compare by (seq, mode, hash) and IGNORE the frame entirely, so
the guard is immune to pacing drift: "the framebuffer when the game reached
state X" is the same whether X was reached at frame 480 or 482.

Usage:
  # First run -- record the baseline:
  python tools/state_smoke.py --game sonic1 --write-baseline

  # Later runs -- assert against the baseline:
  python tools/state_smoke.py --game sonic1

Exit codes: 0 match / write OK; 1 divergence; 2 runner error / no MODEHASH; 3 no baseline.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

# Reuse the game registry + exe resolver from zone_smoke (no duplication).
from zone_smoke import GAMES, resolve_exe, SUBMODULE_ROOT

# `[MODEHASH] seq=0 mode=0x0C frame=412 w=320 h=224 hash=0x1234567890ABCDEF`
MODEHASH_RE = re.compile(
    r"^\[MODEHASH\]\s+seq=(\d+)\s+mode=0x([0-9A-Fa-f]+)\s+frame=(\d+)"
    r"\s+w=\d+\s+h=\d+\s+hash=0x([0-9A-Fa-f]+)\s*$"
)


def baseline_path(game: str, override: str | None) -> Path:
    if override:
        return Path(override).resolve()
    return SUBMODULE_ROOT / GAMES[game]["game_dir"] / "state_smoke_baseline.json"


def run(game: str, max_frames: int, timeout: float, exe_override, keep_log):
    cfg = GAMES[game]
    exe = resolve_exe(game, exe_override)
    rom = exe.parent / cfg["rom_name"]
    if not rom.is_file():
        raise FileNotFoundError(f"ROM {cfg['rom_name']} not next to exe at {rom}")

    args = [str(exe), cfg["rom_name"], "--hash-on-mode",
            "--no-launcher", "--turbo", "--max-frames", str(max_frames)]
    print(f"[state_smoke] launching: {' '.join(args[1:])}")
    try:
        proc = subprocess.run(args, cwd=str(exe.parent), capture_output=True,
                              text=True, timeout=timeout)
    except subprocess.TimeoutExpired as e:
        raise RuntimeError(f"runner did not exit within {timeout}s "
                           f"(--max-frames {max_frames})") from e

    stderr = proc.stderr or ""
    if keep_log:
        (SUBMODULE_ROOT / f"{game}.state_smoke.log").write_text(
            f"=== ARGS ===\n{' '.join(args)}\n\n=== STDERR ===\n{stderr}\n"
            f"=== EXIT {proc.returncode} ===\n")

    checkpoints = []
    for line in stderr.splitlines():
        m = MODEHASH_RE.match(line.strip())
        if m:
            checkpoints.append({
                "seq": int(m.group(1)),
                "mode": f"0x{m.group(2).upper()}",
                "hash": f"0x{m.group(4).upper()}",
                # frame is recorded for human context only; NEVER compared.
                "frame": int(m.group(3)),
            })

    if proc.returncode not in (0, None) and not checkpoints:
        raise RuntimeError(
            f"runner exited {proc.returncode} with no MODEHASH output.\n"
            f"--- stderr tail ---\n" + "\n".join(stderr.splitlines()[-15:]))
    return checkpoints


def diff(baseline, current):
    """Compare by (seq, mode, hash); frame is intentionally ignored."""
    out = []
    if len(baseline) != len(current):
        out.append(f"checkpoint count: baseline {len(baseline)} -> captured {len(current)} "
                   f"(the Game_Mode transition SEQUENCE changed -- a real behavior change)")
    for i, (b, c) in enumerate(zip(baseline, current)):
        if b["mode"] != c["mode"]:
            out.append(f"#{i}: mode {b['mode']} -> {c['mode']} (different state reached)")
        elif b["hash"] != c["hash"]:
            out.append(f"#{i} mode {b['mode']}: hash {b['hash']} -> {c['hash']} "
                       f"(rendering changed at this state; baseline frame {b.get('frame')} "
                       f"vs captured frame {c.get('frame')})")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", required=True, choices=sorted(GAMES))
    ap.add_argument("--max-frames", type=int, default=1200,
                    help="frames to run; must cover the boot transitions of interest")
    ap.add_argument("--timeout", type=float, default=120.0)
    ap.add_argument("--write-baseline", action="store_true")
    ap.add_argument("--baseline", default=None, help="override baseline JSON path")
    ap.add_argument("--exe", default=None)
    ap.add_argument("--keep-log", action="store_true")
    args = ap.parse_args()

    cps = run(args.game, args.max_frames, args.timeout, args.exe, args.keep_log)
    print(f"[state_smoke] captured {len(cps)} Game_Mode checkpoints:")
    for c in cps:
        print(f"    seq={c['seq']:>2} mode={c['mode']} (frame {c['frame']}) {c['hash']}")

    bpath = baseline_path(args.game, args.baseline)
    if args.write_baseline:
        bpath.write_text(json.dumps(cps, indent=2) + "\n")
        print(f"[state_smoke] wrote baseline: {bpath} ({len(cps)} checkpoints)")
        return 0

    if not bpath.is_file():
        print(f"[state_smoke] no baseline at {bpath} -- run with --write-baseline first",
              file=sys.stderr)
        return 3

    baseline = json.loads(bpath.read_text())
    diffs = diff(baseline, cps)
    if diffs:
        print(f"[state_smoke] DIVERGENCE vs {bpath}:", file=sys.stderr)
        for d in diffs:
            print(f"    {d}", file=sys.stderr)
        return 1
    print(f"[state_smoke] OK -- {len(cps)} checkpoints match {bpath} "
          f"(frame-timing ignored)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
