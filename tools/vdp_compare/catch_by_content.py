#!/usr/bin/env python3
"""
catch_by_content.py - CONTENT-locked sync-point capture (layered-parity safe).

Absolute-frame matching across two backends is invalid (boot timelines drift).
Instead we poll the runner's always-on frame ring (light CRAM-only reads) and
trigger a full VDP dump the instant our palette matches the oracle's reference
palette within tolerance -- i.e. the instant our recomp is displaying the SAME
screen the oracle dumped. Then we pull the full frame (vram/cram/vsram) and a
live screenshot.

Usage:
  python catch_by_content.py <port> <out_prefix> <oracle_cram_bin> [--min-match M] [--timeout T]
"""
import socket, sys, json, struct, time

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
    port = int(sys.argv[1]); outp = sys.argv[2]; oracle_cram = sys.argv[3]
    min_match = int(sys.argv[sys.argv.index("--min-match")+1]) if "--min-match" in sys.argv else 58
    timeout = float(sys.argv[sys.argv.index("--timeout")+1]) if "--timeout" in sys.argv else 25.0
    cb = open(oracle_cram, "rb").read()
    ref = [x & 0x0EEE for x in struct.unpack("<%dH" % (len(cb)//2), cb)][:64]

    best = -1; best_frame = None
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            p = cmd(port, {"id":1,"cmd":"ping"})
        except Exception:
            time.sleep(0.03); continue
        cur = p.get("frame", 0)
        f = max(0, cur - 3)
        r = cmd(port, {"id":2,"cmd":"get_frame","frame":f,"include":"cram"})
        if not r.get("ok"):
            time.sleep(0.01); continue
        cram = [x & 0x0EEE for x in r["vdp"]["cram"]][:64]
        match = sum(1 for i in range(64) if cram[i] == ref[i])
        if match > best:
            best = match; best_frame = f
        if match >= min_match:
            full = cmd(port, {"id":3,"cmd":"get_frame","frame":f,"include":"vram,cram,vsram"})
            if full.get("ok"):
                vdp = full["vdp"]
                open(outp+".vram.bin","wb").write(bytes.fromhex(vdp["vram"]))
                open(outp+".cram.json","w").write(json.dumps(vdp["cram"]))
                open(outp+".vsram.json","w").write(json.dumps(vdp["vsram"]))
                open(outp+".frame.txt","w").write(str(f))
                ss = cmd(port, {"id":4,"cmd":"screenshot","path":outp+".png"})
                print(f"[catch] MATCH {match}/64 @ frame {f}; dumped. screenshot ok={ss.get('ok')}")
                return
        time.sleep(0.01)
    print(f"[catch] no match >= {min_match}; best was {best}/64 @ frame {best_frame}")
    sys.exit(2)

if __name__ == "__main__":
    main()
