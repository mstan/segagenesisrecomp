#!/usr/bin/env python3
"""Input-only native menu/character startup checks in both donor chapters."""
import argparse
from pathlib import Path
from run_trilogy_campaign import run

ap=argparse.ArgumentParser()
for key in ('exe','rom','sonic1','sonic2','out'):ap.add_argument('--'+key,type=Path,required=True)
args=ap.parse_args()
for packs,stage in ((1,0x1000),(2,0x2000)):
    for character,name in enumerate(('sonic-tails','sonic','tails','knuckles')):
        run(args,f'{stage:04X}-{name}',packs,stage,character=character)
run(args,'no-save-ghz',3,0x1000,no_save=True)
run(args,'no-save-ehz-knuckles',2,0x2000,character=3,no_save=True)
