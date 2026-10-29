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
    harness=harness.replace('#include "game_spec.h"','#include "game_spec.h"\n#include "native_register_budget.h"')
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
    (root/'legacy_budget.h').write_text('#define GEN_NATIVE_REGISTER_SPAN_READY(c,n) (g_cycle_accumulator<g_vblank_threshold && (c)<g_vblank_threshold-g_cycle_accumulator)\n')
    run([args.recompiler.resolve(),'test.bin','--game','game.toml','--output-dir','generated'],root)
    sources=sorted((root/'generated').glob('skip_part*.c'))+[root/'generated/skip_dispatch.c',root/'harness.c']
    for mode,flags in [('ordinary',['-DGENESIS_COSIM']),('spans',[])]:
        run([args.cc.resolve(),'-std=c11','-O2',*flags,'-include',root/'legacy_budget.h','-I',engine/'runner/include','-I',engine/'runner',*sources,'-o',root/mode],root)
    total=0;hits=0
    for threshold in [1,4,7,8,9,12,16,17,20,24,28,32,40,48,64,80,128,0xffffffff]:
        for ccr in range(32):
            before=run([root/'ordinary',threshold,ccr],root);after=run([root/'spans',threshold,ccr],root)
            assert before.stdout==after.stdout,(threshold,ccr,before.stdout,after.stdout)
            assert before.stderr.strip()=='spans=0'
            hits+=int(re.search(r'spans=(\d+)',after.stderr)[1]);total+=1
    assert hits>0
    # The own backend's cycle accumulator may stay above the old VBlank
    # threshold. Compare actual generated C against ordinary retirement with
    # the real read-only budget rule, including private end-state publication.
    own=HARNESS[:HARNESS.index('int main(')]
    own=own.replace('#include "game_spec.h"','#include "game_spec.h"\n#include "native_register_budget.h"')
    own=own.replace('void glue_check_vblank(void) { abort(); }',r'''
#define active g_native_register_budget.interleave_active
#define service g_native_register_budget.in_vblank_service
#define divisor g_native_register_budget.main_cpu_divisor
#define budget g_native_register_budget.cycle_budget
#define seen g_native_register_budget.budget_cyc_seen
#define base g_native_register_budget.watchdog_base
static uint32_t stalls;static unsigned observations;
static void snapshot(void){printf("%u,%u,%u,%u,%u,%u,%x,%llu,%u,%u,%d,%u,%u,%llu,%u\n",g_cpu.D[0],g_cpu.D[1],g_cpu.D[2],g_cpu.D[3],g_cpu.A[0],g_cpu.A[1],g_cpu.SR,(unsigned long long)g_native_insn_count,g_cycle_accumulator,g_audio_cycle_counter,budget,seen,stalls,(unsigned long long)base,observations);}
void glue_check_vblank(void){
 if(g_native_insn_count-base>=17){++observations;snapshot();base=g_native_insn_count;}
 if(active&&!service&&g_cycle_accumulator>=255712){
  if(divisor>1&&g_audio_cycle_counter>seen){++observations;snapshot();}
  stalls=0;if(g_audio_cycle_counter>seen)budget-=(int32_t)(g_audio_cycle_counter-seen);seen=g_audio_cycle_counter;
  if(budget<=0){++observations;snapshot();++g_cpu.D[3];budget=64;base=g_native_insn_count;}
 }
}''')
    own+=r'''
#ifdef GENESIS_COSIM
uint64_t g_cosim_cycle,g_cosim_next_cp=UINT64_MAX;void cosim_checkpoint(void){abort();}
#endif
int main(int argc,char**argv){(void)argc;unsigned mode=(unsigned)atoi(argv[2]);
g_vblank_threshold=109312;g_cycle_accumulator=(uint32_t)strtoul(argv[1],0,10);
active=mode&1;service=(mode>>1)&1;divisor=1+(mode>>2);budget=atoi(argv[3]);
unsigned scenario=(unsigned)atoi(argv[4]);g_audio_cycle_counter=scenario==2?0xfffffff0u:100;
seen=scenario==1?120:scenario==2?g_audio_cycle_counter:96;stalls=23;base=scenario==2?UINT64_MAX-6:0;g_native_insn_count=base+((atoi(argv[5])&16)?14:0);
g_cpu.A[7]=0xfffe00;g_cpu.A[0]=0x12345678;g_cpu.SR=(uint16_t)(0x2700|atoi(argv[5]));
recomp_call_addr(0x200);snapshot();fprintf(stderr,"spans=%llu\n",(unsigned long long)g_native_span_count);return 0;}
'''
    (root/'harness.c').write_text(own)
    for mode,flags in [('own-ordinary',['-DGENESIS_COSIM']),('own-spans',[])]:
        run([args.cc.resolve(),'-std=c11','-O2','-DGENESIS_NATIVE_WATCHDOG_LIMIT=17',*flags,'-I',engine/'runner/include','-I',engine/'runner',*sources,'-o',root/mode],root)
    own_total=0;own_hits=0
    for cycle in [0,109300,109312,255680,255700,255711,255712,255713,0xfffffff0]:
        for mode in range(8):
            for budget in [1,12,64]:
                for reset in [0,1,2]:
                    for ccr in [0,4,16,31]:
                        before=run([root/'own-ordinary',cycle,mode,budget,reset,ccr],root)
                        after=run([root/'own-spans',cycle,mode,budget,reset,ccr],root)
                        assert before.stdout==after.stdout,(cycle,mode,budget,reset,ccr,before.stdout,after.stdout)
                        own_total+=1;own_hits+=int(re.search(r'spans=(\d+)',after.stderr)[1])
    assert own_hits>0
    run([args.recompiler.resolve(),'test.bin','--game','game.toml','--output-dir','debug','--reverse-debug'],root)
    assert not any('unobservable fixed-cycle register span' in path.read_text() for path in (root/'debug').glob('skip_part*.c'))
    print(f'{total} legacy threshold/CCR comparisons ({hits} spans), {own_total} own-budget/watchdog/private-state comparisons ({own_hits} spans), cosim/reverse-debug fallback pass')
