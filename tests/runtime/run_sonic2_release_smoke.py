#!/usr/bin/env python3
"""Validate production binaries without TCP/debug instrumentation.

Cold-boot persisted Mods choices in isolated executable/settings directories.
Never overwrite the owner's settings. Use the native pre-port captures and
validated diagnostic build captures as independent golden references.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tomllib
from PIL import Image
from run_sonic2_custom_video import timeline


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--exe',type=Path,required=True)
    ap.add_argument('--rom',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--references',type=Path,required=True)
    args=ap.parse_args()
    env=os.environ.copy()
    env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
    cases=[('native',None,320,None,'off'),('fixed16','16:9',398,None,'16-9'),
           ('fixed21','21:9',523,None,None),('fixed32','32:9',796,None,'32-9'),
           ('adaptive','fit',320,None,None),
           ('ultrawide','fit',1603,'2676:374','2676-374'),('override-off','32:9',320,'off','off')]
    for name,aspect,width,override,reference in cases:
        case=args.out.resolve()/name;case.mkdir(parents=True,exist_ok=True)
        exe=case/args.exe.name;shutil.copy2(args.exe,exe)
        shutil.copy2(args.exe.parent/'SDL2.dll',case/'SDL2.dll')
        if aspect:(case/'settings.ini').write_text(f'[mods.widescreen]\nenabled = 1\naspect = {aspect}\n')
        (case/'input.txt').write_text(timeline())
        cmd=[str(exe),str(args.rom.resolve()),'--no-launcher','--input-script','input.txt',
             '--max-frames','3200','--target-fps','1000']
        if override:cmd += ['--widescreen',override]
        with (case/'run.log').open('w') as log:
            subprocess.run(cmd,cwd=case,env=env,stdout=log,stderr=log,timeout=120,check=True,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        log=(case/'run.log').read_text()
        assert '0 unique true-miss addrs, 0 raw miss events' in log,name
        assert not tomllib.loads((case/'dispatch_misses.toml').read_text())['functions']['extra'],name
        for n in range(10):
            image=Image.open(case/f'checkpoint-{n}.png')
            assert image.size==(width,224),(name,n,image.size)
            if reference:
                for suffix in ('png','ram.bin','vram.bin'):
                    filename=f'checkpoint-{n}.{suffix}'
                    assert (case/filename).read_bytes()==(args.references/reference/filename).read_bytes(),(name,filename)
        print(f'PASS production {name}: {width}x224, persisted Mods, 10 captures, zero dispatch misses',flush=True)


if __name__=='__main__':main()
