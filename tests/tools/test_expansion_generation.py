"""Discover, dispatch and charge instructions throughout expansion ROM."""
from pathlib import Path
import argparse,struct,subprocess,tempfile

ap=argparse.ArgumentParser();ap.add_argument('--recompiler',type=Path,required=True);a=ap.parse_args()
with tempfile.TemporaryDirectory(prefix='genesis-expansion-') as directory:
    root=Path(directory);(root/'generated').mkdir();rom=bytearray(b'\xff'*0x800000)
    rom[:0x100]=bytes(0x100);struct.pack_into('>II',rom,0,0xFFFE00,0x400000)
    rom[0x100:0x110]=b'SEGA GENESIS    '
    # JSR into the final word of the supported window, then return.
    rom[0x400000:0x400008]=bytes.fromhex('4EB9 007FFFFE 4E75')
    rom[0x7FFFFE:]=bytes.fromhex('4E75');(root/'expansion.bin').write_bytes(rom)
    (root/'game.toml').write_text('''[game]
output_prefix="expansion"
[functions]
extra=[0x400000,0x7FFFFE]
[ram_layout]
game_mode=0
vint_runcount=0
vint_routine=0
plc_pending=0
initial_ssp=0xFFFE00
vbla_stack=0
intr_stack=0
player_object=0
level_modes=[]
''')
    run=subprocess.run([str(a.recompiler.resolve()),'expansion.bin','--game','game.toml','--output-dir',str(root/'generated')],cwd=root,capture_output=True,text=True)
    if run.returncode:raise AssertionError(run.stdout+run.stderr)
    dispatch=(root/'generated/expansion_dispatch.c').read_text()
    assert '0x400000u, func_400000' in dispatch
    assert '0x7FFFFEu, func_7FFFFE' in dispatch
    cycles=(root/'generated/expansion_cycles.c').read_text()
    assert '{0x400000u,20}' in cycles and '{0x7FFFFEu,16}' in cycles
    with (root/'expansion.bin').open('ab') as f:f.write(b'\x4e\x75')
    bad=subprocess.run([str(a.recompiler.resolve()),'expansion.bin','--game','game.toml'],cwd=root,capture_output=True,text=True)
    assert bad.returncode and 'ROM too large' in bad.stderr
print('PASS expansion-ROM native entries and instruction costs through $7FFFFE; oversize input rejected')
