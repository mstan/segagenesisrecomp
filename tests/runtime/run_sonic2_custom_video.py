#!/usr/bin/env python3
"""Input-only Sonic 2 native/custom captures; never pause or patch game memory."""
import argparse
from collections import deque
import json
import os
from pathlib import Path
import socket
import subprocess
import time
from PIL import Image
from run_sonic1_custom_video import command


def level_select(zone):
    text="WAIT 700\nPRESS START 2\nWAIT 90\nPRESS UP 2\nWAIT 10\nASSERT_RAM8 FFFF86 2\nPRESS START 2\nWAIT_RAM8 FFF600 24\nWAIT 120\n"
    text+="PRESS UP 2\nWAIT 10\nASSERT_RAM8 FFFF8C 2\n"
    current=0
    for target in (0x19,0x65,9,0x17):
        queue=deque([(current,[])])
        seen={current}
        while queue:
            value,keys=queue.popleft()
            if value==target:break
            for key,nxt in (("LEFT",(value-1)%128),("RIGHT",(value+1)%128),("A",value+16 if value+16<128 else 0)):
                if nxt not in seen:seen.add(nxt);queue.append((nxt,keys+[key]))
        for key in keys:text+=f"PRESS {key} 2\nWAIT 4\n"
        text+=f"ASSERT_RAM16 FFFF84 {target:X}\nPRESS B 2\nWAIT 10\n"
        current=target
    text+="ASSERT_RAM16 FFFFD0 0101\nPRESS START 2\nWAIT 700\nPRESS START 2\nWAIT 90\n"
    text+="HOLD A\nPRESS START 2\nWAIT 20\nRELEASE\nWAIT_RAM8 FFF600 28\nWAIT 120\n"
    row={"cpz":2,"arz":4,"cnz":6,"htz":8,"mcz":10,"ooz":12,"mtz":14,"wfz":18,"dez":19,"ss":20}[zone]
    text+="PRESS DOWN 2\nWAIT 4\n"*row
    text+=f"ASSERT_RAM16 FFFF82 {row:X}\nPRESS START 2\n"
    if zone=="ss":return text+"WAIT_RAM8 FFF600 10\nWAIT 240\n"
    return text+"WAIT_RAM8 FFF600 8C\nWAIT_RAM8 FFF600 0C\nWAIT 30\n"


def timeline(zone="ehz"):
    text = "WAIT 700\nPRESS START 2\nWAIT 90\nSCREENSHOT title.png\nPRESS START 2\n"
    text += "WAIT_RAM8 FFF600 8C\nWAIT_RAM8 FFF600 0C\nWAIT 30\n"
    if zone!="ehz":text=level_select(zone)
    for n in range(10):
        text += f"SCREENSHOT checkpoint-{n}.png\nDUMP_RAM checkpoint-{n}.ram.bin\nDUMP_VRAM checkpoint-{n}.vram.bin\n"
        text += "HOLD RIGHT\nPRESS C 8\n"
        text+="WAIT 100\n"
    # Let --max-frames perform normal shutdown and flush dispatch diagnostics.
    return text + "RELEASE\nHOLD LEFT\nWAIT 160\nRELEASE\nWAIT 5\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", type=Path, required=True)
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--modes", nargs="+", default=["off", "10:7", "16:9", "21:9", "32:9", "64:9", "stage"])
    ap.add_argument("--legacy", action="store_true")
    ap.add_argument("--zone", default="ehz", choices=["ehz","cpz","arz","cnz","htz","mcz","ooz","mtz","wfz","dez","ss"])
    ap.add_argument("--native-reference", type=Path)
    ap.add_argument("--resize", action="store_true", help="exercise live adaptive resizing; requires --modes fit")
    args = ap.parse_args()
    exe, rom, out = args.exe.resolve(), args.rom.resolve(), args.out.resolve()
    if args.legacy and args.modes != ["off"]:
        ap.error("--legacy requires --modes off")
    if args.resize and args.modes != ["fit"]:
        ap.error("--resize requires --modes fit")
    for mode in args.modes:
        case = out / mode.replace(":", "-")
        case.mkdir(parents=True, exist_ok=True)
        script = timeline(args.zone)
        for prefix in ("title.png", "checkpoint-"):
            script = script.replace(prefix, (case / prefix).as_posix())
        (case / "input.txt").write_text(script)
        env = os.environ.copy()
        env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software")
        argv = [str(exe), str(rom), "--no-launcher", "--port", "4442", "--max-frames", "3200" if args.zone=="ehz" else "5200",
                "--target-fps", "1000", "--input-script", str(case / "input.txt")]
        if not args.legacy:
            argv += ["--widescreen", mode]
        samples = []
        sizes = [(1600,900,398),(2560,720,796),(4000,500,1792),(960,720,320)]
        pending = list(sizes) if args.resize else []
        requested = None
        resized = []
        with (case / "run.log").open("w") as log:
            proc = subprocess.Popen(argv, cwd=case, env=env, stdout=log, stderr=log,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            sock = None
            try:
                deadline = time.monotonic() + (240 if mode=="stage" else 120)
                while proc.poll() is None:
                    if time.monotonic() > deadline:
                        raise RuntimeError(f"{case}: timed out")
                    if sock is None:
                        try:
                            sock = socket.create_connection(("127.0.0.1", 4442), timeout=1)
                            sock.settimeout(5)
                        except OSError:
                            time.sleep(.03)
                            continue
                    try:
                        video = {} if args.legacy else command(sock, {"cmd": "custom_video"})
                        video["sonic"] = command(sock, {"cmd": "sonic_state"})
                        samples.append(video)
                        if video.get("terrain_checks",0):
                            if requested is not None and video["width"]==requested:
                                resized.append(requested)
                                requested = None
                            if requested is None and pending:
                                w,h,requested = pending.pop(0)
                                reply=command(sock,{"cmd":"video_configure","mode":"fit","window_width":w,"window_height":h})
                                assert not reply.get("error"), reply
                    except (OSError, RuntimeError):
                        break
                    time.sleep(.025)
                proc.wait(timeout=30)
            finally:
                if sock:
                    sock.close()
                if proc.poll() is None:
                    proc.terminate()
                    proc.wait(timeout=10)
        (case / "video.json").write_text(json.dumps(samples, indent=2))
        log_text = (case / "run.log").read_text()
        assert proc.returncode == 0, (case, proc.returncode)
        assert "0 unique true-miss addrs, 0 raw miss events" in log_text, (case, "dispatch misses")
        assert "[ILLEGAL]" not in log_text and "stack mismatch" not in log_text, (case,"runtime failure")
        assert (case / "checkpoint-9.ram.bin").exists(), (case, "incomplete route")
        if args.resize:
            assert resized==[s[2] for s in sizes], (case,"live resize",resized)
        reference=args.native_reference or (out/"off")
        if mode!="off" and args.zone!="ss" and (reference/"checkpoint-0.ram.bin").exists():
            current=(case/"checkpoint-0.ram.bin").read_bytes()
            original=(reference/"checkpoint-0.ram.bin").read_bytes()
            assert current[:0xA800]==original[:0xA800], (case,"stage chunks/layout/blocks changed")
        active = [v for v in samples if v.get("terrain_checks", 0)]
        assert mode == "off" or active or args.zone=="ss", (case, "custom terrain never rendered")
        assert all(v.get("scene")==1 and v.get("sprites",0)>0 for v in active), (case,"custom gameplay dropped its sprite frame (HUD/ring flicker)")
        if args.zone=="ss":
            assert (case/"checkpoint-0.ram.bin").read_bytes()[0xF600]==16, (case,"special stage not entered")
        summary = {"mode": mode, "widths": sorted({v["width"] for v in active}),
            "terrain_errors": max((v["terrain_errors"] for v in active), default=0),
            "background_errors": max((v["background_errors"] for v in active), default=0),
            "pool_pressure": max((v["pool_pressure"] for v in active), default=0),
            "held_sprite_frames": max((v.get("scene_held_frames",0) for v in samples), default=0)}
        (case / "summary.json").write_text(json.dumps(summary, indent=2))
        print(json.dumps(summary), flush=True)
        assert summary["terrain_errors"] == 0, (case, "terrain mismatch")
        assert summary["background_errors"] == 0, (case, "background mismatch")
        if ":" in mode:
            w,h=map(float,mode.split(":"));expected=max(320,round(224*w/h))
            assert all(v["width"]==expected for v in samples if v.get("frames",0)), (case,"aspect changed during transition")
        if mode=="stage":
            assert any(v["width"]==v["stage_width"] and v["width"]>1593 for v in active), (case,"stage width not reached")
        if mode!="off":
            capture=Image.open(case/"checkpoint-3.png").convert("RGB")
            if args.zone=="ss":
                assert capture.crop((0,0,64,224)).getbbox(), (case,"special-stage backdrop pillarboxed")
            else:
                def score_mask(im):
                    crop=im.convert("RGB").crop((16,8,56,16))
                    return tuple(r>200 and g>200 and b<80 for r,g,b in (crop.getpixel((x,y)) for y in range(8) for x in range(40)))
                mask=score_mask(capture)
                # REV01 SCORE glyph pixels, independently captured from the
                # untouched renderer. Only require foreground glyph pixels:
                # CNZ's yellow scenery can appear through the transparent gaps.
                glyph_rows=(0,0x7E3E3C3C3C,0x7E7E7E7E7E,0x0666666666,
                            0x066666660E,0x3E3E66061C,0x1F1F33031C,0x0333333338)
                assert all(mask[y*40+x] for y,row in enumerate(glyph_rows) for x in range(40) if row&(1<<x)), (case,"HUD anchoring changed")
            if (case/"title.png").exists() and capture.width>398:
                title=Image.open(case/"title.png").convert("RGB")
                assert title.crop((0,0,64,224)).getbbox(), (case,"title pillarboxed")
        if mode == "off" and args.native_reference:
            for capture in case.glob("checkpoint-*"):
                assert capture.read_bytes() == (args.native_reference / capture.name).read_bytes(), capture


if __name__ == "__main__":
    main()
