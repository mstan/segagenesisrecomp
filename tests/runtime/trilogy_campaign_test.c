#include "../../sonic3k/trilogy_campaign.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    for(unsigned packs=0;packs<4;++packs)for(unsigned ch=0;ch<4;++ch){
        TrCampaign d={0},copy={0};uint8_t bytes[TR_SAVE_BYTES];uint32_t sequence=0;
        assert(tr_campaign_new(&d,0,packs,ch));assert(tr_campaign_valid(&d));
        unsigned id=d.slots[0].stage,steps=0;
        while(id!=0x3000){assert(++steps<=4);id=tr_next_import(id,packs);assert(id);assert(tr_campaign_advance(&d.slots[0],id));}
        assert(steps==((packs&1)?3u:0u)+((packs&2)?1u:0u));
        assert(!tr_campaign_select_zone(&d.slots[0],0x3000));
        assert(!tr_campaign_advance(&d.slots[0],0x30A1) || ch==TR_KNUCKLES);
        assert(tr_campaign_advance(&d.slots[0],0x3000));
        assert(!tr_campaign_collect(&d.slots[0],127,1));
        assert(tr_campaign_collect(&d.slots[0],127,0));
        assert(tr_campaign_advance(&d.slots[0],0x3070));
        assert(tr_campaign_collect(&d.slots[0],127,1));
        assert(tr_campaign_complete(&d.slots[0]));
        assert(tr_campaign_select_zone(&d.slots[0],0x1000)==((packs&1)!=0));
        assert(tr_campaign_select_zone(&d.slots[0],0x2000)==((packs&2)!=0));
        assert(tr_campaign_select_zone(&d.slots[0],0x3000));
        assert(tr_campaign_select_zone(&d.slots[0],0x1001)==((packs&1)!=0));
        assert(tr_campaign_select_zone(&d.slots[0],0x3001));
        assert(tr_campaign_encode(&d,42,bytes));
        assert(tr_campaign_decode(bytes,sizeof bytes,&copy,&sequence,NULL,0));
        assert(sequence==42 && !memcmp(&d,&copy,sizeof d));
        for(unsigned i=0;i<TR_SAVE_BYTES;++i){bytes[i]^=1;assert(!tr_campaign_decode(bytes,sizeof bytes,&copy,&sequence,NULL,0));bytes[i]^=1;assert(sequence==42 && !memcmp(&d,&copy,sizeof d));}
        assert(!tr_campaign_decode(bytes,sizeof bytes-1,&copy,&sequence,NULL,0));
    }
    TrCampaign data={0};assert(tr_campaign_new(&data,7,3,TR_SONIC));
    assert(!tr_campaign_new(&data,8,0,0));assert(!tr_campaign_new(&data,0,4,0));
    assert(!tr_campaign_new(&data,0,0,4));assert(!tr_campaign_advance(&data.slots[7],0x1003));
    assert(tr_campaign_delete(&data,7));assert(tr_campaign_valid(&data));
    puts("trilogy campaign: routes, characters, emeralds and transactional codec passed");
    return 0;
}
