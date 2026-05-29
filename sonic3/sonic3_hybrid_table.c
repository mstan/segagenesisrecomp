/* sonic3_hybrid_table.c — hybrid table for the Sonic-3-standalone oracle build.
 * Empty at bring-up; populate after the oracle sandbox-compare confirms which
 * functions diverge (see runner/hybrid.c). Single sentinel so the array is
 * never zero-sized. */
#include "hybrid.h"

HybridEntry g_hybrid_table[] = {
    { 0u, 0 },   /* sentinel — never matched (g_hybrid_table_size = 0) */
};

int g_hybrid_table_size = 0;
