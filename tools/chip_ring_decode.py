#!/usr/bin/env python3
"""
chip_ring_decode.py — decode a chip_ring.txt dump in MIXER ORDER and answer
"what was the chip doing at wall frame N?" questions.

Mixer order (mirrors runner/audio/mixer.c exactly — the synth's authority):
  1. Events grouped per wall frame (wf=), the mixer's drain unit.
  2. FM pair guard: each FM ADDRESS write (p0/p2) inherits the stamp of its
     matching DATA write (next FM event on the same part), so no foreign event
     sorts between the halves of an (addr, data) pair.
  3. Stable sort by (mc, capture index) within the frame.
  4. The YM2612 address latch persists ACROSS frames (one latch per part).

Capture-order decoding is WRONG: the own backend pushes events from two stamp
axes (68K counter, raster cursor) so push order is not stamp order; decoding
the ring as-printed produces false register attributions. (See memory:
"mixer-order decode is mandatory".)

Subcommands:
  keyons  RING [--around WF ...] [--window N]
      List key-on/off ($28) events near the given wall frames (default: all),
      with the target channel's full decoded state at that moment.
  state   RING --at WF [--ch N]
      Replay to the START of wall frame WF and dump channel state (all
      channels, or one). Also prints global regs ($22 LFO, $27 ch3 mode, $2B DAC).
  events  RING --from WF --to WF [--ch N] [--reg HEX]
      Mixer-ordered event listing for a wall-frame window, decoded
      (channel/operator/register name), optionally filtered.
  summary RING [--from WF] [--to WF]
      Per-register-class write counts + key-on histogram per channel.

Ring line format (chip_trace.c):
  f=<vint> sl=<line> FM  p<0..3> $VV mc=<cycle> wf=<wall> pcz=$ZZZZ
  f=<vint> sl=<line> PSG $VV mc=<cycle> wf=<wall> pcz=$ZZZZ
"""
from __future__ import annotations
import argparse, re, sys
from collections import defaultdict

LINE_RE = re.compile(
    r"f=(\d+)\s+sl=(\d+)\s+(FM\s+p(\d)|PSG)\s+\$([0-9A-Fa-f]+)"
    r"\s+mc=(\d+)\s+wf=(\d+)\s+pcz=\$([0-9A-Fa-f]+)")

OP_NAMES = ["op1", "op2", "op3", "op4"]   # register-slot order: S1 S3 S2 S4
# Register-slot index -> operator number as musicians count them (1,3,2,4).
SLOT_TO_OPNUM = [1, 3, 2, 4]


def parse(path):
    """-> list of events in capture order:
    dict(vint, sl, kind, port, val, mc, wf, pcz, idx). FM port: 0..3."""
    evs = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for ln in f:
            m = LINE_RE.search(ln)
            if not m:
                continue
            is_fm = m.group(3).startswith("FM")
            evs.append({
                "vint": int(m.group(1)), "sl": int(m.group(2)),
                "kind": "FM" if is_fm else "PSG",
                "port": int(m.group(4)) if is_fm else -1,
                "val": int(m.group(5), 16),
                "mc": int(m.group(6)), "wf": int(m.group(7)),
                "pcz": int(m.group(8), 16),
                "idx": len(evs),
            })
    return evs


def mixer_order(evs):
    """Reorder capture-order events into mixer apply order (global list).
    Mirrors mixer.c: per-wf grouping, FM pair-guard stamp inheritance,
    stable (mc, idx) sort within the frame."""
    by_wf = defaultdict(list)
    for e in evs:
        by_wf[e["wf"]].append(e)
    out = []
    for wf in sorted(by_wf):
        frame = by_wf[wf]
        # Pair guard: address write inherits its data write's stamp.
        stamps = [e["mc"] for e in frame]
        for i, e in enumerate(frame):
            if e["kind"] == "FM" and e["port"] in (0, 2):
                for j in range(i + 1, len(frame)):
                    q = frame[j]
                    if q["kind"] != "FM":
                        continue
                    if q["port"] == e["port"] + 1:
                        stamps[i] = q["mc"]
                        break
                    if q["port"] == e["port"]:
                        break   # re-latched address; keep own stamp
        order = sorted(range(len(frame)), key=lambda k: (stamps[k], frame[k]["idx"]))
        out.extend(frame[k] for k in order)
    return out


class Ym2612State:
    """Register file reconstructed from the mixer-ordered write stream.
    One address latch per part, persistent across frames (matches the synth)."""

    def __init__(self):
        self.latch = {0: None, 1: None}     # part -> latched address
        self.glob = {}                      # global regs ($20-$2B), part-0 only
        # regs[part][addr] for $30-$B6 per-channel space
        self.regs = {0: {}, 1: {}}
        self.keyed = {}                     # ch (0-5) -> bool

    def write(self, port, val):
        part = port >> 1
        if (port & 1) == 0:
            self.latch[part] = val
            return None
        a = self.latch[part]
        if a is None:
            return None
        if part == 0 and 0x20 <= a <= 0x2B:
            self.glob[a] = val
            if a == 0x28:
                ch = (val & 3) + (3 if (val & 4) else 0)
                if (val & 3) != 3:
                    self.keyed[ch] = bool(val & 0xF0)
                return ("keyon", ch, val)
            return ("glob", a, val)
        if 0x30 <= a <= 0xB6:
            self.regs[part][a] = val
            return ("ch", part, a, val)
        return None

    # --- decoded views -------------------------------------------------
    def chan_regs(self, ch):
        """ch 0-5 -> (part, chan-within-part 0-2)."""
        part, c = (ch // 3), (ch % 3)
        return part, c

    def op_reg(self, ch, base, slot):
        part, c = self.chan_regs(ch)
        return self.regs[part].get(base + 4 * slot + c)

    def ch_reg(self, ch, base):
        part, c = self.chan_regs(ch)
        return self.regs[part].get(base + c)

    def describe_channel(self, ch):
        ln = []
        b0 = self.ch_reg(ch, 0xB0)
        b4 = self.ch_reg(ch, 0xB4)
        a4 = self.ch_reg(ch, 0xA4)
        a0 = self.ch_reg(ch, 0xA0)
        freq = ""
        if a4 is not None and a0 is not None:
            block = (a4 >> 3) & 7
            fnum = ((a4 & 7) << 8) | a0
            # YM2612: F = fnum * fsam / 2^20 * 2^(block-1); fsam = 53267 Hz
            hz = fnum * 53267.0 / (1 << 20) * (2 ** (block - 1)) if fnum else 0.0
            freq = f"block={block} fnum={fnum} (~{hz:.1f} Hz carrier)"
        alg = fb = ""
        if b0 is not None:
            alg = f"alg={b0 & 7} fb={(b0 >> 3) & 7}"
        pan = ""
        if b4 is not None:
            pan = (f"pan={'L' if b4 & 0x80 else ''}{'R' if b4 & 0x40 else ''}"
                   f" ams={(b4 >> 4) & 3} fms={b4 & 7}")
        ln.append(f"ch{ch}: keyed={self.keyed.get(ch)} {alg} {pan} {freq}")
        for slot in range(4):
            dt_mul = self.op_reg(ch, 0x30, slot)
            tl     = self.op_reg(ch, 0x40, slot)
            ks_ar  = self.op_reg(ch, 0x50, slot)
            am_dr  = self.op_reg(ch, 0x60, slot)
            sr     = self.op_reg(ch, 0x70, slot)
            sl_rr  = self.op_reg(ch, 0x80, slot)
            ssg    = self.op_reg(ch, 0x90, slot)
            f = lambda v, fmt: fmt.format(v) if v is not None else "--"
            ln.append(
                f"  {OP_NAMES[slot]} (S{SLOT_TO_OPNUM[slot]}):"
                f" dt={f(dt_mul, '{0:#04x}')} (mul={dt_mul & 15 if dt_mul is not None else '--'}"
                f" det={(dt_mul >> 4) & 7 if dt_mul is not None else '--'})"
                f" tl={f(tl, '{0:#04x}')}"
                f"{' [BIT7!]' if tl is not None and tl & 0x80 else ''}"
                f" ar={ks_ar & 31 if ks_ar is not None else '--'}"
                f" ks={(ks_ar >> 6) & 3 if ks_ar is not None else '--'}"
                f" dr={am_dr & 31 if am_dr is not None else '--'}"
                f" am={(am_dr >> 7) & 1 if am_dr is not None else '--'}"
                f" sr={sr & 31 if sr is not None else '--'}"
                f" sl={(sl_rr >> 4) & 15 if sl_rr is not None else '--'}"
                f" rr={sl_rr & 15 if sl_rr is not None else '--'}"
                f" ssg={f(ssg, '{0:#04x}')}"
                f"{' [SSG-ON!]' if ssg is not None and ssg & 8 else ''}")
        return "\n".join(ln)

    def describe_globals(self):
        g = self.glob
        f = lambda a: f"${g[a]:02X}" if a in g else "--"
        return (f"globals: $22(LFO)={f(0x22)} $27(ch3mode/timers)={f(0x27)}"
                f" $2A(DAC)={f(0x2A)} $2B(DACen)={f(0x2B)}")


def reg_class(a):
    if a < 0x30:
        return f"glob ${a:02X}"
    base = a & 0xF0
    names = {0x30: "DT/MUL", 0x40: "TL", 0x50: "KS/AR", 0x60: "AM/DR",
             0x70: "SR", 0x80: "SL/RR", 0x90: "SSG-EG"}
    if base in names:
        return names[base]
    if 0xA0 <= a <= 0xA2: return "FNUM-L"
    if 0xA4 <= a <= 0xA6: return "BLK/FNUM-H"
    if 0xA8 <= a <= 0xAE: return "ch3-supp"
    if 0xB0 <= a <= 0xB2: return "ALG/FB"
    if 0xB4 <= a <= 0xB6: return "PAN/AMS/FMS"
    return f"${a:02X}"


def replay(ordered, until_wf=None, on_event=None):
    """Replay mixer-ordered events into a Ym2612State. Stops before until_wf.
    on_event(e, decoded, state) is called per applied FM event."""
    st = Ym2612State()
    for e in ordered:
        if until_wf is not None and e["wf"] >= until_wf:
            break
        if e["kind"] != "FM":
            continue
        dec = st.write(e["port"], e["val"])
        if on_event:
            on_event(e, dec, st)
    return st


def cmd_keyons(args):
    evs = mixer_order(parse(args.ring))
    targets = args.around
    win = args.window
    hits = []

    def on_event(e, dec, st):
        if not dec or dec[0] != "keyon":
            return
        _, ch, val = dec
        if targets and not any(abs(e["wf"] - t) <= win for t in targets):
            return
        hits.append((e, ch, val, st.describe_channel(ch), st.describe_globals()))

    replay(evs, on_event=on_event)
    for e, ch, val, desc, glob in hits:
        kind = "KEY-ON " if val & 0xF0 else "KEY-OFF"
        print(f"wf={e['wf']} vint={e['vint']} sl={e['sl']} mc={e['mc']} "
              f"pcz=${e['pcz']:04X}  {kind} $28=${val:02X} slots={val >> 4:#x} -> ch{ch}")
        if val & 0xF0:
            print("  " + desc.replace("\n", "\n  "))
            print("  " + glob)
    print(f"\n{len(hits)} key events"
          + (f" within ±{win} frames of {targets}" if targets else ""))


def cmd_state(args):
    evs = mixer_order(parse(args.ring))
    st = replay(evs, until_wf=args.at)
    print(f"state at START of wf={args.at}:")
    print(st.describe_globals())
    for ch in ([args.ch] if args.ch is not None else range(6)):
        print(st.describe_channel(ch))


def cmd_events(args):
    evs = mixer_order(parse(args.ring))
    st = Ym2612State()
    shown = 0
    for e in evs:
        if e["wf"] < args.frm or e["wf"] > args.to:
            if e["kind"] == "FM" and e["wf"] < args.frm:
                st.write(e["port"], e["val"])   # keep latch/state warm
            continue
        if e["kind"] == "PSG":
            if args.ch is None and args.reg is None:
                print(f"wf={e['wf']} mc={e['mc']:>7} pcz=${e['pcz']:04X} PSG ${e['val']:02X}")
                shown += 1
            continue
        dec = st.write(e["port"], e["val"])
        if dec is None:
            continue
        if dec[0] == "keyon":
            _, ch, val = dec
            if args.ch is not None and ch != args.ch:
                continue
            print(f"wf={e['wf']} mc={e['mc']:>7} pcz=${e['pcz']:04X} "
                  f"$28=${val:02X} ({'ON' if val & 0xF0 else 'off'} ch{ch})")
        elif dec[0] == "glob":
            _, a, val = dec
            if args.ch is not None or (args.reg is not None and a != args.reg):
                continue
            print(f"wf={e['wf']} mc={e['mc']:>7} pcz=${e['pcz']:04X} "
                  f"glob ${a:02X}=${val:02X}")
        else:
            _, part, a, val = dec
            ch = part * 3 + (a & 3) if (a & 3) != 3 else None
            if ch is None:
                continue
            if args.ch is not None and ch != args.ch:
                continue
            if args.reg is not None and (a & 0xF0) != (args.reg & 0xF0):
                continue
            print(f"wf={e['wf']} mc={e['mc']:>7} pcz=${e['pcz']:04X} "
                  f"ch{ch} {reg_class(a)} ${a:02X}=${val:02X}")
        shown += 1
        if shown >= args.limit:
            print(f"... (limit {args.limit} reached)")
            break


MASTER_PER_FRAME = 3420 * 262  # 896040, must match genesis_machine.c
MASTER_PER_LINE = 3420


def cmd_psgpairs(args):
    """Tone-frequency LATCH->DATA pair-gap audit (SN76489 protocol).

    A 10-bit tone-frequency update is two writes: LATCH (bit7=1, t=0: new low
    nibble) then DATA (bit7=0: new high 6 bits). Between them the channel
    sounds at new-low + OLD-high — a wrong pitch. On hardware the two writes
    are microseconds apart; in the event-queue pipeline they are rendered
    [stamp_latch, stamp_data) apart, so a stamp gap or a frame-end straddle
    renders the wrong pitch audibly (the jump-SFX "boop" hypothesis).

    Reports every tone-freq latch->data pair whose RENDER gap exceeds
    --gap-lines scanlines, with the intermediate vs final frequency, using the
    same absolute timeline the mixer renders (wf*FRAME + mc, monotonic)."""
    evs = mixer_order(parse(args.ring))
    psg = [e for e in evs if e["kind"] == "PSG"]
    # absolute render timeline, monotonic (mirrors mixer apply + deferral)
    prev_abs = 0
    for e in psg:
        a = e["wf"] * MASTER_PER_FRAME + e["mc"]
        if a < prev_abs:
            a = prev_abs
        e["abs"] = a
        prev_abs = a
    # SN76489 register file: tone freq per channel (10-bit), latched register
    freq = [0, 0, 0]
    latched = None          # (ch, kind); kind: 'tone' | 'vol' | 'noise'
    pend = None             # pending tone latch: (event, ch, old_freq, new_low)
    gap_thresh = args.gap_lines * MASTER_PER_LINE
    tot = flagged = 0
    rows = []
    for e in psg:
        v = e["val"]
        if v & 0x80:
            ch = (v >> 5) & 3
            is_vol = bool(v & 0x10)
            if pend is not None:
                pend = None     # tone latch never completed by a data byte
            if not is_vol and ch < 3:
                old = freq[ch]
                freq[ch] = (freq[ch] & 0x3F0) | (v & 0x0F)
                latched = (ch, "tone")
                pend = (e, ch, old, freq[ch])
                tot += 1
            else:
                latched = (ch, "vol" if is_vol else "noise")
        else:
            if latched and latched[1] == "tone":
                ch = latched[0]
                freq[ch] = (freq[ch] & 0x00F) | ((v & 0x3F) << 4)
                if pend is not None and pend[1] == ch:
                    le, _, old, mid = pend
                    gap = e["abs"] - le["abs"]
                    if gap > gap_thresh:
                        flagged += 1
                        def hz(f):
                            return 3579545 / (32.0 * f) if f else 0.0
                        rows.append((le, e, gap, mid, freq[ch], hz(mid), hz(freq[ch])))
                    pend = None
            # data after vol/noise latch: atomic, ignore
    for le, de, gap, mid, fin, hmid, hfin in rows:
        straddle = " FRAME-STRADDLE" if de["wf"] != le["wf"] else ""
        print(f"latch wf={le['wf']} mc={le['mc']:>7} -> data wf={de['wf']} "
              f"mc={de['mc']:>7}  gap={gap} cyc ({gap/MASTER_PER_LINE:.1f} lines, "
              f"{gap/896040*16.7:.2f} ms)  intermediate={mid} (~{hmid:.0f} Hz) "
              f"final={fin} (~{hfin:.0f} Hz){straddle}")
    print(f"\n{tot} tone-freq latch->data pairs; {flagged} with render gap > "
          f"{args.gap_lines} lines")


def cmd_summary(args):
    evs = mixer_order(parse(args.ring))
    st = Ym2612State()
    classes = defaultdict(int)
    keyons = defaultdict(int)
    wfs = set()
    for e in evs:
        if args.frm is not None and e["wf"] < args.frm:
            if e["kind"] == "FM":
                st.write(e["port"], e["val"])
            continue
        if args.to is not None and e["wf"] > args.to:
            break
        wfs.add(e["wf"])
        if e["kind"] != "FM":
            classes["PSG"] += 1
            continue
        dec = st.write(e["port"], e["val"])
        if not dec:
            continue
        if dec[0] == "keyon":
            _, ch, val = dec
            if val & 0xF0:
                keyons[ch] += 1
            classes["$28 keyon/off"] += 1
        elif dec[0] == "glob":
            classes[f"glob ${dec[1]:02X}"] += 1
        else:
            classes[reg_class(dec[2])] += 1
    span = f"{min(wfs)}..{max(wfs)}" if wfs else "(none)"
    print(f"wall frames covered: {span} ({len(wfs)} frames with events)")
    for k in sorted(classes, key=lambda k: -classes[k]):
        print(f"  {k:>14}: {classes[k]}")
    print("key-ons per channel:", dict(sorted(keyons.items())))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    k = sub.add_parser("keyons")
    k.add_argument("ring")
    k.add_argument("--around", type=int, nargs="*", default=[])
    k.add_argument("--window", type=int, default=8)
    k.set_defaults(fn=cmd_keyons)

    s = sub.add_parser("state")
    s.add_argument("ring")
    s.add_argument("--at", type=int, required=True)
    s.add_argument("--ch", type=int)
    s.set_defaults(fn=cmd_state)

    ev = sub.add_parser("events")
    ev.add_argument("ring")
    ev.add_argument("--from", dest="frm", type=int, required=True)
    ev.add_argument("--to", type=int, required=True)
    ev.add_argument("--ch", type=int)
    ev.add_argument("--reg", type=lambda x: int(x, 16))
    ev.add_argument("--limit", type=int, default=400)
    ev.set_defaults(fn=cmd_events)

    pp = sub.add_parser("psgpairs")
    pp.add_argument("ring")
    pp.add_argument("--gap-lines", type=float, default=1.0,
                    help="flag pairs whose render gap exceeds this many scanlines")
    pp.set_defaults(fn=cmd_psgpairs)

    sm = sub.add_parser("summary")
    sm.add_argument("ring")
    sm.add_argument("--from", dest="frm", type=int)
    sm.add_argument("--to", type=int)
    sm.set_defaults(fn=cmd_summary)

    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
