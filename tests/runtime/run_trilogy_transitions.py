#!/usr/bin/env python3
"""Private-ROM component test for the imported campaign's exit chain.

Positions/velocities are fixtures. Boss hits still go through native collision;
no boss HP, campaign progress, or completion flags are written by this test.
This is not an input-only playthrough.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import socket
import time
from run_trilogy_campaign import records
from run_sonic1_custom_video import command


def main():
    ap=argparse.ArgumentParser()
    for name in ('exe','rom','sonic1','sonic2','out'):
        ap.add_argument('--'+name,type=Path,required=True)
    args=ap.parse_args()
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    runtime=out/'runtime';runtime.mkdir()
    exe=args.exe.resolve();rom=args.rom.resolve()
    shutil.copy2(exe,runtime/exe.name)
    shutil.copy2(exe.parent/'SDL2.dll',runtime/'SDL2.dll')
    (runtime/'settings.ini').write_text('[trilogy]\nenabled=1\n')
    (runtime/'debug.ini').write_text('[debug]\nenabled=1\nport=4395\n')
    script=[]
    def emit(text):script.append(text+'\n')
    def shot(name):emit(f'SCREENSHOT {(out/(name+".png")).as_posix()}\nDUMP_RAM {(out/(name+".ram.bin")).as_posix()}\nWAIT 1')
    def word(a,v):emit(f'WRITE_RAM16 FF{a:04X} {v&65535:X}')
    def byte(a,v):emit(f'WRITE_RAM8 FF{a:04X} {v&255:X}')
    def position(x,y,rolling=False,camera=True):
        word(0xB010,x);word(0xB014,y);word(0xB018,0);word(0xB01A,0x300 if rolling else 0);word(0xB01C,0)
        byte(0xB005,2);byte(0xB02A,6 if rolling else 2);byte(0xB020,2 if rolling else 0)
        byte(0xB01E,14 if rolling else 19);byte(0xB01F,7 if rolling else 9);byte(0xB02E,0)
        if camera:
            cy=min(1024,max(0,y-96));word(0xEE78,max(0,x-160));word(0xEE7C,cy);word(0xEE1A,max(768,cy))
    emit('WAIT 700\nPRESS START 2\nWAIT 90\nPRESS START 2\nWAIT 400\nPRESS START 2\nWAIT_RAM8 FFF600 8C\nWAIT 100')
    shot('title-ghz1')
    emit('WAIT_RAM8 FFF600 0C\nWAIT 60')
    position(0x2500,0x490);word(0xFE20,10)
    emit('HOLD RIGHT\nWAIT 120\nRELEASE\nWAIT 1300');shot('after-ghz1')
    position(0x1F00,0x390);word(0xFE20,10)
    emit('HOLD RIGHT\nWAIT 120\nRELEASE\nWAIT 1300');shot('after-ghz2')
    position(0x2A00,0x390);word(0xFE20,99)
    emit('WAIT 350');shot('boss-entered')
    for i in range(16):
        position(0x2A00,0x2F0,rolling=True,camera=False)
        emit('WAIT 80');shot(f'boss-{i:02}')
    emit('WAIT 350')
    position(0x2B60,0x340);emit('WAIT 90');shot('capsule')
    emit('WAIT 1800');shot('after-ghz3')
    position(0x29E0,0x2C0);word(0xFE20,10)
    emit('HOLD RIGHT\nWAIT 120\nRELEASE\nWAIT 1600');shot('after-ehz')
    emit('WAIT 900');shot('native-aiz')
    emit('WAIT 30')
    (out/'input.txt').write_text(''.join(script))
    env=os.environ.copy()
    for k in ('SONIC_TRILOGY_STAGE','SONIC_TRILOGY_ROM'):env.pop(k,None)
    env.update(SONIC_TRILOGY_S1_ROM=str(args.sonic1.resolve()),SONIC_TRILOGY_S2_ROM=str(args.sonic2.resolve()),
               SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
    native_audio=None;restored=False;sock=None
    with (out/'run.log').open('w') as log:
        proc=subprocess.Popen([str(runtime/exe.name),str(rom),'--no-launcher','--target-fps','1000','--port','4395',
                        '--widescreen','off','--max-frames','12000','--input-script',str(out/'input.txt')],
                       cwd=out,env=env,stdout=log,stderr=log,
                       creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        try:
            deadline=time.monotonic()+180
            while proc.poll() is None:
                if time.monotonic()>deadline:raise RuntimeError('transition probe timed out')
                if sock is None:
                    try:sock=socket.create_connection(('127.0.0.1',4395),timeout=1);sock.settimeout(5)
                    except OSError:time.sleep(.03);continue
                try:
                    state=command(sock,{'cmd':'sonic_state'})
                    if state.get('game_mode')==4 or (state.get('game_mode')==12 and state.get('campaign_stage')==0):
                        z=bytes.fromhex(command(sock,{'cmd':'read_z80_ram','addr':0,'len':8192})['data'])
                        if z[0x1300:0x1302]==b'\x18\x16':
                            tables=b''.join(z[a:a+n] for a,n in ((0xB65,51),(0x1618,102),(0x1387,78),(0xD6,18),(0xC0D,2)))
                            if state.get('game_mode')==4:native_audio=tables
                            elif native_audio is not None:
                                assert tables==native_audio,'donor audio tables leaked into native AIZ'
                                assert z[0x1C3E]<0x80,'donor song still active in AIZ'
                                restored=True
                except (OSError,RuntimeError):break
                time.sleep(.04)
            proc.wait(timeout=15);assert proc.returncode==0
        finally:
            if sock:sock.close()
            if proc.poll() is None:proc.terminate();proc.wait(timeout=10)
    assert native_audio is not None and restored,'native audio restoration was not observed'
    log=(out/'run.log').read_text(errors='replace')
    assert '0 unique true-miss addrs, 0 raw miss events' in log and 'FATAL' not in log
    assert '[Trilogy] Green Hill boss defeated' in log,'boss defeat missing'
    for before,after in ((0x1000,0x1001),(0x1001,0x1002),(0x1002,0x2000),(0x2000,0x3000)):
        assert f'Stage {before:04X} cleared; next {after:04X}' in log,hex(before)
        assert f'Starting stage {after:04X} slot 0' in log,hex(after)
    for name in ('after-ghz1','after-ghz2','after-ghz3','after-ehz'):
        ram=(out/(name+'.ram.bin')).read_bytes()
        assert ram[0xF600]==12 and ram[0xB005]==2,(name,'not in live gameplay')
    rec=records((runtime/(rom.stem+'.srm')).read_bytes())
    assert rec[b'S1EX'][1][3]&7==7 and rec[b'S2EX'][1][3]&1==1,'chapter clears not saved'
    print('PASS: native results, eight-hit GHZ boss, capsule and GHZ1 -> GHZ2 -> GHZ3 -> EHZ1 -> AIZ1; chapter clears saved')


if __name__=='__main__':main()
