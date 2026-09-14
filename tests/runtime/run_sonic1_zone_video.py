#!/usr/bin/env python3
"""Free-running, input-only later-zone renderer regression captures.

Runs native and custom-at-native-width as well as wide. Never rewrites a
baseline: keep separate --out directories for before/after investigations.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import time
import tomllib
from run_sonic1_custom_video import command
from check_sonic1_stage_data import expected_stage

def timeline(row,zone):
    script="WAIT 600\n"
    for key in ("UP","DOWN","LEFT","RIGHT"):
        script+=f"PRESS {key} 2\nWAIT 4\n"
    script+="ASSERT_RAM8 0xFFFFE0 1\nHOLD A\nPRESS START 2\nWAIT 30\nRELEASE\nWAIT 30\n"
    script+="PRESS DOWN 2\nWAIT 4\n"*row
    script+=f"ASSERT_RAM16 0xFFFF82 0x{row:X}\nPRESS START 2\nWAIT 400\nASSERT_RAM16 0xFFFE10 0x{zone:X}\n"
    for n in range(10):
        script+=f"SCREENSHOT checkpoint-{n}.png\nDUMP_RAM checkpoint-{n}.ram.bin\nDUMP_VRAM checkpoint-{n}.vram.bin\n"
        script+="HOLD RIGHT\n"+"PRESS C 10\nWAIT 30\n"*3
    return script+"RELEASE\nWAIT 10\n"

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--exe",type=Path,required=True)
    ap.add_argument("--rom",type=Path,required=True)
    ap.add_argument("--out",type=Path,required=True)
    ap.add_argument("--zones",nargs="+",default=["syz","mz"])
    ap.add_argument("--modes",nargs="+",default=["off","10:7","32:9"])
    ap.add_argument("--legacy",action="store_true",help="compare a pre-custom-renderer binary (off only, no video queries)")
    args=ap.parse_args()
    zones={"ghz":(0,0),"lz":(3,0x100),"mz":(6,0x200),"slz":(9,0x300),"syz":(12,0x400),"sbz":(15,0x500)}
    env=os.environ.copy();env.update(SDL_VIDEODRIVER="dummy",SDL_AUDIODRIVER="dummy",SDL_RENDER_DRIVER="software")
    if args.legacy:
        assert args.modes==["off"]
        env["GENESIS_WIDESCREEN"]="0"
    summaries=[]
    rom=args.rom.read_bytes()
    for zone in args.zones:
        row,zid=zones[zone]
        for mode in args.modes:
            case=args.out.resolve()/f"{zone}-{mode.replace(':','x')}";case.mkdir(parents=True,exist_ok=True)
            (case/"input.txt").write_text(timeline(row,zid).replace("checkpoint-",(case/"checkpoint-").as_posix()))
            samples=[]
            with (case/"run.log").open("w") as log:
                cmd=[str(args.exe.resolve()),str(args.rom.resolve()),"--no-launcher","--port","4438",
                    "--input-script",str(case/"input.txt"),"--max-frames","2600","--target-fps","1000"]
                if not args.legacy:cmd += ["--widescreen",mode]
                proc=subprocess.Popen(cmd,
                    cwd=case,env=env,stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0)
                sock=None
                try:
                    until=time.monotonic()+120
                    if args.legacy:proc.wait(timeout=120)
                    while proc.poll() is None:
                        if time.monotonic()>until:raise RuntimeError(f"{case}: timed out")
                        if sock is None:
                            try:sock=socket.create_connection(("127.0.0.1",4438),timeout=1);sock.settimeout(5)
                            except OSError:time.sleep(.03);continue
                        try:
                            video=command(sock,{"cmd":"custom_video"})
                            video["sonic"]=command(sock,{"cmd":"sonic_state"})
                            samples.append(video)
                        except (OSError,RuntimeError):break
                        time.sleep(.025)
                    proc.wait(timeout=30)
                finally:
                    if sock:sock.close()
                    if proc.poll() is None:proc.terminate();proc.wait(timeout=10)
            (case/"video.json").write_text(json.dumps(samples,indent=2))
            log=(case/"run.log").read_text()
            assert proc.returncode==0,(case,proc.returncode)
            assert "0 unique true-miss addrs, 0 raw miss events" in log,(case,"dispatch misses")
            misses=case/"dispatch_misses.toml"
            if misses.exists():assert not tomllib.loads(misses.read_text()).get("functions",{}).get("extra"),case
            assert (case/"checkpoint-9.ram.bin").exists(),(case,"incomplete route")
            initial=(case/"checkpoint-0.ram.bin").read_bytes()
            for name,base,data in expected_stage(rom,initial[0xFE10],initial[0xFE11]):
                assert initial[base:base+len(data)]==data,(case,"ROM decode mismatch",name)
            if zone=="mz" and not args.legacy:
                # This platform must survive until Sonic reaches its cell.
                # Before the fix native-width custom had already deleted it.
                checkpoint=(case/"checkpoint-2.ram.bin").read_bytes()
                assert any(checkpoint[o]==0x2F and int.from_bytes(checkpoint[o+8:o+10],"big")==640
                    for o in range(0xD800,0xF000,64)),(case,"first MZ platform missing")
            active=[s for s in samples if s.get("terrain_checks",0)]
            assert mode=="off" or active,(case,"custom terrain never rendered")
            summary={"case":case.name,"terrain_errors":max((s["terrain_errors"] for s in active),default=0),
                "background_errors":max((s["background_errors"] for s in active),default=0),
                "pool_pressure":max((s["pool_pressure"] for s in active),default=0)}
            summaries.append(summary);print(json.dumps(summary),flush=True)
            assert summary["terrain_errors"]==0,(case,"foreground mapping differs")
    (args.out/"summary.json").write_text(json.dumps(summaries,indent=2))

if __name__=="__main__":main()
