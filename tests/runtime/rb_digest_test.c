/* Golden XXH64 vectors plus full-byte sensitivity and unaligned snapshots. */
#include "rb_digest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);return 1;}}while(0)
int main(void)
{
    const size_t n=1590965;
    uint8_t *b=malloc(n+8),*copy=malloc(n+8);CHECK(b && copy);
    uint64_t rng=UINT64_C(2654435761);
    for(size_t i=0;i<n;i++){b[i]=(uint8_t)(rng>>56);rng*=UINT64_C(11400714785074694797);}
#if GENESIS_FAST_RB_DIGEST
    CHECK(genesis_rb_digest_bytes(0,b,0)==UINT64_C(0xef46db3751d8e999));
    CHECK(genesis_rb_digest_bytes(2654435761u,b,0)==UINT64_C(0xac75fda2929b17ef));
    CHECK(genesis_rb_digest_bytes(0,b,1)==UINT64_C(0xe934a84adb052768));
    CHECK(genesis_rb_digest_bytes(0,b,14)==UINT64_C(0x8282dcc4994e35c8));
    CHECK(genesis_rb_digest_bytes(0,b,222)==UINT64_C(0xb641ae8cb691c174));
    CHECK(genesis_rb_digest_bytes(2654435761u,b,222)==UINT64_C(0x20cb8ab7ae10c14a));
#else
    CHECK(genesis_rb_digest_bytes(UINT64_C(14695981039346656037),"abc",3)==UINT64_C(0xe71fa2190541574b));
#endif
    const uint64_t seed=UINT64_C(14695981039346656037);
    for(size_t len=0;len<=256;len++)for(size_t off=0;off<8;off++) {
        memcpy(copy+off,b,len);
        CHECK(genesis_rb_digest_bytes(seed,b,len)==genesis_rb_digest_bytes(seed,copy+off,len));
    }
    const uint64_t original=genesis_rb_digest_bytes(seed,b,n);
    for(size_t off=0;off<8;off++){
        memcpy(copy+off,b,n);CHECK(original==genesis_rb_digest_bytes(seed,copy+off,n));
    }
    for(unsigned i=0;i<256;i++) {
        size_t at=i<64?i:i<128?n-1-(i-64):(size_t)(rng%n);
        rng=rng*UINT64_C(6364136223846793005)+1;
        b[at]^=(uint8_t)(1u<<(i&7));
        CHECK(original!=genesis_rb_digest_bytes(seed,b,n));
        b[at]^=(uint8_t)(1u<<(i&7));
        CHECK(original==genesis_rb_digest_bytes(seed,b,n));
    }
    free(copy);free(b);puts("rollback digest golden/alignment/full-byte mutation PASS");return 0;
}
