#!/usr/bin/env python3
"""boot_smoke.py — deterministic boot-state snapshot + baseline regression check.

Connects to a running Genesis recomp binary via TCP (line-based JSON), waits
for the always-on frame_record ring to reach a target frame (default 60),
queries that frame deterministically, and assembles a minimal-fields snapshot:

  - frame, m68k.PC, m68k.SR, m68k.USP, m68k.D[0..7], m68k.A[0..7]
  - layout-resolved Game_Mode (byte), Vint_runcount (longword),
    Vint_routine (byte), PLC_pending (word) — addresses come from the per-
    game `[ram_layout]` section of `game.toml`, so the script is game-
    agnostic from the start.
  - FNV1a-64 hash of full 64KB WRAM.
  - Optional: full 64KB WRAM dump on diff (`--dump-on-diff`).

The runner is launched separately. This script connects, queries the
deterministic frame, and exits. Per the always-on ring philosophy
(PRINCIPLES.md #17), the frame_record ring captures continuously; this
probe queries it backward, never arms recording.

There is no GitHub Actions / cloud CI for this project (the build needs a
ROM that can't be checked in upstream). Run boot_smoke locally before
committing changes that touch shared runner code. Per PRINCIPLES.md #23,
any baseline change commits alongside the code change that justifies it.

Usage:
  python tools/boot_smoke.py --game sonic1
  python tools/boot_smoke.py --game sonic2 --port 4380
  python tools/boot_smoke.py --game sonic1 --write-baseline
  python tools/boot_smoke.py --game sonic1 --frames 300 --dump-on-diff

Exit codes:
  0 — match (or --write-baseline succeeded)
  1 — divergence vs baseline
  2 — environment / connection error
  3 — no baseline file present (after first run, use --write-baseline)
"""

from __future__ import annotations

import argparse
import json
import socket
import sys
import time
from pathlib import Path

try:
    import tomllib  # Python 3.11+
except ImportError:
    import tomli as tomllib  # type: ignore[no-redef]


SCRIPT_DIR = Path(__file__).resolve().parent
SUBMODULE_ROOT = SCRIPT_DIR.parent  # segagenesisrecomp/

GAMES = {
    "sonic1": {"dir": SUBMODULE_ROOT / "sonicthehedgehog",  "default_port": 4378},
    "sonic2": {"dir": SUBMODULE_ROOT / "sonicthehedgehog2", "default_port": 4378},
}


# ---------- FNV1a-64 ----------
_FNV1A_OFFSET = 0xCBF29CE484222325
_FNV1A_PRIME = 0x100000001B3
_FNV1A_MASK = (1 << 64) - 1


def fnv1a64(data: bytes) -> int:
    h = _FNV1A_OFFSET
    for byte in data:
        h ^= byte
        h = (h * _FNV1A_PRIME) & _FNV1A_MASK
    return h


# ---------- TCP probe ----------
class Probe:
    def __init__(self, host: str, port: int, timeout: float):
        self._sock = socket.socket()
        self._sock.connect((host, port))
        self._sock.settimeout(timeout)
        self._next_id = 0

    def cmd(self, name: str, **kw: object) -> dict:
        self._next_id += 1
        msg = {"id": self._next_id, "cmd": name}
        msg.update(kw)
        self._sock.sendall((json.dumps(msg) + "\n").encode())
        buf = b""
        while b"\n" not in buf:
            chunk = self._sock.recv(1 << 20)
            if not chunk:
                raise RuntimeError("TCP connection closed mid-response")
            buf += chunk
        line, _ = buf.split(b"\n", 1)
        return json.loads(line.decode())

    def close(self) -> None:
        try:
            self._sock.close()
        except OSError:
            pass


# ---------- Layout helpers ----------
def load_ram_layout(toml_path: Path) -> dict:
    with toml_path.open("rb") as f:
        cfg = tomllib.load(f)
    return cfg.get("ram_layout", {})


def read_wram_field(wram: bytes, addr: int, size: int) -> int | None:
    """Read a big-endian unsigned field from the 64KB WRAM blob.

    addr is a 24-bit Genesis address ($FF0000-$FFFFFF); the low 16 bits
    index into the wram[] snapshot. addr == 0 means "not configured" —
    we return None so the field shows up as null in the snapshot.
    """
    if addr == 0:
        return None
    off = addr & 0xFFFF
    if off + size > len(wram):
        return None
    if size == 1:
        return wram[off]
    if size == 2:
        return (wram[off] << 8) | wram[off + 1]
    if size == 4:
        return (
            (wram[off] << 24)
            | (wram[off + 1] << 16)
            | (wram[off + 2] << 8)
            | wram[off + 3]
        )
    raise ValueError(f"unsupported field size {size}")


# ---------- Snapshot ----------
def wait_for_frame(probe: Probe, target: int, deadline: float) -> int:
    last_seen = -1
    while True:
        info = probe.cmd("frame_info")
        if not info.get("ok"):
            raise RuntimeError(f"frame_info failed: {info}")
        cur = int(info.get("current_frame", 0))
        if cur > target:
            return cur
        if cur != last_seen:
            last_seen = cur
        if time.time() > deadline:
            raise TimeoutError(
                f"runner only reached frame {cur} before timeout (need > {target})"
            )
        time.sleep(0.05)


def capture_snapshot(
    probe: Probe, target_frame: int, layout: dict
) -> tuple[dict, bytes]:
    r = probe.cmd("get_frame", frame=target_frame, include="wram")
    if not r.get("ok"):
        raise RuntimeError(f"get_frame failed: {r}")

    m = r.get("m68k", {})
    wram_hex = r.get("wram", "")
    if not wram_hex:
        raise RuntimeError("get_frame response missing 'wram' field")
    wram = bytes.fromhex(wram_hex)
    if len(wram) != 0x10000:
        raise RuntimeError(
            f"unexpected wram length {len(wram)} (expected 65536)"
        )

    game_mode_addr = int(layout.get("game_mode", 0))
    vint_runcount_addr = int(layout.get("vint_runcount", 0))
    vint_routine_addr = int(layout.get("vint_routine", 0))
    plc_pending_addr = int(layout.get("plc_pending", 0))

    snap = {
        "frame": int(r.get("frame", target_frame)),
        "verify_pass": int(r.get("verify_pass", -1)),
        "m68k": {
            "PC": int(m.get("PC", 0)),
            "SR": int(m.get("SR", 0)),
            "USP": int(m.get("USP", 0)),
            "D": [int(x) for x in m.get("D", [0] * 8)],
            "A": [int(x) for x in m.get("A", [0] * 8)],
        },
        "layout_resolved": {
            "game_mode_addr":     f"0x{game_mode_addr:06X}",
            "game_mode":          read_wram_field(wram, game_mode_addr, 1),
            "vint_runcount_addr": f"0x{vint_runcount_addr:06X}",
            "vint_runcount":      read_wram_field(wram, vint_runcount_addr, 4),
            "vint_routine_addr":  f"0x{vint_routine_addr:06X}",
            "vint_routine":       read_wram_field(wram, vint_routine_addr, 1),
            "plc_pending_addr":   f"0x{plc_pending_addr:06X}",
            "plc_pending":        read_wram_field(wram, plc_pending_addr, 2),
        },
        "wram_fnv1a64": f"{fnv1a64(wram):016x}",
    }
    return snap, wram


# ---------- Diff ----------
def diff_snapshots(baseline: dict, current: dict) -> list[str]:
    """Return a flat list of "path: baseline → current" diff lines."""
    diffs: list[str] = []

    def walk(path: str, b: object, c: object) -> None:
        if isinstance(b, dict) and isinstance(c, dict):
            for k in sorted(set(b) | set(c)):
                walk(f"{path}.{k}" if path else k, b.get(k), c.get(k))
        elif isinstance(b, list) and isinstance(c, list):
            if len(b) != len(c):
                diffs.append(f"{path}: list len {len(b)} -> {len(c)}")
                return
            for i, (bi, ci) in enumerate(zip(b, c)):
                walk(f"{path}[{i}]", bi, ci)
        else:
            if b != c:
                diffs.append(f"{path}: {b!r} -> {c!r}")

    walk("", baseline, current)
    return diffs


# ---------- Main ----------
def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(
        description="Deterministic boot-state snapshot + baseline check."
    )
    p.add_argument("--game", choices=sorted(GAMES), required=True)
    p.add_argument("--port", type=int, default=None,
                   help="TCP port (default 4378; build commands use 4380)")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--frames", type=int, default=60,
                   help="target frame index (default 60)")
    p.add_argument("--baseline", default=None,
                   help="path to baseline JSON (default: <game_dir>/boot_smoke_baseline.json)")
    p.add_argument("--write-baseline", action="store_true",
                   help="write captured snapshot as the new baseline (no comparison)")
    p.add_argument("--dump-on-diff", action="store_true",
                   help="dump full 64KB WRAM to <game_dir>/boot_smoke_diff_<frame>.bin on mismatch")
    p.add_argument("--game-toml", default=None,
                   help="override path to game.toml (default: <game_dir>/game.toml)")
    p.add_argument("--timeout", type=float, default=120.0,
                   help="seconds to wait for the target frame (default 120)")
    args = p.parse_args(argv)

    game = GAMES[args.game]
    port = args.port if args.port is not None else int(game["default_port"])
    game_dir: Path = game["dir"]
    toml_path = Path(args.game_toml) if args.game_toml else game_dir / "game.toml"
    baseline_path = (
        Path(args.baseline) if args.baseline
        else game_dir / "boot_smoke_baseline.json"
    )

    if not toml_path.is_file():
        print(f"[boot_smoke] game.toml not found at {toml_path}", file=sys.stderr)
        return 2
    layout = load_ram_layout(toml_path)
    if not layout:
        print(
            f"[boot_smoke] {toml_path} has no [ram_layout] section",
            file=sys.stderr,
        )
        return 2

    print(
        f"[boot_smoke] game={args.game} port={port} target_frame={args.frames}"
    )

    deadline = time.time() + args.timeout
    try:
        probe = Probe(args.host, port, args.timeout)
    except OSError as e:
        print(
            f"[boot_smoke] cannot connect to {args.host}:{port} -- is the runner running? ({e})",
            file=sys.stderr,
        )
        return 2

    try:
        try:
            ping = probe.cmd("ping")
            if not ping.get("ok"):
                print(f"[boot_smoke] ping failed: {ping}", file=sys.stderr)
                return 2
            cur = wait_for_frame(probe, args.frames, deadline)
            print(
                f"[boot_smoke] runner at frame {cur}; capturing frame {args.frames}"
            )
            snap, wram = capture_snapshot(probe, args.frames, layout)
        except TimeoutError as e:
            print(f"[boot_smoke] {e}", file=sys.stderr)
            return 2
        except RuntimeError as e:
            msg = str(e)
            print(f"[boot_smoke] {msg}", file=sys.stderr)
            if "frame not in ring buffer" in msg:
                print(
                    "[boot_smoke] hint: target frame was evicted from the 600-frame ring "
                    "before capture. Reduce launch->capture delay, or pick --frames closer "
                    "to the runner's current frame.",
                    file=sys.stderr,
                )
            return 2
    finally:
        probe.close()

    snap["meta"] = {
        "tool": "boot_smoke.py",
        "version": 1,
        "game": args.game,
        "target_frame": args.frames,
    }

    if args.write_baseline:
        baseline_path.write_text(
            json.dumps(snap, indent=2, sort_keys=True) + "\n"
        )
        print(f"[boot_smoke] wrote baseline: {baseline_path}")
        return 0

    if not baseline_path.is_file():
        print(
            f"[boot_smoke] no baseline at {baseline_path}; run with --write-baseline first",
            file=sys.stderr,
        )
        print(json.dumps(snap, indent=2, sort_keys=True))
        return 3

    baseline = json.loads(baseline_path.read_text())
    diffs = diff_snapshots(baseline, snap)
    if not diffs:
        print(
            f"[boot_smoke] OK -- frame {snap['frame']} matches {baseline_path.name}"
        )
        return 0

    print(
        f"[boot_smoke] DIVERGENCE -- {len(diffs)} field(s) differ at frame {snap['frame']}:",
        file=sys.stderr,
    )
    for d in diffs:
        print(f"  {d}", file=sys.stderr)

    if args.dump_on_diff:
        dump_path = game_dir / f"boot_smoke_diff_{snap['frame']}.bin"
        dump_path.write_bytes(wram)
        print(
            f"[boot_smoke] full 64KB WRAM dumped to {dump_path}",
            file=sys.stderr,
        )

    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
