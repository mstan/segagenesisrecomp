#ifndef SEGACD_CATALOG_INDEX_H
#define SEGACD_CATALOG_INDEX_H
#include "catalog_lookup.h"
#include <stdlib.h>

/* Derived immutable-descriptor discovery, not a code-identity cache. Sparse
 * direct PC pages keep visited PCs resident without hash eviction. Variants
 * retain module order and ALWAYS pass the ordinary live instruction guard.
 * Reset at registry replacement; no state or guest storage is owned here. */
enum { SEGACD_INDEX_PAGE_BITS=10, SEGACD_INDEX_PAGE_SLOTS=1024,
       SEGACD_INDEX_PAGES=8192, SEGACD_INDEX_INLINE=4 };
#define SEGACD_INDEX_DEFAULT_LIMIT ((size_t)8*1024*1024)
typedef struct {
    const M68kStepEntry **entry;
    const M68kStepEntry *local[SEGACD_INDEX_INLINE];
    size_t next_module,count,capacity;
    int failed;
} SegaCDIndexSlot;
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
            if(slot){if(slot->entry!=slot->local)free(slot->entry);free(slot);}
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
        slot->entry=slot->local;slot->capacity=SEGACD_INDEX_INLINE;
        index->page[p][i]=slot;++index->visited;
    }
    return slot;
}
static inline void segacd_catalog_index_append(SegaCDCatalogIndex *index,
    SegaCDIndexSlot *slot,const M68kStepEntry *entry)
{
    if(slot->failed)return;
    if(slot->count==slot->capacity) {
        if(slot->capacity>SIZE_MAX/2/sizeof *slot->entry){slot->failed=1;return;}
        size_t old_bytes=slot->entry==slot->local?0:slot->capacity*sizeof *slot->entry;
        size_t new_capacity=slot->capacity*2,new_bytes=new_capacity*sizeof *slot->entry;
        /* Allocate before release so a failed growth leaves a valid prefix. */
        const M68kStepEntry **entries=(const M68kStepEntry **)segacd_catalog_index_alloc(index,new_bytes);
        if(!entries){slot->failed=1;return;}
        memcpy(entries,slot->entry,slot->count*sizeof *entries);
        if(old_bytes){free(slot->entry);index->bytes-=old_bytes;}
        slot->entry=entries;slot->capacity=new_capacity;
    }
    slot->entry[slot->count++]=entry;
}
static inline M68kStepResult segacd_catalog_index_execute(SegaCDCatalogIndex *index,
    SegaCDCatalogLookup *fallback,M68kStepContext *cpu,
    const M68kStepModule *const *modules,size_t count)
{
    SegaCDIndexSlot *slot=index?segacd_catalog_index_slot(index,cpu->state.regs.PC):NULL;
    if(!slot)return segacd_catalog_execute(fallback,cpu,modules,count);
    if(slot->failed)return segacd_catalog_execute_slow(cpu,modules,count);
    for(size_t i=0;i<slot->count;++i) {
        M68kStepResult result=m68k_step_execute(cpu,slot->entry[i]);
        if(result!=M68K_STEP_MISS){++fallback->hits;return result;}
    }
    for(size_t i=slot->next_module;i<count;++i) {
        const M68kStepEntry *entry=segacd_catalog_find(modules[i],cpu->state.regs.PC);
        ++fallback->descriptor_searches;slot->next_module=i+1;
        if(!entry)continue;
        segacd_catalog_index_append(index,slot,entry);
        M68kStepResult result=m68k_step_execute(cpu,entry);
        if(result!=M68K_STEP_MISS)return result;
    }
    return M68K_STEP_MISS;
}
#endif
