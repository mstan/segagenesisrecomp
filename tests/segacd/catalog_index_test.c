#include "catalog_index.h"
#include <stdio.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint16_t live_words[2];
static uint32_t reads[256];
static unsigned count;
static M68kStepBusResult fetch(void *unused,uint32_t address,uint16_t *word)
{
    (void)unused;CHECK(count<256);reads[count++]=address;
    *word=live_words[(address>>1)&1u];return M68K_BUS_OK;
}
static void operation(M68kStepContext *cpu){++cpu->state.regs.D[0];}
static void compare(SegaCDCatalogIndex *index,SegaCDCatalogLookup *fallback,
    const M68kStepModule *const *modules,size_t size,uint32_t pc)
{
    M68kStepContext a,b;M68kStepBus bus={0};bus.fetch16=fetch;
    m68k_step_init(&a,&bus,NULL);m68k_step_init(&b,&bus,NULL);
    a.state.regs.PC=b.state.regs.PC=pc;a.state.regs.SR=b.state.regs.SR=0x2700;
    count=0;M68kStepResult expected=segacd_catalog_execute_slow(&a,modules,size);
    unsigned expected_count=count;uint32_t trace[256];memcpy(trace,reads,count*sizeof *trace);
    count=0;M68kStepResult actual=segacd_catalog_index_execute(index,fallback,&b,modules,size);
    CHECK(expected==actual&&!memcmp(&a.state,&b.state,sizeof a.state));
    CHECK(count==expected_count&&!memcmp(trace,reads,count*sizeof *trace));
}
int main(void)
{
    static SegaCDCatalogLookup fallback;
    static M68kStepEntry entries[32];M68kStepModule modules[32];const M68kStepModule *list[32];
    for(unsigned i=0;i<32;++i) {
        entries[i]=(M68kStepEntry){0x1000,{0x303c,0},2,8,operation};entries[i].words[1]=(uint16_t)i;
        modules[i]=(M68kStepModule){"synthetic",0x1000,4,0,&entries[i],1};list[i]=&modules[i];
    }
    CHECK(!segacd_catalog_index_create(1));
    SegaCDCatalogIndex *index=segacd_catalog_index_create(SEGACD_INDEX_DEFAULT_LIMIT);CHECK(index);
    live_words[0]=0x303c;live_words[1]=31;
    compare(index,&fallback,list,32,0x1000); /* retains original wide-variant fallback */
    size_t searches=fallback.descriptor_searches;
    for(unsigned i=0;i<1000;++i){live_words[1]=(uint16_t)(i%32);compare(index,&fallback,list,32,0x1000);}
    CHECK(searches==32&&fallback.descriptor_searches==searches&&index->visited==1);
    live_words[0]=0xffff;compare(index,&fallback,list,32,0x1000); /* mutable opcode */
    live_words[0]=0x303c;live_words[1]=32;compare(index,&fallback,list,32,0x1000); /* no variant */
    compare(index,&fallback,list,32,0x1001);compare(index,&fallback,list,32,0x1000000);
    segacd_catalog_index_destroy(index);
    segacd_catalog_lookup_reset(&fallback);
    /* Registry replacement has a new empty derivation; earlier precedence
     * and not-yet-seen suffixes are rediscovered with exact read traces. */
    index=segacd_catalog_index_create(SEGACD_INDEX_DEFAULT_LIMIT);CHECK(index);
    const M68kStepModule *replacement[]={&modules[31],&modules[0]};
    live_words[1]=0;compare(index,&fallback,replacement,2,0x1000);
    live_words[1]=31;compare(index,&fallback,replacement,2,0x1000);
    segacd_catalog_index_destroy(index);
    /* Budget exhaustion must never repeat guards or lose the front cache. */
    segacd_catalog_lookup_reset(&fallback);
    index=segacd_catalog_index_create(sizeof *index+SEGACD_INDEX_PAGE_SLOTS*sizeof(void*)+sizeof(SegaCDIndexSlot));CHECK(index);
    live_words[1]=31;compare(index,&fallback,list,32,0x1000);
    for(unsigned i=0;i<32;++i){live_words[1]=(uint16_t)i;compare(index,&fallback,list,32,0x1000);}
    compare(index,&fallback,list,32,0x4000); /* missing page after budget exhaustion */
    CHECK(index->sealed);
    CHECK(index->bytes<=index->limit);segacd_catalog_index_destroy(index);
    /* Many PCs remain resident even when they would evict the old128 slots. */
    static M68kStepEntry many[2048];M68kStepModule large={"many",0x10000,8192,0,many,2048};
    const M68kStepModule *large_list[]={&large};
    for(unsigned i=0;i<2048;++i)many[i]=(M68kStepEntry){0x10000u+i*4u,{0x303c,1},2,8,operation};
    segacd_catalog_lookup_reset(&fallback);index=segacd_catalog_index_create(SEGACD_INDEX_DEFAULT_LIMIT);CHECK(index);
    live_words[1]=1;
    for(unsigned pass=0;pass<3;++pass)for(unsigned i=0;i<2048;++i)compare(index,&fallback,large_list,1,many[i].pc);
    CHECK(index->visited==2048&&fallback.descriptor_searches==2048&&fallback.hits==4096);
    CHECK(index->bytes<=index->limit);segacd_catalog_index_destroy(index);
    puts("catalog PC index:32live variants,2048retained PCs, module replacement, budget fallback and exact CPU/fetch traces PASS");
    return 0;
}
