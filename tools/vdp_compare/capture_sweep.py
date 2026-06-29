#!/usr/bin/env python3
"""
capture_sweep.py - ring-based, content-locked capture of the Sonic 1 Sega-logo
palette sweep from OUR runner, matched against pre-dumped BlastEm CRAM states.

Layered-parity safe + always-on-ring discipline: we do NOT arm/time a capture.
We let the runner free-run past the sweep, then QUERY the always-on 600-frame
frame_record ring for the whole sweep window in a single coherent pipelined
batch (the server drains all buffered request lines in one frame poll, so the
ring does not evict mid-read). We then content-lock each BlastEm CRAM state to
the OUR-side frame whose CRAM byte-matches (masked 0x0EEE) and dump that frame's
VRAM/CRAM/VSRAM in compare_state.py's format.

Usage:
  python capture_sweep.py <port> <lo> <hi> <out_dir> <ref1.cram.bin> [ref2 ...]
"""
import socket, sys, json, struct, time, os

class Conn:
    def __init__(self, port):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=30)
        self.s.settimeout(30)
        self.buf = b""
    def _readline(self):
        while b"\n" not in self.buf:
            ch = self.s.recv(1 << 20)
            if not ch:
                raise EOFError("server closed")
            self.buf += ch
        line, self.buf = self.buf.split(b"\n", 1)
        return json.loads(line.decode(errors="replace"))
    def call(self, obj):
        self.s.sendall((json.dumps(obj) + "\n").encode())
        return self._readline()
    def pipeline(self, objs):
        """Send all requests, then read len(objs) responses (FIFO)."""
        data = b"".join((json.dumps(o) + "\n").encode() for o in objs)
        # chunk the send so we never overrun the server's 8KB recv buffer by
        # more than it can drain in a couple of frames
        CH = 64
        out = []
        i = 0
        while i < len(objs):
            batch = objs[i:i+CH]
            data = b"".join((json.dumps(o) + "\n").encode() for o in batch)
            self.s.sendall(data)
            for _ in batch:
                out.append(self._readline())
            i += CH
        return out

def masked_cram_list(cram):
    return [x & 0x0EEE for x in cram][:64]

def load_ref(path):
    cb = open(path, "rb").read()
    return [x & 0x0EEE for x in struct.unpack("<%dH" % (len(cb)//2), cb)][:64]

def ping_once(port):
    s = socket.create_connection(("127.0.0.1", port), timeout=10)
    s.sendall(b'{"id":1,"cmd":"ping"}\n')
    buf = b""
    while b"\n" not in buf:
        ch = s.recv(1 << 16)
        if not ch: break
        buf += ch
    s.close()
    return json.loads(buf.split(b"\n", 1)[0].decode())

def main():
    port = int(sys.argv[1]); lo = int(sys.argv[2]); hi = int(sys.argv[3])
    out_dir = sys.argv[4]; refs = sys.argv[5:]
    os.makedirs(out_dir, exist_ok=True)

    # 1) wait (reconnect-per-ping) until runner is past the sweep window so the
    #    ring holds it all, but before frame 600 so nothing has evicted yet.
    target_cur = hi + 8
    t0 = time.time()
    cur = 0
    while True:
        try:
            cur = ping_once(port).get("frame", 0)
        except Exception:
            time.sleep(0.1); continue
        if cur >= target_cur:
            break
        if time.time() - t0 > 180:
            print(f"[sweep] timeout waiting for frame {target_cur}; cur={cur}")
            break
        time.sleep(0.15)
    print(f"[sweep] runner at frame {cur}; reading ring [{lo}..{hi}] coherently")
    c = Conn(port)

    # 2) coherent pipelined CRAM read of the whole window.
    reqs = [{"id": 100 + (f - lo), "cmd": "get_frame", "frame": f, "include": "cram"} for f in range(lo, hi + 1)]
    resp = c.pipeline(reqs)
    frame_cram = {}
    for r in resp:
        if r.get("ok") and "vdp" in r and "cram" in r["vdp"]:
            fid = r["id"] - 100 + lo
            frame_cram[fid] = masked_cram_list(r["vdp"]["cram"])
    avail = sorted(frame_cram)
    print(f"[sweep] ring frames available in window: {len(avail)} "
          f"({avail[0] if avail else '-'}..{avail[-1] if avail else '-'})")

    # 3) match each ref to OUR exact-CRAM frame(s).
    results = []
    for ref_path in refs:
        ref = load_ref(ref_path)
        name = os.path.basename(ref_path).replace(".cram.bin", "")
        exact = [f for f in avail if frame_cram[f] == ref]
        # best partial if no exact
        best_f, best_m = None, -1
        for f in avail:
            m = sum(1 for i in range(64) if frame_cram[f][i] == ref[i])
            if m > best_m:
                best_m, best_f = m, f
        # prefer the LAST exact frame: for the terminal settled palette
        # (which also matches the pre-logo boot black frames) this lands on a
        # post-sweep frame where the logo VRAM is loaded, not the black boot.
        chosen = exact[-1] if exact else best_f
        results.append((name, chosen, len(exact), best_m, exact[0] if exact else None,
                        exact[-1] if exact else None))
        # dump full surfaces at chosen frame
        full = c.call({"id": 5, "cmd": "get_frame", "frame": chosen, "include": "vram,cram,vsram"})
        if full.get("ok"):
            vdp = full["vdp"]
            pref = os.path.join(out_dir, "our_" + name)
            open(pref + ".vram.bin", "wb").write(bytes.fromhex(vdp["vram"]))
            open(pref + ".cram.json", "w").write(json.dumps(vdp["cram"]))
            open(pref + ".vsram.json", "w").write(json.dumps(vdp["vsram"]))
            open(pref + ".frame.txt", "w").write(str(chosen))

    print(f"{'ref':>10} {'our_frame':>9} {'exact?':>7} {'exactN':>6} {'range':>11} {'best/64':>7}")
    for name, chosen, nex, bestm, ef0, ef1 in results:
        rng = f"{ef0}-{ef1}" if nex else "-"
        print(f"{name:>10} {chosen:>9} {'YES' if nex else 'no':>7} {nex:>6} {rng:>11} {bestm:>7}")

if __name__ == "__main__":
    main()
