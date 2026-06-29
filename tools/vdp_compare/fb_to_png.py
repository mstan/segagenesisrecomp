#!/usr/bin/env python3
"""
fb_to_png.py - convert a BlastEm oracle framebuffer dump (<prefix>.fb.bgra +
<prefix>.fb.txt) to a PNG. pixel_t is ARGB32 (0xAARRGGBB) => little-endian
bytes are [B,G,R,A].

Usage: python fb_to_png.py <prefix> [out.png]
"""
import sys, numpy as np
from PIL import Image

def read_meta(p):
    m = {}
    for line in open(p + ".fb.txt"):
        if "=" in line:
            k, v = line.strip().split("=", 1)
            m[k] = v
    return m

def main():
    prefix = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else prefix + ".fb.png"
    m = read_meta(prefix)
    pitch = int(m["pitch_bytes"]); h = int(m["height"]); w = int(m["line_pixels"])
    raw = np.fromfile(prefix + ".fb.bgra", dtype=np.uint8)
    raw = raw[: pitch * h].reshape(h, pitch)
    px = raw[:, : w * 4].reshape(h, w, 4)  # B,G,R,A
    rgb = px[:, :, [2, 1, 0]]              # -> R,G,B
    Image.fromarray(rgb, "RGB").save(out)
    print(f"wrote {out} ({w}x{h})")

if __name__ == "__main__":
    main()
