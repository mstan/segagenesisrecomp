#!/usr/bin/env python3
"""
catch_frame.py - tight-poll the runner's frame counter and grab a target
frame's VDP surfaces from the always-on ring the instant it is reachable
(target in [oldest, current)). For fast-running (uncapped) runners where the
600-frame ring would otherwise evict the sync point before a wall-clock dump.
"""
import socket, sys, json, time

def cmd(port, obj):
    s = socket.create_connection(("127.0.0.1", port), timeout=20)
    s.sendall((json.dumps(obj) + "\n").encode())
    buf = b""
    while not buf.endswith(b"\n"):
        ch = s.recv(1 << 20)
        if not ch: break
        buf += ch
    s.close()
    return json.loads(buf.decode(errors="replace"))

def main():
    port = int(sys.argv[1]); outp = sys.argv[2]; target = int(sys.argv[3])
    deadline = time.time() + 25
    got = None
    while time.time() < deadline:
        try:
            p = cmd(port, {"id": 1, "cmd": "ping"})
        except Exception:
            time.sleep(0.05); continue
        cur = p.get("frame", 0)
        # target reachable while still in ring (cur-600 .. cur-1)
        if cur > target and cur - target < 580:
            r = cmd(port, {"id": 2, "cmd": "get_frame", "frame": target, "include": "vram,cram,vsram"})
            if r.get("ok"):
                got = (target, r); break
        if cur - target >= 580:
            # missed it; fall back to a frame still safely in-ring near sync window
            fb = cur - 120
            r = cmd(port, {"id": 3, "cmd": "get_frame", "frame": fb, "include": "vram,cram,vsram"})
            if r.get("ok"):
                got = (fb, r); print(f"[catch] missed {target}; grabbed in-ring {fb}"); break
        time.sleep(0.02)
    if not got:
        print("[catch] failed"); sys.exit(2)
    f, r = got
    vdp = r["vdp"]
    open(outp + ".vram.bin", "wb").write(bytes.fromhex(vdp["vram"]))
    open(outp + ".cram.json", "w").write(json.dumps(vdp["cram"]))
    open(outp + ".vsram.json", "w").write(json.dumps(vdp["vsram"]))
    open(outp + ".frame.txt", "w").write(str(f))
    print(f"[catch] frame={f} vram={len(vdp['vram'])//2}B cram={len(vdp['cram'])} vsram={len(vdp['vsram'])}")

if __name__ == "__main__":
    main()
