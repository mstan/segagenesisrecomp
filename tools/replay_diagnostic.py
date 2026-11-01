#!/usr/bin/env python3
"""Inspect/reproduce a Genesis feedback report using its exact game build."""
import argparse
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

MAX_TEXT = 64 * 1024 * 1024
MAX_STATES = 32 * 1024 * 1024


def fnv(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def inspect(path):
    if path.stat().st_size > MAX_TEXT:
        raise ValueError("report exceeds the input history limit")
    sidecar = Path(str(path) + ".sav")
    if sidecar.stat().st_size > MAX_STATES:
        raise ValueError("report exceeds the state attachment limit")
    for suffix in (".config.ini", ".mods.txt"):
        if not Path(str(path) + suffix).is_file():
            raise ValueError(f"missing {suffix} sidecar; attach the complete report")
    with path.open(encoding="utf-8") as log, sidecar.open("rb") as states:
        if log.readline() != "GENESISREPLAY 1\n":
            raise ValueError("unsupported report format")
        build, rom, sram = log.readline(), log.readline(), log.readline()
        if not build.startswith("BUILD ") or len(rom.split()) != 3 or not rom.startswith("ROM ") or sram not in ("SRAM 0\n", "SRAM 1\n"):
            raise ValueError("invalid report identity")
        executions = 0
        saved = set()
        pending = ended = False
        want_snapshot = True  # Boot SRAM, even on cartridges without SRAM.
        snapshots = 0
        offset = 0
        for number, row in enumerate(log, 5):
            f = row.split()
            try:
                if not f or ended:
                    raise ValueError("record after end or empty record")
                if want_snapshot:
                    if len(f) != 4 or f[0] != "S" or int(f[1]) != offset:
                        raise ValueError("missing or noncontiguous state attachment")
                    size, checksum = int(f[2]), int(f[3], 16)
                    if not 0 <= size <= 8 * 1024 * 1024 or offset + size > MAX_STATES:
                        raise ValueError("oversized state attachment")
                    data = states.read(size)
                    if len(data) != size or fnv(data) != checksum:
                        raise ValueError("missing or damaged state attachment")
                    offset += size
                    snapshots += 1
                    want_snapshot = False
                elif pending:
                    if len(f) != 6 or f[0] != "H" or int(f[1]) != executions + 1:
                        raise ValueError("invalid completed-frame checkpoint")
                    for value in f[2:]:
                        if not 0 <= int(value, 16) <= 0xffffffff:
                            raise ValueError("invalid state hash")
                    executions += 1
                    pending = False
                elif f[0] == "I" and len(f) == 18 and int(f[1]) == executions:
                    pads = [int(v, 16) for v in f[2:8]]
                    types = [int(v) for v in f[8:10]]
                    if any(not 0 <= v <= 0xfff for v in pads) or any(v not in (0, 1) for v in types):
                        raise ValueError("invalid controller inputs")
                    pending = True
                elif f[0] == "K" and len(f) == 2 and int(f[1]) == len(saved) + 1:
                    saved.add(int(f[1]))
                elif f[0] == "R" and len(f) == 2 and int(f[1]) in saved:
                    pass
                elif f == ["L"] or (f == ["A"] and executions == 0 and snapshots == 1):
                    want_snapshot = True
                elif f in (["W"], ["P"]):
                    pass
                elif len(f) == 2 and f[0] == "E" and int(f[1]) == executions:
                    ended = True
                else:
                    raise ValueError("invalid execution or checkpoint order")
            except (ValueError, IndexError) as error:
                raise ValueError(f"record {number}: {error}") from error
        if want_snapshot or states.read(1):
            raise ValueError("missing or unreferenced state attachment")
    print(build.strip())
    print(f"{rom.strip()}; {executions} completed executions, {snapshots} attachments, {len(saved)} checkpoints")
    complete = ended and not pending
    if not complete:
        print("INCOMPLETE: interrupted session; the last input may reproduce a hang/crash.")
    return complete


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("replay", type=Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--exe", type=Path, help="exact recorded executable")
    parser.add_argument("--rom", type=Path, help="your own matching ROM")
    parser.add_argument("--work-dir", type=Path, help="new, isolated reproduction directory")
    parser.add_argument("--headless", action="store_true", help="hidden, muted, uncapped playback")
    args = parser.parse_args()
    source = args.replay.resolve(strict=True)
    complete = inspect(source)
    if args.check:
        return 0 if complete else 2
    if not args.exe or not args.rom:
        parser.error("playback requires --exe and --rom")
    exe, rom = args.exe.resolve(strict=True), args.rom.resolve(strict=True)
    if args.work_dir:
        work = args.work_dir.resolve()
        work.mkdir(parents=True, exist_ok=False)
    else:
        work = Path(tempfile.mkdtemp(prefix="genesis-feedback-"))
    for suffix in ("", ".sav", ".config.ini", ".mods.txt"):
        shutil.copy2(Path(str(source) + suffix), work / ("capture.replay" + suffix))
    shutil.copy2(work / "capture.replay.config.ini", work / "settings.ini")
    env = {k: v for k, v in os.environ.items() if not k.startswith(("GENESIS_", "GENESISRECOMP_"))}
    env["GENESIS_USER_DIR"] = str(work)
    env["GENESIS_REPLAY_RECORD"] = "0"
    command = [str(exe), str(rom), "--no-launcher", "--replay-diagnostic", str(work / "capture.replay")]
    if args.headless:
        env["SDL_VIDEODRIVER"] = env["SDL_AUDIODRIVER"] = "dummy"
        env["SDL_RENDER_DRIVER"] = "software"
        command += ["--benchmark", "2147483647"]
    print(f"Reproduction directory: {work}", flush=True)
    with (work / "replay.log").open("wb") as output:
        result = subprocess.run(command, cwd=work, env=env, stdout=output, stderr=subprocess.STDOUT)
    print((work / "replay.log").read_text(encoding="utf-8", errors="replace"))
    return result.returncode if result.returncode else (0 if complete else 2)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from error
