"""Deterministic native-world fixtures (RAM setup, never ROM writes).

Fixtures intentionally place actors/objects; they are collision/transition
regressions, not evidence of navigating an entire campaign by controller.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tomllib

p = argparse.ArgumentParser(description=__doc__)
for name in ("exe", "rom", "amy", "s3k", "out"):
    p.add_argument(f"--{name}", type=Path, required=True)
p.add_argument("--scene", choices=("spring", "combat", "monitor", "special", "recovery", "boss", "climb"), required=True)
p.add_argument("--swap", action="store_true", help="Special-stage P1 Tails/P2 Sonic")
a = p.parse_args()
out = a.out.resolve()
out.mkdir(parents=True, exist_ok=False)
exe = out / a.exe.name
shutil.copy2(a.exe, exe)
shutil.copy2(a.exe.parent / "SDL2.dll", out / "SDL2.dll")
roster = ["amy", "knuckles", "sonic", "tails"] if a.scene == "special" and not a.swap else ["tails", "sonic", "knuckles", "amy"]
(out / "sonic2-party.ini").write_text("slots=4\namy_enabled=1\ns3k_enabled=1\n" +
    f"amy_path={a.amy.resolve().as_posix()}\ns3k_path={a.s3k.resolve().as_posix()}\n" +
    "".join(f"player{i+1}={name}\n" for i, name in enumerate(roster)))
script = ["WAIT 600", "PRESS START 2", "WAIT 40", "PRESS START 2", "WAIT 220", "ASSERT_RAM8 FFF600 0C"]

def write(size, at, value):
    script.append(f"WRITE_RAM{size} {0xff0000+at:X} {value & ((1<<size)-1):X}")

def capture(name):
    script.extend([f"SCREENSHOT {name}.png", f"DUMP_RAM {name}.bin"])

def object_at(at, identity, x, y, subtype=0):
    script.append(f"ASSERT_RAM8 {0xff0000+at:X} 0")
    for offset in range(0, 64, 4):
        write(32, at+offset, 0)
    write(8, at, identity)
    write(16, at+8, x)
    write(16, at+12, y)
    write(8, at+0x28, subtype)

capture("before")
if a.scene == "spring":
    object_at(0xb800, 0x41, 160, 640)
    object_at(0xb840, 0x41, 240, 640)
    for at, x in ((0xcfc0, 160), (0xcf80, 240)):
        write(32, at+8, x<<16)
        write(32, at+12, 599<<16)
        write(32, at+0x10, 0x100)
        write(16, at+0x14, 0)
        write(8, at+0x22, 2)
    script.append("WAIT 12")
    capture("after")
elif a.scene in ("combat", "monitor"):
    write(32, 0xcf88, 160<<16)
    write(32, 0xcf8c, 656<<16)
    write(32, 0xcf90, 0)
    write(16, 0xcf94, 0)
    write(8, 0xcfa2, 0)
    write(16, 0xcfb0, 0)
    script.extend(["PLAYER 4", "PRESS A 2", "WAIT 14"])
    capture("hammer")
    object_at(0xb900, 0x26 if a.scene=="monitor" else 0x4b, 181, 645, 4 if a.scene=="monitor" else 0)
    script.append("WAIT 4")
    capture("after")
    if a.scene == "monitor":
        script.append("WAIT 80")
        capture("reward")
elif a.scene in ("boss", "climb"):
    write(16, 0xfe10, 1)
    write(16, 0xfe02, 1)
    script.append("WAIT 400")
    positions=((0xb000,0x2a60),(0xb040,0x2980),(0xcfc0,0x2960),(0xcf80,0x2940)) if a.scene=="boss" else ((0xb000,0x1100),(0xb040,0x10e0),(0xcfc0,0x10c0),(0xcf80,0x10a0))
    for at, x in positions:
        write(32, at+8, x<<16)
        write(32, at+12, (0x3e0 if a.scene=="boss" else 0x300)<<16)
        write(32, at+0x10, 0)
        write(8, at+0x22, 2)
    write(16,0xb030,1200) # isolate P4's hit from an idle P1 death/level freeze
    write(16, 0xee00, 0x2910 if a.scene=="boss" else 0x1060)
    write(16, 0xee04, 0x388 if a.scene=="boss" else 0x260)
    script.append("WAIT 400" if a.scene=="boss" else "WAIT 80")
    if a.scene=="boss":
        script.append("WAIT_RAM8 FFB420 0F")
    capture("arena")
    if a.scene == "boss":
        # The real hammer and boss handlers produce damage; no boss health,
        # collision, or state writes. Idle P1 is protected to isolate P4.
        script.extend(["ASSERT_RAM8 FFB400 56", "ASSERT_RAM8 FFB421 8"])
        write(32,0xcf88,0x29bc<<16)
        write(32,0xcf8c,0x430<<16)
        write(32,0xcf90,0)
        write(16,0xcf94,0)
        write(8,0xcfa2,0)
        write(16,0xcfb0,0)
        script.extend(["PLAYER 4", "PRESS A 2", "WAIT 16"])
        capture("boss-hit")
    else:
        # Start in a native jump inside EHZ2's real loop at x=$1150.
        # Glide/grab/climb/wall-jump transitions come solely from pad input.
        write(32,0xcfc8,0x1120<<16)
        write(32,0xcfcc,0x368<<16)
        write(32,0xcfd0,0xfd00)
        write(16,0xcfd4,0)
        write(8,0xcfe2,6)
        write(8,0xcffc,1)
        write(8,0xcfdc,2)
        script.extend(["PLAYER 3", "HOLD C", "WAIT 18"])
        capture("wall")
        script.extend(["HOLD UP", "WAIT 12", "RELEASE UP"])
        capture("climb")
        script.extend(["RELEASE C", "WAIT 2", "PRESS C 2", "WAIT 4"])
        capture("wall-jump")
elif a.scene == "recovery":
    write(8, 0xcfa4, 6)  # companion death must not consume a P1 life
    write(32, 0xcfc8, 3000<<16)
    script.append("WAIT 140")
    capture("after")
    # Native act reload; same chosen characters, fresh independent actor state.
    write(16, 0xfe10, 1)
    write(16, 0xfe02, 1)
    script.append("WAIT 400")
    capture("reload")
else:
    write(8, 0xf600, 0x10)  # enter the unchanged native SS dispatcher
    script.extend(["WAIT 500", "ASSERT_RAM8 FFF600 10"])
    capture("halfpipe")
    script.extend(["PLAYER 2", "HOLD RIGHT", "WAIT 18", "RELEASE", "PRESS C 2", "WAIT 4"])
    capture("input")
    # No ring writes: fail the native checkpoint and wait through native results.
    script.extend(["WAIT_RAM8 FFF600 0C", "WAIT 160"])
    capture("return")
script.append("EXIT")
(out / "fixture.input").write_text("\n".join(script)+"\n")
env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
           SDL_RENDER_DRIVER="software", GENESIS_STRICT_JSR_STACK="1")
with (out / "run.log").open("w") as log:
    result = subprocess.run([str(exe), str(a.rom.resolve()), "--no-launcher", "--widescreen", "off",
        "--input-script", "fixture.input", "--max-frames", "8500", "--target-fps", "1000"],
        cwd=out, env=env, stdout=log, stderr=log, timeout=180,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
# Inspect diagnostics even on failed assertions/exits.
misses = tomllib.loads((out / "dispatch_misses.toml").read_text()).get("functions", {}).get("extra")
assert not misses, f"Unresolved native dispatches: {misses}"
assert result.returncode == 0, f"exit {result.returncode}: {out / 'run.log'}"
ram = lambda name: (out / f"{name}.bin").read_bytes()
word = lambda r, at: int.from_bytes(r[at:at+2], "big")
signed = lambda r, at: int.from_bytes(r[at:at+2], "big", signed=True)
before = ram("before")
if a.scene == "special":
    ss, moved, returned = ram("halfpipe"), ram("input"), ram("return")
    assert [ss[0xb000],ss[0xb040]] == ([0x10,9] if a.swap else [9,9])
    assert ss[0xb066] != moved[0xb066], "P2 half-pipe input did not change angle"
    assert [returned[o] for o in (0xb000,0xb040,0xcfc0,0xcf80)] == ([2,1,1,1] if a.swap else [1,1,1,2])
    assert returned[0xfe12] == before[0xfe12], "SS round-trip spent a life"
elif a.scene not in ("boss", "climb"):
    after = ram("after")
    assert after[0xfe12] == before[0xfe12], "Companion spent a P1 life"
    if a.scene == "spring":
        for at in (0xcfc0, 0xcf80):
            assert signed(after, at+0x12) < -0x800, f"P{3 if at==0xcfc0 else 4} spring failed"
    elif a.scene in ("combat", "monitor"):
        assert after[0xb900] in (0, 0x27) or (a.scene=="monitor" and after[0xb924] in (4,6)), f"Hammer failed to destroy object: ID={after[0xb900]:02x}"
        assert after[0xcfa4] == 2, "Hammer hit hurt Amy"
        if a.scene == "monitor":
            assert word(ram("reward"),0xfe20) >= 10, "Party monitor ring reward missing"
    else:
        for at in (0xcfc0,0xcf80):
            assert after[at+0x24] == 2 and abs(word(after,at+8)-word(after,0xb008)) < 200
        reloaded = ram("reload")
        assert word(reloaded,0xfe10) == 1 and reloaded[0xf600] == 12
        assert [reloaded[o] for o in (0xb000,0xb040,0xcfc0,0xcf80)] == [2,1,1,1]
if a.scene == "boss":
    arena=ram("arena")
    for at in range(0xb400,0xd000,64):
        if arena[at]==0x56:
            print(f"Boss {at:04x} routine={arena[at+0x24]:02x} hp={arena[at+0x21]} x={word(arena,at+8):04x} y={word(arena,at+12):04x}")
    assert arena[0xf7aa]==2
    assert ram("boss-hit")[0xb421] < arena[0xb421], "P4 hammer did not damage native boss"
if a.scene == "climb":
    wall, climbed, jumped = ram("wall"),ram("climb"),ram("wall-jump")
    assert 0xb7<=wall[0xcfda]<=0xbc, f"Glide did not grab wall: frame={wall[0xcfda]:02x}"
    assert word(climbed,0xcfcc)<word(wall,0xcfcc), "Up did not climb wall"
    assert signed(jumped,0xcfd0)<0 and signed(jumped,0xcfd2)<0, "Wall jump did not launch away/up"
print(f"PASS native-world {a.scene} fixture; roster={roster}; zero dispatch/stack errors")
