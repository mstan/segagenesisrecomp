/*
 * sonic2_hybrid_table.c — Sonic 2 dispatch table for the oracle build's
 * hybrid interpreter system. Empty by design: Sonic 2 hasn't yet been
 * subjected to the per-function divergence-verification process that
 * built up Sonic 1's curated list. The oracle build runs every
 * function through clown68000 — no native overrides.
 *
 * As Sonic 2 stabilizes and we identify functions whose recompiled
 * native form has been verified to match interpreter execution
 * bit-exactly (via divergence_diff), they get added here.
 *
 * Required by runner/hybrid.c which iterates g_hybrid_table by index;
 * we provide a single sentinel entry so the array isn't zero-sized
 * (some toolchains warn / pad oddly on 0-element arrays).
 */
#include "hybrid.h"

HybridEntry g_hybrid_table[] = {
    { 0u, 0 },   /* sentinel — never matched (g_hybrid_table_size = 0) */
};

int g_hybrid_table_size = 0;
