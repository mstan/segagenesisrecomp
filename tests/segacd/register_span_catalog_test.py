"""Exact canonical-body proof, first-priority metadata and pure-HLE refusal."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
import native_catalog_compact as compact
import native_register_spans as spans

with tempfile.TemporaryDirectory(prefix='register-span-catalog-') as temp:
    root=Path(temp);original=root/'original';original.mkdir()
    image=root/'fixture.bin';image.write_bytes(bytes.fromhex('700172024e75'))
    subprocess.run([sys.argv[1],str(image),str(original),'fixture','0','0'],check=True)
    catalogs=root/'catalogs.txt';catalogs.write_bytes(f'MAIN|{original.as_posix()}|fixture\n'.encode())
    parent=root/'parent';compact.compact(catalogs,parent)
    output=root/'spans';spans.generate(parent,output,Path(sys.argv[2]))
    manifest=json.loads((output/'native_catalog_manifest.json').read_bytes())
    assert manifest['canonical_ordinary_bodies_verified']==2
    assert 'genesis_spans_main' in (output/'native_register_span_registry.h').read_text()
    assert 'main_effect' not in (output/'native_register_span_registry.h').read_text()
    first=(original/'fixture_part00.c').read_bytes()
    assert b'void fixture_at_000000(M68kStepContext *ctx){\n' in first
    (original/'fixture_part00.c').write_bytes(first.replace(b'void fixture_at_000000(M68kStepContext *ctx){\n',b'void fixture_at_000000(M68kStepContext *ctx){\n  g_cpu.D[7]++;\n'))
    hle=root/'pure-hle';compact.compact(catalogs,hle)
    try:
        spans.generate(hle,root/'refused-hle',Path(sys.argv[2]))
        raise AssertionError('pure HLE arithmetic was silently replaced from opcode')
    except ValueError as error:
        assert 'noncanonical pure operation body' in str(error),error
    assert not (root/'refused-hle/native_catalog_manifest.json').exists()
    path=next(parent.glob('genesis_catalog_*.c'));path.write_bytes(path.read_bytes()+b'/* stale */\n')
    try:
        spans.generate(parent,root/'refused-stale',Path(sys.argv[2]))
        raise AssertionError('stale qualified parent was accepted')
    except ValueError as error:
        assert 'parent output hash differs' in str(error),error
print('Canonical span bodies verified; pure HLE and stale parents refused')
