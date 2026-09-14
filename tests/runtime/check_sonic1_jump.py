#!/usr/bin/env python3
"""Check a jump-run chip/WRITEDUMP capture for the stopped-SFX note restart.

The ROM's jump is PSG1 F2 for 5 ticks, then Bb2 + modulation for 21 ticks.
At tick 26 cfStopTrack must silence it; a full-volume Bb2 re-latch at that
tick is the regression. Requires at least three exercised jumps.
"""
import argparse
from pathlib import Path
import re

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("capture", type=Path)
    args = ap.parse_args()
    events = []
    pattern = re.compile(r"PSG \$([0-9A-F]{2}).*?wf=(\d+)")
    for line in args.capture.read_text().splitlines():
        match = pattern.search(line)
        if match: events.append((int(match[2]),int(match[1],16)))
    starts, restarts = [], []
    for i in range(2,len(events)):
        values = [events[j][1] for j in range(i-2,i+1)]
        frame = events[i][0]
        if values == [0x80,0x14,0x90] and frame > 1100:
            starts.append(frame)
        if values == [0x8F,0x0E,0x90] and any(frame == start+26 for start in starts):
            restarts.append(frame)
    print(f"Jump starts: {starts}; unwanted restarts at effect end: {restarts}")
    if len(starts)<3: raise SystemExit("Insufficient jump coverage (need at least 3)")
    if restarts: raise SystemExit(1)
    print("PASS: jump effects stop without reopening the last note")

if __name__ == "__main__": main()
