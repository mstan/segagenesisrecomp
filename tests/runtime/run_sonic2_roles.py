"""Real-input native-character role swap smoke (not a four-player acceptance test)."""
import argparse
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
(out / "sonic2-party.ini").write_text("slots=2\nplayer1=tails\nplayer2=sonic\nplayer3=none\nplayer4=none\n")
(out / "roles.input").write_text(
    "WAIT 600\nPRESS START 2\nWAIT 40\nPRESS START 2\nWAIT 220\n"
    "ASSERT_RAM8 FFF600 0C\nASSERT_RAM8 FFB000 02\nASSERT_RAM8 FFB040 01\n"
    "SCREENSHOT roles-start.png\nDUMP_RAM roles-start.bin\n"
    "PLAYER 2\nHOLD RIGHT\nWAIT 45\nRELEASE\nPRESS C 20\nWAIT 20\n"
    "SCREENSHOT roles-p2.png\nDUMP_RAM roles-p2.bin\n"
    "PLAYER 1\nHOLD RIGHT\nWAIT 70\nRELEASE\nPRESS C 20\nWAIT 20\n"
    "SCREENSHOT roles-p1.png\nDUMP_RAM roles-p1.bin\nEXIT\n")
env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
           SDL_RENDER_DRIVER="software", GENESIS_STRICT_JSR_STACK="1")
with (out / "run.log").open("w") as log:
    result = subprocess.run([str(exe), str(a.rom.resolve()), "--no-launcher",
        "--widescreen", "off", "--input-script", "roles.input", "--max-frames", "1300",
        "--target-fps", "1000"], cwd=out, env=env, stdout=log, stderr=log,
        timeout=120, creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
assert result.returncode == 0, f"exit {result.returncode}: {out / 'run.log'}"
assert not tomllib.loads((out / "dispatch_misses.toml").read_text()).get("functions", {}).get("extra")
ram = [(out / f"roles-{s}.bin").read_bytes() for s in ("start", "p2", "p1")]
word = lambda r, at: int.from_bytes(r[at:at+2], "big")
assert word(ram[1], 0xb048) > word(ram[0], 0xb048) + 10, "P2 Sonic did not move independently"
assert word(ram[2], 0xb008) > word(ram[1], 0xb008) + 10, "P1 Tails did not move"
assert all(r[0xfe12] == ram[0][0xfe12] for r in ram), "Unexpected life loss"
print("PASS P1 Tails / P2 Sonic native gameplay, independent move/jump input, zero dispatch/stack errors")
