#!/usr/bin/env python3
"""Private-ROM quickstate probes with explicit scene positioning fixtures."""
import argparse
from pathlib import Path
from run_trilogy_special import Script,boot,execute,ram,word,objects

def main():
    ap=argparse.ArgumentParser()
    for k in ('exe','rom','sonic1','sonic2','out'):ap.add_argument('--'+k,type=Path,required=True)
    args=ap.parse_args();args.out=args.out.resolve();args.out.mkdir(parents=True,exist_ok=False)
    out=args.out/'save';out.mkdir();s=Script(out);boot(s)
    s.position(0x240,0x390);s.rings(99);s.emit('WAIT 60');s.shot('saved')
    state=out/'scene.state';s.emit(f'SAVE_STATE {state.as_posix()}\nWAIT 90');s.shot('future')
    s.emit(f'LOAD_STATE {state.as_posix()}\nWAIT 2');s.shot('restored');s.emit('WAIT 88');s.shot('replayed');s.emit('WAIT 1')
    log=execute(args,'save',s,stage=0x1000);assert '[SAVE] saved' in log and '[LOAD] loaded' in log
    before=ram(out,'saved');restored=ram(out,'restored')
    assert restored[0xF600]==12 and restored[0xB005]==2
    assert abs(word(restored,0xB010)-word(before,0xB010))<3
    # All host objects must continue from the same positions, not respawn.
    future=ram(out,'future');replay=ram(out,'replayed')
    for a in range(0xB0DE,0xCAE2,0x4A):
        if int.from_bytes(future[a:a+4],'big')==0x420000:
            assert replay[a:a+4]==future[a:a+4],hex(a)
            assert abs(word(replay,a+0x10)-word(future,a+0x10))<=3,hex(a)
            assert abs(word(replay,a+0x14)-word(future,a+0x14))<=3,hex(a)
    print('PASS same-process load and object replay',flush=True)
    cross=args.out/'fresh-process';cross.mkdir();s=Script(cross);boot(s)
    s.emit(f'LOAD_STATE {state.as_posix()}\nWAIT 2');s.shot('restored');s.emit('WAIT 88');s.shot('replayed');s.emit('WAIT 1')
    log=execute(args,'fresh-process',s,stage=0x2000);assert '[LOAD] loaded' in log
    fresh=ram(cross,'restored');assert fresh[0xF600]==12 and fresh[0xB005]==2
    assert fresh[0xB000:0xCAE2]==restored[0xB000:0xCAE2],'cross-process object state differs'
    assert ram(cross,'replayed')[0xB000:0xCAE2]==replay[0xB000:0xCAE2]
    print('PASS fresh process EHZ -> restored GHZ with identical player/object RAM',flush=True)
    corrupt=args.out/'damaged.state';b=bytearray(state.read_bytes());b[-10]^=1;corrupt.write_bytes(b)
    for name,path,packs in (('damaged',corrupt,3),('missing-donor',state,2)):
        out=args.out/name;out.mkdir();s=Script(out);boot(s);s.shot('before')
        s.emit(f'LOAD_STATE {path.as_posix()}\nWAIT 2');s.shot('after');s.emit('WAIT 1')
        log=execute(args,name,s,stage=0x2000,packs=packs)
        assert '[LOAD] incompatible or damaged host state' in log and '[LOAD] loaded' not in log
        r=ram(out,'after');assert r[0xF600]==12 and r[0xB005]==2 and word(r,0xB010)==word(ram(out,'before'),0xB010)
        print(f'PASS {name}: rejected before machine mutation',flush=True)
    out=args.out/'motobugs';out.mkdir();s=Script(out);boot(s)
    for n,(x,y) in enumerate(((0x240,0x390),(0xBE0,0x280),(0x13C0,0x260))):
        s.position(x,y);s.rings(99);s.emit('WAIT 90');s.shot(f'scene-{n}')
        for t in range(4):s.emit('WAIT 20');s.shot(f'scene-{n}-{t}')
    s.emit('WAIT 1');execute(args,'motobugs',s,stage=0x1000)
    for t in range(4):
        r=ram(out,f'scene-1-{t}');m=[a for a in objects(r,{0x420000}) if r[a+0x28]==12]
        assert len(m)==1 and not r[m[0]+4]&128,'buried initializing Motobug must remain invisible like S1'
    print('PASS buried Motobug hidden; captured live walking/turn sequences for visual review',flush=True)

if __name__=='__main__':main()
