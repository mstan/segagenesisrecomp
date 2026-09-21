#!/usr/bin/env python3
"""Create isolated native-menu starter saves, then verify every playtest slot.
Never reads or replaces the user's normal save file.
"""
import argparse
from pathlib import Path
import subprocess
from run_trilogy_campaign import run

ap=argparse.ArgumentParser()
for key in ('exe','rom','sonic1','sonic2','out','seed_tool'):ap.add_argument('--'+key.replace('_','-'),type=Path,required=True)
args=ap.parse_args();seed=None
characters=(0,0,0,0,1,2,3,3)
packs=(3,3,3,2,3,3,3,2)
for slot in range(8):
    seed=run(args,f'create-{slot+1}',packs[slot],0x2000 if packs[slot]==2 else 0x1000,
             seed,character=characters[slot],slot=slot,creating=True)
path=args.out.resolve()/'playtest-starter.srm';path.write_bytes(seed)
subprocess.run([str(args.seed_tool.resolve()),str(path)],check=True)
seed=path.read_bytes()
for slot,stage in enumerate((0x1000,0x1001,0x1002,0x2000,0x1000,0x1000,0x1000,0x2000)):
    run(args,f'verify-{slot+1}',3,stage,seed,character=characters[slot],slot=slot)
print(f'PASS: all eight playtest slots launch from the native menu: {path}')
