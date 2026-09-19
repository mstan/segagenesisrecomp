#!/usr/bin/env python3
"""Isolated real-input roster smoke; copies only exe/DLL, never edits the ROM."""
import argparse
import configparser
import os
from pathlib import Path
import shutil
import subprocess
import tomllib

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--exe", type=Path, required=True)
p.add_argument("--rom", type=Path, required=True)
p.add_argument("--out", type=Path, required=True)
a = p.parse_args()
out = a.out.resolve()
out.mkdir(parents=True, exist_ok=False)
exe = out / a.exe.name
shutil.copy2(a.exe, exe)
shutil.copy2(a.exe.parent / "SDL2.dll", out / "SDL2.dll")
shutil.copy2(Path(__file__).with_name("sonic2_options.input"), out / "options.input")
env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
           SDL_RENDER_DRIVER="software", GENESIS_STRICT_JSR_STACK="1")
with (out / "run.log").open("w") as log:
    result = subprocess.run([str(exe), str(a.rom.resolve()), "--no-launcher",
        "--widescreen", "off", "--input-script", "options.input", "--max-frames", "1200",
        "--target-fps", "1000"], cwd=out, env=env, stdout=log, stderr=log,
        timeout=120, creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
if result.returncode:
    raise RuntimeError(f"Options smoke exited {result.returncode}: {out / 'run.log'}")
for name in ("options-default.png", "options-four-slots.png", "options-unique.png", "options-back-title.png"):
    assert (out / name).is_file(), name
assert not tomllib.loads((out / "dispatch_misses.toml").read_text()).get("functions", {}).get("extra")
config = configparser.ConfigParser()
config.read_string("[party]\n" + (out / "sonic2-party.ini").read_text())
assert config["party"]["slots"] == "4"
assert [config["party"][f"player{i}"] for i in range(1, 5)] == ["sonic", "tails", "none", "none"]
print("PASS native title -> roster, slot count, uniqueness, persistence -> title; no dispatch/stack errors")
