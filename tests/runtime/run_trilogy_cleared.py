#!/usr/bin/env python3
"""Prepare an offline cleared slot; check preservation and native menu launches."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
from run_trilogy_campaign import records


def verify_preserved(before,after):
    changed={base+i*2 for base in (0x281,0x32D) for i in list(range(70,80))+[82,83]}
    assert all(a==b or i in changed for i,(a,b) in enumerate(zip(before[:0x4000],after[:0x4000]))),'unrelated native bytes changed'
    old,new=records(before),records(after)
    for tag,value in old.items():
        if tag in (b'S1EX',b'S2EX'):assert new[tag][1][:56]==value[1][:56]
        elif tag==b'CAMP':
            for offset,length in ((32,112),(256,28),(288,56)):
                assert new[tag][1][offset:offset+length]==value[1][offset:offset+length]
        else:assert new[tag]==value,'unrelated extension changed'
    assert new[b'CAMP'][1][146]==2 and new[b'CAMP'][1][149]==127
    assert new[b'S1EX'][1][59]==7 and new[b'S2EX'][1][59]==1


def main():
    ap=argparse.ArgumentParser()
    for key in ('exe','rom','sonic1','sonic2','seed','tool','out'):ap.add_argument('--'+key,type=Path,required=True)
    args=ap.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    seed=out/'cleared.srm';seed.write_bytes(args.seed.read_bytes())
    subprocess.run([str(args.tool.resolve()),str(seed),'8'],check=True)
    verify_preserved(args.seed.read_bytes(),seed.read_bytes())
    for name,stage,steps,button in [('ghz2',0x1001,1,'UP'),('ghz3',0x1002,2,'UP'),('ehz',0x2000,3,'UP'),('aiz2',0x3001,5,'UP'),('ddz',0x30C0,1,'DOWN')]:
        case=out/name;case.mkdir();runtime=case/'runtime';runtime.mkdir()
        exe=args.exe.resolve();rom=args.rom.resolve()
        for f in (exe,exe.parent/'SDL2.dll'):shutil.copy2(f,runtime/f.name)
        shutil.copy2(seed,runtime/(rom.stem+'.srm'))
        (runtime/'settings.ini').write_text('[trilogy]\nenabled=1\n')
        def shot(n):return f'SCREENSHOT {(case/(n+".png")).as_posix()}\nDUMP_RAM {(case/(n+".ram.bin")).as_posix()}\n'
        script='WAIT 700\nPRESS START 2\nWAIT 90\nPRESS START 2\nWAIT 400\n'+('PRESS RIGHT 2\nWAIT 30\n'*7)
        script+=shot('cleared')+(f'PRESS {button} 2\nWAIT 30\n'*steps)+shot('selected')
        script+='PRESS START 2\nWAIT_RAM8 FFF600 8C\nWAIT_RAM8 FFF600 0C\nWAIT 120\n'+shot('level')+'WAIT 10\n'
        (case/'input.txt').write_text(script)
        env=os.environ.copy()
        for k in ('SONIC_TRILOGY_STAGE','SONIC_TRILOGY_ROM'):env.pop(k,None)
        env.update(SONIC_TRILOGY_S1_ROM=str(args.sonic1.resolve()),SONIC_TRILOGY_S2_ROM=str(args.sonic2.resolve()),
            SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
        with (case/'run.log').open('w') as log:
            subprocess.run([str(runtime/exe.name),str(rom),'--no-launcher','--widescreen','off','--target-fps','1000',
                '--max-frames','2800','--input-script',str(case/'input.txt')],cwd=case,env=env,stdout=log,stderr=log,
                check=True,timeout=90,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        log=(case/'run.log').read_text(errors='replace')
        assert f'Starting stage {stage:04X} slot 7' in log
        assert '0 unique true-miss addrs, 0 raw miss events' in log and 'FATAL' not in log
        r=(case/'level.ram.bin').read_bytes();assert r[0xF600]==12 and r[0xB005]==2
        print(f'PASS slot 8 -> {stage:04X}; other seven saves preserved',flush=True)


if __name__=='__main__':main()
