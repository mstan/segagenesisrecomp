import json, socket, time, sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 4390

def q(cmd, **kw):
    s = socket.create_connection(('127.0.0.1', PORT), timeout=10)
    req = {"id": 1, "cmd": cmd}; req.update(kw)
    s.sendall((json.dumps(req)+"\n").encode())
    buf = b""
    while not buf.endswith(b"\n"):
        d = s.recv(1<<20)
        if not d: break
        buf += d
    s.close()
    return json.loads(buf)

def rd16(a): return q("read_memory", addr=a, size=2)["hex"]

def tap(mask, hold=0.3):
    q("set_input", keys=mask); time.sleep(hold); q("set_input", keys="00")

def nav_to_game(deadline_s=300):
    """Natural navigation: title -> selector(0) -> start. No WRAM writes."""
    deadline = time.time() + deadline_s
    while time.time() < deadline:
        m, sm, live = rd16("FFB002"), rd16("FFB004"), rd16("FFB194")
        if m == "0001" and sm == "0001":
            tap("80"); time.sleep(0.4)
        elif m == "0001" and sm == "0004":
            if rd16("FFB010") != "0000":
                tap("01", 0.2); time.sleep(0.3)
            else:
                tap("80"); time.sleep(0.6)
        elif m == "0004" and live == "0000":
            return True
        elif m == "0007" and live == "0000":
            return True
        else:
            time.sleep(0.4)
    return False

def wait_hud(timeout=120):
    """Wait until display on and gameplay visually up (mode 4 live)."""
    end = time.time() + timeout
    while time.time() < end:
        v = q("vdp_state")["vdp"]
        if v["display"] == 1 and rd16("FFB194") == "0000":
            return True
        time.sleep(1)
    return False

def hscroll_table(base=0xAC00):
    hexs = ""
    for off in range(0, 0x380, 0x100):
        hexs += q("read_vram", addr=f"{base+off:X}", size=0x100)["hex"]
    return bytes.fromhex(hexs)

def strips(data):
    out = []
    for strip in range(28):
        o = strip*8*4
        a = int.from_bytes(data[o:o+2],'big'); b = int.from_bytes(data[o+2:o+4],'big')
        out.append((a,b))
    return out

def full_wram():
    buf = b""
    for off in range(0, 0x10000, 0x1000):
        buf += bytes.fromhex(q("read_ram", addr=f"{off:X}", size=0x1000)["hex"])
    return buf

if __name__ == "__main__":
    print(q("frame_info"))
