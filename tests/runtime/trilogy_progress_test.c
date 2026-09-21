#include "../../sonic3k/trilogy_progress.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void finish_imports(TrProgress *p,unsigned slot,unsigned available)
{
    TrResume resume;unsigned guard=0;
    assert(tr_progress_resolve(p,slot,available,&resume));
    while(tr_stage(resume.stage)->pack){
        assert(++guard<=4);assert(tr_progress_checkpoint(p,slot,resume.stage,0));
        assert(tr_progress_finish_import(p,slot,available,&resume));
    }
}
int main(void)
{
    const char *path="trilogy-progress.srm";uint8_t base[TR_SRAM_BASE],loaded[TR_SRAM_BASE];
    for(unsigned i=0;i<sizeof base;++i)base[i]=(uint8_t)(i*43);
    for(unsigned enrolled=0;enrolled<4;++enrolled){
        remove(path);TrSram store={0};TrProgress p,copy;TrResume resume;
        assert(tr_sram_open(&store,path,loaded));assert(tr_progress_load(&p,&store));
        assert(!p.present&&!p.dirty);assert(tr_progress_store(&p,&store));assert(!store.tail_size);
        for(unsigned slot=0;slot<TR_SLOTS;++slot){
            assert(tr_progress_new(&p,slot,enrolled,slot%4));
            assert(tr_progress_checkpoint(&p,slot,tr_first_stage(enrolled),slot+1));
        }
        assert(tr_progress_store(&p,&store));assert(tr_sram_commit(&store,base));
        /* A native play session changes only the native chapter, and an empty
         * native slot detaches its imported data. Grey emeralds still count
         * toward the shared Chaos set, as in sub_DA1E. */
        TrProgress native_copy=p;uint8_t native[10]={0,0,0,6,0,0,0xAA,0xA8,3,0};TrSlot native_slot;
        assert(tr_native_slot_decode(native,&native_slot));assert(native_slot.stage==0x3070);
        assert(native_slot.chaos==127&&!native_slot.super&&native_slot.entered_sk);
        assert(tr_progress_sync_native(&native_copy,0,native));
        assert(native_copy.chapters[2][0].stage==0x3070);
        assert(!memcmp(native_copy.chapters[0],p.chapters[0],sizeof p.chapters[0]));
        native[0]=0x80;assert(tr_progress_sync_native(&native_copy,0,native));assert(!native_copy.tokens[0]);
        assert(tr_sram_open(&store,path,loaded));assert(!memcmp(loaded,base,sizeof base));
        assert(tr_progress_load(&copy,&store));assert(!copy.dirty);
        assert(!memcmp(copy.campaign.slots,p.campaign.slots,sizeof p.campaign.slots));
        for(unsigned available=0;available<4;++available)for(unsigned slot=0;slot<TR_SLOTS;++slot){
            TrProgress before=copy;
            assert(tr_progress_resolve(&copy,slot,available,&resume));
            assert(resume.stage==tr_first_stage(enrolled&available));
            assert(resume.unavailable_packs==(enrolled&~available));
            assert(!memcmp(&before,&copy,sizeof copy)); /* Resolving never erases a missing checkpoint. */
            if(enrolled&1){assert(tr_progress_resolve(&copy,slot,3,&resume));assert(resume.checkpoint==slot+1);}
        }
        /* Progressing in S3K while a donor is absent leaves the donor checkpoint
         * waiting. Native completion alone cannot clear an unfinished trilogy. */
        assert(tr_progress_checkpoint(&copy,0,0x3010,7));assert(tr_progress_finish_native(&copy,0));
        assert(copy.campaign.slots[0].state==(enrolled?TR_ACTIVE:TR_COMPLETE));
        assert(tr_progress_resolve(&copy,0,0,&resume));assert(resume.stage==0x3010);
        if(enrolled){assert(tr_progress_resolve(&copy,0,enrolled,&resume));
            assert(resume.stage==tr_first_stage(enrolled));assert(resume.checkpoint==1);}
        finish_imports(&copy,0,3);assert(copy.campaign.slots[0].state==TR_COMPLETE);
        assert(tr_progress_select_zone(&copy,0,0x3001,3));
        uint8_t cleared_native[10]={2,0,0,14,0,0,0x55,0x54,3,0};
        assert(tr_progress_sync_native(&copy,0,cleared_native));
        assert(tr_progress_resolve(&copy,0,3,&resume)&&resume.stage==0x3001);
        assert(tr_progress_select_zone(&copy,0,0x3000,0));
        assert(tr_progress_select_zone(&copy,0,0x1000,3)==((enrolled&1)!=0));
        assert(!tr_progress_select_zone(&copy,0,0x1000,0));
        assert(tr_progress_store(&copy,&store));assert(tr_sram_commit(&store,base));
        assert(tr_progress_load(&copy,&store));assert(copy.campaign.slots[0].state==TR_COMPLETE);
        /* Installing another ROM does not modify membership; enrolling it does. */
        if(!(enrolled&1)){assert(tr_progress_resolve(&copy,0,3,&resume));assert(resume.stage==0x3000);
            assert(tr_progress_add_pack(&copy,0,TR_PACK_S1));assert(copy.campaign.slots[0].state==TR_ACTIVE);
            assert(tr_progress_resolve(&copy,0,3,&resume));assert(resume.stage==0x1000);}
        uint32_t old_token=copy.tokens[0];assert(tr_progress_delete(&copy,0));
        assert(tr_progress_new(&copy,0,3,TR_KNUCKLES));assert(copy.tokens[0]!=old_token);
        assert(tr_progress_resolve(&copy,0,3,&resume));assert(resume.stage==0x1000&&resume.checkpoint==0);
        assert(!tr_progress_collect(&copy,0,127,1));assert(tr_progress_collect(&copy,0,127,0));
        assert(tr_progress_checkpoint(&copy,0,0x3070,0));assert(tr_progress_collect(&copy,0,127,1));
        assert(tr_progress_store(&copy,&store));assert(tr_sram_commit(&store,base));
        /* A future S1 record protects S1 only. S2 and native progress still work,
         * and unrelated edits preserve the future payload exactly. */
        assert(tr_sram_set(&store,"S1EX",99,"future chapter",14));assert(tr_progress_load(&copy,&store));
        assert(copy.protected_records==TR_RECORD_S1);
        assert(tr_progress_resolve(&copy,0,3,&resume));assert(resume.stage==0x2000&&resume.unavailable_packs==1);
        assert(!tr_progress_checkpoint(&copy,0,0x1000,0));
        assert(tr_progress_checkpoint(&copy,0,0x2000,2));assert(tr_progress_store(&copy,&store));
        unsigned version;size_t size;const uint8_t *data=tr_sram_get(&store,"S1EX",&version,&size);
        assert(data&&version==99&&size==14&&!memcmp(data,"future chapter",14));
        assert(tr_sram_set(&store,"CAMP",99,"future campaign",15));
        assert(!tr_progress_load(&copy,&store));assert(!tr_progress_new(&copy,0,0,0));
        assert(tr_sram_commit(&store,base));assert(!memcmp(store.original,base,sizeof base));
        tr_sram_dispose(&store);
    }
    for(unsigned character=0;character<4;++character){
        uint8_t native[10]={3,0,0,14,0,0,0xFF,0xFC,3,0};TrSlot slot;
        native[2]=(uint8_t)(character<<4);assert(tr_native_slot_decode(native,&slot));
        assert(slot.state==TR_COMPLETE&&slot.chaos==127&&slot.super==127);
        assert(slot.stage==(character==TR_KNUCKLES?0x30A1:character==TR_TAILS?0x30B0:0x30C0));
    }
    remove(path);remove("trilogy-progress.srm.bak");remove("trilogy-progress.srm.lock");
    puts("trilogy progress: file/ROM permutations, suspended checkpoints, completion, enrollment, slot reuse and future versions passed");
    return 0;
}
