#!/usr/bin/env python3
"""Owner-ROM audio smoke test. Cue writes are fixtures, not game progression.

Captures each donor score and checks the real Z80 driver bank, drum bank,
unchanged SFX envelopes, 1-up return, and non-silent unclipped output.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import time
import wave
import numpy as np
from run_sonic1_custom_video import command


def main():
    ap=argparse.ArgumentParser()
    for n in ('exe','rom','donor','out'):ap.add_argument('--'+n,type=Path,required=True)
    ap.add_argument('--stage',type=lambda s:int(s,16),default=0x1000)
    ap.add_argument('--port',type=int,default=4393)
    args=ap.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    exe=args.exe.resolve();runtime=out/'runtime';runtime.mkdir()
    shutil.copy2(exe,runtime/exe.name);shutil.copy2(exe.parent/'SDL2.dll',runtime/'SDL2.dll')
    (runtime/'debug.ini').write_text(f'[debug]\nenabled=1\nport={args.port}\n')
    (out/'input.txt').write_text('WAIT 700\nPRESS START 2\nWAIT 90\nPRESS START 2\nWAIT 400\nPRESS START 2\nWAIT_RAM8 FFF600 8C\nWAIT_RAM8 FFF600 0C\nWAIT 10\nRELEASE\n')
    env=os.environ.copy();env.update(SONIC_TRILOGY_STAGE=f'{args.stage:X}',SONIC_TRILOGY_ROM=str(args.donor.resolve()),
        SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
    bank=0xA8 if args.stage==0x2000 else 0xA0;sock=None;checks=[]
    with (out/'run.log').open('w') as log:
        proc=subprocess.Popen([str(runtime/exe.name),str(args.rom.resolve()),'--no-launcher','--port',str(args.port),
            '--target-fps','480','--max-frames','20000','--widescreen','off','--input-script',str(out/'input.txt')],
            cwd=out,env=env,stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        try:
            deadline=time.monotonic()+100
            while sock is None:
                if proc.poll() is not None or time.monotonic()>deadline:raise RuntimeError('no debug connection')
                try:sock=socket.create_connection(('127.0.0.1',args.port),timeout=1);sock.settimeout(10)
                except OSError:time.sleep(.03)
            def zram(a,n):return bytes.fromhex(command(sock,{'cmd':'read_z80_ram','addr':a,'len':n})['data'])
            def wait_frames(count):
                start=command(sock,{'cmd':'sonic_state'})['internal_frame']
                while command(sock,{'cmd':'sonic_state'})['internal_frame']-start<count:
                    if time.monotonic()>deadline:raise RuntimeError('audio probe timed out')
                    time.sleep(.02)
            native_envelopes=None
            while True:
                s=command(sock,{'cmd':'sonic_state'})
                if s.get('game_mode')==4:native_envelopes=zram(0x1387,78)
                if s.get('game_mode')==12 and s.get('campaign_stage')==args.stage and s.get('routine')==2:break
                if time.monotonic()>deadline:raise RuntimeError('level did not start')
                time.sleep(.02)
            assert zram(0x1C3E,1)[0]==bank,('wrong stage bank',zram(0x1C3E,1).hex())
            envelopes=zram(0x1387,78)
            assert native_envelopes is not None,'native title envelope baseline missing'
            for effect in (3,13,14,15,17,21,22,23,29):
                off=(effect-1)*2
                assert envelopes[off:off+2]==native_envelopes[off:off+2],('S3 sound effect envelope changed',effect)
            # Each cue is routed through the real driver; a fixture writes its queue.
            for index,cue,name,frames in ((0,2,'stage',2400),(3,0x2A,'one-up',480),(1,0x19,'boss',900),
                    (2,0x29,'results',600),(4,0x2C,'invincible',900),(5,0x27,'game-over',600),(6,0x31,'drowning',600)):
                path=out/(name+'.wav')
                command(sock,{'cmd':'audio_wav','action':'start','path':str(path)})
                command(sock,{'cmd':'write_memory','addr':'A01C0A','hex':f'{cue:02X}'})
                wait_frames(12)
                current=zram(0x1C3E,1)[0];assert current==bank+index,(name,'bank',hex(current))
                wait_frames(frames)
                state=command(sock,{'cmd':'z80_state','include_ram':1})
                (out/(name+'.z80.json')).write_text(json.dumps(state,indent=2))
                if name=='one-up':assert zram(0x1C3E,1)[0]==bank,'1-up did not resume stage music'
                assert zram(0x1387,78)==envelopes,'driver overwrote envelope pointers'
                command(sock,{'cmd':'audio_wav','action':'stop'})
                with wave.open(str(path)) as w:
                    assert w.getnframes()/w.getframerate()>=frames/60*.95,'capture skipped audio frames'
                    data=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').astype(float)
                rms=float(np.sqrt(np.mean(data*data)));peak=float(np.max(np.abs(data)))
                assert rms>25 and peak<32767,(name,'silent/clipped',rms,peak)
                checks.append({'cue':name,'bank':current,'rms':rms,'peak':peak})
            command(sock,{'cmd':'quit'});proc.wait(timeout=15)
        finally:
            if sock:sock.close()
            if proc.poll() is None:proc.terminate();proc.wait(timeout=10)
    (out/'checks.json').write_text(json.dumps(checks,indent=2))
    text=(out/'run.log').read_text(errors='replace')
    assert '0 unique true-miss addrs, 0 raw miss events' in text and 'FATAL' not in text
    print(f'PASS: {args.stage:04X} seven donor music cues, original banks, non-silent audio, 1-up return')


if __name__=='__main__':main()
