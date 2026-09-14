#!/usr/bin/env python3
"""Run Sonic 1 enhanced scenes/geometry and resize checks with the owner's ROM.

Uses finite free-running input scripts and the existing debug server. Captures
are written under --out; game simulation is never paused or memory-patched.
Requires Pillow and numpy for pixel comparisons.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import time
import tomllib
import numpy as np
from PIL import Image

def command(sock, payload):
    sock.sendall((json.dumps({"id":1,**payload})+"\n").encode())
    data=b""
    while b"\n" not in data:
        chunk=sock.recv(65536)
        if not chunk: raise RuntimeError("debug connection closed")
        data+=chunk
    return json.loads(data.split(b"\n")[0])

def run_case(exe,rom,out,name,mode,template,resize=False,require_level=True,max_frames=1800):
    case=out/name
    case.mkdir(parents=True,exist_ok=True)
    script=template.replace("custom-",(case/"custom-").as_posix())
    if any(c.isspace() for c in str(case)): raise RuntimeError("output path must have no whitespace")
    (case/"input.txt").write_text(script)
    env=os.environ.copy()
    env.update(SDL_VIDEODRIVER="dummy",SDL_AUDIODRIVER="dummy",SDL_RENDER_DRIVER="software")
    samples=[]
    with (case/"run.log").open("w") as log:
        proc=subprocess.Popen([str(exe),str(rom),"--no-launcher","--port","4438",
            "--max-frames",str(max_frames),"--target-fps","1000","--widescreen",mode,
            "--input-script",str(case/"input.txt")],cwd=case,env=env,stdout=log,stderr=log,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0)
        sock=None
        try:
            until=time.monotonic()+15
            while sock is None and proc.poll() is None and time.monotonic()<until:
                try: sock=socket.create_connection(("127.0.0.1",4438),timeout=1)
                except OSError: time.sleep(.03)
            if sock is None: raise RuntimeError(f"{name}: no debug server; see {case/'run.log'}")
            sock.settimeout(5)
            sizes=[(1600,900,398),(2560,720,796),(4000,500,1792),(960,720,320)]
            pending=list(sizes) if resize else []
            seen=[]
            last_requested=None
            deadline=time.monotonic()+120
            while proc.poll() is None:
                if time.monotonic()>deadline: raise RuntimeError(f"{name}: run exceeded 120 seconds")
                try: state=command(sock,{"cmd":"custom_video"})
                except (OSError,RuntimeError): break
                samples.append(state)
                if state.get("frames",0)>5 and pending and (last_requested is None or state["width"]==last_requested):
                    if last_requested is not None: seen.append(last_requested)
                    w,h,last_requested=pending.pop(0)
                    response=command(sock,{"cmd":"video_configure","mode":"fit","window_width":w,"window_height":h})
                    if response.get("error"): raise RuntimeError(response)
                elif resize and not pending and last_requested is not None and state["width"]==last_requested:
                    seen.append(last_requested);last_requested=None
                time.sleep(.025)
            proc.wait(timeout=90)
            if proc.returncode: raise RuntimeError(f"{name}: exit {proc.returncode}")
            if resize and seen != [x[2] for x in sizes]: raise RuntimeError(f"resize widths {seen}, expected {sizes}")
        finally:
            if sock: sock.close()
            if proc.poll() is None:
                proc.terminate();proc.wait(timeout=10)
    (case/"video.json").write_text(json.dumps(samples,indent=2))
    log_text=(case/"run.log").read_text()
    if "0 unique true-miss addrs, 0 raw miss events" not in log_text: raise RuntimeError(f"{name}: dispatch regression")
    misses=case/"dispatch_misses.toml"
    if misses.exists() and tomllib.loads(misses.read_text()).get("functions",{}).get("extra"):
        raise RuntimeError(f"{name}: nonempty dispatch_misses.toml")
    if "[ILLEGAL]" in log_text or "stack mismatch" in log_text: raise RuntimeError(f"{name}: runtime failure")
    active=[x for x in samples if x.get("terrain_checks",0)]
    if mode != "off" and require_level and not active: raise RuntimeError(f"{name}: custom terrain never rendered")
    if mode != "off" and not require_level and not any(x.get("scene")==2 and x.get("sprites",0)>10 for x in samples):
        raise RuntimeError(f"{name}: expanded special-stage scene never rendered")
    expected_width={"wide32":796,"wide64":1593}.get(name)
    if expected_width and not any(x["width"]==expected_width for x in active):
        raise RuntimeError(f"{name}: expected width {expected_width} not reached")
    if name=="stage" and not any(x["width"]==x["stage_width"] and x["width"]>1593 for x in active):
        raise RuntimeError("stage: full-stage width not reached")
    errors=max((x["terrain_errors"] for x in active),default=0)
    bg_errors=max((x.get("background_errors",0) for x in active),default=0)
    if bg_errors:raise RuntimeError(f"{name}: background differs within the native streamed strip ({bg_errors})")
    if mode in ("32:9","64:9"):
        expected=796 if mode=="32:9" else 1593
        if any(x.get("frames",0) and x["width"]!=expected for x in samples):
            raise RuntimeError(f"{name}: output width changed during a scene/transition")
    if mode != "off" and require_level and not any(x.get("scene")==1 and x.get("sprites",0)>0 for x in active):
        raise RuntimeError(f"{name}: host level sprites never rendered")
    print(f"{name}: widths {sorted({x['width'] for x in samples})}, terrain errors {errors}, BG errors {bg_errors}, "
          f"host ring pickups {max((x.get('host_rings_collected',0) for x in samples),default=0)}",flush=True)
    return case,errors

def check_hud(native,wide,name):
    # The SCORE label is fixed art: compare its yellow glyph mask at the
    # screen's left inset, not backgrounds, changing digits, or camera offset.
    def mask(im):
        p=np.array(im.convert("RGB"))[8:16,16:56]
        return (p[:,:,0]>200)&(p[:,:,1]>200)&(p[:,:,2]<80)
    expected=mask(native)
    if expected.sum()<25 or not np.array_equal(expected,mask(wide)):
        raise RuntimeError(f"{name}: SCORE label is not anchored at the screen's left inset")

def special_template():
    # Enter via the real REV00 title cheat, then select row 19 (Special Stage).
    # Input-only: no paused simulation, save-state warp, or RAM patch.
    script="WAIT 600\n"
    for key in ("UP","DOWN","LEFT","RIGHT"):
        script+=f"PRESS {key} 2\nWAIT 4\n"
    script+="ASSERT_RAM8 0xFFFFE0 1\nHOLD A\nPRESS START 2\nWAIT 30\nRELEASE\nWAIT 30\n"
    script+="SCREENSHOT custom-menu.png\nPRESS UP 2\nWAIT 4\nPRESS UP 2\nWAIT 4\n"
    script+="ASSERT_RAM16 0xFFFF82 0x13\nPRESS START 2\nWAIT 5\nSCREENSHOT custom-special-fade.png\n"
    script+="WAIT 295\nASSERT_RAM8 0xFFD000 9\nSCREENSHOT custom-special.png\nDUMP_RAM custom-special.ram.bin\nDUMP_VRAM custom-special.vram.bin\n"
    script+="WAIT 120\nSCREENSHOT custom-special-rotated.png\nHOLD RIGHT\nWAIT 300\nRELEASE\nWAIT 600\nSCREENSHOT custom-special-late.png\nWAIT 5\n"
    return script

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--exe",type=Path,required=True)
    ap.add_argument("--rom",type=Path,required=True)
    ap.add_argument("--out",type=Path,required=True)
    ap.add_argument("--native-reference",type=Path,help="optional untouched first-pass native capture directory")
    args=ap.parse_args()
    if not (args.exe.resolve().parent/"debug.ini").is_file():
        ap.error("create debug.ini beside the executable to enable its debug server")
    root=Path(__file__).resolve().parents[2]
    template=(root/"tests/regression/sonic1_custom_video.input").read_text()
    template=template.replace("WAIT 720\n","WAIT 600\nSCREENSHOT custom-title.png\nWAIT 120\n",1)
    template=template.replace("WAIT 400\n","WAIT 12\nSCREENSHOT custom-fade.png\nWAIT 388\n",1)
    results={}
    for name,mode in (("native","off"),("wide32","32:9"),("wide64","64:9"),("stage","stage"),("resize","fit")):
        case_template=template
        if mode!="off":
            # Wider activation changes when enemies reach Sonic. Use an
            # earlier repeated-jump route, keeping checkpoint frame numbers
            # unchanged; preserve the original native baseline route.
            first=case_template.index("HOLD RIGHT\n")
            last=case_template.index("SCREENSHOT custom-scroll.png")
            case_template=case_template[:first]+"HOLD RIGHT\n"+"PRESS C 10\nWAIT 30\n"*12+case_template[last:]
        results[name]=run_case(args.exe.resolve(),args.rom.resolve(),args.out.resolve(),name,mode,case_template,name=="resize")
    for name,(case,errors) in results.items():
        for checkpoint in ("start","scroll"):
            stock=results["native"][0]
            # Enhanced mode intentionally changes actor activation/collected
            # ring state and relocates HUD. RAM identity/native pixel copying
            # are no longer its contract. Native opt-out still is unchanged.
            if name=="native" and args.native_reference:
                for ext in ("ram.bin","vram.bin","png"):
                    if (case/f"custom-{checkpoint}.{ext}").read_bytes() != (args.native_reference/f"custom-{checkpoint}.{ext}").read_bytes():
                        raise RuntimeError(f"native/{checkpoint}: reference {ext} changed")
            check_hud(Image.open(stock/f"custom-{checkpoint}.png"),Image.open(case/f"custom-{checkpoint}.png"),f"{name}/{checkpoint}")
        if errors:
            raise RuntimeError(f"{name}: terrain mapping differs from native plane ({errors})")
        if name in ("wide32","wide64"):
            title=np.array(Image.open(case/"custom-title.png").convert("RGB"))
            if np.count_nonzero(title[:,:64])<1000 or np.count_nonzero(title[:,-64:])<1000:
                raise RuntimeError(f"{name}: title scenery is still pillarboxed")
    for name,mode in (("special-native","off"),("special-wide","64:9")):
        run_case(args.exe.resolve(),args.rom.resolve(),args.out.resolve(),name,mode,special_template(),require_level=False,max_frames=2400)
    print("PASS: full-width scenes, anchored HUD, expanded sprite lists, live resize, terrain/BG mapping, no dispatch misses")

if __name__=="__main__":main()
