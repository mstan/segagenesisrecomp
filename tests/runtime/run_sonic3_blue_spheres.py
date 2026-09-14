#!/usr/bin/env python3
"""Read-only F2 replay: host-only Blue Spheres must not change native state.

Supply a user-owned save and pristine pre-change executable. Every run has
isolated SRAM. Never overwrite the input save, edit RAM, or pause the CPU.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from PIL import Image


def replay(exe, rom, save, case, mode):
    case.mkdir(parents=True, exist_ok=False)
    runtime = case / "runtime"
    runtime.mkdir()
    shutil.copy2(exe, runtime / exe.name)
    shutil.copy2(exe.parent / "SDL2.dll", runtime / "SDL2.dll")
    script = f"WAIT 20\nLOAD_STATE {save.as_posix()}\nWAIT 180\n"
    for frame in range(100):
        for command, extension in (("SCREENSHOT", "png"), ("DUMP_RAM", "ram.bin"), ("DUMP_VRAM", "vram.bin")):
            script += f"{command} {(case / f'frame-{frame}.{extension}').as_posix()}\n"
        # Start, collect, turn in both directions, jump, then allow an exit.
        buttons = {0: "UP", 6: "LEFT", 8: "", 16: "RIGHT", 18: "", 24: "C", 26: "", 40: "RIGHT", 42: ""}
        if frame in buttons:
            script += "RELEASE\n"
            if buttons[frame]:
                script += f"HOLD {buttons[frame]}\n"
        script += "WAIT 8\n"
    (case / "input.txt").write_text(script + "EXIT\n")
    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software")
    with (case / "run.log").open("w") as log:
        subprocess.run([str(runtime / exe.name), str(rom), "--no-launcher", "--widescreen", mode,
                        "--target-fps", "1000", "--input-script", str(case / "input.txt")],
                       cwd=case, env=env, stdout=log, stderr=log, check=True, timeout=120,
                       creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("exe", "baseline-exe", "rom", "save", "out"):
        parser.add_argument("--" + key, type=Path, required=True)
    parser.add_argument("--modes", nargs="+", default=["off", "16:9", "21:9", "32:9", "2676:374"])
    args = parser.parse_args()
    for key in ("exe", "baseline_exe", "rom", "save", "out"):
        setattr(args, key, getattr(args, key).resolve())
    save_hash = hashlib.sha256(args.save.read_bytes()).hexdigest()
    reference = args.out / "reference"
    replay(args.baseline_exe, args.rom, args.save, reference, "off")
    report = {}
    for mode in args.modes:
        case = args.out / mode.replace(":", "-")
        replay(args.exe, args.rom, args.save, case, mode)
        checked, counters, angles, modes, widths = 0, set(), set(), set(), set()
        for frame in range(100):
            name = f"frame-{frame}"
            ram = (reference / f"{name}.ram.bin").read_bytes()
            current = (case / f"{name}.ram.bin").read_bytes()
            modes.add(current[0xF600])
            widths.add(Image.open(case / f"{name}.png").width)
            if ram[0xF600] in (0x34, 0x48) or mode == "off":
                assert current == ram, (mode, name, "guest RAM changed")
                assert (case / f"{name}.vram.bin").read_bytes() == (reference / f"{name}.vram.bin").read_bytes(), (mode, name, "VRAM changed")
                checked += 1
            if ram[0xF600] == 0x34:
                counters.add(int.from_bytes(ram[0xE438:0xE43A], "big"))
                angles.add(ram[0xE426])
            if mode == "off":
                assert (case / f"{name}.png").read_bytes() == (reference / f"{name}.png").read_bytes(), (name, "native output changed")
        assert checked >= 10 and len(angles) > 1 and len(counters) > 1, ("insufficient replay coverage", checked, angles, counters)
        expected = 320 if mode == "off" else max(320, round(224 * float(mode.split(":")[0]) / float(mode.split(":")[1])))
        assert widths == {expected}, (mode, widths, expected)
        report[mode] = dict(native_exact_checkpoints=checked, blue_counts=sorted(counters), angles=sorted(angles), modes=sorted(modes), width=expected)
        print(mode, "PASS", report[mode], flush=True)
    assert hashlib.sha256(args.save.read_bytes()).hexdigest() == save_hash
    (args.out / "report.json").write_text(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
