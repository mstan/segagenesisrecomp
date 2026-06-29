#!/usr/bin/env python3
"""
getframe_dump.py - pull a single frame's VDP surfaces from the runner's
always-on frame_record ring (get_frame), saving byte-exact dumps comparable
to the BlastEm oracle. Tries each candidate frame and reports which are in the
ring; dumps the first available (or a specific one).

Usage: python getframe_dump.py <port> <out_prefix_abs> <frame> [frame2 frame3 ...]
"""
import socket, sys, json

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
    port = int(sys.argv[1]); outp = sys.argv[2]
    frames = [int(x) for x in sys.argv[3:]]
    chosen = None
    for f in frames:
        r = cmd(port, {"id": 1, "cmd": "get_frame", "frame": f, "include": "vram,cram,vsram"})
        if r.get("ok"):
            chosen = (f, r);
            print(f"[getframe] frame {f}: OK (in ring)")
            break
        else:
            print(f"[getframe] frame {f}: {r.get('error')}")
    if not chosen:
        print("[getframe] no requested frame available in ring"); sys.exit(2)
    f, r = chosen
    vdp = r["vdp"] if "vdp" in r else r
    vram_hex = vdp["vram"]; cram = vdp["cram"]; vsram = vdp["vsram"]
    open(outp + ".vram.bin", "wb").write(bytes.fromhex(vram_hex))
    open(outp + ".cram.json", "w").write(json.dumps(cram))
    open(outp + ".vsram.json", "w").write(json.dumps(vsram))
    open(outp + ".frame.txt", "w").write(str(f))
    print(f"[getframe] wrote {outp}.vram.bin ({len(vram_hex)//2}B), cram[{len(cram)}], vsram[{len(vsram)}], frame={f}")

if __name__ == "__main__":
    main()
