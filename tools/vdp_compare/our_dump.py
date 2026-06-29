#!/usr/bin/env python3
"""
our_dump.py - drive a running SonicTheHedgehogRecomp (cmd_server on --port) to
capture the live VDP state once the picture is confirmed static.

Polls read_cram until two reads N seconds apart are identical (a held screen),
then dumps VRAM (dump_vram), CRAM (read_cram), VSRAM (read_vsram) and a PNG
screenshot. Outputs <out_prefix>.vram.bin / .cram.txt / .vsram.txt / .png.

Usage: python our_dump.py <port> <out_prefix_abs> [--settle-secs S] [--timeout T]
"""
import socket, sys, json, time, os

def cmd(port, obj):
    s = socket.create_connection(("127.0.0.1", port), timeout=15)
    s.sendall((json.dumps(obj) + "\n").encode())
    buf = b""
    while not buf.endswith(b"\n"):
        ch = s.recv(1 << 20)
        if not ch: break
        buf += ch
    s.close()
    return json.loads(buf.decode(errors="replace"))

def main():
    port = int(sys.argv[1]); outp = sys.argv[2]
    settle = float(sys.argv[sys.argv.index("--settle-secs")+1]) if "--settle-secs" in sys.argv else 1.5
    timeout = float(sys.argv[sys.argv.index("--timeout")+1]) if "--timeout" in sys.argv else 30.0
    t0 = time.time(); prev = None; stable_since = None
    while time.time() - t0 < timeout:
        r = cmd(port, {"id": 1, "cmd": "read_cram"})
        hexv = r.get("hex", "")
        nonblack = any(c != "0" for c in hexv)
        now = time.time()
        if hexv == prev and nonblack:
            if stable_since is None: stable_since = now
            if now - stable_since >= settle:
                pf = cmd(port, {"id": 2, "cmd": "ping"})
                print(f"[our_dump] static screen confirmed (frame={pf.get('frame')}), dumping")
                break
        else:
            stable_since = None
        prev = hexv
        time.sleep(0.5)
    else:
        print("[our_dump] WARNING: never confirmed static; dumping current state")
    # full dump
    dv = cmd(port, {"id": 3, "cmd": "dump_vram", "path": outp + ".vram.bin"})
    cr = cmd(port, {"id": 4, "cmd": "read_cram"})
    vs = cmd(port, {"id": 5, "cmd": "read_vsram"})
    ss = cmd(port, {"id": 6, "cmd": "screenshot", "path": outp + ".png"})
    open(outp + ".cram.txt", "w").write(cr.get("hex", ""))
    open(outp + ".vsram.json", "w").write(json.dumps(vs.get("vsram", [])))
    print("[our_dump] dump_vram:", dv.get("ok"), dv.get("size"))
    print("[our_dump] cram entries:", cr.get("entries"))
    print("[our_dump] vsram len:", len(vs.get("vsram", [])))
    print("[our_dump] screenshot:", ss)

if __name__ == "__main__":
    main()
