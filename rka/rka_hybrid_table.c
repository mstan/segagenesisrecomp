/*
 * rka_hybrid_table.c — oracle-build hybrid dispatch table for Rocket Knight.
 *
 * The native (RKARecomp) target does NOT compile this file; only the dev-only
 * RKARecomp_oracle target lists it (see add_recomp_mode in the game-repo
 * CMakeLists). The oracle build runs every function through the clown68000
 * interpreter with NO native overrides — an empty table is exactly what the
 * discovery runtime oracle wants: a clean, trusted execution trace.
 *
 * Required by runner/hybrid.c which iterates g_hybrid_table by index; we
 * provide a single sentinel entry so the array isn't zero-sized (some
 * toolchains warn / pad oddly on 0-element arrays). Populate with verified
 * native overrides only if/when RKA is subjected to per-function divergence
 * verification (mirrors sonic2_hybrid_table.c).
 */
#include "hybrid.h"

HybridEntry g_hybrid_table[] = {
    { 0u, 0 },   /* sentinel — never matched (g_hybrid_table_size = 0) */
};

int g_hybrid_table_size = 0;
