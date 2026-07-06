#!/usr/bin/env python3
"""ring_window_dump.py — pull a wall-frame window from a paged always-on ring
(native Tier-1 `rdb_dump` or oracle Tier-3 `t3_dump`) into a JSONL file.

Both dumps page over a snapshot with {start, count} and return entries
stamped with a wall frame `f`. This tool binary-searches the ring for the
first entry at --lo, then streams entries through --hi to --out. The rings
are always-on; the probe runs against a live process and reads backward.

t3 note: arm the capture filter early (right after boot) to stretch the
1M-entry ring across more frames, e.g. the sound-driver PC band:
  tcp> {"cmd":"t3_range","lo":0x071000,"hi":0x074000}

Usage:
  python tools/ring_window_dump.py --port 4379 --cmd t3_dump \
         --lo 1290 --hi 1310 --out orc_t3.jsonl
  python tools/ring_window_dump.py --port 4378 --cmd rdb_dump \
         --lo 1290 --hi 1310 --out nat_rdb.jsonl
"""
import argparse, json, socket

def rpc(sock, cmd, **kw):
    sock.sendall((json.dumps({'cmd': cmd, 'id': 1, **kw}) + '\n').encode())
    buf = b''
    while b'\n' not in buf:
        ch = sock.recv(1 << 22)
        if not ch:
            raise RuntimeError(cmd + ': connection closed')
        buf += ch
    return json.loads(buf.split(b'\n', 1)[0].decode())

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port', type=int, required=True)
    ap.add_argument('--cmd', choices=['rdb_dump', 't3_dump'], required=True)
    ap.add_argument('--lo', type=int, required=True, help='first wall frame')
    ap.add_argument('--hi', type=int, required=True, help='last wall frame')
    ap.add_argument('--out', required=True)
    args = ap.parse_args()

    s = socket.create_connection(('127.0.0.1', args.port), timeout=60)

    def entry_frame(idx):
        r = rpc(s, args.cmd, start=idx, count=1)
        log = r.get('log') or []
        return (log[0]['f'] if log else None), r.get('total', 0)

    _, total = entry_frame(0)
    if not total:
        raise SystemExit('ring empty (is the tier compiled into this build?)')
    lo, hi = 0, total - 1
    while lo < hi:
        mid = (lo + hi) // 2
        f, _ = entry_frame(mid)
        if f is None or f < args.lo:
            lo = mid + 1
        else:
            hi = mid
    start, n = lo, 0
    with open(args.out, 'w') as out:
        while start < total:
            r = rpc(s, args.cmd, start=start, count=50000)
            log = r.get('log') or []
            if not log:
                break
            stop = False
            for e in log:
                if e['f'] > args.hi:
                    stop = True
                    break
                if e['f'] >= args.lo:
                    out.write(json.dumps(e) + '\n')
                    n += 1
            start += len(log)
            if stop:
                break
    print('%s: %d entries (frames %d..%d) -> %s' %
          (args.cmd, n, args.lo, args.hi, args.out))
    s.close()

if __name__ == '__main__':
    main()
