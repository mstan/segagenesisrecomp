/* sonic3_hybrid_table.c — oracle sandbox-compare table (runner/hybrid.c).
 * Empty by default; populate with function entry PCs to diff the recompiled
 * version against the interpreter during a divergence investigation. */
#include "hybrid.h"

HybridEntry g_hybrid_table[] = {
    { 0u, 0 },   /* sentinel — never matched (g_hybrid_table_size = 0) */
};

int g_hybrid_table_size = 0;
