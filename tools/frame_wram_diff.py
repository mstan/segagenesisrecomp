#!/usr/bin/env python3
"""frame_wram_diff.py — wall-aligned WRAM diff between two running builds
(native vs oracle) using the always-on Tier-2 frame ring.

For each wall frame in [--lo, --hi], fetches the full 64 KB WRAM snapshot
from BOTH debug servers (`get_frame` + include="wram", hex payload) and
reports differing bytes inside [--range-lo, --range-hi). This is the
first-divergence instrument for "which RAM forked, and when" questions
(e.g. sound-driver state at an SFX tick): both games run free; the probe
queries the rings backward — no pause, no stepping.

Wall alignment is only valid for script-driven runs (input lands at the
same wall frame on both sides). For attract/demo content the backends'
vint phase differs — align on game events instead.

Usage:
  python tools/frame_wram_diff.py --lo 1290 --hi 1310 \
         [--porta 4378] [--portb 4379] [--range-lo 0xF000] [--range-hi 0xF740]
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

def wram(sock, frame):
    r = rpc(sock, 'get_frame', frame=frame, include='wram')
    if not r.get('ok'):
        raise RuntimeError('get_frame %d: %s' % (frame, r.get('error')))
    return bytes.fromhex(r['wram'])

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--porta', type=int, default=4378)
    ap.add_argument('--portb', type=int, default=4379)
    ap.add_argument('--lo', type=int, required=True, help='first wall frame')
    ap.add_argument('--hi', type=int, required=True, help='last wall frame')
    ap.add_argument('--offset', type=int, default=0,
                    help='side-B wall offset (B frame = A frame + offset)')
    ap.add_argument('--range-lo', type=lambda s: int(s, 0), default=0x0000)
    ap.add_argument('--range-hi', type=lambda s: int(s, 0), default=0x10000)
    ap.add_argument('--max-print', type=int, default=32)
    args = ap.parse_args()

    a = socket.create_connection(('127.0.0.1', args.porta), timeout=30)
    b = socket.create_connection(('127.0.0.1', args.portb), timeout=30)
    print('A:', {k: v for k, v in rpc(a, 'frame_info').items() if 'frame' in k})
    print('B:', {k: v for k, v in rpc(b, 'frame_info').items() if 'frame' in k})
    prev = None
    for wf in range(args.lo, args.hi + 1):
        try:
            wa = wram(a, wf)
            wb = wram(b, wf + args.offset)
        except RuntimeError as e:
            print('wf %d: %s' % (wf, e))
            continue
        diffs = [(off, wa[off], wb[off])
                 for off in range(args.range_lo, args.range_hi)
                 if wa[off] != wb[off]]
        jump = '   <<< JUMP' if prev is not None and len(diffs) > prev + 4 else ''
        print('wf %d vs %d: %d diffs%s' % (wf, wf + args.offset, len(diffs), jump))
        if diffs and (jump or prev == 0):
            for off, av, bv in diffs[:args.max_print]:
                print('   $FF%04X: A=%02X B=%02X' % (off, av, bv))
        prev = len(diffs)
    a.close(); b.close()

if __name__ == '__main__':
    main()
