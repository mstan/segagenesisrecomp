#!/usr/bin/env python3
"""Input-only Sonic 3 family renderer checks; never pause or modify guest RAM."""
import argparse
import json
import os
import re
from pathlib import Path
import socket
import shutil
import subprocess
import time
from PIL import Image
from run_sonic1_custom_video import command


def timeline(variant="sonic3k"):
    text = "WAIT 700\nSCREENSHOT title.png\nPRESS START 2\n"
    if variant != "sandk":
        text += "WAIT 90\nPRESS START 2\n"
    if variant == "sonic3k":
        text += "WAIT 400\nPRESS START 2\nWAIT_RAM8 FFF600 8C\n"
    text += "WAIT_RAM8 FFF600 0C\nWAIT 30\n"
    for n in range(16):
        text += f"SCREENSHOT checkpoint-{n}.png\nDUMP_RAM checkpoint-{n}.ram.bin\nDUMP_VRAM checkpoint-{n}.vram.bin\n"
        text += "HOLD RIGHT\nPRESS C 8\n"
        if n == 8:
            text += "WAIT 120\n"
            for frame in range(16):
                text += f"DUMP_RAM refresh-{frame}.ram.bin\nDUMP_VRAM refresh-{frame}.vram.bin\nWAIT 1\n"
            text += "WAIT 1\n"
        else:
            text += "WAIT 137\n"
    return text + "RELEASE\nWAIT 5\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", type=Path, required=True)
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--modes", nargs="+", default=["off", "16:9", "32:9", "64:9"])
    ap.add_argument("--legacy", action="store_true")
    ap.add_argument("--native-reference", type=Path)
    ap.add_argument("--resize", action="store_true", help="exercise live Adaptive resizing with --modes fit")
    ap.add_argument("--variant", choices=["sonic3k", "sonic3", "sandk"], default="sonic3k")
    args = ap.parse_args()
    exe, rom, out = args.exe.resolve(), args.rom.resolve(), args.out.resolve()
    for mode in args.modes:
        case = out / mode.replace(":", "-")
        case.mkdir(parents=True, exist_ok=True)
        # SRAM/settings are executable-relative: isolate every run so prior
        # play sessions cannot change the save selector or native reference.
        runtime = case / "runtime"
        runtime.mkdir(exist_ok=True)
        shutil.copy2(exe, runtime / exe.name)
        shutil.copy2(exe.parent / "SDL2.dll", runtime / "SDL2.dll")
        if not args.legacy:
            (runtime / "debug.ini").write_text("[debug]\nenabled=1\nport=4386\n")
        script = timeline(args.variant)
        script = re.sub(r"(?m)^((?:SCREENSHOT|DUMP_RAM|DUMP_VRAM) )([^\n]+)$",
                        lambda m: m[1] + (case / m[2]).as_posix(), script)
        (case / "input.txt").write_text(script)
        env = os.environ.copy()
        env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software")
        argv = [str(runtime / exe.name), str(rom), "--no-launcher", "--port", "4386", "--max-frames", "6000",
                "--target-fps", "1000", "--input-script", str(case / "input.txt")]
        if not args.legacy:
            argv += ["--widescreen", mode]
        samples = []
        sizes = [(1600,900,398),(2560,720,796),(4000,500,1792),(960,720,320)] if args.resize else []
        pending, resized, requested, next_resize = list(sizes), [], None, 2700
        with (case / "run.log").open("w") as log:
            proc = subprocess.Popen(argv, cwd=case, env=env, stdout=log, stderr=log,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            sock = None
            try:
                deadline = time.monotonic() + 240
                while proc.poll() is None:
                    if time.monotonic() > deadline:
                        raise RuntimeError(f"{case}: timed out")
                    if args.legacy:
                        time.sleep(.1)
                        continue
                    if sock is None:
                        try:
                            sock = socket.create_connection(("127.0.0.1", 4386), timeout=1)
                            sock.settimeout(5)
                        except OSError:
                            time.sleep(.03)
                            continue
                    try:
                        sample = command(sock, {"cmd": "custom_video"})
                        samples.append(sample)
                        if requested is not None and sample.get("width") == requested:
                            resized.append(requested)
                            requested = None
                            next_resize = sample.get("frames", 0) + 90
                        if pending and requested is None and sample.get("frames", 0) >= next_resize:
                            w, h, requested = pending.pop(0)
                            reply = command(sock, {"cmd":"video_configure", "mode":"fit", "window_width":w, "window_height":h})
                            assert not reply.get("error"), reply
                    except (OSError, RuntimeError):
                        break
                    time.sleep(.025)
                proc.wait(timeout=60)
            finally:
                if sock:
                    sock.close()
                if proc.poll() is None:
                    proc.terminate()
                    proc.wait(timeout=10)
        (case / "video.json").write_text(json.dumps(samples, indent=2))
        if not args.legacy and not samples:
            raise RuntimeError(f"{case}: no custom-video diagnostics received")
        if args.resize:
            assert resized == [item[2] for item in sizes], resized
        # During boot before the first level SAT, live scroll registers and
        # RAM can still describe different scenes. There is no published
        # level to compare yet; the renderer preserves the native center.
        bad = [s for s in samples if s.get("scene") == 1 and
               (s.get("terrain_errors",0) or s.get("background_errors",0))]
        assert not bad, ("terrain/background mismatch", bad[:2])
        steady = [s for s in samples if s.get("frames",0)>=2800 and s.get("tick_samples",0)]
        if len(steady)>1:
            assert steady[-1]["tick_lag"] == steady[0]["tick_lag"], "gameplay tick lag"
            assert steady[-1]["tick_multi"] == steady[0]["tick_multi"], "double gameplay tick"
            assert steady[-1]["publication_lag"] == steady[0]["publication_lag"], "sprite publication lag"
        if proc.returncode:
            raise RuntimeError(f"{case}: exit {proc.returncode}")
        log = (case / "run.log").read_text(errors="replace")
        if "0 unique true-miss addrs, 0 raw miss events" not in log or "ASSERT_RAM" in log or "FATAL" in log:
            raise RuntimeError(f"{case}: inspect dispatch/assertion diagnostics")
        for name in ("dispatch_misses.log", "dispatch_misses.toml"):
            path = case / name
            if path.exists() and any(line.strip() and not line.startswith("#") for line in path.read_text().splitlines()):
                raise RuntimeError(f"{case}: dispatch misses in {name}")
        for n in range(16):
            path = case / f"checkpoint-{n}.png"
            with Image.open(path) as img:
                expected = 320 if mode == "off" else img.width if mode == "fit" else round(224 * float(mode.split(":")[0]) / float(mode.split(":")[1]))
                assert img.size == (expected, 224), (path, img.size, expected)
            if mode == "off" and args.native_reference:
                for suffix in ("png", "ram.bin", "vram.bin"):
                    file = f"checkpoint-{n}.{suffix}"
                    assert (case / file).read_bytes() == (args.native_reference / file).read_bytes(), file
        print(f"PASS {case}: 16 checkpoints, no dispatch misses", flush=True)


if __name__ == "__main__":
    main()
