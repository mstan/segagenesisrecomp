#!/usr/bin/env python3
"""Native Data Select and single-file SRAM integration, with private donors.
These tests validate menu routing/save continuity, not complete stage play.
"""
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import zlib


def records(data):
    if len(data) == 0x4000:
        return {}
    assert data[0x4000:0x4008] == b"S3KEXT01"
    version, length, count, crc = struct.unpack_from(">4I", data, 0x4008)
    assert version == 1 and length == len(data)-0x4000 and crc == zlib.crc32(data[0x4018:])
    result, pos = {}, 0x4018
    for _ in range(count):
        tag, version, flags, size, crc = struct.unpack_from(">4sHHII", data, pos)
        payload = data[pos+16:pos+16+size]
        assert flags == 0 and zlib.crc32(payload) == crc
        result[tag] = (version, payload)
        pos += 16+size
    assert pos == len(data)
    return result


def run(args, name, packs, expected, seed=None, add=False, character=0, no_save=False, slot=0, creating=False):
    out = args.out.resolve()/name
    out.mkdir(parents=True,exist_ok=False)
    runtime = out/"runtime"
    runtime.mkdir()
    exe, rom = args.exe.resolve(), args.rom.resolve()
    shutil.copy2(exe,runtime/exe.name)
    shutil.copy2(exe.parent/"SDL2.dll",runtime/"SDL2.dll")
    save = runtime/(rom.stem+".srm")
    if seed is not None:
        save.write_bytes(seed)
    capture = lambda name:f"SCREENSHOT {(out/(name+'.png')).as_posix()}\nDUMP_RAM {(out/(name+'.ram.bin')).as_posix()}\n"
    script = "WAIT 700\nPRESS START 2\nWAIT 90\nPRESS START 2\nWAIT 400\n"+capture("menu")
    if no_save:script += "PRESS LEFT 2\nWAIT 30\n"
    else:
        for _ in range(slot):script += "PRESS RIGHT 2\nWAIT 30\n"
    if seed is None or creating or no_save:
        for _ in range(character):script += "PRESS UP 2\nWAIT 30\n"
    if add:
        script += "PRESS B 2\nWAIT 30\n"+capture("enrolled")
    script += "PRESS START 2\nWAIT_RAM8 FFF600 8C\nWAIT_RAM8 FFF600 0C\nWAIT 120\n"+capture("level")+"WAIT 10\n"
    (out/"input.txt").write_text(script)
    env=os.environ.copy()
    for key in ("SONIC_TRILOGY_STAGE","SONIC_TRILOGY_ROM","SONIC_TRILOGY_S1_ROM","SONIC_TRILOGY_S2_ROM"):
        env.pop(key,None)
    if packs&1:env["SONIC_TRILOGY_S1_ROM"]=str(args.sonic1.resolve())
    if packs&2:env["SONIC_TRILOGY_S2_ROM"]=str(args.sonic2.resolve())
    (runtime/"settings.ini").write_text("[trilogy]\nenabled=1\n")
    env.update(SDL_VIDEODRIVER="dummy",SDL_AUDIODRIVER="dummy",SDL_RENDER_DRIVER="software")
    cmd=[str(runtime/exe.name),str(rom),"--no-launcher","--target-fps","1000","--max-frames","2100",
         "--widescreen","off","--input-script",str(out/"input.txt")]
    with (out/"run.log").open("w") as log:
        subprocess.run(cmd,cwd=out,env=env,stdout=log,stderr=log,check=True,timeout=120,
                       creationflags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0)
    log=(out/"run.log").read_text(errors="replace")
    assert f"Starting stage {expected:04X} slot {-1 if no_save else slot}" in log, (name,"wrong chapter")
    assert "0 unique true-miss addrs, 0 raw miss events" in log and "FATAL" not in log
    ram=(out/"level.ram.bin").read_bytes()
    assert ram[0xF600]==12 and ram[0xB005]==2,(name,"player did not enter gameplay")
    assert ram[0xB038]==(0,0,1,2)[character],(name,"wrong playable character")
    data=save.read_bytes()
    rec=records(data)
    if no_save:
        assert b"CAMP" not in rec,(name,"No Save created campaign progress")
        print(f"PASS {name}: native No Save launched {expected:04X}",flush=True)
        return data
    assert b"CAMP" in rec
    for tag in (b"S1EX",b"S2EX"):
        if seed and tag in records(seed):
            old=records(seed)[tag]
            if creating:
                for n in range(8):
                    if n!=slot:assert rec[tag][1][n*8:n*8+8]==old[1][n*8:n*8+8],(name,"another slot changed")
            else:assert rec[tag]==old,(name,"chapter progress changed during resume")
    print(f"PASS {name}: native menu launched {expected:04X}; chapter records preserved",flush=True)
    return data


def main():
    ap=argparse.ArgumentParser()
    for key in ("exe","rom","sonic1","sonic2","out"):
        ap.add_argument("--"+key,type=Path,required=True)
    args=ap.parse_args()
    both=run(args,"new-both",3,0x1000)
    run(args,"reload-both",3,0x1000,both)
    missing1=run(args,"missing-sonic1",2,0x2000,both)
    run(args,"restored-sonic1",3,0x1000,missing1)
    run(args,"missing-sonic2",1,0x1000,both)
    missing_both=run(args,"missing-both",0,0x3000,both)
    run(args,"restored-both",3,0x1000,missing_both)
    run(args,"add-existing-native",3,0x1000,both[:0x4000],add=True)


if __name__=="__main__":
    main()
