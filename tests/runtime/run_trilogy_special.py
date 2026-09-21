#!/usr/bin/env python3
"""Private ROM integration of donor entrance rules into native Blue Spheres.
Positions and last-sphere outcome are explicit fixtures, not full playthroughs.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
from run_trilogy_campaign import records, run

BOOT='WAIT 700\nPRESS START 2\nWAIT 90\nPRESS START 2\nWAIT 400\n'
class Script:
    def __init__(self,out):self.out=out;self.lines=[]
    def emit(self,s):self.lines.append(s+'\n')
    def word(self,a,v):self.emit(f'WRITE_RAM16 FF{a:04X} {v&65535:X}')
    def byte(self,a,v):self.emit(f'WRITE_RAM8 FF{a:04X} {v&255:X}')
    def shot(self,n):self.emit(f'SCREENSHOT {(self.out/(n+".png")).as_posix()}\nDUMP_RAM {(self.out/(n+".ram.bin")).as_posix()}\nWAIT 1')
    def position(self,x,y):
        for a,v in ((0xB010,x),(0xB014,y),(0xB018,0),(0xB01A,0),(0xB01C,0),(0xEE78,max(0,x-160)),(0xEE7C,max(0,y-96)),(0xEE1A,max(768,y-96))):self.word(a,v)
        for a,v in ((0xB005,2),(0xB02A,2),(0xB020,0),(0xB01E,19),(0xB01F,9),(0xB02E,0)):self.byte(a,v)
    def rings(self,n):self.word(0xFE20,n);self.byte(0xFE1D,1)

def execute(args,name,script,stage=None,seed=None,slot=0,packs=3):
    out=script.out;runtime=out/'runtime';runtime.mkdir()
    exe=args.exe.resolve();rom=args.rom.resolve()
    shutil.copy2(exe,runtime/exe.name);shutil.copy2(exe.parent/'SDL2.dll',runtime/'SDL2.dll')
    (runtime/'settings.ini').write_text('[trilogy]\nenabled=1\n')
    if seed is not None:(runtime/(rom.stem+'.srm')).write_bytes(seed)
    (out/'input.txt').write_text(''.join(script.lines))
    env=os.environ.copy()
    for k in ('SONIC_TRILOGY_STAGE','SONIC_TRILOGY_ROM','SONIC_TRILOGY_S1_ROM','SONIC_TRILOGY_S2_ROM'):env.pop(k,None)
    if packs&1:env['SONIC_TRILOGY_S1_ROM']=str(args.sonic1.resolve())
    if packs&2:env['SONIC_TRILOGY_S2_ROM']=str(args.sonic2.resolve())
    if stage is not None:env.update(SONIC_TRILOGY_STAGE=f'{stage:X}',SONIC_TRILOGY_ROM=str((args.sonic2 if stage==0x2000 else args.sonic1).resolve()))
    env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
    with (out/'run.log').open('w') as log:
        subprocess.run([str(runtime/exe.name),str(rom),'--no-launcher','--target-fps','1000','--max-frames','7500','--widescreen','off','--input-script',str(out/'input.txt')],cwd=out,env=env,stdout=log,stderr=log,check=True,timeout=100,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
    log=(out/'run.log').read_text(errors='replace')
    assert '0 unique true-miss addrs, 0 raw miss events' in log and 'FATAL' not in log,name
    return log

def ram(out,n):return (out/(n+'.ram.bin')).read_bytes()
def word(r,a):return int.from_bytes(r[a:a+2],'big')
def objects(r,codes):return [a for a in range(0xB0DE,0xCAE2,0x4A) if int.from_bytes(r[a:a+4],'big') in codes]
def boot(s,slot=0):s.emit(BOOT+'PRESS RIGHT 2\nWAIT 30\n'*slot+'PRESS START 2\nWAIT_RAM8 FFF600 8C\nWAIT_RAM8 FFF600 0C\nWAIT 120');s.shot('start')

def main():
    ap=argparse.ArgumentParser()
    for key in ('exe','rom','sonic1','sonic2','seed','out'):ap.add_argument('--'+key,type=Path,required=True)
    args=ap.parse_args();args.out=args.out.resolve();args.out.mkdir(parents=True,exist_ok=False)
    for slot,stage,win in ((0,0x1000,False),(1,0x1001,True),(3,0x2000,True)):
        name=f'{stage:04X}';out=args.out/name;out.mkdir();s=Script(out);boot(s,slot)
        if stage<0x2000:
            x,y=(0x2500,0x490) if stage==0x1000 else (0x1F00,0x390)
            s.position(x,y);s.rings(49);s.emit('WAIT 120');s.shot('below')
            s.rings(50);s.emit('WAIT 120');s.shot('eligible')
            # A real jump after approaching the goal, not writing the entrance flag.
            s.emit('HOLD RIGHT\nWAIT 50\nPRESS C 40\nWAIT 55\nRELEASE')
        else:
            s.position(0xDF0,0x96);s.rings(50);s.emit('WAIT 170');s.shot('eligible')
            s.emit('PRESS C 30\nWAIT 40')
        s.emit('WAIT_RAM8 FFF600 34\nWAIT_RAM8 FFE439 66\nWAIT 40');s.shot('blue')
        # Exercise Blue Spheres quicksave/load as well as native stage/results return.
        state=out/'blue.state';s.emit(f'SAVE_STATE {state.as_posix()}\nWAIT 20\nLOAD_STATE {state.as_posix()}\nWAIT 1');s.shot('blue-restored')
        if win:s.emit('WRITE_RAM16 FFE438 0\nWRITE_RAM8 FFE44C 1')
        s.emit('WAIT_RAM8 FFF600 48\nWAIT 200');s.shot('results')
        result_state=out/'results.state';s.emit(f'SAVE_STATE {result_state.as_posix()}\nWAIT 30\nLOAD_STATE {result_state.as_posix()}\nWAIT 2');s.shot('results-restored')
        s.emit('WAIT_RAM8 FFF600 8C\nWAIT_RAM8 FFF600 0C\nWAIT 90');s.shot('returned');s.emit('WAIT 1')
        log=execute(args,name,s,seed=args.seed.read_bytes())
        assert not objects(ram(out,'start'),{0x6166A,0x61682}),name
        if stage<0x2000:
            assert word(ram(out,'below'),0xFE20)==49
            assert not objects(ram(out,'below'),{0x6166A,0x61682})
            assert objects(ram(out,'eligible'),{0x61682}),name
            assert f'Goal ring collected in {stage:04X}; act results first' in log
            assert f'Goal -> Blue Spheres after {stage:04X}' in log
        else:assert 'Checkpoint 1 -> Blue Spheres from 2000' in log
        r=ram(out,'returned');assert r[0xF600]==12 and r[0xB005]==2 and not r[0xFE48]
        assert ram(out,'blue-restored')[0xF600]==0x34 and '[LOAD] loaded' in log
        assert ram(out,'results-restored')[0xF600]==0x48
        assert bool(r[0xFFB2])==win,(stage,'emerald award')
        if stage==0x2000:assert r[0xFE2A]&127==1 and abs(word(r,0xB010)-0xDF0)<32
        else:assert f'Starting stage {stage+1:04X}' in log
        data=(out/'runtime'/f'{args.rom.stem}.srm').read_bytes();rec=records(data)
        assert rec[b'SPCL'][0]==2
        assert int.from_bytes(rec[b'SPCL'][1][slot*20+4+slot*4:slot*20+8+slot*4],'big')==(2 if slot==3 else 1)
        target=stage if slot==3 else stage+1
        run(args,'reload-'+name,3,target,data,slot=slot)
        rr=ram(args.out/('reload-'+name),'level');assert bool(rr[0xFFB2])==win
        print(f'PASS {name}: donor entrance, native Blue Spheres, quickload, outcome, return and SRAM reload',flush=True)
    for name,stage,rings,emeralds,expected in (('ehz49',0x2000,49,0,False),('ehz50',0x2000,50,0,True),('ehz-complete',0x2000,50,7,False),('ghz-complete',0x1000,50,7,False),('ghz3',0x1002,50,0,False)):
        out=args.out/name;out.mkdir();s=Script(out);boot(s)
        s.byte(0xFFB0,emeralds)
        if stage==0x2000:s.position(0xDF0,0x96)
        elif stage==0x1000:s.position(0x2500,0x490)
        s.rings(rings);s.emit('WAIT 180');s.shot('eligible');s.emit('WAIT 450');s.shot('expired');s.emit('WAIT 1')
        log=execute(args,name,s,stage=stage)
        def stars(r):return [a for a in objects(r,{0x420000}) if r[a+0x28]==0xD8]
        r=ram(out,'eligible');assert bool(stars(r))==expected,name
        assert not stars(ram(out,'expired')),name
        assert not objects(r,{0x61682}),name
        assert 'Blue Spheres' not in log,name
        print(f'PASS {name}: threshold/emerald limit/no fabricated ring/portal expiry',flush=True)

if __name__=='__main__':main()
