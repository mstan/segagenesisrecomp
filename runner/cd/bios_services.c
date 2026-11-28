#include "bios_services.h"
#include "bios_tables.h"
#include "sha256.h"
#include <string.h>

enum { MAX_MODULES=512 };
static SegaCDCoordinator *owner;
static M68kStepOperation original;
static unsigned original_cycles;
static M68kStepEntry entry;
static M68kStepModule module;
static const M68kStepModule *modules[MAX_MODULES];
static uint64_t completed,declined;

static void protection_tables(M68kStepContext *cpu)
{
    if(owner && cpu==&owner->cpu[SEGACD_SUB] &&
       segacd_bios_protection_service(owner->devices,cpu)) {++completed;return;}
    ++declined;
    cpu->charge=original_cycles;
    original(cpu);
}
void segacd_bios_services_install(SegaCDCoordinator *c,const void *bios,size_t bytes)
{
    static const uint8_t expected[32]={
        0x80,0x73,0x84,0x67,0x8e,0xf2,0x59,0x49,0x4d,0x94,0x3e,0xdd,0xb6,0x95,0x4d,0xfc,
        0x70,0x69,0x69,0x3d,0x0d,0xde,0x0c,0x42,0x4e,0x20,0x28,0x48,0x8d,0x88,0xe8,0x1a};
    if(!c || !c->devices)return;
    /* A title may prepend its own modules after this installation. Find our
     * identity anywhere in the registry; never capture our wrapper as its own
     * fallback or overwrite a live installation on a declined request. */
    for(size_t m=0;m<c->module_count[SEGACD_SUB];++m)
        if(c->modules[SEGACD_SUB][m]==&module)return;
    if(!bios || bytes!=SEGACD_BIOS_BYTES || c->busy[0] || c->busy[1] || c->external_active)return;
    Sha256 sha;uint8_t digest[32];sha_init(&sha);sha_update(&sha,bios,bytes);sha_final(&sha,digest);
    if(memcmp(digest,expected,sizeof digest))return;
    if(c->module_count[SEGACD_SUB]>=MAX_MODULES)return;
    const M68kStepEntry *found=NULL;
    for(size_t m=0;m<c->module_count[SEGACD_SUB]&&!found;++m) {
        const M68kStepModule *candidate=c->modules[SEGACD_SUB][m];
        for(size_t i=0;i<candidate->count;++i) {
            const M68kStepEntry *e=&candidate->entries[i];
            /* MOVEM.L D6-D7,-(SP), the qualified helper's entry ABI. Copy
             * actual generated guard metadata rather than firmware bytes. */
            if(e->pc==0x50d8u && e->word_count==2 && e->words[0]==0x48e7u && e->words[1]==0x0300u) {found=e;break;}
        }
    }
    if(!found)return;
    original=found->execute;original_cycles=found->cycles;entry=*found;entry.execute=protection_tables;
    /* A bounded native entry/return charge, not the old sort's cycle count.
     * Standard firmware computation has no minimum-duration contract. */
    entry.cycles=40;
    memset(&module,0,sizeof module);module.name="Sega CD firmware protection tables";
    module.image_base=0x50d8u;module.image_size=0x5174u-0x50d8u;module.entries=&entry;module.count=1;
    modules[0]=&module;
    memcpy(modules+1,c->modules[SEGACD_SUB],c->module_count[SEGACD_SUB]*sizeof *modules);
    owner=c;completed=declined=0;
    if(!segacd_coordinator_modules(c,SEGACD_SUB,modules,c->module_count[SEGACD_SUB]+1u))owner=NULL;
}
void segacd_bios_services_stats(uint64_t *done,uint64_t *fallback)
{if(done)*done=completed;if(fallback)*fallback=declined;}
