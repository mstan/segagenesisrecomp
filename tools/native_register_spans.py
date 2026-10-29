#!/usr/bin/env python3
"""Build fixed-cycle register companions from a qualified compact catalog.

Freeze and verify every parent output. Export its unchanged entry arrays, then
give the common core emitter only each PC's FIRST descriptor/effect in original
priority order. Other variants and arbitrary/HLE entries retain ordinary steps.
This produces new native objects; the unchanged instruction archive is separate.
"""
from __future__ import annotations
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess
from native_catalog_compact import ENTRY

def sha(data):
    return hashlib.sha256(data).hexdigest()

def generate(source: Path, output: Path, emitter: Path):
    if source.resolve()==output.resolve():
        raise ValueError('companion output must be separate from the qualified parent')
    parent_raw=(source/'native_catalog_manifest.json').read_bytes()
    parent=json.loads(parent_raw)
    files={}
    for name, expected in parent['outputs'].items():
        if Path(name).name!=name:
            raise ValueError('parent output name is not local')
        raw=(source/name).read_bytes()
        if sha(raw)!=expected:
            raise ValueError('parent output hash differs: '+name)
        files[name]=raw
    output.mkdir(parents=True,exist_ok=True)
    if any(output.iterdir()):
        raise ValueError('companion output must be empty')
    registry=files['native_catalog_registry.h'].decode()
    groups={}
    for group, value in re.findall(r'^#define GENESIS_CATALOG_(\w+)_MODULES (&[A-Za-z_0-9,&]+)$',registry,re.M):
        groups[group]=[name.removeprefix('&').removesuffix('_module') for name in value.split(',')]
    first={group:{} for group in groups}
    for group,names in groups.items():
        for name in names:
            text=files[name+'.c'].decode()
            records=[ENTRY.fullmatch(line) for line in text.splitlines() if line.startswith(' {0x')]
            if not records or any(record is None for record in records):
                raise ValueError('unsupported parent entries')
            effects=re.search(r'_register_effects\[\]=\{\s*([0-9,\s]+)\};',text)
            flags=list(map(int,effects[1].replace('\n','').split(','))) if effects else []
            if len(flags)!=len(records):
                raise ValueError('effect/entry count differs')
            array=name+'_entries'
            for index,(record,flag) in enumerate(zip(records,flags)):
                pc=int(record[1],16)
                first[group].setdefault(pc,(array,index,pc,int(record[3]),int(record[4]),flag,record[2],record[5]))
            if text.count('static const M68kStepEntry entries[]={')!=1:
                raise ValueError('unsupported entry array')
            text=text.replace('static const M68kStepEntry entries[]={',f'const M68kStepEntry {array}[]={{')
            text=text.replace('entries,sizeof entries/sizeof *entries',f'{array},sizeof {array}/sizeof *{array}')
            files[name+'.c']=text.encode()
    for name,raw in files.items():
        (output/name).write_bytes(raw)
    declarations=[]
    generated=[]
    bodies={}
    def emit_group(group,entries,excluded):
        prefix='genesis_spans_'+group.lower()
        input_file=output/(prefix+'.txt')
        with input_file.open('w',encoding='utf8',newline='\n') as stream:
            for _,record in sorted(entries.items()):
                array,index,pc,count,cycles,flag,words,symbol=record
                if symbol in excluded:
                    flag=0
                values=' '.join(f'{int(word,16):04x}' for word in words.split(','))
                stream.write(f'{array} {index} {pc:06x} {count} {cycles} {flag} {values} {symbol}\n')
        subprocess.run([str(emitter),str(input_file),str(output),prefix],check=True)
        with (output/(prefix+'_ordinary_bodies.jsonl')).open(encoding='utf8') as proof:
            for line in proof:
                symbol,body=json.loads(line)
                digest=sha(body.replace('\r\n','\n').encode())
                if bodies.setdefault(symbol,digest)!=digest:
                    raise ValueError('canonical symbol has conflicting ordinary emissions: '+symbol)
    for group,entries in first.items():
        prefix='genesis_spans_'+group.lower()
        emit_group(group,entries,set())
        declarations.extend([f'extern const M68kStepRegisterSpan {prefix}_spans[];',f'extern const size_t {prefix}_count;'])
        generated.extend([prefix+'_module.c']+[f'{prefix}_part{part:02}.c' for part in range(32)])
    # Effect qualification is insufficient: a pure HLE can use different
    # arithmetic with the same instruction guard. Require EXACT ordinary
    # common-emitter body equivalence, including PC-bearing temporaries.
    alias_file=source/'native_catalog_aliases.jsonl.gz'
    if sha(alias_file.read_bytes())!=parent['alias_map_sha256']:
        raise ValueError('parent canonical body map hash differs')
    unmatched=set(bodies)
    expected={}
    with gzip.open(alias_file,'rt',encoding='utf8') as stream:
        for line in stream:
            original,target,digest=json.loads(line)
            if target in unmatched:
                expected[target]=digest
                unmatched.remove(target)
    if unmatched:
        raise ValueError('original operation body proof missing: '+next(iter(unmatched)))
    excluded={symbol for symbol,digest in bodies.items() if expected[symbol]!=digest}
    if excluded:
        # A different producer revision or a pure HLE is an ordinary-path
        # boundary, never permission to replace its semantics from the opcode.
        # Regenerate EVERY part/table so no unproven span remains eligible.
        bodies.clear()
        for group,entries in first.items():
            emit_group(group,entries,excluded)
        if any(expected.get(symbol)!=digest for symbol,digest in bodies.items()):
            raise ValueError('noncanonical body survived companion filtering')
    (output/'native_register_span_registry.h').write_bytes(('#ifndef NATIVE_REGISTER_SPAN_REGISTRY_H\n#define NATIVE_REGISTER_SPAN_REGISTRY_H\n#include "m68k_step.h"\n#define GENESIS_CATALOG_HAS_REGISTER_SPANS 1\n'+'\n'.join(declarations)+'\n#endif\n').encode())
    (output/'native_catalog_registry.h').write_bytes(files['native_catalog_registry.h']+b'\n#include "native_register_span_registry.h"\n')
    cmake=files['native_catalog_sources.cmake'].decode()+'\nlist(APPEND GENESIS_COMPACT_CATALOG_SOURCES\n'+''.join(f'  "${{CMAKE_CURRENT_LIST_DIR}}/{name}"\n' for name in generated)+')\n'
    (output/'native_catalog_sources.cmake').write_bytes(cmake.encode())
    identities={path.name:sha(path.read_bytes()) for path in output.iterdir() if path.is_file()}
    parent.update(schema='M68kStepRegisterSpanCatalog1',parent_manifest_sha256=sha(parent_raw),
                  companion_generator_sha256=sha(Path(__file__).read_bytes()),
                  companion_emitter_sha256=sha(emitter.read_bytes()),outputs=identities,
                  canonical_ordinary_bodies_verified=len(bodies),
                  excluded_noncanonical_symbols=sorted(excluded),
                  span_priority='first descriptor at each PC; full live guards; runtime configuration revalidates identity/effects/priority')
    (output/'native_catalog_manifest.json').write_bytes((json.dumps(parent,indent=2)+'\n').encode())
    return {group:len(entries) for group,entries in first.items()}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--emitter',type=Path,required=True)
    args=parser.parse_args()
    print(json.dumps(generate(args.source,args.output,args.emitter)))
