"""Own-ROM fallback quickstate failure atomicity; private files, serial/muted.

Uses the complete architectural fingerprint to compare a rejected load with
an otherwise identical control. No older-build state compatibility is tested.
"""
import argparse,json,os,re,shutil,subprocess
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
for name in ('exe','rom','out'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
exe=out/a.exe.name;shutil.copy2(a.exe,exe);shutil.copy2(a.exe.parent/'SDL2.dll',out/'SDL2.dll')
env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software',GENESIS_RUN_DONE='1')
def run(name,script):
    (out/(name+'.input')).write_text(script+'\n')
    with (out/(name+'.log')).open('w') as log:
        result=subprocess.run([str(exe),str(a.rom.resolve()),'--no-launcher','--target-fps','1000',
            '--max-frames','30','--hash-frames','1','--input-script',name+'.input'],cwd=out,env=env,
            stdout=log,stderr=log,timeout=30,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    text=(out/(name+'.log')).read_text(errors='replace')
    assert result.returncode==0,(name,result.returncode,text[-1000:])
    assert re.search(r'extra\s*=\s*\[\s*\]',(out/'dispatch_misses.toml').read_text()),name
    state=re.search(r'RUN_DONE frames=30 state=(\w+)',text);assert state,(name,text[-1000:])
    return text,state[1],re.findall(r'^\[FBHASH\].*$',text,re.M)

text,_,_=run('save','WAIT 20\nSAVE_STATE valid.state\nWAIT 2')
assert '[SAVE] saved' in text
blob=(out/'valid.state').read_bytes();assert blob[:8]==b'GROWNS2\0','This fixture targets the fallback serializer'
prefix='WAIT 2\nWRITE_RAM8 FF0100 63\n'
_,control,control_pixels=run('control',prefix+'DUMP_RAM control.bin')
# Prefixes exercise failures before and after the in-place CPU/RAM/machine
# readers have changed live state, as well as rejection after all sections.
cases={'short-header':blob[:4],'short-cpu':blob[:40],
       'short-ram':blob[:8+93+32768],'short-machine':blob[:8+93+65536+2000],
       'short-tail':blob[:-1],'trailing':blob+b'bad','bad-magic':b'INVALID!'+blob[8:]}
reports=[]
for name,data in cases.items():
    (out/(name+'.state')).write_bytes(data)
    text,state,pixels=run(name,prefix+f'LOAD_STATE {name}.state\nDUMP_RAM {name}.bin')
    rejected=('[LOAD] failed/truncated' in text or 'is not an own-backend save' in text)
    marker=(out/(name+'.bin')).read_bytes()[0x100]
    report=dict(case=name,rejected=rejected,marker=marker,state=state,control=control,pixels_equal=pixels==control_pixels)
    reports.append(report);(out/'report.json').write_text(json.dumps(reports,indent=2)+'\n')
    assert rejected and marker==99 and state==control and pixels==control_pixels,report
print('PASS seven prefix/trailing/bad-magic rejections: unchanged RAM, complete guest/audio state and all frame pixels')
