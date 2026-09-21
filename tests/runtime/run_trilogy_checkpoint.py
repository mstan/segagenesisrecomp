#!/usr/bin/env python3
"""Donor post/death/quit/reload component test. Only player positions are fixtures."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
from run_trilogy_campaign import records,run

ap=argparse.ArgumentParser()
for key in ('exe','rom','sonic1','sonic2','out'):ap.add_argument('--'+key,type=Path,required=True)
args=ap.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
runtime=out/'runtime';runtime.mkdir();exe=args.exe.resolve();rom=args.rom.resolve()
shutil.copy2(exe,runtime/exe.name);shutil.copy2(exe.parent/'SDL2.dll',runtime/'SDL2.dll')
(runtime/'settings.ini').write_text('[trilogy]\nenabled=1\n')
def capture(n):return f'SCREENSHOT {(out/(n+".png")).as_posix()}\nDUMP_RAM {(out/(n+".ram.bin")).as_posix()}\nWAIT 1\n'
script='WAIT 700\nPRESS START 2\nWAIT 90\nPRESS START 2\nWAIT 400\nPRESS START 2\nWAIT_RAM8 FFF600 8C\nWAIT_RAM8 FFF600 0C\nWAIT 60\n'
script+='WRITE_RAM16 FFB010 11E8\nWRITE_RAM16 FFB014 21C\nWRITE_RAM16 FFEE78 1148\nWRITE_RAM16 FFEE7C 1BC\nWAIT 90\n'
script+=capture('post')
script+='WRITE_RAM16 FFB014 800\nWAIT_RAM8 FFB005 6\nWAIT 10\n'+capture('death')
script+='WAIT_RAM8 FFF600 8C\nWAIT_RAM8 FFF600 0C\nWAIT 120\n'+capture('retry')+'WAIT 30\n'
(out/'input.txt').write_text(script)
env=os.environ.copy()
for k in ('SONIC_TRILOGY_STAGE','SONIC_TRILOGY_ROM'):env.pop(k,None)
env.update(SONIC_TRILOGY_S1_ROM=str(args.sonic1.resolve()),SONIC_TRILOGY_S2_ROM=str(args.sonic2.resolve()),
           SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
with (out/'run.log').open('w') as log:
    subprocess.run([str(runtime/exe.name),str(rom),'--no-launcher','--target-fps','1000','--max-frames','3500',
                    '--widescreen','off','--input-script',str(out/'input.txt')],cwd=out,env=env,stdout=log,stderr=log,
                   check=True,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
log=(out/'run.log').read_text(errors='replace')
assert '0 unique true-miss addrs, 0 raw miss events' in log and 'FATAL' not in log
post=(out/'post.ram.bin').read_bytes();retry=(out/'retry.ram.bin').read_bytes()
assert post[0xFE2A]&127==1,'post did not activate'
assert retry[0xB005]==2 and abs(int.from_bytes(retry[0xB010:0xB012],'big')-0x11E8)<32,'retry missed donor post'
assert retry[0xFE12]==post[0xFE12]-1,'death did not consume exactly one life'
seed=(runtime/(rom.stem+'.srm')).read_bytes()
assert records(seed)[b'S1EX'][1][2]==1,'checkpoint not saved'
run(args,'reload',3,0x1000,seed)
loaded=(out/'reload/level.ram.bin').read_bytes()
assert abs(int.from_bytes(loaded[0xB010:0xB012],'big')-0x11E8)<32,'reopened save missed donor post'
print('PASS: donor post touch -> death -> retry -> quit -> native-menu reload preserves checkpoint and lives')
