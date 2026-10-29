#ifndef SEGACD_CATALOG_LOOKUP_H
#define SEGACD_CATALOG_LOOKUP_H
#include "m68k_step.h"
#include <string.h>

/* Cache immutable descriptor discovery, never opcode/extension-byte identity.
 * Every attempted entry still passes m68k_step_execute's CURRENT mapped-byte
 * guards, in the original module order. Unknown suffixes are discovered only
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
    uint8_t span_known;
} SegaCDLookupSlot;
typedef struct {
    SegaCDLookupSlot slots[SEGACD_LOOKUP_SLOTS];
    uint64_t hits,descriptor_searches;
} SegaCDCatalogLookup;

static inline void segacd_catalog_lookup_reset(SegaCDCatalogLookup *cache)
{memset(cache,0,sizeof *cache);}
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
    if(slot->overflow)return segacd_catalog_execute_slow(cpu,modules,count);
    for(unsigned i=0;i<slot->count;i++) {
        M68kStepResult result=(slot->register_only[i]&M68K_STEP_EFFECT_TRACKED_BUS)?
            m68k_step_execute_tracked(cpu,slot->entry[i]):m68k_step_execute(cpu,slot->entry[i]);
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
        M68kStepResult result=(effect&M68K_STEP_EFFECT_TRACKED_BUS)?
            m68k_step_execute_tracked(cpu,entry):m68k_step_execute(cpu,entry);
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
