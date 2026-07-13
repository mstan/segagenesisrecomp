import json, socket, sys, time
def q(cmd, **kw):
    s = socket.create_connection(('127.0.0.1', 4390), timeout=8)
    req = {"id": 1, "cmd": cmd}; req.update(kw)
    s.sendall((json.dumps(req)+"\n").encode())
    buf = b""
    while not buf.endswith(b"\n"):
        d = s.recv(1<<20)
        if not d: break
        buf += d
    s.close()
    return json.loads(buf)
dur = float(sys.argv[1]) if len(sys.argv) > 1 else 300
t0 = time.time()
n = 0
with open("scroll_sample_log.txt", "w") as f:
    f.write("# t frame mode hint r10 vsram0..3 E030 E280 hscroll_cells(A/B every 4th)\n")
    while time.time() - t0 < dur:
        try:
            fi = q("frame_info")["current_frame"]
            m = q("read_memory", addr="FFB002", size=2)["hex"]
            v = q("vdp_state")["vdp"]
            vs = q("read_vsram").get("vsram")[:4]
            e030 = q("read_memory", addr="FFE030", size=2)["hex"]
            e280 = q("read_memory", addr="FFE280", size=4)["hex"]
            hx = q("read_vram", addr="AC00", size=1024)["hex"]
            cells = ' '.join(hx[blk*64:blk*64+4]+'/'+hx[blk*64+4:blk*64+8] for blk in range(0,28,4))
            line = f"{time.time()-t0:7.1f} f={fi} m={m} hint={v['hint']} r10={v['hint_int']} vs={vs} E030={e030} E280={e280} | {cells}"
            f.write(line+"\n"); f.flush()
            n += 1
            if v['hint'] == 1 and n % 2 == 0:
                q("screenshot", path=f"hint_armed_{fi}.png")
        except Exception as e:
            f.write(f"ERR {e}\n"); f.flush()
        time.sleep(0.5)
print("done", n)
