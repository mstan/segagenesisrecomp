"""Serial native campaign fixtures. Private ROMs/art and saves stay in --out.

Menu actions use controller input. Progression fixtures place a native results
object, enter native SS/ending dispatchers, and exercise their committed hooks;
they do not assert that a whole campaign was played by controller.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tomllib
import zlib

p=argparse.ArgumentParser(description=__doc__)
for name in ("exe","rom","amy","s3k","out"):
    p.add_argument(f"--{name}",type=Path,required=True)
a=p.parse_args()
root=a.out.resolve(); root.mkdir(parents=True,exist_ok=False)
env=dict(os.environ,SDL_VIDEODRIVER="dummy",SDL_AUDIODRIVER="dummy",
         SDL_RENDER_DRIVER="software",GENESIS_STRICT_JSR_STACK="1")
identity=bytes.fromhex("193bc4064ce0daf27ea9e908ed246d87ec576cc294833badebb590b6ad8e8f6b")
assert hashlib.sha256(a.rom.read_bytes()).digest()==identity
TITLE=["WAIT 600","PRESS START 2","WAIT 40"]
MENU=TITLE+["PRESS START 2","WAIT 100","ASSERT_RAM8 FFF600 24"]
START=["PRESS START 2","WAIT 220","ASSERT_RAM8 FFF600 0C"]
results=[]

def setup(name,roster=("sonic","tails","none","none"),enabled=True,donor=True):
    out=root/name; out.mkdir()
    shutil.copy2(a.exe,out/a.exe.name)
    shutil.copy2(a.exe.parent/"SDL2.dll",out/"SDL2.dll")
    (out/"sonic2-party.ini").write_text(f"slots={4 if roster[2]!='none' else 2}\namy_enabled=1\ns3k_enabled=1\nsave_menu_enabled={int(enabled)}\n"
        f"amy_path={a.amy.resolve().as_posix()}\ns3k_path={a.s3k.resolve().as_posix() if donor else 'missing-donor.bin'}\n"+
        "".join(f"player{i+1}={c}\n" for i,c in enumerate(roster)))
    return out

def run(out,name,lines,max_frames=8500):
    (out/f"{name}.input").write_text("\n".join(lines+["EXIT"])+"\n")
    with (out/f"{name}.log").open("w") as log:
        result=subprocess.run([str(out/a.exe.name),str(a.rom.resolve()),"--no-launcher",
            "--widescreen","off","--input-script",f"{name}.input","--max-frames",str(max_frames),
            "--target-fps","1000"],cwd=out,env=env,stdout=log,stderr=log,timeout=180,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0)
    misses=tomllib.loads((out/"dispatch_misses.toml").read_text()).get("functions",{}).get("extra")
    assert not misses,(name,misses)
    assert result.returncode==0,(name,result.returncode,out/f"{name}.log")
    assert "[input_script] EXIT" in (out/f"{name}.log").read_text(), f"{name} did not finish its input script"
    results.append(name)

def read_save(out):
    b=(out/"sonic2-campaign.sav").read_bytes(); assert len(b)==128
    assert b[:8]==b"S2SAVE\r\n" and b[24:56]==identity
    c=bytearray(b); c[60:64]=bytes(4)
    assert struct.unpack_from(">I",b,60)[0]==zlib.crc32(c)
    return [tuple(b[64+8*i:67+8*i]) for i in range(8)]

def seed(out,slot=0,state=1,stage=0,emeralds=0):
    b=bytearray(128); b[:8]=b"S2SAVE\r\n"
    struct.pack_into(">IIII",b,8,1,128,8,1); b[24:56]=identity
    b[64+slot*8:67+slot*8]=bytes((state,stage,emeralds))
    struct.pack_into(">I",b,60,zlib.crc32(b)); (out/"sonic2-campaign.sav").write_bytes(b)

def capture(name): return [f"SCREENSHOT {name}.png",f"DUMP_RAM {name}.bin","WAIT 1"]
def object(at,identity,routine):
    return [f"WRITE_RAM32 {0xFF0000+at+n:X} 0" for n in range(0,64,4)]+[
        f"WRITE_RAM8 {0xFF0000+at:X} {identity:X}",f"WRITE_RAM8 {0xFF0024+at:X} {routine:X}"]

out=setup("new-resume")
run(out,"new",MENU+capture("menu")+START+capture("new-game"))
assert read_save(out)[0]==(1,0,0)
run(out,"act-transition",MENU+START+object(0xB800,0x3A,0x10)+[
    "WAIT 400","ASSERT_RAM16 FFFE10 0001"]+capture("act-two"))
assert read_save(out)[0]==(1,1,0)
run(out,"reload-act-two",MENU+START+["ASSERT_RAM16 FFFE10 0001"]+capture("reloaded"))

out=setup("no-save")
run(out,"no-save",MENU+["PRESS LEFT 2","WAIT 20"]+START+
    object(0xB800,0x3A,0x10)+["WAIT 400","ASSERT_RAM16 FFFE10 0001"])
assert not (out/"sonic2-campaign.sav").exists()
out=setup("back")
run(out,"back",MENU+["PRESS B 2","WAIT 100","ASSERT_RAM8 FFF600 04"])
assert not (out/"sonic2-campaign.sav").exists()

for name,enabled,donor in (("disabled",False,True),("missing-donor",True,False)):
    out=setup(name,enabled=enabled,donor=donor)
    run(out,name,TITLE+START)
    assert not (out/"sonic2-campaign.sav").exists()

out=setup("emerald-resume",("amy","knuckles","sonic","tails"))
seed(out,stage=1,emeralds=0x15)
run(out,"emerald-resume",MENU+START+["ASSERT_RAM16 FFFE10 0001","ASSERT_RAM8 FFFFB1 3",
    "ASSERT_RAM8 FFFFB2 FF","ASSERT_RAM8 FFFFB3 0","ASSERT_RAM8 FFFFB4 FF","ASSERT_RAM8 FFFFB6 FF"]+capture("party"))
ram=(out/"party.bin").read_bytes()
assert [ram[o] for o in (0xB000,0xB040,0xCFC0,0xCF80)]==[1,1,1,2]
assert ram[0xFE12]==3 and ram[0xFE18]==0
run(out,"party-emerald-return",MENU+START+["WRITE_RAM8 FFF600 10","WAIT 500",
    "ASSERT_RAM8 FFF600 10"]+capture("party-halfpipe")+object(0xB800,0x59,8)+[
    "WAIT_RAM8 FFF600 0C","WAIT 160"]+capture("party-return"))
assert read_save(out)[0]==(1,1,0x17)
ram=(out/"party-return.bin").read_bytes()
assert [ram[o] for o in (0xB000,0xB040,0xCFC0,0xCF80)]==[1,1,1,2]

out=setup("late-transitions")
seed(out,stage=16)
run(out,"metropolis-three",MENU+START+["ASSERT_RAM16 FFFE10 0500"]+
    object(0xB800,0x3A,0x10)+["WAIT 400","ASSERT_RAM16 FFFE10 1000"])
assert read_save(out)[0]==(1,17,0)
run(out,"sky-chase-transition",MENU+START+[
    "WRITE_RAM8 FFEEDF 6","WRITE_RAM16 FFEECA 1600",
    "WRITE_RAM16 FFEE00 1500","WRITE_RAM32 FFB008 15680000","WAIT 400",
    "ASSERT_RAM16 FFFE10 0600"])
assert read_save(out)[0]==(1,18,0)
run(out,"wing-fortress-transition",MENU+START+object(0xB800,0xB2,6)+[
    "WRITE_RAM8 FFB825 10","WRITE_RAM16 FFB82A 09C0","WAIT 400",
    "ASSERT_RAM16 FFFE10 0E00"])
assert read_save(out)[0]==(1,19,0)

out=setup("complete")
seed(out,stage=19,emeralds=0x15)
run(out,"completion",MENU+START+["ASSERT_RAM16 FFFE10 0E00","WRITE_RAM8 FFF600 20","WAIT 180"])
assert read_save(out)[0]==(2,19,0x15)
run(out,"completed-replay",MENU+["PRESS UP 2","WAIT 20"]+START+[
    "ASSERT_RAM16 FFFE10 0000","ASSERT_RAM8 FFFFB1 3"]+capture("replay"))
assert read_save(out)[0]==(2,0,0x15)

# Award through native Obj59 routine 8 after loading a completed file. Its
# committed hook must retain the campaign destination and completion status.
run(out,"post-clear-emerald",MENU+START+["WRITE_RAM8 FFF600 10","WAIT 500",
    "ASSERT_RAM8 FFF600 10"]+capture("halfpipe")+object(0xB800,0x59,8)+[
    "WAIT 120"]+capture("awarded"))
assert read_save(out)[0]==(2,0,0x17)
halfpipe=(out/"halfpipe.bin").read_bytes()
assert [halfpipe[0xB000],halfpipe[0xB040]]==[9,16]
run(out,"post-clear-reload",MENU+START+["ASSERT_RAM8 FFFFB1 4","ASSERT_RAM8 FFFFB3 FF"])

out=setup("delete")
seed(out,emeralds=3)
to_eighth=sum((["PRESS RIGHT 2","WAIT 20"] for _ in range(7)),[])
run(out,"independent-eighth",MENU+to_eighth+START)
assert read_save(out)[0]==(1,0,3) and read_save(out)[7]==(1,0,0)
to_delete=sum((["PRESS RIGHT 2","WAIT 20"] for _ in range(8)),[])
to_first=sum((["PRESS LEFT 2","WAIT 20"] for _ in range(8)),[])
erase_menu=MENU+to_delete+["PRESS A 2","WAIT 10"]+to_first+["PRESS A 2","WAIT 10"]
run(out,"delete-cancel",erase_menu+capture("confirm")+["PRESS B 2","WAIT 10"])
assert read_save(out)[0]==(1,0,3)
run(out,"delete-confirm",erase_menu+["PRESS A 2","WAIT 10"]+capture("deleted"))
assert read_save(out)[0]==(0,0,0)
assert read_save(out)[7]==(1,0,0)

out=setup("delete-directions")
seed(out,slot=1,stage=3,emeralds=3)
before=(out/"sonic2-campaign.sav").read_bytes()
to_second=sum((["PRESS LEFT 2","WAIT 20"] for _ in range(7)),[])
confirm_second=MENU+to_delete+["PRESS A 2","WAIT 10"]+to_second+["PRESS A 2","WAIT 10"]
run(out,"delete-right-no",confirm_second+capture("yes-no")+[
    "PRESS RIGHT 2","WAIT 10"]+capture("spinning")+[
    "PRESS B 2","WAIT 120"]+capture("returned"))
assert (out/"sonic2-campaign.sav").read_bytes()==before
run(out,"delete-left-yes",confirm_second+["PRESS LEFT 2","WAIT 10"]+capture("erased"))
assert read_save(out)==[(0,0,0)]*8

out=setup("vs")
seed(out,stage=1,emeralds=3); before=(out/"sonic2-campaign.sav").read_bytes()
run(out,"vs-entry",TITLE+["PRESS DOWN 2","WAIT 8","PRESS START 2","WAIT 100",
    "ASSERT_RAM8 FFF600 1C"]+capture("vs-select"))
assert (out/"sonic2-campaign.sav").read_bytes()==before
out=setup("options")
run(out,"options-entry",TITLE+["PRESS DOWN 2","WAIT 8","PRESS DOWN 2","WAIT 8",
    "PRESS START 2","WAIT 90","ASSERT_RAM8 FFF600 24","PRESS RIGHT 2","WAIT 8",
    "PRESS START 2","WAIT 90","ASSERT_RAM8 FFF600 04"])
assert "slots=3\n" in (out/"sonic2-party.ini").read_text()
assert not (out/"sonic2-campaign.sav").exists()

out=setup("invalid-save")
(out/"sonic2-campaign.sav").write_bytes(b"future or corrupt data")
run(out,"invalid-no-save",MENU+["PRESS LEFT 2","WAIT 20"]+START)
assert (out/"sonic2-campaign.sav").read_bytes()==b"future or corrupt data"

print(f"PASS {len(results)} campaign fixtures: {', '.join(results)}")
