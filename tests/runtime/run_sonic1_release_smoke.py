#!/usr/bin/env python3
"""Cold-boot the actual production executable using persisted mod selections.

No debug.ini or TCP is needed. Each finite run owns an isolated executable and
settings directory; no player's configuration is overwritten. Requires Pillow.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tomllib
from PIL import Image

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--exe",type=Path,required=True)
    ap.add_argument("--rom",type=Path,required=True)
    ap.add_argument("--out",type=Path,required=True)
    ap.add_argument("--native-reference",type=Path,required=True)
    args=ap.parse_args()
    env=os.environ.copy();env.update(SDL_VIDEODRIVER="dummy",SDL_AUDIODRIVER="dummy",SDL_RENDER_DRIVER="software")
    for name,aspect,width,override in (("native",None,320,None),("fixed16","16:9",398,None),
            ("fixed21","21:9",523,None),("fixed32","32:9",796,None),("override-off","32:9",320,"off")):
        case=args.out.resolve()/name;case.mkdir(parents=True,exist_ok=True)
        exe=case/args.exe.name;shutil.copy2(args.exe,exe)
        shutil.copy2(args.exe.parent/"SDL2.dll",case/"SDL2.dll")
        if aspect:(case/"settings.ini").write_text(f"[mods.widescreen]\nenabled = 1\naspect = {aspect}\n")
        (case/"input.txt").write_text("WAIT 720\nPRESS START 2\nWAIT 400\nSCREENSHOT start.png\nDUMP_RAM start.ram.bin\nWAIT 10\n")
        cmd=[str(exe),str(args.rom.resolve()),"--no-launcher","--input-script","input.txt","--max-frames","1200","--target-fps","1000"]
        if override:cmd += ["--widescreen",override]
        with (case/"run.log").open("w") as log:
            subprocess.run(cmd,cwd=case,env=env,stdout=log,stderr=log,timeout=90,check=True,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0)
        log=(case/"run.log").read_text()
        assert "0 unique true-miss addrs, 0 raw miss events" in log,(name,"dispatch summary")
        misses=case/"dispatch_misses.toml"
        if misses.exists():assert not tomllib.loads(misses.read_text()).get("functions",{}).get("extra"),name
        im=Image.open(case/"start.png")
        assert im.size==(width,224),(name,im.size)
        if width==320:
            assert (case/"start.ram.bin").read_bytes()==(args.native_reference/"custom-start.ram.bin").read_bytes(),name
            assert im.convert("RGBA").tobytes()==Image.open(args.native_reference/"custom-start.png").convert("RGBA").tobytes(),name
        print(f"PASS production {name}: {width}x224, no dispatch misses",flush=True)

if __name__=="__main__":main()
