/*
 * hybrid_jmp.c — Interpreter fallback for native code.
 *
 * Two use cases:
 *
 * 1. hybrid_jmp_interpret(target_pc) — JMP table fallback.
 *    Native code computed a jump target but the handler code wasn't
 *    generated.  Runs interpreter from target until the function's RTS.
 *    Sets g_hybrid_jmp_done so hybrid_pre_insn skips its own RTS sim.
 *
 * 2. hybrid_call_interpret(target_pc) — call_by_address fallback.
 *    Native code calls a function that isn't in the dispatch table.
 *    Pushes return address, runs interpreter from target until RTS
 *    pops it, syncs back.  Transparent to the caller.
 *
 * Both sync g_cpu <-> Clown68000_State and use the real bus callbacks
 * so VDP/IO/RAM all work normally during interpreter execution.
 */

#include "genesis_runtime.h"
#include "clown68000.h"
#include "clownmdemu.h"
#include "bus-main-m68k.h"
#include "bus-common.h"

#include <stdio.h>

extern M68KState g_cpu;

/* Set by hybrid_jmp_interpret, checked by hybrid_pre_insn */
int g_hybrid_jmp_done = 0;

static ClownMDEmu *s_emu_ref = NULL;
static CPUCallbackUserData *s_cpu_data_ref = NULL;

void hybrid_jmp_init(ClownMDEmu *emu, CPUCallbackUserData *cpu_data)
{
    s_emu_ref = emu;
    s_cpu_data_ref = cpu_data;
}

/* -------------------------------------------------------------------------
 * Core: run interpreter from current g_cpu state at target_pc until
 * PC reaches ret_addr.  Syncs g_cpu <-> m68k around the run.
 * ---------------------------------------------------------------------- */

static void run_interp_until(uint32_t target_pc, uint32_t ret_addr)
{
    Clown68000_ReadWriteCallbacks cbs;
    cc_u32l cycles = 0;
    int i;

    if (!s_emu_ref || !s_cpu_data_ref)
        return;

    /* Sync g_cpu -> interpreter */
    for (i = 0; i < 8; i++)
        s_emu_ref->m68k.data_registers[i] = g_cpu.D[i];
    for (i = 0; i < 8; i++)
        s_emu_ref->m68k.address_registers[i] = g_cpu.A[i];
    s_emu_ref->m68k.status_register = g_cpu.SR;
    s_emu_ref->m68k.program_counter = target_pc;

    /* Real bus callbacks — full VDP/IO/RAM access */
    cbs.read_callback = M68kReadCallback;
    cbs.write_callback = M68kWriteCallback;
    cbs.user_data = s_cpu_data_ref;

    /* Run 1 instruction at a time until PC == ret_addr */
    while (cycles < 500000) {
        Clown68000_DoCycles(&s_emu_ref->m68k, &cbs, 1);
        cycles += 4;
        if (s_emu_ref->m68k.program_counter == ret_addr)
            break;
    }

    if (cycles >= 500000) {
        fprintf(stderr, "[HYBRID] interpret timeout: target=$%06X ret=$%06X "
                "PC=$%06X after %u cycles\n",
                (unsigned)target_pc, (unsigned)ret_addr,
                (unsigned)s_emu_ref->m68k.program_counter, (unsigned)cycles);
    }

    /* Sync interpreter -> g_cpu */
    for (i = 0; i < 8; i++)
        g_cpu.D[i] = s_emu_ref->m68k.data_registers[i];
    for (i = 0; i < 8; i++)
        g_cpu.A[i] = s_emu_ref->m68k.address_registers[i];
    g_cpu.SR = (uint16_t)s_emu_ref->m68k.status_register;
    g_cpu.PC = s_emu_ref->m68k.program_counter;
}

/* -------------------------------------------------------------------------
 * JMP table fallback — called from generated code at JMP sites.
 * The return address is on the stack (from the original BSR/JSR to
 * this function).  After the handler's RTS, hybrid_pre_insn should
 * NOT do its own RTS simulation.
 * ---------------------------------------------------------------------- */

void hybrid_jmp_interpret(uint32_t target_pc)
{
    uint32_t ret_addr;
    extern uint32_t m68k_read32(uint32_t byte_addr);

    ret_addr = m68k_read32(g_cpu.A[7]);
    run_interp_until(target_pc, ret_addr);
    g_hybrid_jmp_done = 1;
}

/* -------------------------------------------------------------------------
 * call_by_address fallback — called when sonic_dispatch.c can't find
 * the target in its dispatch table.  The native caller already did a
 * JSR (return address is on the stack).  We run the target function
 * through the interpreter until its RTS, then return normally.
 * ---------------------------------------------------------------------- */

void hybrid_call_interpret(uint32_t target_pc)
{
    extern void m68k_write32(uint32_t byte_addr, uint32_t val);

    /* In native code, call_by_address is a C function call — there's no
     * JSR on the 68K stack.  The interpreter needs a return address to
     * know when the target function's RTS should stop execution.
     *
     * Solution: push a sentinel return address onto the 68K stack,
     * run the interpreter, then pop it back off.  We use address 0
     * as the sentinel (no real code lives there). */
    uint32_t sentinel = 0x00000000u;

    /* Push sentinel onto 68K stack */
    g_cpu.A[7] -= 4;
    m68k_write32(g_cpu.A[7], sentinel);

    /* Run interpreter from target until RTS pops the sentinel */
    run_interp_until(target_pc, sentinel);

    /* Clean up: if PC == sentinel, the RTS already popped it and
     * incremented A7.  If timeout, restore A7 manually. */
    if (g_cpu.PC != sentinel) {
        /* Timeout — undo the push */
        g_cpu.A[7] += 4;
    }
    /* No g_hybrid_jmp_done — the caller continues normally */
}
