#ifndef SEGACD_CATALOG_LOOKUP_H
#define SEGACD_CATALOG_LOOKUP_H
#include "m68k_step.h"
#include <string.h>

/* Ordinary execution caches descriptor discovery and checks CURRENT bytes.
 * An explicitly qualified immutable-code lease may retain its first exact
 * variant only until that lease expires. Unknown suffixes are discovered only
 * when the known prefix misses, so cold execution does no speculative work.
 * Registry replacement resets the cache. Fixed storage has no allocation or
 * lifetime contract; long variant lists use the original universal search. */
#ifndef SEGACD_CATALOG_LOOKUP_CACHE
#define SEGACD_CATALOG_LOOKUP_CACHE 1
#endif
enum { SEGACD_LOOKUP_SLOTS=128, SEGACD_LOOKUP_VARIANTS=8 };
typedef struct {
    uint32_t pc;
    size_t next_module;
    const M68kStepEntry *entry[SEGACD_LOOKUP_VARIANTS];
    uint8_t valid,count,overflow;
    uint8_t register_only[SEGACD_LOOKUP_VARIANTS];
    const M68kStepRegisterSpan *span;
    const M68kStepRegisterSpan *span_fallback;
    uint8_t span_known,span_control;
    /* Derived proof is usable only while its exact owner lease is active. */
    uint64_t code_epoch;
    const M68kStepEntry *owned_entry;
    const M68kStepRegisterSpan *owned_span;
    uint8_t owned_tracked;
} SegaCDLookupSlot;
typedef struct {
    SegaCDLookupSlot slots[SEGACD_LOOKUP_SLOTS];
    uint64_t hits,descriptor_searches;
    uint64_t code_epoch,code_reuses;
    uint32_t code_begin,code_end;
} SegaCDCatalogLookup;

static inline void segacd_catalog_lookup_reset(SegaCDCatalogLookup *cache)
{memset(cache,0,sizeof *cache);}
static inline int segacd_catalog_code_owned(const SegaCDCatalogLookup *cache,const SegaCDLookupSlot *slot)
{return cache->code_epoch && cache->code_epoch==slot->code_epoch;}
static inline int segacd_catalog_code_claim(SegaCDCatalogLookup *cache,SegaCDLookupSlot *slot,uint32_t pc,size_t bytes)
{
    if(!cache->code_epoch || pc<cache->code_begin || pc>=cache->code_end || bytes>cache->code_end-pc)return 0;
    if(slot->code_epoch!=cache->code_epoch) {
        slot->owned_entry=NULL;slot->owned_span=NULL;slot->code_epoch=cache->code_epoch;
    }
    return 1;
}
static inline M68kStepResult segacd_catalog_code_execute(SegaCDCatalogLookup *cache,SegaCDLookupSlot *slot,
    M68kStepContext *cpu,const M68kStepEntry *entry,int tracked)
{
#if defined(M68K_STEP_HAS_CODE_INTERVALS) && M68K_STEP_HAS_CODE_INTERVALS
    if(segacd_catalog_code_owned(cache,slot)&&slot->owned_entry==entry) {
        M68kStepResult result=m68k_step_execute_validated(cpu,entry,tracked);
        if(result==M68K_STEP_OK)++cache->code_reuses;
        return result;
    }
#endif
    M68kStepResult result=tracked?m68k_step_execute_tracked(cpu,entry):m68k_step_execute(cpu,entry);
    if(result==M68K_STEP_OK&&segacd_catalog_code_claim(cache,slot,entry->pc,(size_t)entry->word_count*2u)) {
        slot->owned_entry=entry;slot->owned_tracked=(uint8_t)tracked;
    }
    return result;
}
static inline M68kStepResult segacd_catalog_execute_slow(M68kStepContext *cpu,
    const M68kStepModule *const *modules,size_t count)
{
    M68kStepResult result=M68K_STEP_MISS;
    for(size_t i=0;i<count;i++) {
        result=m68k_step_module_execute(cpu,modules[i]);
        if(result!=M68K_STEP_MISS)break;
    }
    return result;
}
static inline const M68kStepEntry *segacd_catalog_find(const M68kStepModule *m,uint32_t pc)
{
    if(!m || !m->entries)return NULL;
    size_t lo=0,hi=m->count;
    while(lo<hi) {
        size_t mid=lo+(hi-lo)/2;
        if(m->entries[mid].pc<pc)lo=mid+1;
        else if(m->entries[mid].pc>pc)hi=mid;
        else return &m->entries[mid];
    }
    return NULL;
}
static inline unsigned segacd_catalog_register_effect(const M68kStepEffectModule *effects,
    size_t effect_count,const M68kStepModule *module,const M68kStepEntry *entry)
{
    for(size_t i=0;i<effect_count;++i)if(effects[i].module==module)
        return effects[i].register_only?effects[i].register_only[entry-module->entries]:0;
    return 0;
}
static inline M68kStepResult segacd_catalog_execute_qualified(SegaCDCatalogLookup *cache,
    M68kStepContext *cpu,const M68kStepModule *const *modules,size_t count,
    const M68kStepEffectModule *effects,size_t effect_count)
{
#if SEGACD_CATALOG_LOOKUP_CACHE
    uint32_t pc=cpu->state.regs.PC;
    unsigned index=((pc>>1)^(pc>>9)^(pc>>17))&(SEGACD_LOOKUP_SLOTS-1);
    SegaCDLookupSlot *slot=&cache->slots[index];
    if(!slot->valid || slot->pc!=pc) {
        memset(slot,0,sizeof *slot);slot->valid=1;slot->pc=pc;
    }
#if defined(M68K_STEP_HAS_CODE_INTERVALS) && M68K_STEP_HAS_CODE_INTERVALS
    if(segacd_catalog_code_owned(cache,slot)&&slot->owned_entry)
        return segacd_catalog_code_execute(cache,slot,cpu,slot->owned_entry,slot->owned_tracked);
#endif
    if(slot->overflow)return segacd_catalog_execute_slow(cpu,modules,count);
    for(unsigned i=0;i<slot->count;i++) {
        M68kStepResult result=segacd_catalog_code_execute(cache,slot,cpu,slot->entry[i],
            (slot->register_only[i]&M68K_STEP_EFFECT_TRACKED_BUS)!=0);
        if(result!=M68K_STEP_MISS){++cache->hits;return result;}
    }
    for(size_t i=slot->next_module;i<count;i++) {
        const M68kStepEntry *entry=segacd_catalog_find(modules[i],pc);
        ++cache->descriptor_searches;slot->next_module=i+1;
        if(!entry)continue;
        if(!slot->overflow) {
            if(slot->count<SEGACD_LOOKUP_VARIANTS) {
                slot->register_only[slot->count]=(uint8_t)segacd_catalog_register_effect(effects,effect_count,modules[i],entry);
                slot->entry[slot->count++]=entry;
            }
            else slot->overflow=1;
        }
        unsigned effect=segacd_catalog_register_effect(effects,effect_count,modules[i],entry);
        M68kStepResult result=segacd_catalog_code_execute(cache,slot,cpu,entry,(effect&M68K_STEP_EFFECT_TRACKED_BUS)!=0);
        if(result!=M68K_STEP_MISS)return result;
    }
    return M68K_STEP_MISS;
#else
    (void)cache;
    (void)effects;(void)effect_count;
    return segacd_catalog_execute_slow(cpu,modules,count);
#endif
}
static inline M68kStepResult segacd_catalog_execute(SegaCDCatalogLookup *cache,
    M68kStepContext *cpu,const M68kStepModule *const *modules,size_t count)
{return segacd_catalog_execute_qualified(cache,cpu,modules,count,NULL,0);}
#endif
