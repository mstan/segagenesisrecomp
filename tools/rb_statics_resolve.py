#!/usr/bin/env python3
"""rb_statics_resolve.py - map GENESIS_RB_PROBE_STATICS runs to symbols.

  tools/rb_statics_resolve.py <exe> <stderr.log> [--pass N] [--allow FILE]

Reads the "rb_probe: statics pass=N data=.. bss=.." header and the run lines
that follow it, relocates them against `nm -S` of the (PIE) executable, and
prints one line per symbol that the restore did NOT put back. Symbols listed
in the allow file (one regex per line; default tools/rb_statics_allow.txt)
are diagnostics that are expected to differ and are shown separately; every
other symbol is a CARRIER CANDIDATE: state a tick reads that the snapshot
does not restore.
"""
import argparse, bisect, os, re, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("exe"); ap.add_argument("log")
ap.add_argument("--pass", dest="pno", type=int, default=0, help="0 = first pass with runs")
ap.add_argument("--allow", default=os.path.join(os.path.dirname(__file__), "rb_statics_allow.txt"))
a = ap.parse_args()

syms = []
for ln in subprocess.run(["nm", "-S", "--defined-only", a.exe], capture_output=True, text=True).stdout.splitlines():
    p = ln.split()
    if len(p) == 4 and p[2] in "bBdDgGsS":
        syms.append((int(p[0], 16), int(p[1], 16), p[3]))
    elif len(p) == 3 and p[2] == "__data_start":
        pass
syms.sort()
addrs = [s[0] for s in syms]
nm_data = next((int(l.split()[0], 16) for l in subprocess.run(["nm", a.exe], capture_output=True, text=True).stdout.splitlines() if l.endswith(" __data_start")), None)

hdr = re.compile(r"rb_probe: statics pass=(\d+) data=(0x[0-9a-f]+) bss=(0x[0-9a-f]+)")
run = re.compile(r"rb_probe:\s+\.(data|bss )\s+(0x[0-9a-f]+) \+(\d+)")
cur = None; base = None; runs = []
for ln in open(a.log, errors="replace"):
    m = hdr.search(ln)
    if m:
        if cur is not None and runs and (a.pno == 0 or cur == a.pno):
            break
        cur = int(m[1]); runs = []
        base = int(m[2], 16) - nm_data
        continue
    m = run.search(ln)
    if m and cur is not None:
        runs.append((int(m[2], 16) - base, int(m[3])))
if not runs:
    print("no runs found"); sys.exit(1)

allow = []
if os.path.exists(a.allow):
    allow = [re.compile(l.strip()) for l in open(a.allow) if l.strip() and not l.startswith("#")]
hits = {}
for addr, n in runs:
    i = bisect.bisect_right(addrs, addr) - 1
    while n > 0:
        if i < 0 or not (syms[i][0] <= addr < syms[i][0] + max(syms[i][1], 1)):
            nxt = addrs[i + 1] if i + 1 < len(addrs) else addr + n
            key = f"<gap after {syms[i][2] if i >= 0 else '?'}>"; step = max(1, min(n, nxt - addr))
        else:
            key = syms[i][2]; step = min(n, syms[i][0] + syms[i][1] - addr)
        hits[key] = hits.get(key, 0) + step
        addr += step; n -= step
        i = bisect.bisect_right(addrs, addr) - 1
carriers = {k: v for k, v in hits.items() if not any(r.search(k) for r in allow)}
print(f"pass {cur}: {len(runs)} runs, {len(hits)} symbols, {len(carriers)} carrier candidates")
for k, v in sorted(carriers.items(), key=lambda kv: -kv[1]):
    print(f"  CARRIER? {k:50s} {v} bytes")
for k, v in sorted(hits.items(), key=lambda kv: -kv[1]):
    if k not in carriers:
        print(f"  diag     {k:50s} {v} bytes")
