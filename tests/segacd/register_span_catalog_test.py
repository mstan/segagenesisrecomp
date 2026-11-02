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
    image=root/'fixture.bin';image.write_bytes(bytes.fromhex('7001720266fc4e75'))
    subprocess.run([sys.argv[1],str(image),str(original),'fixture','0','0'],check=True)
    catalogs=root/'catalogs.txt';catalogs.write_bytes(f'MAIN|{original.as_posix()}|fixture\n'.encode())
    parent=root/'parent';compact.compact(catalogs,parent)
    output=root/'spans';spans.generate(parent,output,Path(sys.argv[2]))
    manifest=json.loads((output/'native_catalog_manifest.json').read_bytes())
    assert manifest['canonical_ordinary_bodies_verified']==3
    assert 'genesis_spans_main' in (output/'native_register_span_registry.h').read_text()
    assert 'main_effect' not in (output/'native_register_span_registry.h').read_text()
    assert '#define GENESIS_CATALOG_HAS_FLOW_SPANS 1' in (output/'native_register_span_registry.h').read_text()
    assert 'const size_t genesis_spans_main_flow_count=1;' in (output/'genesis_spans_main_module.c').read_text()
    first=(original/'fixture_part00.c').read_bytes()
    assert b'void fixture_at_000000(M68kStepContext *ctx){\n' in first
    (original/'fixture_part00.c').write_bytes(first.replace(b'void fixture_at_000000(M68kStepContext *ctx){\n',b'void fixture_at_000000(M68kStepContext *ctx){\n  g_cpu.D[7]++;\n'))
    hle=root/'pure-hle';compact.compact(catalogs,hle)
    spans.generate(hle,root/'filtered-hle',Path(sys.argv[2]))
    filtered=json.loads((root/'filtered-hle/native_catalog_manifest.json').read_bytes())
    assert filtered['excluded_noncanonical_symbols']==['fixture_at_000000']
    assert 'const size_t genesis_spans_main_count=0;' in (root/'filtered-hle/genesis_spans_main_module.c').read_text()
    assert 'fixture_at_000000' in (root/'filtered-hle/genesis_catalog_main_0000.c').read_text()
    assert 'const size_t genesis_spans_main_flow_count=1;' in (root/'filtered-hle/genesis_spans_main_module.c').read_text()
    filtered_parts=''.join(path.read_text() for path in (root/'filtered-hle').glob('genesis_spans_main_part*.c'))
    assert 'flow_members_0[]={&genesis_catalog_main_0000_entries[1],&genesis_catalog_main_0000_entries[2]}' in filtered_parts
    # A terminal that looks like Bcc is still arbitrary HLE until its actual
    # archived callback body matches canonical emission. Never trust opcode.
    (original/'fixture_part00.c').write_bytes(first)
    terminal=next(path for path in original.glob('fixture_part*.c') if b'void fixture_at_000004(M68kStepContext *ctx){\n' in path.read_bytes())
    terminal_bytes=terminal.read_bytes()
    terminal.write_bytes(terminal_bytes.replace(b'void fixture_at_000004(M68kStepContext *ctx){\n',b'void fixture_at_000004(M68kStepContext *ctx){\n g_cpu.D[7]++;\n'))
    terminal_hle=root/'terminal-hle';compact.compact(catalogs,terminal_hle)
    spans.generate(terminal_hle,root/'terminal-filtered',Path(sys.argv[2]))
    refused_terminal=json.loads((root/'terminal-filtered/native_catalog_manifest.json').read_bytes())
    assert refused_terminal['excluded_noncanonical_symbols']==['fixture_at_000004']
    assert 'const size_t genesis_spans_main_flow_count=0;' in (root/'terminal-filtered/genesis_spans_main_module.c').read_text()
    assert 'const size_t genesis_spans_main_count=1;' in (root/'terminal-filtered/genesis_spans_main_module.c').read_text()
    terminal.write_bytes(terminal_bytes)
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
        # Compile/link/run the ACTUAL producer output, not a handwritten copy
        # of its declarations. This also checks the ordinary archive ABI.
        (compile_source/'flow.c').write_text('''#include "native_register_span_registry.h"
#include <stdio.h>
static const unsigned char image[]={0x70,1,0x72,2,0x66,0xfc,0x4e,0x75};
static const unsigned char*view(void*u,uint32_t pc,size_t n){(void)u;return pc<=sizeof image&&n<=sizeof image-pc?image+pc:0;}
static M68kStepBusResult fetch(void*u,uint32_t pc,uint16_t*v){const unsigned char*p=view(u,pc,2);if(!p)return M68K_BUS_FAULT;*v=(uint16_t)((unsigned)p[0]<<8|p[1]);return M68K_BUS_OK;}
int main(void){M68kStepContext c={0};c.bus.fetch16=fetch;c.code_span=view;c.state.regs.SR=0x2700;
 if(genesis_spans_main_flow_count!=1||m68k_step_execute_flow_span(&c,genesis_spans_main_flow_spans,18)!=M68K_STEP_OK||c.state.regs.D[0]!=1||c.state.regs.D[1]!=2||c.state.regs.PC!=2||c.state.cycles!=18||c.state.instructions!=3)return 1;
 puts("actual canonical producer flow linked and retired");return 0;}
''')
        sources=[compile_source/'flow.c',core/'m68k_step.c',*original.glob('fixture_part*.c'),*output.glob('*.c')]
        with (compile_source/'CMakeLists.txt').open('a') as file:
            file.write('add_executable(flow\n'+''.join(' "'+p.as_posix()+'"\n' for p in sources)+')\ntarget_include_directories(flow PRIVATE "'+core.as_posix()+'" "'+output.as_posix()+'" "'+original.as_posix()+'")\n')
        subprocess.run([sys.argv[3],'-S',str(compile_source),'-B',str(root/'registry-build')],check=True)
        subprocess.run([sys.argv[3],'--build',str(root/'registry-build'),'--config','Release','--target','flow','--parallel','1'],check=True)
        flow_executable=root/'registry-build'/('Release/flow.exe' if sys.platform=='win32' else 'flow')
        subprocess.run([str(flow_executable)],check=True)
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
