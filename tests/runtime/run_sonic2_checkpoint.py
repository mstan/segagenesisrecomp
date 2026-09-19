"""Checkpoint-created SS return and companion recovery regression.

RAM fixtures place the party at EHZ1's real second starpost and supply 50
rings. Native starpost activation, jumping into its stars, special-stage
failure and checkpoint return all execute unchanged. Never patches ROM.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tomllib

p = argparse.ArgumentParser(description=__doc__)
for name in ('exe', 'rom', 'amy', 's3k', 'out'):
    p.add_argument('--'+name, type=Path, required=True)
p.add_argument('--native-extras', action='store_true')
a = p.parse_args()
out = a.out.resolve()
out.mkdir(parents=True, exist_ok=False)
exe = out/a.exe.name
shutil.copy2(a.exe, exe)
shutil.copy2(a.exe.parent/'SDL2.dll', out/'SDL2.dll')
roster = ['amy','knuckles','sonic','tails'] if a.native_extras else ['sonic','tails','knuckles','amy']
(out/'sonic2-party.ini').write_text('slots=4\namy_enabled=1\ns3k_enabled=1\n' +
    f'amy_path={a.amy.resolve().as_posix()}\ns3k_path={a.s3k.resolve().as_posix()}\n' +
    ''.join(f'player{i+1}={name}\n' for i,name in enumerate(roster)))
script = ['WAIT 600','PRESS START 2','WAIT 40','PRESS START 2','WAIT 220','ASSERT_RAM8 FFF600 0C']
def write(size, at, value):
    script.append(f'WRITE_RAM{size} {0xff0000+at:X} {value & ((1<<size)-1):X}')
def capture(name):
    # Capture requests are serviced at frame end, after this script tick.
    # Do not let the next fixture's RAM writes contaminate the capture.
    script.extend([f'SCREENSHOT {name}.png',f'DUMP_RAM {name}.bin','WAIT 1'])

capture('before')
for index,at in enumerate((0xb000,0xb040,0xcfc0,0xcf80)):
    write(32,at+8,(0x18c0-index*24)<<16)
    write(32,at+12,0x2ac<<16)
    write(32,at+16,0)
    write(16,at+20,0)
    write(8,at+0x22,2)
write(16,0xee00,0x1820)
write(16,0xee04,0x24c)
write(16,0xfe20,50)
script.extend(['WAIT_RAM8 FFFE30 2','WAIT 140'])
capture('checkpoint')
script.extend(['PRESS C 24','WAIT_RAM8 FFF600 10','WAIT 500'])
capture('halfpipe')
script.extend(['WAIT_RAM8 FFF600 0C','WAIT 60'])
capture('return-early')
script.append('WAIT 180')
capture('return')
# Reproduce the owner's empty-space contact at the captured coordinates.
# Start airborne, allowing the game's floor queries/gravity to settle him.
write(32,0xb008,6347<<16)
write(32,0xcfc8,6309<<16)
write(32,0xcfcc,652<<16)
write(32,0xcfd0,0)
write(16,0xcfd4,0)
write(8,0xcfe2,2)
script.append('WAIT 90')
capture('settled')
# Checkpoint recovery must inherit P1's collision plane for every role.
for at in (0xb040,0xcfc0,0xcf80): write(8,at+0x24,6)
script.append('WAIT 2')
capture('checkpoint-recovery')
# No checkpoint is also important: native Obj01_Init would overwrite a
# pre-seeded secondary plane unless inheritance happens at its init tail.
write(8,0xfe30,0)
write(16,0xb03e,0x0e0f)
write(16,0xb002,0x8780)
for at in (0xb040,0xcfc0,0xcf80): write(8,at+0x24,6)
script.append('WAIT 2')
capture('secondary-recovery')
script.append('EXIT')
(out/'checkpoint.input').write_text('\n'.join(script)+'\n')
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
           SDL_RENDER_DRIVER='software', GENESIS_STRICT_JSR_STACK='1')
with (out/'run.log').open('w') as log:
    result = subprocess.run([str(exe),str(a.rom.resolve()),'--no-launcher','--widescreen','off',
        '--input-script','checkpoint.input','--max-frames','10000','--target-fps','1000'],
        cwd=out,env=env,stdout=log,stderr=log,timeout=180,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
assert not tomllib.loads((out/'dispatch_misses.toml').read_text()).get('functions',{}).get('extra')
assert result.returncode==0, f'exit {result.returncode}: {out / "run.log"}'
ram = lambda name: (out/(name+'.bin')).read_bytes()
word = lambda r, at: int.from_bytes(r[at:at+2],'big')
assert ram('checkpoint')[0xfe30]==2 and word(ram('checkpoint'),0xfe3e)==0x0c0d
assert ram('halfpipe')[0xf600]==16
for name in ('return-early','return','settled','checkpoint-recovery','secondary-recovery'):
    r=ram(name)
    assert r[0xfe12]==ram('before')[0xfe12], f'{name}: companion spent P1 life'
    for at in (0xb040,0xcfc0,0xcf80):
        assert r[at+0x24]==2, f'{name}: actor {at:X} not active'
        assert word(r,at+0x3e)==word(r,0xb03e), f'{name}: actor {at:X} has invalid collision plane {word(r,at+0x3e):04X}'
        assert word(r,at+2)&0x8000==word(r,0xb002)&0x8000, f'{name}: priority inheritance'
    if name=='return-early':
        assert word(r,0xcff0)==word(r,0xcfb0)==0, 'ordinary return spawn is blinking'
    if name.endswith('recovery'):
        assert all(0<word(r,at+0x30)<=120 for at in (0xb040,0xcfc0,0xcf80)), 'recovery protection lost'
assert 680<=word(ram('settled'),0xcfcc)<=690, 'actor retained the ghost floor above the visible terrain'
print(f'PASS real checkpoint SS return, grounded contact and primary/secondary recovery: {roster}')
