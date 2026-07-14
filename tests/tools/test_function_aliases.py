#!/usr/bin/env python3
"""Synthetic integration test for conservative function-entry aliasing.

The fixture proves three invariants through the real GenesisRecomp CLI:
  * a branch-reachable interior callable entry shares its host body;
  * an overlap candidate whose unbounded walk reaches an illegal encoding is
    kept as a hard boundary (data-run-on is not accepted as alias evidence);
  * a configured root absent from the code-address oracle is rejected.
"""

from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path


def be32(buf: bytearray, off: int, value: int) -> None:
    buf[off : off + 4] = value.to_bytes(4, "big")


def main() -> int:
    # MSYS Python receives PowerShell's backslash-containing argv[0] literally;
    # normalize it before asking pathlib for repository parents.
    repo = Path(str(__file__).replace("\\", "/")).resolve().parents[2]
    default_exe = repo / "recompiler" / "build" / "Release" / "GenesisRecomp.exe"
    ap = argparse.ArgumentParser()
    ap.add_argument("--recompiler", type=Path, default=default_exe)
    args = ap.parse_args()
    exe = args.recompiler.resolve()
    if not exe.is_file():
        raise SystemExit(f"recompiler not found: {exe}")

    with tempfile.TemporaryDirectory(prefix="genesis_alias_test_") as td:
        root = Path(td)
        rom = bytearray(b"\xFF" * 0x400)
        be32(rom, 0, 0x00FFFE00)
        be32(rom, 4, 0x00000200)
        rom[0x100:0x200] = b" " * 0x100
        rom[0x120:0x12A] = b"ALIAS TEST"

        # Canonical host: MOVEQ; BRA to the configured interior entry; RTS.
        rom[0x200:0x20C] = bytes.fromhex(
            "7000"      # moveq #0,d0
            "6004"      # bra.s $208
            "4e71"      # unreachable nop
            "ffff"      # unreachable illegal/data word
            "4e71"      # $208 alias entry: nop
            "4e75"      # rts
        )

        # Unsafe apparent overlap: both starts linearly reach MOVEA.B, which
        # the MC68000 validator rejects. They must remain separate boundaries.
        rom[0x220:0x226] = bytes.fromhex(
            "4e71"      # $220 nop
            "4e71"      # $222 configured entry
            "1040"      # illegal MOVEA.B D0,A0
        )
        (root / "alias.bin").write_bytes(rom)

        (root / "code_addrs.txt").write_text(
            "000200\n000202\n000204\n000208\n00020A\n000220\n000222\n",
            encoding="ascii",
        )
        (root / "game.toml").write_text(
            """[game]
output_prefix = "alias_test"
code_addrs_file = "code_addrs.txt"
function_aliases = true

[functions]
extra = [0x000200, 0x000206, 0x000208, 0x000220, 0x000222]

[ram_layout]
game_mode = 0
vint_runcount = 0
vint_routine = 0
plc_pending = 0
initial_ssp = 0x00FFFE00
vbla_stack = 0
intr_stack = 0
player_object = 0
level_modes = []
""",
            encoding="ascii",
        )

        dump = root / "functions.txt"
        proc = subprocess.run(
            [str(exe), "alias.bin", "--game", "game.toml",
             "--output-dir", "build-generated",
             "--dump-functions", str(dump)],
            cwd=root,
            text=True,
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        if proc.returncode:
            raise AssertionError(f"GenesisRecomp failed ({proc.returncode}):\n{proc.stdout}")

        full = (root / "build-generated" / "alias_test_full.c").read_text(encoding="utf-8")
        dispatch = (root / "build-generated" / "alias_test_dispatch.c").read_text(encoding="utf-8")
        manifest = dump.read_text(encoding="utf-8")

        assert "static void func_body_000200(uint32_t _entry)" in full
        assert "void func_000200(void) { func_body_000200(0x000200u); }" in full
        assert "void func_000208(void) { func_body_000200(0x000208u); }" in full
        assert "# alias 000208 -> 000200" in manifest

        assert "void func_000220(void) {" in full
        assert "void func_000222(void) {" in full
        assert "# alias 000222" not in manifest

        assert "func_000206" not in full
        assert "func_000206" not in dispatch
        assert "rejected by blacklist/code gate" in proc.stdout
        assert "{ 0x000200u, func_000200 }" in dispatch
        assert "{ 0x000208u, func_000208 }" in dispatch

    print("function_aliases: all checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
