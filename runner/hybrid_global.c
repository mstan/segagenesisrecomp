/*
 * hybrid_global.c — always compiled into the executable.
 *
 * Defines g_hybrid_pre_insn_fn, referenced by clown68000.c.
 * NULL by default (no-op); set to a real function by HybridInit()
 * when HYBRID_RECOMPILED_CODE is active.
 */

#include "clowncommon.h"

/* Legacy hook — no longer used but kept so bus-main-m68k.c still links
 * if it hasn't been rebuilt yet. */
cc_u8l (*g_hybrid_dispatch_fn)(cc_u32f byte_addr, cc_u16f *out_word);

/* Pre-instruction hook — called from clown68000.c's instruction loop. */
void (*g_hybrid_pre_insn_fn)(cc_u32l pc);

/* Fake cycle counter — incremented by m68k_read/write during native dispatch
 * so VDP/Z80 sync progresses. Reset before each native function call.
 * Throwaway shim; dies when the interpreter is dropped. */
cc_u32f g_hybrid_cycle_counter;

/* Frame counter — defined in glue.c for native/hybrid builds.
 * Provide a fallback here for interpreter-only builds so cmd_server.c links. */
#if !ENABLE_RECOMPILED_CODE && !HYBRID_RECOMPILED_CODE
#include <stdint.h>
uint64_t g_frame_count = 0;
#endif

/* g_chunk_yield_count: interleave-chunk yield counter from glue.c.
 * Only defined there under ENABLE_RECOMPILED_CODE; provide a zero for
 * oracle (HYBRID_RECOMPILED_CODE) and interpreter-only builds so
 * cmd_server's mem_write_log reference links. */
#if !ENABLE_RECOMPILED_CODE
#include <stdint.h>
uint64_t g_chunk_yield_count = 0;
#endif
