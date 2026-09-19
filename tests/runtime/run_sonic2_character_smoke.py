"""Owner-donor live gameplay smoke. Never changes the stock Sonic 2 ROM."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tomllib

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--exe", type=Path, required=True)
p.add_argument("--rom", type=Path, required=True)
p.add_argument("--donor", type=Path, required=True)
p.add_argument("--character", choices=("amy", "knuckles"), required=True)
p.add_argument("--out", type=Path, required=True)
p.add_argument("--vs", action="store_true")
p.add_argument("--p2", choices=("sonic", "tails"), default="tails")
p.add_argument("--swap-roles", action="store_true", help="Put the donor character in P2 instead")
a = p.parse_args()
out = a.out.resolve()
out.mkdir(parents=True, exist_ok=False)
exe = out / a.exe.name
shutil.copy2(a.exe, exe)
shutil.copy2(a.exe.parent / "SDL2.dll", out / "SDL2.dll")
resource = "amy" if a.character == "amy" else "s3k"
roster=[a.p2,a.character] if a.swap_roles else [a.character,a.p2]
(out / "sonic2-party.ini").write_text(
    f"slots=2\nplayer1={roster[0]}\nplayer2={roster[1]}\nplayer3=none\nplayer4=none\n"
    f"{resource}_enabled=1\n{resource}_path={a.donor.resolve().as_posix()}\n")
script = "WAIT 600\nPRESS START 2\nWAIT 40\n"
if a.vs:
    script += "PRESS DOWN 2\nWAIT 8\n"
script += "PRESS START 2\nWAIT 220\n"
if a.vs:
    script += "ASSERT_RAM8 FFF600 1C\nPRESS START 2\nWAIT 220\n"
script += "ASSERT_RAM8 FFF600 0C\nSCREENSHOT character-start.png\nDUMP_RAM character-start.bin\n"
if a.swap_roles:
    script += "PLAYER 2\n"
if a.character == "amy":
    script += "PRESS A 2\nWAIT 16\nSCREENSHOT character-special.png\nDUMP_RAM character-special.bin\nWAIT 40\n"
    script += "HOLD DOWN\nPRESS A 2\nWAIT 8\nRELEASE DOWN\nSCREENSHOT character-jump.png\nDUMP_RAM character-jump.bin\n"
else:
    script += "PRESS C 12\nWAIT 20\nPRESS C 45\nWAIT 8\nSCREENSHOT character-special.png\nDUMP_RAM character-special.bin\n"
    script += "WAIT 20\nSCREENSHOT character-jump.png\nDUMP_RAM character-jump.bin\n"
script += "HOLD RIGHT\nWAIT 100\nRELEASE\nSCREENSHOT character-move.png\nDUMP_RAM character-move.bin\nEXIT\n"
(out / "character.input").write_text(script)
env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
           SDL_RENDER_DRIVER="software", GENESIS_STRICT_JSR_STACK="1")
with (out / "run.log").open("w") as log:
    result = subprocess.run([str(exe), str(a.rom.resolve()), "--no-launcher",
        "--widescreen", "off", "--input-script", "character.input", "--max-frames", "1800",
        "--target-fps", "1000"], cwd=out, env=env, stdout=log, stderr=log,
        timeout=120, creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
assert result.returncode == 0, f"exit {result.returncode}: {out / 'run.log'}"
assert not tomllib.loads((out / "dispatch_misses.toml").read_text()).get("functions", {}).get("extra")
ram = [(out / f"character-{s}.bin").read_bytes() for s in ("start", "special", "jump", "move")]
assert all([r[0xb000],r[0xb040]] == [2 if name=="tails" else 1 for name in roster] for r in ram)
assert all(bool(int.from_bytes(r[0xffd8:0xffda], "big")) == a.vs for r in ram)
frame=0xb05a if a.swap_roles else 0xb01a
assert ram[1][frame] != ram[0][frame], "Special action did not change donor frame"
assert all(r[0xfe12] == ram[0][0xfe12] for r in ram), "Unexpected life loss"
print(f"PASS {a.character} owner-donor gameplay smoke; inspect captures for visual correctness")
