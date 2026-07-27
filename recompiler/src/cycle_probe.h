/*
 * cycle_probe.h — OPTIONAL clown68000 cycle oracle, for VALIDATION ONLY.
 *
 * This is no longer a cost source. Emitted cycle stamps come exclusively from
 * the clean-room PRM model in code_generator.c (estimate_cycles_prm), so the
 * recompiler builds and produces byte-identical output with no clownmdemu
 * checkout present. That is what lets a public clone build.
 *
 * The probe survives as the validation oracle for that model. Configure with
 * -DGENESIS_CYCLE_ORACLE=ON (requires the clownmdemu-core submodule) and run
 * with GENESIS_CYCLE_DIAG=<path>: every instruction is logged with BOTH the
 * model's cost and clown68000's measurement, which is how the model was
 * driven to 99.9994% agreement in the first place. Without the option the
 * probe compiles to stubs and the diagnostic records -1 for `measured`.
 *
 * Two known probe artifacts, documented so nobody re-chases them:
 *   - Conditional branches are measured under ONE synthetic flag state, so
 *     Bcc/DBcc/Scc readings are arbitrary. The model pins these by policy.
 *   - The synthetic index register is odd, so PC/An-indexed EAs can compute
 *     an odd address and trap; those readings are exception costs, not
 *     instruction costs.
 */
#pragma once

#include <stdint.h>
#include "rom_parser.h"

/* Call once after ROM load, before any codegen. 0 on success, negative on
 * error or when the oracle is not compiled in. */
int  cycle_probe_init(const GenesisRom *rom);

/* clown68000's cycle count for the instruction at ROM address `addr`, or a
 * negative value when the oracle is absent or the measurement failed.
 * VALIDATION ONLY — never feed this into an emitted cost. */
int  cycle_probe_measure(uint32_t addr);

void cycle_probe_shutdown(void);
