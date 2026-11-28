#ifndef GENESIS_RB_DIGEST_H
#define GENESIS_RB_DIGEST_H
#include <stddef.h>
#include <stdint.h>

/* Build-fixed. Peers still require identical executable/content identities;
 * this does not permit cross-build netplay or change the snapshot byte domain. */
#ifndef GENESIS_FAST_RB_DIGEST
#define GENESIS_FAST_RB_DIGEST 1
#endif
#if GENESIS_FAST_RB_DIGEST
#define XXH_INLINE_ALL
#define XXH_NO_XXH3
#define XXH_NO_STREAM
#include "external/xxhash/xxhash.h"
#define GENESIS_RB_DIGEST_IMPL "XXH64-v0.8.3"
#else
#define GENESIS_RB_DIGEST_IMPL "FNV1a64"
#endif

static inline uint64_t genesis_rb_digest_bytes(uint64_t h,const void *src,size_t n)
{
#if GENESIS_FAST_RB_DIGEST
    return XXH64(src,n,h);
#else
    const uint8_t *b=(const uint8_t *)src;
    for(size_t i=0;i<n;i++){h^=b[i];h*=UINT64_C(1099511628211);}
    return h;
#endif
}
#endif
