#ifndef HYBRID_H
#define HYBRID_H

/*
 * hybrid.h — Hybrid interpreter + native override system.
 *
 * When HYBRID_RECOMPILED_CODE is defined, the interpreter continues to run
 * as normal, but specific 68K functions can be replaced with native C
 * implementations one at a time.
 *
 * The hook runs at a pre-instruction boundary inside clown68000.c's
 * instruction loop, which is safe for calling user code (unlike the old
 * ROM-read callback approach that caused audio buzzing).
 */

#include "clowncommon.h"
#include "clownmdemu.h"

/* A single dispatch table entry. Tagged so game_spec.h can forward-
 * declare it without pulling in the clownmdemu surface. */
typedef struct HybridEntry {
    cc_u32l addr;           /* 68K byte address of the function entry point */
    void  (*fn)(void);      /* native C implementation                       */
} HybridEntry;

/* Defined in hybrid_table.c */
extern HybridEntry g_hybrid_table[];
extern int         g_hybrid_table_size;

/*
 * Initialise the hybrid dispatch system.
 * Stores the ClownMDEmu pointer and installs the pre-instruction hook.
 */
void HybridInit(ClownMDEmu *emu);

#endif /* HYBRID_H */
