#ifndef SEGACD_CATALOG_INDEX_H
#define SEGACD_CATALOG_INDEX_H
#include "catalog_lookup.h"
#include <stdlib.h>

/* Derived immutable-descriptor discovery, not a code-identity cache. Sparse
 * direct PC pages keep visited PCs resident without hash eviction. Variants
 * retain module order and ALWAYS pass the ordinary live instruction guard.
 * Reset at registry replacement; no state or guest storage is owned here. */
enum { SEGACD_INDEX_PAGE_BITS=10, SEGACD_INDEX_PAGE_SLOTS=1024,
       SEGACD_INDEX_PAGES=8192 };
#define SEGACD_INDEX_DEFAULT_LIMIT ((size_t)8*1024*1024)
typedef SegaCDLookupSlot SegaCDIndexSlot;
typedef struct {
    SegaCDIndexSlot **page[SEGACD_INDEX_PAGES];
    size_t bytes,limit,visited;
    int sealed;
} SegaCDCatalogIndex;

static inline SegaCDCatalogIndex *segacd_catalog_index_create(size_t limit)
{
    if(limit<sizeof(SegaCDCatalogIndex))return NULL;
    SegaCDCatalogIndex *index=(SegaCDCatalogIndex *)calloc(1,sizeof *index);
    if(index){index->bytes=sizeof *index;index->limit=limit;}
    return index;
}
static inline void segacd_catalog_index_destroy(SegaCDCatalogIndex *index)
{
    if(!index)return;
    for(unsigned p=0;p<SEGACD_INDEX_PAGES;++p)if(index->page[p]) {
        for(unsigned i=0;i<SEGACD_INDEX_PAGE_SLOTS;++i) {
            SegaCDIndexSlot *slot=index->page[p][i];
            free(slot);
        }
        free(index->page[p]);
    }
    free(index);
}
static inline void *segacd_catalog_index_alloc(SegaCDCatalogIndex *index,size_t bytes)
{
    if(index->sealed || bytes>index->limit-index->bytes){index->sealed=1;return NULL;}
    void *memory=calloc(1,bytes);
    if(!memory){index->sealed=1;return NULL;}
    index->bytes+=bytes;return memory;
}
static inline SegaCDIndexSlot *segacd_catalog_index_slot(SegaCDCatalogIndex *index,uint32_t pc)
{
    if(pc>0xffffffu || (pc&1u))return NULL;
    unsigned p=pc>>(SEGACD_INDEX_PAGE_BITS+1),i=(pc>>1)&(SEGACD_INDEX_PAGE_SLOTS-1);
    if(!index->page[p]) {
        index->page[p]=(SegaCDIndexSlot **)segacd_catalog_index_alloc(index,
                             SEGACD_INDEX_PAGE_SLOTS*sizeof *index->page[p]);
        if(!index->page[p])return NULL;
    }
    SegaCDIndexSlot *slot=index->page[p][i];
    if(!slot) {
        slot=(SegaCDIndexSlot *)segacd_catalog_index_alloc(index,sizeof *slot);
        if(!slot)return NULL;
        index->page[p][i]=slot;++index->visited;
    }
    return slot;
}
static inline M68kStepResult segacd_catalog_index_execute(SegaCDCatalogIndex *index,
    SegaCDCatalogLookup *fallback,M68kStepContext *cpu,
    const M68kStepModule *const *modules,size_t count)
{
#if SEGACD_CATALOG_LOOKUP_CACHE
    if(index) {
        uint32_t pc=cpu->state.regs.PC;
        unsigned i=((pc>>1)^(pc>>9)^(pc>>17))&(SEGACD_LOOKUP_SLOTS-1);
        SegaCDLookupSlot *front=&fallback->slots[i];
        /* Keep the original compact hot path. The sparse table only saves
         * and restores immutable discovery when a PC evicts that front slot.
         * No new dependent pointer loads occur on a front-cache hit. */
        if(!front->valid || front->pc!=pc) {
            if(front->valid) {
                SegaCDIndexSlot *previous=segacd_catalog_index_slot(index,front->pc);
                if(previous)*previous=*front;
            }
            SegaCDIndexSlot *saved=segacd_catalog_index_slot(index,pc);
            if(saved&&saved->valid)*front=*saved;
        }
    }
#else
    (void)index;
#endif
    return segacd_catalog_execute(fallback,cpu,modules,count);
}
#endif
