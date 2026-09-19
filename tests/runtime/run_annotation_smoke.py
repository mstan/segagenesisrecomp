#!/usr/bin/env python3
"""Isolated stock-ROM boot/attract smoke for an annotation-only rebuild."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tomllib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=3600)
    args = parser.parse_args()
    target = args.out.resolve()
    target.mkdir(parents=True, exist_ok=False)
    exe = target / args.exe.name
    shutil.copy2(args.exe, exe)
    shutil.copy2(args.exe.parent / "SDL2.dll", target / "SDL2.dll")
    (target / "smoke.input").write_text(
        "WAIT 600\nSCREENSHOT title.png\nWAIT 1200\nSCREENSHOT attract.png\n"
        "DUMP_RAM attract.ram.bin\nWAIT 1600\nSCREENSHOT gameplay.png\nDUMP_RAM gameplay.ram.bin\n", encoding="utf-8")
    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software")
    with (target / "run.log").open("w") as log:
        subprocess.run([str(exe), str(args.rom.resolve()), "--no-launcher", "--widescreen", "off",
                        "--input-script", "smoke.input", "--max-frames", str(args.frames), "--target-fps", "1000"],
                       cwd=target, env=env, stdout=log, stderr=log, check=True, timeout=180,
                       creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    misses = tomllib.loads((target / "dispatch_misses.toml").read_text())
    if misses.get("functions", {}).get("extra"):
        raise RuntimeError(f"Dispatch misses: {target}")
    for name in ("title.png", "attract.png", "gameplay.png", "attract.ram.bin", "gameplay.ram.bin"):
        if not (target / name).is_file() or (target / name).stat().st_size == 0:
            raise RuntimeError(f"Missing checkpoint: {name}")
    if (target / "attract.ram.bin").read_bytes() == (target / "gameplay.ram.bin").read_bytes():
        raise RuntimeError("RAM did not advance between checkpoints")
    if (target / "attract.ram.bin").read_bytes()[0xf600] != 0x08:
        raise RuntimeError("The attract checkpoint did not reach demo gameplay")
    (target / "result.json").write_text(json.dumps({"executable": args.exe.name, "frames": args.frames,
        "dispatch_misses": 0, "checkpoints": 3}, indent=2) + "\n")
    print(f"PASS {args.exe.name}: {args.frames} frames, three captures, zero dispatch misses", flush=True)


if __name__ == "__main__":
    main()
