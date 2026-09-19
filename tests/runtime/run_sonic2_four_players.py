"""Live four-actor smoke with independent P3/P4 pad scripts and private donors."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tomllib

p = argparse.ArgumentParser(description=__doc__)
for name in ("exe", "rom", "amy", "s3k", "out"):
    p.add_argument(f"--{name}", type=Path, required=True)
p.add_argument("--native-extras", action="store_true")
p.add_argument("--widescreen", default="off")
a = p.parse_args()
out = a.out.resolve()
out.mkdir(parents=True, exist_ok=False)
exe = out / a.exe.name
shutil.copy2(a.exe, exe)
shutil.copy2(a.exe.parent / "SDL2.dll", out / "SDL2.dll")
roster = ["amy", "knuckles", "sonic", "tails"] if a.native_extras else ["sonic", "tails", "knuckles", "amy"]
(out / "sonic2-party.ini").write_text("slots=4\namy_enabled=1\ns3k_enabled=1\n" +
    f"amy_path={a.amy.resolve().as_posix()}\ns3k_path={a.s3k.resolve().as_posix()}\n" +
    "".join(f"player{i+1}={name}\n" for i, name in enumerate(roster)))
(out / "four.input").write_text(
    "WAIT 600\nPRESS START 2\nWAIT 40\nPRESS START 2\nWAIT 220\nASSERT_RAM8 FFF600 0C\n"
    "SCREENSHOT four-start.png\nDUMP_RAM four-start.bin\n"
    "PLAYER 3\nHOLD RIGHT\nWAIT 35\nRELEASE\nPRESS C 12\nWAIT 16\nPRESS C 24\nWAIT 12\n"
    "SCREENSHOT four-p3.png\nDUMP_RAM four-p3.bin\n"
    "PLAYER 4\nHOLD RIGHT\nWAIT 32\nRELEASE\nPRESS A 2\nWAIT 16\n"
    "SCREENSHOT four-p4.png\nDUMP_RAM four-p4.bin\n"
    "PLAYER 1\nHOLD RIGHT\nWAIT 140\nRELEASE\n"
    "SCREENSHOT four-move.png\nDUMP_RAM four-move.bin\nSAVE_STATE party-rejected.bin\nWAIT 2\nEXIT\n")
env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
           SDL_RENDER_DRIVER="software", GENESIS_STRICT_JSR_STACK="1")
with (out / "run.log").open("w") as log:
    result = subprocess.run([str(exe), str(a.rom.resolve()), "--no-launcher", "--widescreen", a.widescreen,
        "--input-script", "four.input", "--max-frames", "1600", "--target-fps", "1000"],
        cwd=out, env=env, stdout=log, stderr=log, timeout=120,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
assert result.returncode == 0, f"exit {result.returncode}: {out / 'run.log'}"
assert not tomllib.loads((out / "dispatch_misses.toml").read_text()).get("functions", {}).get("extra")
ram = [(out / f"four-{s}.bin").read_bytes() for s in ("start", "p3", "p4", "move")]
word = lambda r, at: int.from_bytes(r[at:at+2], "big")
for state in ram:
    assert [state[o] for o in (0xb000, 0xb040, 0xcfc0, 0xcf80)] == ([1,1,1,2] if a.native_extras else [1,2,1,1])
assert word(ram[1],0xcfc8) > word(ram[0],0xcfc8)+8, "P3 did not move"
assert word(ram[1],0xcf88) == word(ram[0],0xcf88), "P3 input leaked into P4"
assert word(ram[2],0xcf88) > word(ram[1],0xcf88)+8, "P4 did not move"
assert all(r[0xfe12] == ram[0][0xfe12] for r in ram), "Companion consumed P1 lives"
assert not (out / "party-rejected.bin").exists(), "Incomplete host state was serialized"
assert "[SAVE] unavailable:" in (out / "run.log").read_text()
for prev, following, frames in zip(ram,ram[1:],(63,48,140)):
    delta=(word(following,0xfe04)-word(prev,0xfe04))%65536
    # Widescreen's existing variable V-int/DMA debt can put an end-of-output-
    # frame RAM dump on opposite sides of one native tick. Across the entire
    # 251-frame window the count must still be exact, never four world ticks.
    assert abs(delta-frames) <= (a.widescreen!="off"), "World tick count differs from output frames"
assert (word(ram[-1],0xfe04)-word(ram[0],0xfe04))%65536 == 251
print(f"PASS four independent actors {roster}, isolated P3/P4 movement, no life/dispatch/stack errors")
