#!/usr/bin/env python3
"""Private-ROM Blue Spheres integration. Positions and last-sphere state are fixtures.

Native giant-ring collision, emerald award, results and return run
normally. This is not a full Blue Spheres playthrough.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
from run_trilogy_campaign import records, run


def main():
    ap=argparse.ArgumentParser()
    for key in ('exe','rom','sonic1','sonic2','seed','out'):
        ap.add_argument('--'+key,type=Path,required=True)
    args=ap.parse_args();args.out=args.out.resolve();args.out.mkdir(parents=True,exist_ok=False)
    seed=args.seed.read_bytes()
    cases=[(slot,stage,False) for slot,stage in enumerate((0x1000,0x1001,0x1002,0x2000))]
    cases.append((0,0x1000,True))
    for slot,stage,checkpoint in cases:
        name=f'{stage:04X}'+('-post' if checkpoint else '')
        out=args.out/name;out.mkdir();runtime=out/'runtime';runtime.mkdir()
        exe=args.exe.resolve();rom=args.rom.resolve()
        shutil.copy2(exe,runtime/exe.name);shutil.copy2(exe.parent/'SDL2.dll',runtime/'SDL2.dll')
        (runtime/'settings.ini').write_text('[trilogy]\nenabled=1\n')
        save=runtime/(rom.stem+'.srm');save.write_bytes(seed)
        donor=(args.sonic2 if slot==3 else args.sonic1).read_bytes()
        p=0xC1D0 if slot==3 else 0x6112+slot*4
        x=int.from_bytes(donor[p:p+2],'big')+128;y=int.from_bytes(donor[p+2:p+4],'big')-56
        def shot(n):return f'SCREENSHOT {(out/(n+".png")).as_posix()}\nDUMP_RAM {(out/(n+".ram.bin")).as_posix()}\nWAIT 1\n'
        script='WAIT 700\nPRESS START 2\nWAIT 90\nPRESS START 2\nWAIT 400\n'
        script+='PRESS RIGHT 2\nWAIT 30\n'*slot
        script+='PRESS START 2\nWAIT_RAM8 FFF600 8C\nWAIT_RAM8 FFF600 0C\nWAIT 120\n'+shot('entrance')
        if checkpoint:
            x,y=0x11E8,0x1EC
            script+='WRITE_RAM16 FFFE20 17\nWRITE_RAM16 FFB010 11E8\nWRITE_RAM16 FFB014 21C\nWRITE_RAM16 FFEE78 1148\nWRITE_RAM16 FFEE7C 1BC\nWAIT 150\n'+shot('post')
        script+=f'WRITE_RAM16 FFB010 {x:X}\nWRITE_RAM16 FFB014 {y:X}\nWRITE_RAM16 FFB018 0\nWRITE_RAM16 FFB01A 0\nWRITE_RAM16 FFB01C 0\nWRITE_RAM16 FFFE20 17\n'
        script+='WAIT_RAM8 FFF600 34\nWAIT_RAM8 FFE439 66\nWAIT 90\n'+shot('blue')
        win=slot in (0,3)
        if win:script+='WRITE_RAM16 FFE438 0\nWRITE_RAM8 FFE44C 1\n' # state after last blue pickup
        script+='WAIT_RAM8 FFF600 48\nWAIT 200\n'+shot('results')
        script+='WAIT_RAM8 FFF600 8C\nWAIT_RAM8 FFF600 0C\nWAIT 90\n'+shot('returned')+'WAIT 30\n'
        (out/'input.txt').write_text(script)
        env=os.environ.copy()
        for k in ('SONIC_TRILOGY_STAGE','SONIC_TRILOGY_ROM'):env.pop(k,None)
        env.update(SONIC_TRILOGY_S1_ROM=str(args.sonic1.resolve()),SONIC_TRILOGY_S2_ROM=str(args.sonic2.resolve()),
                   SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
        with (out/'run.log').open('w') as log:
            subprocess.run([str(runtime/exe.name),str(rom),'--no-launcher','--target-fps','1000',
                '--max-frames','5000','--widescreen','off','--input-script',str(out/'input.txt')],
                cwd=out,env=env,stdout=log,stderr=log,check=True,timeout=90,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        log=(out/'run.log').read_text(errors='replace')
        assert '0 unique true-miss addrs, 0 raw miss events' in log and 'FATAL' not in log
        assert f'Entering Blue Spheres from {stage:04X}' in log and f'Returning to {stage:04X}' in log
        r=(out/'returned.ram.bin').read_bytes()
        assert r[0xF600]==12 and r[0xB005]==2 and r[0xFE48]==0
        assert abs(int.from_bytes(r[0xB010:0xB012],'big')-x)<32
        assert int.from_bytes(r[0xFE20:0xFE22],'big')==23
        if checkpoint:assert r[0xFE2A]&127==1,'special return lost checkpoint'
        assert bool(r[0xFFB2])==win,(stage,'emerald award')
        assert '[Trilogy audio] Native Sonic 3 score restored' in log
        assert log.count(f'[Trilogy audio] Sonic {2 if slot==3 else 1} score active')>=2
        data=save.read_bytes();rec=records(data)
        mask=2 if checkpoint else 1
        assert int.from_bytes(rec[b'SPCL'][1][slot*20+4+slot*4:slot*20+8+slot*4],'big')==mask
        # Relaunch through Data Select; usage and emerald must survive a process exit.
        run(args,f'reload-{name}',3,stage,data,slot=slot)
        rr=(args.out/f'reload-{name}/level.ram.bin').read_bytes()
        assert bool(rr[0xFFB2])==win and int.from_bytes(rr[0xFF92:0xFF96],'big')==mask
        if checkpoint:assert abs(int.from_bytes(rr[0xB010:0xB012],'big')-x)<32
        assert not any(int.from_bytes(rr[a:a+4],'big')==0x61682 for a in range(0xB0DE,0xCAE2,0x4A))
        print(f'PASS {name}: {"emerald" if win else "failure"}, native results/return, rings/music, saved entrance and reload',flush=True)


if __name__=='__main__':main()
