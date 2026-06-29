#!/usr/bin/env python3
"""
match_vram.py - content-lock OUR runner to an oracle frame by exact VRAM hash,
scanning the always-on ring. VRAM is the strongest cross-binary content key for
a dynamic scene: an exact 64KB VRAM match pins the exact animation phase (sprite
frame + scroll tile state), so CRAM/VSRAM/framebuffer can then be compared
apples-to-apples. Falls back to the best partial match (reports % identical).

Usage:
  python match_vram.py <port> <out_dir> <oracle_prefix1> [oracle_prefix2 ...]
  (each oracle_prefix has .vram.bin)
"""
import socket, sys, json, hashlib, os, time

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
    port = int(sys.argv[1]); out_dir = sys.argv[2]; prefixes = sys.argv[3:]
    os.makedirs(out_dir, exist_ok=True)
    cur = cmd(port, {"id": 1, "cmd": "ping"}).get("frame", 0)
    lo = max(1, cur - 590)
    print(f"[match] runner frame {cur}; scanning ring [{lo}..{cur}] for VRAM")
    # snapshot all ring VRAMs once (coherent-ish; one pass)
    ring = {}
    for f in range(lo, cur + 1):
        r = cmd(port, {"id": 2, "cmd": "get_frame", "frame": f, "include": "vram"})
        if r.get("ok"):
            ring[f] = bytes.fromhex(r["vdp"]["vram"])
    print(f"[match] captured {len(ring)} ring VRAMs")
    md5map = {hashlib.md5(v).hexdigest(): f for f, v in ring.items()}
    for pref in prefixes:
        name = os.path.basename(pref)
        ov = open(pref + ".vram.bin", "rb").read()
        om = hashlib.md5(ov).hexdigest()
        exact_f = md5map.get(om)
        if exact_f is None:
            # best partial
            best_f, best_eq = None, -1
            for f, v in ring.items():
                eq = sum(1 for a, b in zip(v, ov) if a == b)
                if eq > best_eq:
                    best_eq, best_f = eq, f
            pct = 100.0 * best_eq / len(ov)
            print(f"[match] {name}: NO exact VRAM; best frame {best_f} {pct:.3f}% bytes equal")
            chosen = best_f
        else:
            print(f"[match] {name}: EXACT VRAM match @ our frame {exact_f}")
            chosen = exact_f
        full = cmd(port, {"id": 3, "cmd": "get_frame", "frame": chosen, "include": "vram,cram,vsram"})
        if full.get("ok"):
            v = full["vdp"]
            op = os.path.join(out_dir, "our_" + name)
            open(op + ".vram.bin", "wb").write(bytes.fromhex(v["vram"]))
            open(op + ".cram.json", "w").write(json.dumps(v["cram"]))
            open(op + ".vsram.json", "w").write(json.dumps(v["vsram"]))
            open(op + ".frame.txt", "w").write(str(chosen))

if __name__ == "__main__":
    main()
