#!/usr/bin/env python3
"""Private ROM object integration probes. Position fixtures are component tests,
not evidence of an input-only stage clear. Output includes RAM and screenshots.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess


def run(args):
    exe, rom, out = args.exe.resolve(), args.rom.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    runtime = out / "runtime"
    runtime.mkdir()
    shutil.copy2(exe, runtime / exe.name)
    shutil.copy2(exe.parent / "SDL2.dll", runtime / "SDL2.dll")
    script = "WAIT 700\nPRESS START 2\nWAIT 90\nPRESS START 2\nWAIT 400\nPRESS START 2\nWAIT_RAM8 FFF600 8C\nWAIT_RAM8 FFF600 0C\nWAIT 60\n"
    if args.position:
        x, y = args.position
        camera_y=min(1024,max(0,y-96))
        script += f"WRITE_RAM16 FFB010 {x:X}\nWRITE_RAM16 FFB014 {y:X}\nWRITE_RAM16 FFEE78 {max(0,x-160):X}\nWRITE_RAM16 FFEE7C {camera_y:X}\nWRITE_RAM16 FFEE1A {max(768,camera_y):X}\nWRITE_RAM16 FFFE20 63\nWAIT 2\n"
    for i in range(args.samples):
        script += f"SCREENSHOT {(out/f'{i:02}.png').as_posix()}\nDUMP_RAM {(out/f'{i:02}.ram.bin').as_posix()}\n"
        if args.right:
            script += "HOLD RIGHT\n"
        if args.jump:
            script += f"PRESS C {args.jump_frames}\n"
        script += f"WAIT {args.interval}\n"
    script += "RELEASE\nWAIT 30\n"
    (out / "input.txt").write_text(script)
    env = os.environ.copy()
    env.update(SONIC_TRILOGY_STAGE=f"{args.stage:X}", SONIC_TRILOGY_ROM=str(args.donor.resolve()),
               SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software")
    command = [str(runtime / exe.name), str(rom), "--no-launcher", "--max-frames", str(3500+args.samples*(args.interval+12)),
               "--target-fps", "1000", "--widescreen", "off", "--input-script", str(out/"input.txt")]
    with (out/"run.log").open("w") as log:
        subprocess.run(command, cwd=out, env=env, stdout=log, stderr=log, check=True, timeout=180,
                       creationflags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0)
    log = (out/"run.log").read_text(errors="replace")
    assert "0 unique true-miss addrs, 0 raw miss events" in log, "Dispatch misses"
    assert "FATAL" not in log and "ASSERT_RAM" not in log
    for i in range(args.samples):
        b = (out/f"{i:02}.ram.bin").read_bytes()
        word = lambda a: int.from_bytes(b[a:a+2], "big")
        objects = [(a, int.from_bytes(b[a:a+4], "big")) for a in range(0xB0DE,0xCAE2,0x4A) if any(b[a:a+4])]
        bosses = [(hex(a),word(a+0x10),word(a+0x14),b[a+0x29]) for a,code in objects if code==0x420000 and b[a+0x29]]
        print(i, f"mode={b[0xF600]:02X}", f"player={word(0xB010)},{word(0xB014)}", f"rings={word(0xFE20)}",
              f"lives={b[0xFE12]}", f"objects={len(objects)}", f"boss={bosses}", flush=True)
    print(f"Probe completed: {out}. Review gameplay assertions separately.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    for name in ("exe","rom","donor","out"):
        parser.add_argument("--"+name, type=Path, required=True)
    parser.add_argument("--stage", type=lambda v:int(v,16), default=0x1000)
    parser.add_argument("--position", type=lambda v:int(v,16), nargs=2)
    parser.add_argument("--samples",type=int,default=16)
    parser.add_argument("--interval",type=int,default=60)
    parser.add_argument("--right",action="store_true")
    parser.add_argument("--jump",action="store_true")
    parser.add_argument("--jump-frames",type=int,default=24)
    run(parser.parse_args())
