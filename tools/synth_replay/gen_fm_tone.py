#!/usr/bin/env python3
"""Generate a synthetic chip_ring.txt with a single sustained FM tone on
channel 0 (algorithm 7 = all-carriers, loud), no DAC, no PSG. Used to validate
the Nuked-OPN2 integration in synth_replay against ymfm/clownmdemu on a clean
signal: all three synths receive the IDENTICAL register stream, so a low
ours-vs-nuked correlation here would mean an integration bug, not a real gap.

Format mirrors chip_trace.c:  f=<vint> sl=<line> FM p<0..3> $VV mc=<cyc> wf=<wf>
  p0/p2 = address (bank 0/1), p1/p3 = data.
"""
import sys

MASTER_PER_FRAME = 3420 * 262   # 896040 (matches synth_replay)
events = []
wf = 0
mc = 500

def w(addr, val):
    """Emit an FM bank-0 address+data pair at the current (wf, mc). Spacing is
    wide (>24 OPN2 clocks = 1008 master cycles between writes) so the die-accurate
    Nuked core has time to apply each register at its slot phase, mirroring a real
    SMPS driver's write cadence."""
    global mc
    events.append((wf, mc,     0, addr))  # p0 = address
    events.append((wf, mc+600, 1, val))   # p1 = data  (~14 OPN2 clocks later)
    mc += 1400                            # ~33 OPN2 clocks between pairs

# --- setup burst (wall frame 0) ---
w(0x22, 0x00)            # LFO off
w(0x27, 0x00)            # timer / ch3 normal mode
w(0x28, 0x00)            # key off all
w(0x2B, 0x00)            # DAC off
# channel 0 operators: regs at +0x00 (op1), +0x04 (op2), +0x08 (op3), +0x0C (op4)
for off in (0x00, 0x04, 0x08, 0x0C):
    w(0x30 + off, 0x01)  # DT=0 MUL=1
    w(0x40 + off, 0x00)  # TL=0 (loudest) — all carriers in algo 7
    w(0x50 + off, 0x1F)  # KS=0 AR=31 (instant attack)
    w(0x60 + off, 0x00)  # DR=0
    w(0x70 + off, 0x00)  # SR=0
    w(0x80 + off, 0x0F)  # SL=0 RR=15
    w(0x90 + off, 0x00)  # SSG-EG off
w(0xA4, 0x22)            # ch0 block=4, fnum high bits (0x2)
w(0xA0, 0x69)            # ch0 fnum low  -> ~440 Hz region
w(0xB0, 0x07)            # feedback=0, algorithm=7 (all 4 operators are carriers)
w(0xB4, 0xC0)            # L+R output enable
w(0x28, 0xF0)            # key ON ch0, all 4 operators

# --- sustain: one keep-alive write per frame so synth_replay advances time and
#     renders the held note (it only renders during inter-event gaps). ~1.0 s. ---
for f in range(1, 61):
    wf = f
    mc = 500
    events.append((wf, mc, 0, 0x22))     # harmless re-write of LFO reg (addr)
    events.append((wf, mc + 10, 1, 0x00))  # data

out = sys.argv[1] if len(sys.argv) > 1 else "tone_ring.txt"
with open(out, "w") as fp:
    for (wf_, mc_, port, val) in events:
        kind = "FM"
        fp.write(f"f={wf_} sl=0 FM p{port} ${val:02X} mc={mc_} wf={wf_} pcz=$0000\n")
print(f"wrote {len(events)} events to {out}")
