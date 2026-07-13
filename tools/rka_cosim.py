#!/usr/bin/env python3
"""EXPERIMENTAL state-aligned RKA native-vs-oracle co-simulation audit.

This is diagnostic scaffolding, not an acceptance test.  In particular,
``--drive-manual`` currently steers both backends through WRAM mode writes to
bypass RKA's prologue.  That forced checkpoint was shown to begin with already
different WRAM/VRAM state and must not be used to claim parity or identify the
scrolling root cause.  The useful next version should align naturally reached
gameplay by level/camera coordinates and compare VDP/VRAM write streams.

Both runners free-run with equivalent input scripts.  Wall-frame numbers are
not comparable because the native and interpreter backends execute at different
rates, so records are joined by RKA's own manual-game marker:

    $FFB002 == 7      gameplay dispatcher
    $FFB194 == 0      live controller input (not attract-demo playback)
    $FFB000           game tick, reset when gameplay is initialized

The runner's frame ring already contains full WRAM/VRAM/VDP/Z80 snapshots.
This tool captures requested game ticks independently on each backend and then
uses oracle_parity_audit.diff_record for the subsystem comparison.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from oracle_parity_audit import DebugClient, diff_record


def parse_ticks(spec: str) -> list[int]:
    """Accept comma values and inclusive start:end:step ranges."""
    out: set[int] = set()
    for token in spec.split(","):
        token = token.strip()
        if not token:
            continue
        if ":" not in token:
            out.add(int(token, 0))
            continue
        parts = [int(x, 0) for x in token.split(":")]
        if len(parts) == 2:
            start, end = parts
            step = 1
        elif len(parts) == 3:
            start, end, step = parts
        else:
            raise ValueError(f"bad tick range: {token}")
        if step <= 0:
            raise ValueError("tick step must be positive")
        out.update(range(start, end + 1, step))
    return sorted(out)


def manual_tick_map(c: DebugClient) -> tuple[dict[int, int], int, int]:
    """Return {RKA tick: wall frame} for manual-game records in the ring."""
    fi = c.cmd("frame_info")
    if not fi.get("ok"):
        raise RuntimeError(f"{c.label}: frame_info failed: {fi}")
    lo, hi = int(fi["oldest_frame"]), int(fi["current_frame"])
    result: dict[int, int] = {}
    for start in range(lo, hi + 1, 600):
        end = min(start + 599, hi)
        fields = {}
        for name, field in (
            ("tick", "wram16[B000]"),
            ("mode", "wram16[B002]"),
            ("demo", "wram16[B194]"),
        ):
            r = c.cmd("frame_timeseries", field=field,
                      **{"from": start, "to": end})
            fields[name] = r.get("values") or []
        count = min(map(len, fields.values()))
        for i in range(count):
            if fields["mode"][i] == 7 and fields["demo"][i] == 0:
                # Keep the latest wall-frame sample for a tick. This avoids an
                # earlier transitional sample if the tick is repeated.
                result[int(fields["tick"][i])] = start + i
    return result, lo, hi


def latest_state(c: DebugClient) -> tuple[int, int, int, int, int]:
    """Return wall frame, game mode, submode, game tick, and demo flag."""
    fi = c.cmd("frame_info")
    frame = int(fi["current_frame"])
    values = []
    for field in ("wram16[B002]", "wram16[B004]", "wram16[B000]",
                  "wram16[B194]"):
        r = c.cmd("frame_timeseries", field=field,
                  **{"from": frame, "to": frame})
        sample = r.get("values") or [0]
        values.append(int(sample[-1]))
    return frame, values[0], values[1], values[2], values[3]


def drive_manual(c: DebugClient, timeout: float, stop_tick: int) -> int:
    """Enter a new game and apply tick-keyed fuzz input through TCP.

    Driving from observed game state avoids assuming that native and oracle
    reach title/menu states on the same wall frame.  Once gameplay starts,
    input changes are keyed to RKA's own tick so both machines see the same
    logical sequence.
    """
    deadline = time.time() + timeout
    phase = "first"
    release_at = -1
    nav_release_at = -1
    next_nav_press = -1
    last_state = None
    last_keys = None
    game_entry = None
    while time.time() < deadline:
        frame, mode, submode, tick, _demo = latest_state(c)
        state = (mode, submode)
        if state != last_state:
            print(f"[{c.label}] frame {frame}: mode={mode} submode={submode}")
            last_state = state

        if phase == "first" and mode == 1 and submode == 1:
            c.cmd("set_input", keys=0x80)
            release_at = frame + 4
            phase = "release_first"
        elif phase == "release_first" and frame >= release_at:
            c.cmd("set_input", keys=0)
            release_at = frame + 4
            phase = "second"
        elif phase == "second" and frame >= release_at and mode == 1 and submode == 4:
            c.cmd("set_input", keys=0)
            c.cmd("write_memory", addr="FFB002", hex="0002")
            c.cmd("write_memory", addr="FFB004", hex="0000")
            c.cmd("write_memory", addr="FFB006", hex="0000")
            c.cmd("write_memory", addr="FFB194", hex="0000")
            phase = "gameplay"
            next_nav_press = frame + 120
        elif phase == "gameplay":
            if mode == 7 and _demo == 0:
                if game_entry is None:
                    game_entry = frame
                    print(f"[{c.label}] manual mode-7 entry at frame {frame}")
                rel = frame - game_entry
                # Hold right, with short periodic jumps. Keys are selected
                # from frames relative to the common mode-7 boundary.
                keys = 0x08 | (0x10 if any(a <= rel < a + 3
                                          for a in (300, 600, 900)) else 0)
                if keys != last_keys:
                    c.cmd("set_input", keys=keys)
                    last_keys = keys
                if rel >= stop_tick:
                    c.cmd("set_input", keys=0)
                    print(f"[{c.label}] reached manual frame {rel} (wall frame {frame})")
                    return game_entry
            elif game_entry is not None:
                c.cmd("set_input", keys=0)
                print(f"[{c.label}] mode 7 exited at relative frame "
                      f"{frame - game_entry}; capturing retained window")
                return game_entry
            elif mode == 4 and nav_release_at != -2:
                # Diagnostic checkpoint steering: mode 2 is the ROM's game
                # initializer (2 -> 6 -> 7).  Bypass the long prologue after
                # both backends have entered its mode-4 boundary.
                c.cmd("write_memory", addr="FFB002", hex="0002")
                c.cmd("write_memory", addr="FFB004", hex="0000")
                c.cmd("write_memory", addr="FFB194", hex="0000")
                nav_release_at = -2  # only steer this boundary once
                next_nav_press = frame + 1000000
            elif nav_release_at >= 0 and frame >= nav_release_at:
                c.cmd("set_input", keys=0)
                nav_release_at = -1
                next_nav_press = frame + 120
            elif nav_release_at < 0 and frame >= next_nav_press:
                # Some opening-cinematic phases accept Start only during
                # particular intervals. Repeated edges navigate them without
                # tying the two backends to the same host frame.
                c.cmd("set_input", keys=0x80)
                nav_release_at = frame + 4
        time.sleep(0.002)
    raise TimeoutError(f"{c.label}: failed to reach manual tick {stop_tick}")


def capture_relative_frames(c: DebugClient, entry: int,
                            targets: list[int]) -> dict[int, dict]:
    """Capture snapshots keyed by frames since a state-aligned mode-7 entry."""
    got = {}
    for rel in targets:
        rec = c.cmd("get_frame", frame=entry + rel, include="all")
        if rec.get("ok"):
            got[rel] = rec
        else:
            print(f"[{c.label}] missing relative frame {rel}: {rec}")
    return got


def drive_after_attract(c: DebugClient, timeout: float, stop_tick: int) -> None:
    """Wait through attract, then start and fuzz the following manual game."""
    deadline = time.time() + timeout
    seen_demo = False
    phase = "attract"
    release_at = -1
    last_state = None
    last_keys = None
    while time.time() < deadline:
        frame, mode, submode, tick, demo = latest_state(c)
        state = (mode, submode, demo)
        if state != last_state:
            print(f"[{c.label}] frame {frame}: mode={mode} submode={submode} demo={demo}")
            last_state = state
        seen_demo |= demo != 0

        if (phase == "attract" and seen_demo and demo == 0 and
                mode == 1 and submode == 4):
            c.cmd("set_input", keys=0x80)
            release_at = frame + 4
            phase = "release_start"
        elif phase == "release_start" and frame >= release_at:
            c.cmd("set_input", keys=0)
            phase = "gameplay"
        elif phase == "gameplay" and mode == 7 and demo == 0:
            keys = 0x08 | (0x10 if any(a <= tick < a + 3
                                      for a in (900, 1500, 2100)) else 0)
            if keys != last_keys:
                c.cmd("set_input", keys=keys)
                last_keys = keys
            if tick >= stop_tick:
                c.cmd("set_input", keys=0)
                print(f"[{c.label}] reached manual tick {tick} (frame {frame})")
                return
        time.sleep(0.002)
    raise TimeoutError(f"{c.label}: attract/manual route did not reach tick {stop_tick}")


def capture(c: DebugClient, targets: list[int], timeout: float) -> dict[int, dict]:
    want = set(targets)
    got: dict[int, dict] = {}
    deadline = time.time() + timeout
    last_tick = None
    while want and time.time() < deadline:
        mapping, _, _ = manual_tick_map(c)
        if mapping:
            last_tick = max(mapping)
        for tick in sorted(want):
            frame = mapping.get(tick)
            if frame is None:
                continue
            rec = c.cmd("get_frame", frame=frame, include="all")
            if rec.get("ok"):
                got[tick] = rec
                want.remove(tick)
        if want:
            time.sleep(0.05)
    if want:
        print(f"[{c.label}] missed ticks {sorted(want)} (last manual tick={last_tick})")
    return got


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--native-port", type=int, default=4390)
    ap.add_argument("--oracle-port", type=int, default=4391)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--ticks", default="0:3600:60",
                    help="comma values and/or inclusive start:end:step ranges")
    ap.add_argument("--timeout", type=float, default=300.0)
    ap.add_argument("--drive-manual", action="store_true",
                    help="enter gameplay and drive deterministic right/jump input via TCP")
    ap.add_argument("--after-attract", action="store_true",
                    help="wait through attract, then enter and drive manual gameplay")
    args = ap.parse_args()
    targets = parse_ticks(args.ticks)
    print(f"[rka-cosim] target manual ticks: {targets}")

    results: dict[str, dict[int, dict]] = {}

    def worker(port: int, label: str) -> None:
        c = DebugClient(args.host, port, label)
        try:
            if args.after_attract:
                drive_after_attract(c, args.timeout, max(targets) + 60)
                results[label] = capture(c, targets, args.timeout)
            elif args.drive_manual:
                entry = drive_manual(c, args.timeout, max(targets))
                results[label] = capture_relative_frames(c, entry, targets)
            else:
                results[label] = capture(c, targets, args.timeout)
        finally:
            c.close()

    threads = [
        threading.Thread(target=worker, args=(args.native_port, "native")),
        threading.Thread(target=worker, args=(args.oracle_port, "oracle")),
    ]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    native = results.get("native", {})
    oracle = results.get("oracle", {})
    common = sorted(set(native) & set(oracle))
    print(f"[rka-cosim] common captured ticks: {common}")
    if not common:
        print("[rka-cosim] no state-aligned records captured")
        return 2

    first = None
    for tick in common:
        diffs = diff_record(oracle[tick], native[tick])
        comparable = {k: v for k, v in diffs.items()
                      if k != "m68k_ADVISORY"}
        if comparable:
            first = (tick, comparable)
            break

    if first is None:
        print(f"[rka-cosim] parity through manual tick {common[-1]}")
        return 0

    tick, diffs = first
    print(f"\n[rka-cosim] FIRST DIVERGENCE at manual tick {tick}")
    for subsystem, summary in diffs.items():
        print(f"  {subsystem}: {summary.get('n', 0)} differences")
        for line in summary.get("sample", []):
            print(f"    {line}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
