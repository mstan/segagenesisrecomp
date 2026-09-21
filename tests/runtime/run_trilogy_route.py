#!/usr/bin/env python3
"""Input-only route probe with live, read-only state observations.

This controller is deliberately simple; a failed run can be poor play.
Only a logged native stage clear is treated as a successful traversal.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import time
from run_sonic1_custom_video import command


def main():
    ap=argparse.ArgumentParser()
    for n in ('exe','rom','donor','out'):ap.add_argument('--'+n,type=Path,required=True)
    ap.add_argument('--stage',type=lambda s:int(s,16),default=0x1000)
    ap.add_argument('--mode',default='off')
    ap.add_argument('--frames',type=int,default=10000)
    ap.add_argument('--port',type=int,default=4387)
    ap.add_argument('--until-boss',action='store_true')
    args=ap.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    runtime=out/'runtime';runtime.mkdir();exe=args.exe.resolve()
    shutil.copy2(exe,runtime/exe.name);shutil.copy2(exe.parent/'SDL2.dll',runtime/'SDL2.dll')
    (runtime/'debug.ini').write_text(f'[debug]\nenabled=1\nport={args.port}\n')
    script='WAIT 700\nPRESS START 2\nWAIT 90\nPRESS START 2\nWAIT 400\nPRESS START 2\nWAIT_RAM8 FFF600 8C\nWAIT_RAM8 FFF600 0C\nWAIT 30\nRELEASE\n'
    (out/'input.txt').write_text(script)
    env=os.environ.copy()
    env.update(SONIC_TRILOGY_STAGE=f'{args.stage:X}',SONIC_TRILOGY_ROM=str(args.donor.resolve()),
               SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
    loops=[]
    if args.stage in (0x1000,0x1001,0x1002):
        donor=args.donor.read_bytes();layout=(0x68C7E,0x68D74,0x68E40)[args.stage-0x1000]
        width=donor[layout]+1;height=donor[layout+1]+1
        loops=[(x*256,y*256) for y in range(height) for x in range(width)
               if donor[layout+2+y*width+x] in (0xB5,0x7F)]
    samples=[];sock=None;held=0;jump_until=0;next_jump=0;next_shot=0;shot_number=0;last_x=None;stuck=0
    progress_x=0;progress_frame=0;recover=0;started=False
    with (out/'run.log').open('w') as log:
        proc=subprocess.Popen([str(runtime/exe.name),str(args.rom.resolve()),'--no-launcher','--port',str(args.port),
                               '--target-fps','240','--max-frames',str(args.frames),'--widescreen',args.mode,
                               '--input-script',str(out/'input.txt')],cwd=out,env=env,stdout=log,stderr=log,
                              creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        try:
            deadline=time.monotonic()+150
            while proc.poll() is None:
                if time.monotonic()>deadline:raise RuntimeError('probe timed out')
                if sock is None:
                    try:sock=socket.create_connection(('127.0.0.1',args.port),timeout=1);sock.settimeout(5)
                    except OSError:time.sleep(.03);continue
                try:
                    s=command(sock,{'cmd':'sonic_state'});samples.append(s);f=s.get('internal_frame',0)
                    if started and s.get('game_mode')==4:
                        command(sock,{'cmd':'quit'});break
                    if s.get('game_mode')!=12 or s.get('routine')!=2:keys=0
                    else:
                        started=True
                        keys=8
                        if s['x']>progress_x+32:progress_x=s['x'];progress_frame=f
                        if s['x']==last_x:stuck+=1
                        else:stuck=0
                        last_x=s['x']
                        flat=s['angle']<=16 or s['angle']>=240
                        in_loop=any(x-128<=s['x']<x+384 and y-80<=s['y']<y+300 for x,y in loops)
                        if f>=next_jump and not(s['status']&2) and flat and not in_loop:jump_until=f+26;next_jump=f+30
                        if f<jump_until:keys|=0x20
                        if not recover and f-progress_frame>320 and not(s['status']&2):recover=f;progress_frame=f
                        if recover:
                            dt=f-recover
                            keys=4 if dt<45 else 0 if dt<60 else (0x22 if (dt//8)&1 else 2) if dt<125 else 8
                            jump_until=0
                            if dt>=220:recover=0;progress_frame=f
                    if keys!=held:command(sock,{'cmd':'set_input','keys':f'{keys:X}'});held=keys
                    if s.get('game_mode')==12 and f>=next_shot:
                        command(sock,{'cmd':'screenshot','path':str(out/f'route-{shot_number:03}.png')})
                        next_shot=f+180;shot_number+=1
                    if s.get('campaign_stage') not in (0,args.stage):
                        command(sock,{'cmd':'set_input','keys':'0'});command(sock,{'cmd':'quit'});break
                    if args.until_boss and s.get('campaign_stage')==0x1002 and s.get('camera_x',0)>=0x2960:
                        command(sock,{'cmd':'set_input','keys':'0'});command(sock,{'cmd':'run_frames','count':120})
                        command(sock,{'cmd':'screenshot','path':str(out/'boss-arrival.png')})
                        command(sock,{'cmd':'quit'});break
                except OSError:break # the socket can close just before process exit
                time.sleep(.01)
            proc.wait(timeout=15)
        finally:
            if sock:sock.close()
            if proc.poll() is None:proc.terminate();proc.wait(timeout=10)
    (out/'states.json').write_text(json.dumps(samples,indent=2))
    log=(out/'run.log').read_text(errors='replace')
    clear=f'Stage {args.stage:04X} cleared;' in log
    if args.until_boss:clear='Green Hill boss entered' in log
    maximum=max((s.get('x',0) for s in samples if s.get('game_mode')==12),default=0)
    deaths=sum(a.get('routine')!=6 and b.get('routine')==6 for a,b in zip(samples,samples[1:]))
    print(f'{"PASS" if clear else "INCOMPLETE"}: input-only {args.stage:04X}, furthest x={maximum:04X}, deaths={deaths}, captures={shot_number}')
    assert '0 unique true-miss addrs, 0 raw miss events' in log and 'FATAL' not in log


if __name__=='__main__':main()
