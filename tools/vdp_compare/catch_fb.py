#!/usr/bin/env python3
"""
catch_fb.py - live framebuffer capture content-locked to a reference CRAM state.

Polls the LIVE CRAM (read_cram = current frame's palette) and the instant it
byte-matches the reference (masked 0x0EEE), takes a live screenshot, then
re-reads the live CRAM to CONFIRM it is still the same state (guards against a
fast palette sweep moving on between match and screenshot). Writes <out>.png
only on a confirmed lock.

Usage: python catch_fb.py <port> <out_prefix> <ref.cram.bin> [--timeout T]
"""
import socket, sys, json, struct, time

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

def live_cram_masked(port):
    r = cmd(port, {"id": 1, "cmd": "read_cram"})
    hexv = r.get("hex", "")
    vals = [int(hexv[i:i+4], 16) for i in range(0, min(len(hexv), 256), 4)]
    return [v & 0x0EEE for v in vals][:64]

def main():
    port = int(sys.argv[1]); outp = sys.argv[2]; ref_path = sys.argv[3]
    timeout = float(sys.argv[sys.argv.index("--timeout")+1]) if "--timeout" in sys.argv else 30.0
    cb = open(ref_path, "rb").read()
    ref = [x & 0x0EEE for x in struct.unpack("<%dH" % (len(cb)//2), cb)][:64]
    t0 = time.time(); best = -1
    while time.time() - t0 < timeout:
        try:
            cur = live_cram_masked(port)
        except Exception:
            time.sleep(0.02); continue
        m = sum(1 for i in range(64) if cur[i] == ref[i])
        best = max(best, m)
        if cur == ref:
            ss = cmd(port, {"id": 2, "cmd": "screenshot", "path": outp + ".png"})
            confirm = live_cram_masked(port)
            pf = cmd(port, {"id": 3, "cmd": "ping"}).get("frame")
            if confirm == ref:
                print(f"[catch_fb] CONFIRMED lock @ frame {pf}; screenshot ok={ss.get('ok')} -> {outp}.png")
                return 0
            else:
                print(f"[catch_fb] lock slipped during screenshot @ frame {pf}; retrying")
        time.sleep(0.005)
    print(f"[catch_fb] no exact lock within {timeout}s; best {best}/64")
    return 2

if __name__ == "__main__":
    sys.exit(main())
