#!/usr/bin/env python3
# [CHIP-TRACE] TEMP diagnostic — strip before commit (PRINCIPLES #18).
#
# Splits S1's "part of the music is off" (#1) into tempo (A) vs synth (B)
# from the two always-on ring dumps produced by F12:
#   snd_ring.txt  — sound-command lifecycle + VINT-ASSERT/VINT-ACCEPT phase
#   chip_ring.txt — raw FM port/value + PSG byte stream
#
# Decision rule:
#   * V-int ACCEPTED ~1x/frame and steady musical writes  -> tempo input OK -> synth bug (B)
#   * V-int accepts irregular / VASSERT pending_was=1 spikes -> sticky-V-int stacking -> tempo bug (A)
#
# Usage:  python chip_trace_analyze.py [DIR]    (DIR defaults to cwd; expects the two .txt files)
import sys, os, re
from collections import Counter, defaultdict

d = sys.argv[1] if len(sys.argv) > 1 else "."
snd = os.path.join(d, "snd_ring.txt")
chip = os.path.join(d, "chip_ring.txt")

# ---- snd_ring: V-int cadence (the tempo input to the SMPS driver) ----
accepts_per_frame = Counter()
asserts_per_frame = Counter()
sticky = 0          # VASSERT with pending_was=1 -> a prior frame's V-int was never taken
frames_seen = set()
if os.path.exists(snd):
    for ln in open(snd, encoding="utf-8", errors="replace"):
        m = re.match(r"f=(\d+)", ln)
        if not m: continue
        f = int(m.group(1)); frames_seen.add(f)
        if "VINT-ACCEPT" in ln: accepts_per_frame[f] += 1
        elif "VINT-ASSERT" in ln:
            asserts_per_frame[f] += 1
            mw = re.search(r"pending_was=(\d)", ln)
            if mw and mw.group(1) == "1": sticky += 1
    fr = sorted(frames_seen)
    if fr:
        span = fr[-1] - fr[0] + 1
        tot_acc = sum(accepts_per_frame.values())
        tot_asr = sum(asserts_per_frame.values())
        missed = [f for f in range(fr[0], fr[-1]+1) if accepts_per_frame.get(f,0) == 0]
        doubled = [f for f in range(fr[0], fr[-1]+1) if accepts_per_frame.get(f,0) > 1]
        print(f"== V-INT (tempo input) over frames {fr[0]}..{fr[-1]} ({span} frames) ==")
        print(f"  asserts/frame avg = {tot_asr/span:.3f}   accepts/frame avg = {tot_acc/span:.3f}  (want ~1.000)")
        print(f"  frames with 0 accepts (DROPPED tick): {len(missed)}  {missed[:20]}{'...' if len(missed)>20 else ''}")
        print(f"  frames with >1 accept (catch-up):    {len(doubled)} {doubled[:20]}{'...' if len(doubled)>20 else ''}")
        print(f"  VASSERT pending_was=1 (sticky stack-up): {sticky}")
        verdict = "TEMPO OK (input steady)" if (not missed and sticky == 0) else "TEMPO SUSPECT (A): V-int ticks irregular"
        print(f"  --> {verdict}")
else:
    print(f"(no {snd})")

# ---- chip_ring: musical-event + DAC cadence ----
print()
if os.path.exists(chip):
    fm_keyon = defaultdict(int)   # FM p0 $28 writes per frame (key on/off = note events)
    dac_addr = defaultdict(int)   # FM p0 $2A (DAC address latch) per frame
    fm_writes = Counter(); psg_writes = Counter()
    frames = set()
    last_fm_addr = {0: None, 2: None}
    for ln in open(chip, encoding="utf-8", errors="replace"):
        m = re.match(r"f=(\d+) sl=(\d+) (FM|PSG)(?:\s+p(\d) )?\s*\$([0-9A-Fa-f]+)", ln)
        if not m: continue
        f = int(m.group(1)); kind = m.group(3); frames.add(f)
        if kind == "PSG":
            psg_writes[f] += 1
        else:
            port = int(m.group(4)); val = int(m.group(5), 16); fm_writes[f] += 1
            if port in (0, 2):
                if val == 0x28: fm_keyon[f] += 1
                if val == 0x2A: dac_addr[f] += 1
    fr = sorted(frames)
    if fr:
        span = fr[-1] - fr[0] + 1
        print(f"== CHIP writes over frames {fr[0]}..{fr[-1]} ({span} frames) ==")
        print(f"  FM writes/frame  avg = {sum(fm_writes.values())/span:.1f}")
        print(f"  PSG writes/frame avg = {sum(psg_writes.values())/span:.1f}")
        print(f"  key-on($28)/frame avg = {sum(fm_keyon.values())/span:.2f}  (note-event rate)")
        print(f"  DAC($2A latch)/frame avg = {sum(dac_addr.values())/span:.1f}  (drum playback)")
        # per-frame stability: coefficient of variation of FM writes
        vals = [fm_writes.get(f,0) for f in range(fr[0], fr[-1]+1)]
        mean = sum(vals)/len(vals)
        var = sum((v-mean)**2 for v in vals)/len(vals)
        cv = (var**0.5)/mean if mean else 0
        print(f"  FM writes/frame stability: mean={mean:.1f} cv={cv:.2f} (low cv = steady tempo)")
else:
    print(f"(no {chip})")

# ---- SFX correlation: per $1FFF deposit, the chip writes that follow ----
# Sonic 1's driver consumes a command from Z80-RAM $1FFF, then emits the SFX's
# FM/PSG writes. If a deposited command yields few/no chip writes -> driver-side
# drop (C/D). If it yields a full burst but the sound is still quiet/partial ->
# synth/render (E). Correlate by frame across the two ring files.
print()
deposits = []   # (frame, newval)
if os.path.exists(snd):
    for ln in open(snd, encoding="utf-8", errors="replace"):
        if "68KW" in ln and "z80[$1FFF]" in ln:
            mf = re.match(r"f=(\d+)", ln); mv = re.search(r"->\$([0-9A-Fa-f]+)", ln)
            if mf and mv: deposits.append((int(mf.group(1)), mv.group(1).upper()))
# chip writes per frame (FM total, PSG total, key-on) for correlation
fmpf = defaultdict(int); psgpf = defaultdict(int); keyonpf = defaultdict(int)
chip_frames = set()
if os.path.exists(chip):
    for ln in open(chip, encoding="utf-8", errors="replace"):
        m = re.match(r"f=(\d+) sl=\d+ (FM|PSG)(?:\s+p(\d) )?\s*\$([0-9A-Fa-f]+)", ln)
        if not m: continue
        f = int(m.group(1)); chip_frames.add(f)
        if m.group(2) == "PSG": psgpf[f] += 1
        else:
            fmpf[f] += 1
            if int(m.group(3)) in (0,2) and int(m.group(4),16) == 0x28: keyonpf[f] += 1
if deposits and chip_frames:
    lo, hi = min(chip_frames), max(chip_frames)
    inrange = [(f,v) for (f,v) in deposits if lo <= f <= hi-6]
    print(f"== SFX correlation: {len(inrange)} deposits within chip coverage (frames {lo}..{hi}) ==")
    print(f"   {'cmd':>4} {'frame':>6}  FM[f..f+5]  PSG[f..f+5]  keyon[f..f+5]")
    for (f,v) in inrange[:40]:
        fm5  = sum(fmpf.get(f+k,0)  for k in range(6))
        psg5 = sum(psgpf.get(f+k,0) for k in range(6))
        ko5  = sum(keyonpf.get(f+k,0) for k in range(6))
        flag = "  <-- LOW (suspect drop)" if fm5 < 50 and psg5 < 5 else ""
        print(f"   ${v:>3} {f:>6}  {fm5:>9}  {psg5:>10}  {ko5:>11}{flag}")
else:
    print("(no overlap between deposits and chip coverage — need a capture window containing the SFX)")

