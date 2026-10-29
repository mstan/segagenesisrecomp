#!/usr/bin/env python3
"""Same-body native callgraph placement and actual portable generated execution."""
import argparse,hashlib,os,re,tempfile
from pathlib import Path
from test_stack_skip import CONFIG,HARNESS,run

DEFINITION=re.compile(r'^void (func_(?:body_)?[0-9A-F]{6})\([^;\n]*\)\s*\{',re.M)
TOKEN=re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27|[{}]',re.S)
def definitions(folder):
    bodies={};parts={}
    for part in sorted(folder.glob('skip_part*.c')):
        text=part.read_text(encoding='utf8')
        for match in DEFINITION.finditer(text):
            depth=0;end=None
            for token in TOKEN.finditer(text,match.end()-1):
                if token[0]=='{':depth+=1
                elif token[0]=='}':
                    depth-=1
                    if not depth:end=token.end();break
            assert end is not None and match[1] not in bodies
            bodies[match[1]]=hashlib.sha256(text[match.start():end].encode()).hexdigest();parts[match[1]]=part.name
    return bodies,parts
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--recompiler',type=Path,required=True);ap.add_argument('--baseline-generator',type=Path)
    comp=ap.add_mutually_exclusive_group(required=True);comp.add_argument('--cmake',type=Path);comp.add_argument('--cc',type=Path)
    args=ap.parse_args();repo=Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix='genesis-callgraph-') as temp:
        root=Path(temp);rom=bytearray(b'\xff'*0x6000);rom[:8]=bytes.fromhex('00fffe00 00000200');rom[0x100:0x200]=b' '*256;rom[0x100:0x104]=b'SEGA'
        rom[0x200:0x210]=bytes.fromhex('4eb9000003004eb900000320702a4e75')
        rom[0x300:0x304]=bytes.fromhex('72014e75');rom[0x320:0x324]=bytes.fromhex('74024e75')
        cold=[0x1000+n*0x100 for n in range(64)]
        for pc in cold:rom[pc:pc+130]=bytes.fromhex('4e71')*64+bytes.fromhex('4e75')
        (root/'test.bin').write_bytes(rom)
        config=CONFIG.replace('0x200, 0x240, 0x280, 0x2C0, 0x2E0, 0x2F0',','.join(hex(pc) for pc in [0x200,0x300,0x320,*cold]))
        (root/'game.toml').write_text(config,encoding='utf8')
        run([args.recompiler.resolve(),'test.bin','--game','game.toml','--output-dir','generated'],root)
        bodies,parts=definitions(root/'generated');assert len(bodies)==67
        assert parts['func_000200']==parts['func_000300']==parts['func_000320']
        assert len(list((root/'generated').glob('skip_part*.c')))==32
        if args.baseline_generator:
            run([args.baseline_generator.resolve(),'test.bin','--game','game.toml','--output-dir','baseline'],root)
            previous,_=definitions(root/'baseline');assert bodies==previous,'layout changed a generated function body'
            print('All67 generated function definitions byte-identical to frozen previous layout')
        harness=HARNESS.split('int main(')[0]+'''int main(void){g_cpu.A[7]=0xfffe00;g_cpu.SR=0x2700;recomp_call_addr(0x200);
 if(g_cpu.D[0]!=42||g_cpu.D[1]!=1||g_cpu.D[2]!=2||g_cpu.A[7]!=0xfffe00||pending||g_native_direct_calls!=2)return 1;return 0;}
'''
        (root/'harness.c').write_text(harness,encoding='utf8')
        sources=[*sorted((root/'generated').glob('skip_part*.c')),root/'generated/skip_dispatch.c',root/'harness.c']
        if args.cmake:
            listing='\n'.join(' "'+p.as_posix()+'"' for p in sources)
            (root/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.16)\nproject(Callgraph C)\nadd_executable(callgraph\n'+listing+'\n)\ntarget_include_directories(callgraph PRIVATE "'+(repo/'runner/include').as_posix()+'" "'+(repo/'runner').as_posix()+'")\ntarget_compile_features(callgraph PRIVATE c_std_11)\n',encoding='utf8')
            configure=[args.cmake.resolve(),'-S',root,'-B',root/'build']
            if os.name=='nt':configure+=['-G','Visual Studio 17 2022','-A','x64']
            run(configure,root);run([args.cmake.resolve(),'--build',root/'build','--config','Release','--parallel','1'],root);exe=root/'build/Release/callgraph.exe'
        else:
            exe=root/'callgraph';run([args.cc.resolve(),'-std=c11','-O2','-I',repo/'runner/include','-I',repo/'runner',*sources,'-o',exe],root)
        run([exe],root);print('Bounded common-emitter callgraph placement and actual native calls pass')
if __name__=='__main__':main()
