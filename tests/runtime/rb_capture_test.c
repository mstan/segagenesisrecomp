/* Controlled serializer; production callback and ring, allocator failures. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "retcomm_rbengine/snap_ring.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(2); } ++checks; } while (0)
static unsigned checks,scenarios;
typedef struct Allocation {void *ptr;size_t bytes;} Allocation;
static Allocation allocations[32];
static size_t active,live_bytes,peak_bytes,successful_grows,failed_grows;
static int fail_next;
static size_t find(void *p){for(size_t i=0;i<32;++i)if(allocations[i].ptr==p)return i;CHECK(0);return 0;}
static void record(void *p,size_t n){CHECK(p);for(size_t i=0;i<32;++i)if(!allocations[i].ptr){allocations[i]=(Allocation){p,n};++active;live_bytes+=n;if(live_bytes>peak_bytes)peak_bytes=live_bytes;return;}CHECK(0);}
static void *test_calloc(size_t n,size_t m){if(fail_next){fail_next=0;return NULL;}void*p=calloc(n,m);if(p)record(p,n*m);return p;}
static void test_free(void*p){if(!p)return;size_t i=find(p);live_bytes-=allocations[i].bytes;--active;memset(p,0xdd,allocations[i].bytes);allocations[i]=(Allocation){0};free(p);}
static void *test_realloc(void*p,size_t n){if(fail_next){fail_next=0;++failed_grows;return NULL;}int had=p!=NULL;size_t i=had?find(p):0,old=had?allocations[i].bytes:0;void*next=realloc(p,n);if(next){++successful_grows;if(had){live_bytes-=old;--active;allocations[i]=(Allocation){0};}record(next,n);}return next;}
#define calloc test_calloc
#define realloc test_realloc
#define free test_free
#ifndef GENESIS_TEST_RING_SOURCE
#define GENESIS_TEST_RING_SOURCE "../../external/rbengine/src/snap/rbe_snap_ring.c"
#endif
#include GENESIS_TEST_RING_SOURCE
#ifndef GENESIS_TEST_CAPTURE_SOURCE
#define GENESIS_TEST_CAPTURE_SOURCE "../../runner/rb_capture.c"
#endif
#include GENESIS_TEST_CAPTURE_SOURCE
#undef calloc
#undef realloc
#undef free
static RbeSnapRing *ring;
static size_t wanted=128,expected_size[1000];
static uint8_t seed,expected_seed[1000];
static int fail_save;
static unsigned save_calls;
size_t genesis_rb_bound(void){return wanted;}
size_t genesis_rb_save(void*dst,size_t cap){
 ++save_calls;CHECK(dst&&cap>=wanted);
 for(uint32_t i=0;i<ring->depth;++i)CHECK(!ring->slots[i].valid||ring->slots[i].data!=dst);
 memset(dst,seed,wanted);return fail_save?0:wanted;
}
static void verify(void){
 unsigned count=0;size_t bytes=sizeof *ring+(size_t)ring->depth*sizeof *ring->slots;
 for(uint32_t i=0;i<ring->depth;++i){RbeSnapSlot*s=&ring->slots[i];if(s->data){CHECK(allocations[find(s->data)].bytes==s->capacity);bytes+=s->capacity;CHECK(s->data!=ring->spare.data);}if(!s->valid)continue;++count;CHECK(s->tick<1000&&s->size==expected_size[s->tick]);for(size_t j=0;j<s->size;++j)CHECK(s->data[j]==expected_seed[s->tick]);}
 if(ring->spare.data){CHECK(allocations[find(ring->spare.data)].bytes==ring->spare.capacity);bytes+=ring->spare.capacity;}
 CHECK(count==ring->count&&active<=ring->depth+3u&&live_bytes==bytes);
}
static int capture(uint32_t tick){seed=(uint8_t)(tick*17u+3u);int ok=rbe_snap_ring_capture(ring,tick,genesis_rb_capture,NULL);if(ok){CHECK(tick<1000);expected_seed[tick]=seed;expected_size[tick]=wanted;}verify();return ok;}
static uint64_t history(void){uint64_t h=UINT64_C(14695981039346656037);for(uint32_t i=0;i<ring->depth;++i){RbeSnapSlot*s=&ring->slots[i];h^=s->valid;h*=UINT64_C(1099511628211);if(s->valid){h^=s->tick;h*=UINT64_C(1099511628211);h^=s->size;for(size_t j=0;j<s->size;++j){h^=s->data[j];h*=UINT64_C(1099511628211);}}}return h;}
int main(void){
 ring=rbe_snap_ring_create(3);CHECK(ring);for(uint32_t t=1;t<=16;++t)CHECK(capture(t));CHECK(successful_grows==4);++scenarios;
 size_t grows=successful_grows;for(uint32_t t=17;t<=40;++t)CHECK(capture(t));CHECK(successful_grows==grows);++scenarios;
 uint64_t old=history();uint32_t count=ring->count;fail_save=1;CHECK(!capture(41));fail_save=0;CHECK(history()==old&&ring->count==count&&!rbe_snap_ring_has(ring,41));++scenarios;
 wanted=512;fail_next=1;unsigned before=save_calls;CHECK(!capture(42));CHECK(save_calls==before&&history()==old&&failed_grows==1);CHECK(capture(42));for(uint32_t t=43;t<=47;++t)CHECK(capture(t));CHECK(successful_grows==grows+4);++scenarios;
 grows=successful_grows;wanted=17;for(uint32_t t=48;t<=54;++t)CHECK(capture(t));CHECK(successful_grows==grows);CHECK(capture(53));CHECK(ring->count==3);++scenarios;
 old=history();wanted=4096;fail_save=1;CHECK(!capture(55));fail_save=0;CHECK(history()==old&&ring->spare.capacity==4096);wanted=17;CHECK(capture(55));CHECK(ring->count==3);++scenarios;
 CHECK(rbe_snap_ring_drop_after(ring,53)==2);verify();CHECK(ring->count==1);for(uint32_t t=56;t<=64;++t)CHECK(capture(t));++scenarios;
 /* Exact4KiB capacity fluctuations reuse storage; larger shrink sheds it. */
 grows=successful_grows;wanted=8192;for(uint32_t t=65;t<=72;++t)CHECK(capture(t));CHECK(successful_grows==grows+4);++scenarios;
 grows=successful_grows;wanted=4096;for(uint32_t t=73;t<=76;++t)CHECK(capture(t));CHECK(successful_grows==grows&&ring->spare.capacity==8192);++scenarios;
 old=history();before=save_calls;wanted=4095;uint8_t*spare=ring->spare.data;fail_next=1;CHECK(!capture(77));CHECK(save_calls==before&&history()==old&&ring->spare.data==spare&&ring->spare.capacity==8192);++scenarios;
 CHECK(capture(77));for(uint32_t t=78;t<=84;++t)CHECK(capture(t));CHECK(successful_grows==grows+4&&ring->spare.capacity==4095);for(uint32_t i=0;i<ring->depth;++i)CHECK(ring->slots[i].capacity==4095);++scenarios;
 old=history();wanted=0;before=save_calls;CHECK(!capture(85));CHECK(history()==old&&save_calls==before);++scenarios;
 rbe_snap_ring_clear(ring);verify();CHECK(ring->count==0&&active==2&&ring->spare.data==NULL);wanted=257;CHECK(capture(1));++scenarios;
 rbe_snap_ring_destroy(ring);ring=NULL;CHECK(active==0&&live_bytes==0);++scenarios;
 fail_next=1;CHECK(!rbe_snap_ring_create(3)&&active==0);++scenarios;
 uint8_t *data=NULL;size_t cap=0,n=99;wanted=0;CHECK(!genesis_rb_capture(NULL,0,&data,&cap,&n)&&n==0&&!data&&!cap&&active==0);++scenarios;
 printf("CAPTURE PASS scenarios=%u checks=%u successful_grows=%zu failed_grows=%zu peak_bytes=%zu live=%zu\n",scenarios,checks,successful_grows,failed_grows,peak_bytes,active);return 0;
}
