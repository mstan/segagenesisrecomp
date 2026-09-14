#!/usr/bin/env python3
"""Execute real generated C for nested RTS stack skips, including tail splits.

No game ROM is needed. Pass a built GenesisRecomp and a native GCC-compatible
C compiler. The C harness supplies only the memory bus and timing boundary;
dispatch, calls, stack adjustment and return propagation come from codegen.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

CONFIG = """[game]
output_prefix = "skip"
[functions]
extra = [0x200, 0x240, 0x280, 0x2C0, 0x2E0, 0x2F0]
[ram_layout]
game_mode = 0
vint_runcount = 0
vint_routine = 0
plc_pending = 0
initial_ssp = 0xFFFE00
vbla_stack = 0
intr_stack = 0
player_object = 0
level_modes = []
"""
HARNESS = r'''
#include "genesis_runtime.h"
#include "game_spec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
M68KState g_cpu;
uint8_t g_rom[0x400000], g_ram[0x10000];
const GameSpec g_game_spec = {0};
uint64_t g_native_insn_count;
uint32_t g_cycle_accumulator, g_audio_cycle_counter, g_vblank_threshold = 0xFFFFFFFFu;
int pending, *g_rte_pending_ptr = &pending, g_early_return, g_ws_margin;
uint8_t m68k_read8(uint32_t a) { return g_ram[a & 65535]; }
uint16_t m68k_read16(uint32_t a) { return (m68k_read8(a)<<8)|m68k_read8(a+1); }
uint32_t m68k_read32(uint32_t a) { return ((uint32_t)m68k_read16(a)<<16)|m68k_read16(a+2); }
void m68k_write8(uint32_t a,uint8_t v) { g_ram[a&65535]=v; }
void m68k_write16(uint32_t a,uint16_t v) { m68k_write8(a,v>>8);m68k_write8(a+1,v); }
void m68k_write32(uint32_t a,uint32_t v) { m68k_write16(a,v>>16);m68k_write16(a+2,v); }
void recomp_push_return(uint32_t a) { g_cpu.A[7]-=4;m68k_write32(g_cpu.A[7],a); }
void glue_check_vblank(void) { abort(); }
uint32_t recomp_resolve_ram_trampoline(uint32_t a) { return a; }
int recomp_dispatch_ram_stub(uint32_t a) { (void)a; return 0; }
void genesis_log_dispatch_miss(uint32_t a) { fprintf(stderr,"miss %x\n",a); abort(); }
void hybrid_jmp_interpret(uint32_t a) { call_by_address(a); }
void hybrid_call_interpret(uint32_t a) { call_by_address(a); }
void genesis_log_interior_label_miss(uint32_t a) { genesis_log_dispatch_miss(a); }
int main(int argc,char **argv) {
    int skip=atoi(argv[1]); (void)argc;
    g_cpu.A[7]=0xFFFE00; g_cpu.SR=0x2700;
    recomp_call_addr(0x200);
    if (g_cpu.D[0]!=42 || g_cpu.D[1]!=(skip<2 ? 1u:0u) ||
        g_cpu.D[2]!=(skip<1 ? 2u:0u) || g_cpu.A[7]!=0xFFFE00 || pending) {
        fprintf(stderr,"skip=%d D0=%u D1=%u D2=%u A7=%x pending=%d\n",skip,
                g_cpu.D[0],g_cpu.D[1],g_cpu.D[2],g_cpu.A[7],pending); return 1;
    }
    return 0;
}
'''

def run(cmd, cwd):
    p = subprocess.run([str(x) for x in cmd], cwd=cwd, capture_output=True, text=True)
    if p.returncode:
        raise RuntimeError(f"{cmd[0]} failed ({p.returncode})\n{p.stdout}\n{p.stderr}")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--recompiler", type=Path, required=True)
    ap.add_argument("--cc", type=Path, required=True)
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix="genesis-stack-skip-") as temp:
        td = Path(temp)
        (td/"game.toml").write_text(CONFIG)
        (td/"harness.c").write_text(HARNESS)
        for slots in (0,1,2):
            for split in (False,True):
                rom = bytearray(b"\xff"*0x400)
                rom[0:8] = bytes.fromhex("00fffe00 00000200")
                rom[0x100:0x200] = b" "*256
                rom[0x100:0x104] = b"SEGA"
                for addr,target,reg,value in ((0x200,0x240,0,42),(0x240,0x280,1,1),(0x280,0x2C0,2,2)):
                    code = bytes.fromhex("4eb9") + target.to_bytes(4,"big") + bytes([0x70+reg*2,value,0x4e,0x75])
                    rom[addr:addr+len(code)] = code
                adjust = bytes.fromhex({0:"4e71",1:"584f",2:"504f"}[slots])
                if split:
                    rom[0x2C0:0x2C6] = bytes.fromhex("4ef9 000002e0")
                    rom[0x2E0:0x2E8] = adjust + bytes.fromhex("4ef9 000002f0")
                    rom[0x2F0:0x2F2] = bytes.fromhex("4e75")
                else:
                    rom[0x2C0:0x2C4] = adjust + bytes.fromhex("4e75")
                    rom[0x2E0:0x2E2] = rom[0x2F0:0x2F2] = bytes.fromhex("4e75")
                (td/"test.bin").write_bytes(rom)
                run([args.recompiler.resolve(),"test.bin","--game","game.toml","--output-dir","generated"],td)
                exe = td/"test.exe"
                sources = sorted((td/"generated").glob("skip_part*.c")) + [td/"generated/skip_dispatch.c",td/"harness.c"]
                run([args.cc.resolve(),"-std=c11","-O1","-I",root/"runner/include","-I",root/"runner",*sources,"-o",exe],td)
                run([exe,str(slots)],td)
                print(f"PASS skip {slots} return slots, split={split}")

if __name__ == "__main__": main()
