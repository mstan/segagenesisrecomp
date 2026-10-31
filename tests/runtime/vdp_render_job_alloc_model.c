/* This object exercises allocation failure through the unchanged production
 * interface; no failure switch or allocator override reaches production. */
#include <stddef.h>
static unsigned allocation_attempts;
static void *deny_plane_cache(size_t count,size_t size)
{
    (void)count;(void)size;++allocation_attempts;return NULL;
}
unsigned gvdp_test_alloc_attempts(void){return allocation_attempts;}
#define GENESIS_RENDER_REUSE_CALLOC deny_plane_cache
#include "../../runner/video/genesis_vdp.c"
