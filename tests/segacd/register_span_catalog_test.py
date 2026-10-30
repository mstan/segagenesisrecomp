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
    spans.generate(hle,root/'filtered-hle',Path(sys.argv[2]))
    filtered=json.loads((root/'filtered-hle/native_catalog_manifest.json').read_bytes())
    assert filtered['excluded_noncanonical_symbols']==['fixture_at_000000']
    assert 'const size_t genesis_spans_main_count=0;' in (root/'filtered-hle/genesis_spans_main_module.c').read_text()
    assert 'fixture_at_000000' in (root/'filtered-hle/genesis_catalog_main_0000.c').read_text()
    rmw_source=root/'rmw-original';rmw_source.mkdir()
    rmw_image=root/'rmw.bin';rmw_image.write_bytes(bytes.fromhex('0668000100084e75'))
    subprocess.run([sys.argv[1],str(rmw_image),str(rmw_source),'rmw','0','0'],check=True)
    rmw_list=root/'rmw-list.txt';rmw_list.write_text(f'MAIN|{rmw_source.as_posix()}|rmw\n')
    rmw_parent=root/'rmw-parent';compact.compact(rmw_list,rmw_parent)
    rmw_output=root/'rmw-owned';spans.generate(rmw_parent,rmw_output,Path(sys.argv[2]))
    rmw_manifest=json.loads((rmw_output/'native_catalog_manifest.json').read_bytes())
    assert len(rmw_manifest['canonical_owned_rmw_redirects'])==1
    # An owned callback must not clobber the separate span-registry declaration
    # list. Both declarations are required by the compiled catalog consumer.
    rmw_registry=(rmw_output/'native_register_span_registry.h').read_text()
    assert 'extern const M68kStepRegisterSpan genesis_spans_main_spans[];\n' in rmw_registry
    assert 'extern const size_t genesis_spans_main_count;\n' in rmw_registry
    if len(sys.argv)>=5:
        compile_source=root/'registry-compile';compile_source.mkdir()
        (compile_source/'registry.c').write_text('#include "native_register_span_registry.h"\nint registry_is_valid(void){return 1;}\n')
        core=Path(__file__).resolve().parents[2]/'external/m68k-recomp-core/runtime'
        (compile_source/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.16)\nproject(RegistryContract C)\nadd_library(registry OBJECT registry.c)\ntarget_include_directories(registry PRIVATE "'+core.as_posix()+'" "'+rmw_output.as_posix()+'")\n')
        subprocess.run([sys.argv[3],'-S',str(compile_source),'-B',str(root/'registry-build'),'-G',sys.argv[4]],check=True)
        subprocess.run([sys.argv[3],'--build',str(root/'registry-build'),'--config','Release','--parallel','1'],check=True)
    array,index,original,replacement=rmw_manifest['canonical_owned_rmw_redirects'][0]
    assert original=='rmw_at_000000' and index==0
    old=(rmw_parent/'genesis_catalog_main_0000.c').read_text()
    new=(rmw_output/'genesis_catalog_main_0000.c').read_text()
    old_entries=[compact.ENTRY.fullmatch(line).groups() for line in old.splitlines() if compact.ENTRY.fullmatch(line)]
    new_entries=[compact.ENTRY.fullmatch(line).groups() for line in new.splitlines() if compact.ENTRY.fullmatch(line)]
    assert old_entries[0][:-1]==new_entries[0][:-1] and new_entries[0][-1]==replacement
    assert old_entries[1:]==new_entries[1:]
    body=rmw_source/'rmw_part00.c';body.write_text(body.read_text().replace('M68kStepContext *ctx){','M68kStepContext *ctx){\n g_cpu.D[7]++;',1))
    rmw_hle=root/'rmw-hle-parent';compact.compact(rmw_list,rmw_hle)
    spans.generate(rmw_hle,root/'rmw-hle-filtered',Path(sys.argv[2]))
    refused=json.loads((root/'rmw-hle-filtered/native_catalog_manifest.json').read_bytes())
    assert not refused['canonical_owned_rmw_redirects'] and refused['excluded_noncanonical_symbols']==['rmw_at_000000']
    path=next(parent.glob('genesis_catalog_*.c'));path.write_bytes(path.read_bytes()+b'/* stale */\n')
    try:
        spans.generate(parent,root/'refused-stale',Path(sys.argv[2]))
        raise AssertionError('stale qualified parent was accepted')
    except ValueError as error:
        assert 'parent output hash differs' in str(error),error
print('Canonical span bodies verified; pure HLE and stale parents refused')
