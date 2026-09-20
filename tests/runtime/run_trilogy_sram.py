#!/usr/bin/env python3
"""Native input-only save regression with all optional chapter record sets.

Requires the private game executable/ROM and a native SRAM produced by the
stock renderer input run (slot one active at Angel Island). No donor is loaded.
Every case gets its own executable directory and disposable SRAM copy.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import zlib

from run_sonic3_custom_video import timeline


def u32(value):
    return struct.pack(">I", value)


def chapter(stage, checkpoint=0, cleared=0):
    return struct.pack(">HBBI", stage, checkpoint, cleared, 1) + bytes(56)


def tail(packs):
    if not packs:
        return b""
    campaign = bytearray(256)
    campaign[:8] = b"S3KTRI01"
    campaign[8:16] = u32(1) + u32(2)
    stage = 0x1001 if packs & 1 else 0x2000
    campaign[32:40] = struct.pack(">H6B", stage, 1, packs, 0, 0, 0, 0)
    campaign[252:256] = u32(zlib.crc32(campaign[:252]))
    core = bytes(campaign) + u32(1) + bytes(28) + chapter(0x3000) + bytes(32)
    # Deliberately different physical order: tags define meaning, not positions.
    records = []
    if packs & 2:
        records.append((b"S2EX", 1, chapter(0x2000, 2)))
    records.append((b"CAMP", 1, core))
    if packs & 1:
        records.append((b"S1EX", 1, chapter(0x1001, 3, 1)))
    records.append((b"FUTR", 99, b"opaque future expansion"))
    body = b"".join(tag + struct.pack(">HHII", version, 0, len(data), zlib.crc32(data)) + data
                    for tag, version, data in records)
    return b"S3KEXT01" + struct.pack(">4I", 1, 24 + len(body), len(records), zlib.crc32(body)) + body


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True, type=Path)
    ap.add_argument("--rom", required=True, type=Path)
    ap.add_argument("--native-sram", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()
    exe, rom, out = args.exe.resolve(), args.rom.resolve(), args.out.resolve()
    base = args.native_sram.read_bytes()
    assert len(base) == 0x4000
    assert base[0x281:0x295:2] == bytes.fromhex("00000000000000000300"), "Expected native input-run fixture"
    seeded = bytearray(base)
    # Invalid competition primary AND backup force the game's normal default
    # write at startup, so this checks an actual native SRAM flush, not just load.
    seeded[0x11] ^= 1
    seeded[0xBD] ^= 1
    reference = None
    for scenario in range(5):
        repair = scenario == 4
        packs = 3 if repair else scenario
        case = out / ("native-repair" if repair else str(packs))
        case.mkdir(parents=True, exist_ok=False)
        runtime = case / "runtime"
        runtime.mkdir()
        shutil.copy2(exe, runtime / exe.name)
        shutil.copy2(exe.parent / "SDL2.dll", runtime / "SDL2.dll")
        save = runtime / (rom.stem + ".srm")
        suffix = tail(packs)
        case_seed = bytearray(seeded)
        if repair:
            case_seed[0x281] ^= 1  # native campaign primary
            case_seed[0x32D] ^= 1  # native campaign backup
        save.write_bytes(case_seed + suffix)
        script = re.sub(r"(?m)^((?:SCREENSHOT|DUMP_RAM|DUMP_VRAM) )([^\n]+)$",
                        lambda m: m[1] + (case / m[2]).as_posix(), "WAIT 700\n" if repair else timeline())
        (case / "input.txt").write_text(script)
        env = os.environ.copy()
        env.pop("SONIC_TRILOGY_STAGE", None)
        env.pop("SONIC_TRILOGY_ROM", None)
        env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software")
        command = [str(runtime / exe.name), str(rom), "--no-launcher", "--max-frames", "800" if repair else "6000",
                   "--target-fps", "1000", "--widescreen", "off", "--input-script", str(case / "input.txt")]
        with (case / "run.log").open("w") as log:
            subprocess.run(command, cwd=case, env=env, stdout=log, stderr=log, check=True, timeout=180,
                           creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        data = save.read_bytes()
        assert data[:0x4000] != case_seed, "Game did not exercise native SRAM writes"
        assert data[0x4000:] == suffix, "Missing donor or native save altered expansion progress"
        log = (case / "run.log").read_text(errors="replace")
        assert "0 unique true-miss addrs, 0 raw miss events" in log
        assert "FATAL" not in log and "ASSERT_RAM" not in log
        if repair:
            assert data[0x281] == 0x80, "Expected the game to initialize the damaged native slots"
            print(f"PASS {case}: native checksum repair retained expansion progress", flush=True)
            continue
        if reference is None:
            reference = case
            native_result = data[:0x4000]
        else:
            assert data[:0x4000] == native_result
            for n in range(16):
                for extension in ("png", "ram.bin", "vram.bin"):
                    name = f"checkpoint-{n}.{extension}"
                    assert (case / name).read_bytes() == (reference / name).read_bytes(), name
        print(f"PASS {case}: native save writes preserved optional chapters; 16 native checkpoints agree", flush=True)


if __name__ == "__main__":
    main()
