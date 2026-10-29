#!/usr/bin/env python3
"""Real cartridge-generated spans match per-instruction observable boundaries."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
from test_stack_skip import HARNESS,CONFIG

ap=argparse.ArgumentParser()
ap.add_argument('--recompiler',type=Path,required=True)
ap.add_argument('--cc',type=Path,required=True)
args=ap.parse_args()
engine=Path(__file__).resolve().parents[2]
def run(argv,cwd):
    process=subprocess.run(list(map(str,argv)),cwd=cwd,capture_output=True,text=True)
    if process.returncode:
        raise RuntimeError(str(argv)+'\n'+process.stdout+'\n'+process.stderr)
    return process
with tempfile.TemporaryDirectory(prefix='genesis-register-boundaries-') as temp:
    root=Path(temp);image=bytearray(b'\xff'*0x400)
    image[:8]=bytes.fromhex('00fffe00 00000200');image[0x100:0x200]=b' '*256;image[0x100:0x104]=b'SEGA'
    code=bytes.fromhex('7001 06400001 7203 d141 41e80008 43e80004 3400 4e75')
    image[0x200:0x200+len(code)]=code
    (root/'test.bin').write_bytes(image)
    (root/'game.toml').write_text(CONFIG.replace('extra = [0x200, 0x240, 0x280, 0x2C0, 0x2E0, 0x2F0]','extra = [0x200]'))
    harness=HARNESS[:HARNESS.index('int main(')]
    harness=harness.replace('void glue_check_vblank(void) { abort(); }',
        'static void snapshot(void){printf("%u,%u,%u,%u,%u,%x,%llu,%u,%u\\n",g_cpu.D[0],g_cpu.D[1],g_cpu.D[2],g_cpu.A[0],g_cpu.A[1],g_cpu.SR,(unsigned long long)g_native_insn_count,g_cycle_accumulator,g_audio_cycle_counter);}\n'
        'void glue_check_vblank(void){snapshot();}')
    harness+='''
#ifdef GENESIS_COSIM
uint64_t g_cosim_cycle, g_cosim_next_cp=UINT64_MAX;
void cosim_checkpoint(void){abort();}
#endif
int main(int argc,char**argv){(void)argc;g_vblank_threshold=(uint32_t)atoi(argv[1]);
g_cpu.A[7]=0xfffe00;g_cpu.A[0]=0x12345678;g_cpu.SR=(uint16_t)(0x2700|atoi(argv[2]));
recomp_call_addr(0x200);snapshot();fprintf(stderr,"spans=%llu\\n",(unsigned long long)g_native_span_count);return 0;}
'''
    (root/'harness.c').write_text(harness)
    run([args.recompiler.resolve(),'test.bin','--game','game.toml','--output-dir','generated'],root)
    sources=sorted((root/'generated').glob('skip_part*.c'))+[root/'generated/skip_dispatch.c',root/'harness.c']
    for mode,flags in [('ordinary',['-DGENESIS_COSIM']),('spans',[])]:
        run([args.cc.resolve(),'-std=c11','-O2',*flags,'-I',engine/'runner/include','-I',engine/'runner',*sources,'-o',root/mode],root)
    total=0;hits=0
    for threshold in [1,4,7,8,9,12,16,17,20,24,28,32,40,48,64,80,128,0xffffffff]:
        for ccr in range(32):
            before=run([root/'ordinary',threshold,ccr],root);after=run([root/'spans',threshold,ccr],root)
            assert before.stdout==after.stdout,(threshold,ccr,before.stdout,after.stdout)
            assert before.stderr.strip()=='spans=0'
            hits+=int(re.search(r'spans=(\d+)',after.stderr)[1]);total+=1
    assert hits>0
    run([args.recompiler.resolve(),'test.bin','--game','game.toml','--output-dir','debug','--reverse-debug'],root)
    assert not any('unobservable fixed-cycle register span' in path.read_text() for path in (root/'debug').glob('skip_part*.c'))
    print(f'{total} cartridge threshold/CCR comparisons, {hits} executed spans, cosim/reverse-debug fallback pass')
