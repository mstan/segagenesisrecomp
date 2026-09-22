"""Serial owner-ROM quickstate tests. Never touch the owner's executable/saves."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tomllib
import struct
import zlib

p=argparse.ArgumentParser(description=__doc__)
for name in ('exe','rom','amy','s3k','out'):
    p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args()
root=a.out.resolve(); root.mkdir(parents=True,exist_ok=False)
shutil.copy2(a.exe,root/a.exe.name)
shutil.copy2(a.exe.parent/'SDL2.dll',root/'SDL2.dll')
config=('slots=4\namy_enabled=1\ns3k_enabled=1\nsave_menu_enabled=1\n'
        'player1=sonic\nplayer2=tails\nplayer3=knuckles\nplayer4=amy\n'
        f'amy_path={a.amy.resolve().as_posix()}\ns3k_path={a.s3k.resolve().as_posix()}\n')
(root/'sonic2-party.ini').write_text(config)
env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software',GENESIS_STRICT_JSR_STACK='1')
results=[]
def run(name,lines,wide='off'):
    (root/(name+'.input')).write_text('\n'.join(lines+['EXIT'])+'\n')
    with (root/(name+'.log')).open('w') as log:
        result=subprocess.run([str(root/a.exe.name),str(a.rom.resolve()),'--no-launcher','--widescreen',wide,
            '--input-script',name+'.input','--max-frames','8500','--target-fps','1000'],cwd=root,env=env,
            stdout=log,stderr=log,timeout=180,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
    assert not tomllib.loads((root/'dispatch_misses.toml').read_text()).get('functions',{}).get('extra'),name
    text=(root/(name+'.log')).read_text()
    assert result.returncode==0 and '[input_script] EXIT' in text,(name,result.returncode,text[-2000:])
    results.append(name)
    return text
start=['WAIT 600','PRESS START 2','WAIT 40','PRESS START 2','WAIT 100',
       'ASSERT_RAM8 FFF600 24','PRESS START 2','WAIT 220','ASSERT_RAM8 FFF600 0C']
for label,wide in [('native','off'),('wide','16:9')]:
    state=f'{label}.state'
    text=run(label,start+[f'SAVE_STATE {state}','WAIT 8',
        'HOLD RIGHT','WAIT 75','RELEASE','WRITE_RAM8 FFFE12 63',f'LOAD_STATE {state}',
        f'DUMP_RAM {label}-loaded.bin','WAIT 1','HOLD RIGHT','WAIT 90','RELEASE',f'DUMP_RAM {label}-branch1.bin',
        'WAIT 1',f'LOAD_STATE {state}','WAIT 1','HOLD RIGHT','WAIT 90','RELEASE',f'DUMP_RAM {label}-branch2.bin','WAIT 1'],wide)
    assert '[SAVE] saved' in text and text.count('[LOAD] loaded')==2,text[-2000:]
    blob=(root/state).read_bytes(); sections=struct.unpack_from('<6I',blob,88)
    saved=blob[112+sections[0]+sections[1]:112+sections[0]+sections[1]+65536]
    (root/f'{label}-saved.bin').write_bytes(saved)
    assert saved==(root/f'{label}-loaded.bin').read_bytes(),f'{label} immediate restore'
    assert (root/f'{label}-branch1.bin').read_bytes()==(root/f'{label}-branch2.bin').read_bytes(),f'{label} repeat branch'
    campaign=(root/'sonic2-campaign.srm').read_bytes()
    text=run(label+'-cold',['WAIT 2',f'LOAD_STATE {state}',f'DUMP_RAM {label}-cold.bin',
        'WAIT 1','HOLD RIGHT','WAIT 90','RELEASE',f'DUMP_RAM {label}-cold-branch.bin','WAIT 1'],wide)
    assert '[LOAD] loaded' in text,text[-2000:]
    assert (root/f'{label}-saved.bin').read_bytes()==(root/f'{label}-cold.bin').read_bytes(),f'{label} cold restore'
    assert (root/f'{label}-branch1.bin').read_bytes()==(root/f'{label}-cold-branch.bin').read_bytes(),f'{label} cold continuation'
    assert campaign==(root/'sonic2-campaign.srm').read_bytes(),'Quickload wrote campaign SRAM'

good=(root/'native.state').read_bytes()
bad=bytearray(good); bad[-200]^=128
for label,data in [('corrupt',bad),('short',good[:-1]),('trailing',good+b'bad'),('legacy',b'GROWNS2\0'+good[8:]),('empty',b'')]:
    (root/(label+'.state')).write_bytes(data)
    text=run(label,['WAIT 2','WRITE_RAM8 FFFE12 63',f'LOAD_STATE {label}.state',
                    'DUMP_RAM rejected.bin','WAIT 1'])
    assert '[LOAD] rejected' in text and (root/'rejected.bin').read_bytes()[0xfe12]==0x63,label
for label,modified in [('roster',config.replace('player3=knuckles','player3=amy').replace('player4=amy','player4=knuckles')),
                       ('path',config+'campaign_path=another.srm\n'),('missing-donor',config.replace(a.s3k.resolve().as_posix(),'missing.bin'))]:
    (root/'sonic2-party.ini').write_text(modified)
    text=run(label,['WAIT 2','WRITE_RAM8 FFFE12 63','LOAD_STATE native.state','DUMP_RAM rejected.bin','WAIT 1'])
    assert '[LOAD] rejected' in text and (root/'rejected.bin').read_bytes()[0xfe12]==0x63,label
(root/'sonic2-party.ini').write_text(config)
text=run('special',start+['WRITE_RAM8 FFF600 10','WAIT 500','ASSERT_RAM8 FFF600 10',
    'SAVE_STATE special.state','WAIT 8'])
assert '[SAVE] saved' in text
text=run('special-cold',['WAIT 2','LOAD_STATE special.state','DUMP_RAM special-loaded.bin','WAIT 1',
    'WAIT_RAM8 FFF600 0C','WAIT 160','DUMP_RAM return.bin','WAIT 1'])
assert '[LOAD] loaded' in text,text[-2000:]
blob=(root/'special.state').read_bytes(); sections=struct.unpack_from('<6I',blob,88)
saved=blob[112+sections[0]+sections[1]:112+sections[0]+sections[1]+65536]
assert saved==(root/'special-loaded.bin').read_bytes()
ram=(root/'return.bin').read_bytes()
assert [ram[o] for o in (0xb000,0xb040,0xcfc0,0xcf80)]==[1,2,1,1],'Special-stage return roster'

# Restoring a snapshot rewinds only its active campaign slot. A different
# slot changed between processes remains intact through the next autosave.
campaign=root/'sonic2-campaign.srm'
b=bytearray(campaign.read_bytes()); b[120:125]=bytes((1,14,7,3,0)); b[60:64]=bytes(4)
struct.pack_into('>I',b,60,zlib.crc32(b)); campaign.write_bytes(b)
results_object=[f'WRITE_RAM32 {0xFFB800+i:X} 0' for i in range(0,64,4)]+[
    'WRITE_RAM8 FFB800 3A','WRITE_RAM8 FFB824 10']
text=run('campaign-rewind',['WAIT 2','LOAD_STATE native.state','WAIT 20']+results_object+[
    'WAIT 400','ASSERT_RAM16 FFFE10 0001'])
assert '[LOAD] loaded' in text
b=campaign.read_bytes(); assert tuple(b[64:67])==(1,1,0) and tuple(b[120:123])==(1,14,7)

# A state can rebuild Sonic/Tails extra-player art caches on a cold load;
# No Save and paused native gameplay must work without a campaign session.
for label,settings,pause in [('native-extras',config.replace('player1=sonic','player1=amy').replace('player2=tails','player2=knuckles').replace('player3=knuckles','player3=sonic').replace('player4=amy','player4=tails'),False),
                             ('paused',config,True),
                             ('vanilla',config.replace('slots=4','slots=2').replace('player3=knuckles','player3=none').replace('player4=amy','player4=none').replace('save_menu_enabled=1','save_menu_enabled=0'),False)]:
    (root/'sonic2-party.ini').write_text(settings)
    boot=start if label!='vanilla' else ['WAIT 600','PRESS START 2','WAIT 40','PRESS START 2','WAIT 220','ASSERT_RAM8 FFF600 0C']
    if pause: boot+=['PRESS START 2','WAIT 12','ASSERT_RAM16 FFF63A 0001']
    text=run(label+'-save',boot+[f'SAVE_STATE {label}.state','WAIT 8'])
    assert '[SAVE] saved' in text,label
    text=run(label+'-restore',['WAIT 2',f'LOAD_STATE {label}.state','WAIT 30',
        'ASSERT_RAM8 FFF600 0C']+(['ASSERT_RAM16 FFF63A 0001','PRESS START 2','WAIT 20','ASSERT_RAM16 FFF63A 0000'] if pause else []))
    assert '[LOAD] loaded' in text,label
# Native split-screen keeps its original two competitors and selected roles.
(root/'sonic2-party.ini').write_text(config.replace('player1=sonic','player1=amy').replace('player4=amy','player4=sonic'))
vs_start=['WAIT 600','PRESS START 2','WAIT 40','PRESS DOWN 2','WAIT 8',
          'PRESS START 2','WAIT 220','ASSERT_RAM8 FFF600 1C','PRESS START 2',
          'WAIT 220','ASSERT_RAM8 FFF600 0C','ASSERT_RAM16 FFFFD8 0001']
text=run('versus-save',vs_start+['SAVE_STATE versus.state','WAIT 8',
    'LOAD_STATE versus.state','WAIT 1','HOLD RIGHT','WAIT 90','RELEASE',
    'DUMP_RAM versus-branch.bin','WAIT 1'])
assert '[SAVE] saved' in text and '[LOAD] loaded' in text
text=run('versus-cold',['WAIT 2','LOAD_STATE versus.state','WAIT 1',
    'HOLD RIGHT','WAIT 90','RELEASE','DUMP_RAM versus-cold.bin','WAIT 1'])
assert '[LOAD] loaded' in text
assert (root/'versus-branch.bin').read_bytes()==(root/'versus-cold.bin').read_bytes(),'VS fresh-process continuation'
print(f'PASS {len(results)} quickstate fixtures, including fresh-process continuation and rejected files')
