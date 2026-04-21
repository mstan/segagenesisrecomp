/*
 * cycle_probe.h — codegen-time exact-cycle measurement via clown68000.
 *
 * The recompiler's emitted `g_cycle_accumulator += N;` values drive native's
 * VBla pacing. To match the oracle (clown68000 interpreter) exactly, we
 * invoke clown68000 itself at codegen time on a stub bus + fresh CPU state,
 * single-step one instruction, and read back the cycle count. That number
 * becomes N.
 *
 * Data-dependent opcodes (MULx/DIVx/register-count shifts) are measured with
 * mid-range synthetic operands, so their cycle cost is an average rather than
 * exact. Every other 68K instruction's cost is fully determined by its
 * static encoding and gets perfect parity with the oracle.
 */
#pragma once

#include <stdint.h>
#include "rom_parser.h"

/* Call once after ROM load, before any codegen. 0 on success, negative on error. */
int  cycle_probe_init(const GenesisRom *rom);

/* Return the exact 68K cycle count for the instruction at ROM address `addr`,
 * as produced by clown68000. Returns a negative value if the probe isn't
 * initialised or if the measurement fails (in which case the caller should
 * fall back to the PRM estimate). */
int  cycle_probe_measure(uint32_t addr);

void cycle_probe_shutdown(void);
