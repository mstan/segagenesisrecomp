#ifndef SEGACD_CODE_IDENTITY_H
#define SEGACD_CODE_IDENTITY_H
#include "m68k_step.h"
#include <string.h>

/* Derived physical-storage ownership, not guest/snapshot state. Mapping and
 * opaque-observer fences invalidate cached physical-page references. Otherwise
 * max(two monotonically versioned 512-byte pages) is a complete token for the
 * longest (22-byte) instruction; changed tokens resolve the current view again.
 * No hash of guest bytes
 * is trusted. Unknown storage and unowned execution keep the live-byte guard. */
enum { SEGACD_CODE_RANGES=4, SEGACD_CODE_PAGES=2048,
       SEGACD_CODE_PAGE_BITS=9, SEGACD_CODE_GUARDS=1024 };
typedef struct {
    const uint8_t *base;
    size_t bytes,first_page;
} SegaCDCodeRange;
typedef struct {
    const M68kStepEntry *entry;
    const uint8_t *span;
    uint64_t serial;
    const uint64_t *first_page,*last_page;
    const void *owner;
    uint64_t fence;
    int result;
} SegaCDCodeGuard;
typedef struct {
    SegaCDCodeRange ranges[SEGACD_CODE_RANGES];
    uint64_t pages[SEGACD_CODE_PAGES],serial,fence;
    SegaCDCodeGuard guards[SEGACD_CODE_GUARDS];
    uint64_t hits,checks,writes,fences;
    unsigned active,suspended;
} SegaCDCodeIdentity;

static inline uint64_t segacd_code_serial(SegaCDCodeIdentity *id)
{
    if(++id->serial==0) {
        memset(id->guards,0,sizeof id->guards);
        memset(id->pages,0,sizeof id->pages);
        id->serial=id->fence=1;
    }
    return id->serial;
}
static inline void segacd_code_fence(SegaCDCodeIdentity *id)
{id->fence=segacd_code_serial(id);++id->fences;}
static inline void segacd_code_observer(SegaCDCodeIdentity *id,int entering)
{
    if(!id->active)return;
    segacd_code_fence(id);
    if(entering)++id->suspended;
    else if(id->suspended)--id->suspended;
}
static inline void segacd_code_range(SegaCDCodeIdentity *id,unsigned n,
                                     const uint8_t *base,size_t bytes)
{
    size_t first=0;
    for(unsigned i=0;i<n;++i)first+=(id->ranges[i].bytes+511u)>>SEGACD_CODE_PAGE_BITS;
    id->ranges[n].base=base;id->ranges[n].bytes=bytes;id->ranges[n].first_page=first;
}
static inline const SegaCDCodeRange *segacd_code_locate(const SegaCDCodeIdentity *id,
                                                       const uint8_t *p,size_t bytes)
{
    uintptr_t address=(uintptr_t)p;
    for(unsigned i=0;i<SEGACD_CODE_RANGES;++i) {
        const SegaCDCodeRange *r=&id->ranges[i];uintptr_t base=(uintptr_t)r->base;
        if(r->base&&address>=base&&address-base<r->bytes&&bytes<=r->bytes-(address-base))return r;
    }
    return NULL;
}
static inline void segacd_code_write(SegaCDCodeIdentity *id,const uint8_t *p,size_t bytes)
{
    if(!id->active||!bytes)return;
    const SegaCDCodeRange *r=segacd_code_locate(id,p,bytes);
    if(!r){segacd_code_fence(id);return;}
    size_t offset=(size_t)((uintptr_t)p-(uintptr_t)r->base);
    size_t first=r->first_page+(offset>>SEGACD_CODE_PAGE_BITS);
    size_t last=r->first_page+((offset+bytes-1)>>SEGACD_CODE_PAGE_BITS);
    if(last>=SEGACD_CODE_PAGES){segacd_code_fence(id);return;}
    uint64_t serial=segacd_code_serial(id);++id->writes;
    for(size_t page=first;page<=last;++page)id->pages[page]=serial;
}
static inline int segacd_code_guard(SegaCDCodeIdentity *id,const M68kStepEntry *entry,
                                    void *owner,M68kStepCodeSpan view)
{
    if(!id->active||id->suspended||!view)return 0;
    unsigned slot=(unsigned)(((uintptr_t)entry>>4)^((uintptr_t)entry>>14))&(SEGACD_CODE_GUARDS-1);
    SegaCDCodeGuard *guard=&id->guards[slot];
    if(guard->entry==entry&&guard->owner==owner&&guard->fence==id->fence) {
        uint64_t current=*guard->first_page;
        if(*guard->last_page>current)current=*guard->last_page;
        if(guard->serial==current){++id->hits;return guard->result;}
    }
    size_t bytes=(size_t)entry->word_count*2u;
    const uint8_t *span=view(owner,entry->pc,bytes);
    if(!span)return 0;
    const SegaCDCodeRange *r=segacd_code_locate(id,span,bytes);
    if(!r||!bytes||bytes>22)return 0;
    size_t offset=(size_t)((uintptr_t)span-(uintptr_t)r->base);
    size_t first=r->first_page+(offset>>SEGACD_CODE_PAGE_BITS);
    size_t last=r->first_page+((offset+bytes-1)>>SEGACD_CODE_PAGE_BITS);
    if(last>=SEGACD_CODE_PAGES)return 0;
    uint64_t serial=0;
    if(id->pages[first]>serial)serial=id->pages[first];
    if(id->pages[last]>serial)serial=id->pages[last];
    if(guard->entry==entry&&guard->span==span&&guard->fence==id->fence&&guard->serial==serial) {
        guard->owner=owner;++id->hits;return guard->result;
    }
    int result=1;++id->checks;
    for(unsigned n=0;n<entry->word_count;++n)
        if((((unsigned)span[2u*n]<<8)|span[2u*n+1u])!=entry->words[n]){result=-1;break;}
    guard->entry=entry;guard->span=span;guard->serial=serial;guard->result=result;
    guard->owner=owner;guard->fence=id->fence;
    guard->first_page=&id->pages[first];guard->last_page=&id->pages[last];
    return result;
}
#endif
