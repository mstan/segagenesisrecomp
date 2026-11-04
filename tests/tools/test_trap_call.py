#!/usr/bin/env python3
"""Execute real generated C for TRAP #n used as a subroutine call.

No game ROM is needed. Pass a built GenesisRecomp and a native GCC-compatible
C compiler (same shape as test_stack_skip.py). The synthetic ROM checks:

  * discovery registers trap handlers from the TRAP instructions alone
    (game.toml seeds only the entry point);
  * the exception frame matches the 68000: SR pushed below PC, the handler
    runs with S set and T clear, and sees the caller's SR and return PC;
  * the handler's RTE restores SR (CCR and T) and A7, its RTE signal is
    consumed, and the instruction after the TRAP runs exactly once;
  * a TRAP inside a trap handler (nesting) works;
  * the per-instruction cycle charge for TRAP is applied exactly once;
  * a vector that does not point at ROM code still emits the loud
    m68k_trap_vector() abort instead of a call.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

CONFIG = """[game]
output_prefix = "trap"
[functions]
extra = [0x200, 0x380]
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
void m68k_trap_vector(uint8_t v) { fprintf(stderr,"unexpected trap vector %u\n",v); abort(); }
uint32_t recomp_resolve_ram_trampoline(uint32_t a) { return a; }
int recomp_dispatch_ram_stub(uint32_t a) { (void)a; return 0; }
void genesis_log_dispatch_miss(uint32_t a) { fprintf(stderr,"miss %x\n",a); abort(); }
void hybrid_jmp_interpret(uint32_t a) { call_by_address(a); }
void hybrid_call_interpret(uint32_t a) { call_by_address(a); }
void genesis_log_interior_label_miss(uint32_t a) { genesis_log_dispatch_miss(a); }
static int check(const char *what, uint32_t got, uint32_t want) {
    if (got == want) return 0;
    fprintf(stderr, "%s: got $%X want $%X\n", what, got, want); return 1;
}
int main(void) {
    int bad = 0;
    g_cpu.A[7] = 0xFFFE00; g_cpu.A[1] = 0xFF8000;
    g_cpu.SR = 0xA700;                    /* T and S set, CCR clear */
    recomp_call_addr(0x200);
    /* Trap #0 handler log, one 8-byte record per call: SR inside the
     * handler, stacked SR, stacked return PC. Call 1 from main, call 2
     * nested inside the trap #2 handler. */
    bad |= check("call1 SR in handler", m68k_read16(0xFF8000), 0x2704);
    bad |= check("call1 stacked SR",    m68k_read16(0xFF8002), 0xA704);
    bad |= check("call1 stacked PC",    m68k_read32(0xFF8004), 0x000204);
    bad |= check("call2 SR in handler", m68k_read16(0xFF8008), 0x2700);
    bad |= check("call2 stacked SR",    m68k_read16(0xFF800A), 0x2700);
    bad |= check("call2 stacked PC",    m68k_read32(0xFF800C), 0x000342);
    bad |= check("SR after first RTE",  g_cpu.D[4] & 0xFFFF, 0xA704);
    bad |= check("continuations ran once (D0)", g_cpu.D[0], 2);
    bad |= check("trap #0 handler calls (D1)",  g_cpu.D[1], 2);
    bad |= check("trap #2 handler ran (D7)",    g_cpu.D[7], 1);
    bad |= check("A7 balanced", g_cpu.A[7], 0xFFFE00);
    bad |= check("RTE signal consumed", (uint32_t)pending, 0);
    bad |= check("final SR", g_cpu.SR, 0xA700);
    return bad;
}
'''


def run(cmd, cwd):
    p = subprocess.run([str(x) for x in cmd], cwd=cwd, capture_output=True, text=True)
    if p.returncode:
        raise RuntimeError(f"{cmd[0]} failed ({p.returncode})\n{p.stdout}\n{p.stderr}")


def build_rom():
    rom = bytearray(b"\xff" * 0x400)
    rom[0:8] = bytes.fromhex("00fffe00 00000200")
    rom[0x100:0x200] = b" " * 256
    rom[0x100:0x104] = b"SEGA"
    rom[0x80:0x84] = (0x300).to_bytes(4, "big")       # TRAP #0 -> ROM
    rom[0x88:0x8C] = (0x340).to_bytes(4, "big")       # TRAP #2 -> ROM
    rom[0x8C:0x90] = (0xFF0000).to_bytes(4, "big")    # TRAP #3 -> RAM: must abort
    code = {
        # main: moveq #0,d0 / trap #0 / move sr,d4 / addq.l #1,d0 /
        #       trap #2 / addq.l #1,d0 / rts
        0x200: "7000 4e40 40c4 5280 4e42 5280 4e75",
        # trap #0: move sr,(a1)+ / move.w (sp),(a1)+ / move.l 2(sp),(a1)+ /
        #          addq.l #1,d1 / rte
        0x300: "40d9 32d7 22ef0002 5281 4e73",
        # trap #2: trap #0 (nested) / addq.l #1,d7 / rte
        0x340: "4e40 5287 4e73",
        # never executed; only its generated text is checked: trap #3 / rts
        0x380: "4e43 4e75",
    }
    for addr, hexcode in code.items():
        b = bytes.fromhex(hexcode.replace(" ", ""))
        rom[addr:addr + len(b)] = b
    return rom


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--recompiler", type=Path, required=True)
    ap.add_argument("--cc", type=Path, required=True)
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix="genesis-trap-call-") as temp:
        td = Path(temp)
        (td / "game.toml").write_text(CONFIG)
        (td / "harness.c").write_text(HARNESS)
        (td / "test.bin").write_bytes(build_rom())
        run([args.recompiler.resolve(), "test.bin", "--game", "game.toml",
             "--output-dir", "generated"], td)
        parts = sorted((td / "generated").glob("trap_part*.c"))
        text = "".join(p.read_text() for p in parts)

        for handler in ("000300", "000340"):
            if not re.search(rf"^void func_{handler}\(void\)", text, re.M):
                raise AssertionError(f"trap handler ${handler} was not discovered")
        if text.count("recomp_call_func(func_000300)") != 2:
            raise AssertionError("expected two direct calls to the trap #0 handler")
        if "m68k_trap_vector(0x23u)" not in text:
            raise AssertionError("TRAP #3 into RAM must keep the runtime abort")
        # TRAP costs 34 cycles; it must be charged once per site, after the call.
        main_fn = re.search(r"^void func_000200\(void\) \{\n(.*?)^\}", text, re.S | re.M)
        site = main_fn and re.search(r"/\* \$000202 \*/(.*?)/\* \$000204 \*/",
                                     main_fn.group(1), re.S)
        if not site or site.group(1).count("g_cycle_accumulator += 34;") != 1:
            raise AssertionError("TRAP cycle charge must appear exactly once per site")
        print("PASS discovery, direct calls, RAM-vector abort, single cycle charge")

        exe = td / "test.exe"
        sources = parts + [td / "generated/trap_dispatch.c", td / "harness.c"]
        run([args.cc.resolve(), "-std=c11", "-O1", "-I", root / "runner/include",
             "-I", root / "runner", *sources, "-o", exe], td)
        run([exe], td)
        print("PASS executed frames, SR/T/S handling, nesting, A7 balance")


if __name__ == "__main__":
    main()
