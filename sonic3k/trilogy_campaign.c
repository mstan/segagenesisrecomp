#include "trilogy_campaign.h"
#include <stdio.h>
#include <string.h>

/* Native ordering is deliberately explicit: Flying Battery follows Mushroom
 * Hill, HPZ uses $1601, and SSZ2 is Knuckles' final arena, not a second act. */
const TrStage tr_stages[]={
    {0x1000,0xFFFF,TR_PACK_S1,1,"GREEN HILL"},
    {0x1001,0xFFFF,TR_PACK_S1,2,"GREEN HILL"},
    {0x1002,0xFFFF,TR_PACK_S1,3,"GREEN HILL"},
    {0x2000,0xFFFF,TR_PACK_S2,1,"EMERALD HILL"},
    {0x3000,0x0000,0,1,"ANGEL ISLAND"}, {0x3001,0x0001,0,2,"ANGEL ISLAND"},
    {0x3010,0x0100,0,1,"HYDROCITY"}, {0x3011,0x0101,0,2,"HYDROCITY"},
    {0x3020,0x0200,0,1,"MARBLE GARDEN"}, {0x3021,0x0201,0,2,"MARBLE GARDEN"},
    {0x3030,0x0300,0,1,"CARNIVAL NIGHT"}, {0x3031,0x0301,0,2,"CARNIVAL NIGHT"},
    {0x3050,0x0500,0,1,"ICECAP"}, {0x3051,0x0501,0,2,"ICECAP"},
    {0x3060,0x0600,0,1,"LAUNCH BASE"}, {0x3061,0x0601,0,2,"LAUNCH BASE"},
    {0x3070,0x0700,0,1,"MUSHROOM HILL"}, {0x3071,0x0701,0,2,"MUSHROOM HILL"},
    {0x3040,0x0400,0,1,"FLYING BATTERY"}, {0x3041,0x0401,0,2,"FLYING BATTERY"},
    {0x3080,0x0800,0,1,"SANDOPOLIS"}, {0x3081,0x0801,0,2,"SANDOPOLIS"},
    {0x3090,0x0900,0,1,"LAVA REEF"}, {0x3091,0x0901,0,2,"LAVA REEF"},
    {0x3161,0x1601,0,1,"HIDDEN PALACE"},
    {0x30A0,0x0A00,0,1,"SKY SANCTUARY"}, {0x30A1,0x0A01,0,2,"SKY SANCTUARY"},
    {0x30B0,0x0B00,0,1,"DEATH EGG"}, {0x30B1,0x0B01,0,2,"DEATH EGG"},
    {0x30C0,0x0C00,0,1,"DOOMSDAY"}
};
const unsigned tr_stage_count=sizeof tr_stages/sizeof *tr_stages;
const TrStage *tr_stage(unsigned id)
{ for(unsigned i=0;i<tr_stage_count;++i)if(tr_stages[i].id==id)return &tr_stages[i]; return NULL; }
unsigned tr_first_stage(unsigned packs)
{ return packs&TR_PACK_S1?0x1000:packs&TR_PACK_S2?0x2000:0x3000; }
unsigned tr_next_import(unsigned id,unsigned packs)
{
    const TrStage *s=tr_stage(id);
    if((packs&~3u)||!s||!s->pack||!(packs&s->pack))return 0;
    if(id==0x1000||id==0x1001)return id+1;
    return id==0x1002 && (packs&TR_PACK_S2)?0x2000:0x3000;
}
static int reachable(const TrSlot *s,const TrStage *stage)
{
    if(!stage||(stage->pack && !(stage->pack&s->packs)))return 0;
    if(stage->id==0x30A1)return s->character==TR_KNUCKLES;
    if(s->character==TR_KNUCKLES && stage->id>=0x30B0 && stage->id<=0x30C0)return 0;
    if(stage->id==0x30C0)return s->character<=TR_SONIC && s->chaos==127;
    return 1;
}
int tr_slot_valid(const TrSlot *s)
{
    if(!s)return 0;
    if(!s->state)return !(s->stage|s->packs|s->character|s->chaos|s->super|s->entered_sk);
    return s->state<=TR_COMPLETE && s->packs<=3 && s->character<=TR_KNUCKLES &&
        s->chaos<=127 && s->super<=127 && s->entered_sk<=1 &&
        (!s->super || (s->entered_sk && s->chaos==127)) && reachable(s,tr_stage(s->stage));
}
int tr_campaign_valid(const TrCampaign *d)
{ if(!d)return 0;for(unsigned i=0;i<TR_SLOTS;++i)if(!tr_slot_valid(&d->slots[i]))return 0;return 1; }
int tr_campaign_new(TrCampaign *d,unsigned i,unsigned packs,unsigned character)
{
    if(!d||i>=TR_SLOTS||packs>3||character>TR_KNUCKLES)return 0;
    TrSlot s={0};s.state=TR_ACTIVE;s.packs=(uint8_t)packs;s.character=(uint8_t)character;
    s.stage=(uint16_t)tr_first_stage(packs);d->slots[i]=s;return 1;
}
int tr_campaign_delete(TrCampaign *d,unsigned i)
{if(!d||i>=TR_SLOTS)return 0;memset(&d->slots[i],0,sizeof d->slots[i]);return 1;}
int tr_campaign_advance(TrSlot *s,unsigned id)
{
    if(!tr_slot_valid(s)||!s->state||!reachable(s,tr_stage(id)))return 0;
    s->stage=(uint16_t)id;
    if(id==0x3070)s->entered_sk=1;
    return 1;
}
int tr_campaign_collect(TrSlot *s,unsigned chaos,unsigned super)
{
    if(!tr_slot_valid(s)||!s->state||chaos>127||super>127)return 0;
    TrSlot next=*s;next.chaos|=(uint8_t)chaos;next.super|=(uint8_t)super;
    if(!tr_slot_valid(&next))return 0;*s=next;return 1;
}
int tr_campaign_complete(TrSlot *s)
{if(!tr_slot_valid(s)||!s->state)return 0;s->state=TR_COMPLETE;return 1;}
int tr_campaign_select_zone(TrSlot *s,unsigned id)
{
    const TrStage *stage=tr_stage(id);
    if(!tr_slot_valid(s)||s->state!=TR_COMPLETE||!stage)return 0;
    return tr_campaign_advance(s,id);
}
static void put32(uint8_t *p,uint32_t v)
{p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static uint32_t get32(const uint8_t *p)
{return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint32_t crc(const uint8_t *p,size_t n)
{uint32_t c=~0u;while(n--){c^=*p++;for(unsigned j=0;j<8;++j)c=(c>>1)^((0u-(c&1))&0xEDB88320u);}return ~c;}
int tr_campaign_encode(const TrCampaign *d,uint32_t sequence,uint8_t out[TR_SAVE_BYTES])
{
    if(!out||!tr_campaign_valid(d))return 0;
    memset(out,0,TR_SAVE_BYTES);memcpy(out,"S3KTRI01",8);
    /* Version 1 identifies the GHZ/EHZ experiment, not the eventual full trilogy. */
    put32(out+8,1);put32(out+12,sequence);
    for(unsigned i=0;i<TR_SLOTS;++i){const TrSlot *s=&d->slots[i];uint8_t *p=out+32+i*16;
        p[0]=(uint8_t)(s->stage>>8);p[1]=(uint8_t)s->stage;p[2]=s->state;p[3]=s->packs;
        p[4]=s->character;p[5]=s->chaos;p[6]=s->super;p[7]=s->entered_sk;}
    put32(out+252,crc(out,252));return 1;
}
int tr_campaign_decode(const uint8_t *p,size_t n,TrCampaign *d,uint32_t *seq,char *err,size_t cap)
{
    const char *why="Invalid trilogy campaign save";TrCampaign next={0};uint8_t check[TR_SAVE_BYTES];
    if(!p||!d||n!=TR_SAVE_BYTES)goto fail;
    if(memcmp(p,"S3KTRI01",8)||get32(p+8)!=1){why="Unsupported campaign identity or version";goto fail;}
    if(get32(p+252)!=crc(p,252)){why="Campaign checksum mismatch";goto fail;}
    for(unsigned i=0;i<TR_SLOTS;++i){const uint8_t *b=p+32+i*16;TrSlot *s=&next.slots[i];
        s->stage=(uint16_t)((b[0]<<8)|b[1]);s->state=b[2];s->packs=b[3];s->character=b[4];
        s->chaos=b[5];s->super=b[6];s->entered_sk=b[7];}
    if(!tr_campaign_encode(&next,get32(p+12),check)||memcmp(check,p,n))goto fail;
    *d=next;if(seq)*seq=get32(p+12);if(err&&cap)*err=0;return 1;
fail:if(err&&cap)snprintf(err,cap,"%s",why);return 0;
}
