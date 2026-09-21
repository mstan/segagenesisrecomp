#include "trilogy_progress.h"
#include <stdio.h>
#include <string.h>

/* CAMP v1: campaign codec (256), slot tokens (32), S3K checkpoints (64),
 * reserved zero (32). S1EX/S2EX v1: eight 8-byte chapter entries. Each entry
 * is stage:u16, checkpoint:u8, cleared:u8, slot-token:u32, all big endian.
 * Tokens prevent retained/unknown chapter data attaching to a reused slot. */
enum { CAMP_BYTES=384, CHAPTER_BYTES=64 };
static const unsigned packs[TR_CHAPTERS]={TR_PACK_S1,TR_PACK_S2,0};
static const unsigned records[TR_CHAPTERS]={TR_RECORD_S1,TR_RECORD_S2,TR_RECORD_CAMP};
static const unsigned first[TR_CHAPTERS]={0x1000,0x2000,0x3000};
static const unsigned cleared_all[TR_CHAPTERS]={7,1,1};
static uint32_t get32(const uint8_t *p)
{return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void put32(uint8_t *p,uint32_t n)
{p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n;}
static int fail(TrProgress *p,const char *why)
{snprintf(p->error,sizeof p->error,"%s",why);return 0;}
static unsigned chapter_of(unsigned id)
{const TrStage *s=tr_stage(id);return !s?TR_CHAPTERS:s->pack==TR_PACK_S1?0:s->pack==TR_PACK_S2?1:2;}
static void encode_chapter(const TrChapterProgress in[TR_SLOTS],uint8_t out[CHAPTER_BYTES])
{
    for(unsigned i=0;i<TR_SLOTS;++i){const TrChapterProgress *c=in+i;uint8_t *b=out+i*8;
        b[0]=(uint8_t)(c->stage>>8);b[1]=(uint8_t)c->stage;b[2]=c->checkpoint;b[3]=c->cleared;put32(b+4,c->token);}
}
static int decode_chapter(const uint8_t *in,unsigned chapter,TrChapterProgress out[TR_SLOTS])
{
    for(unsigned i=0;i<TR_SLOTS;++i){const uint8_t *b=in+i*8;TrChapterProgress *c=out+i;
        c->stage=(uint16_t)((b[0]<<8)|b[1]);c->checkpoint=b[2];c->cleared=b[3];c->token=get32(b+4);
        if(!c->token){if(c->stage||c->checkpoint||c->cleared)return 0;}
        else if(chapter_of(c->stage)!=chapter||(c->cleared&~cleared_all[chapter]))return 0;
    }
    return 1;
}
static int active(const TrProgress *p,unsigned slot)
{return p&&slot<TR_SLOTS&&!(p->protected_records&TR_RECORD_CAMP)&&p->tokens[slot]&&
    tr_slot_valid(&p->campaign.slots[slot])&&p->campaign.slots[slot].state;}
static int included(const TrProgress *p,unsigned slot,unsigned ch)
{return ch==TR_CHAPTER_S3K||(p->campaign.slots[slot].packs&packs[ch]);}
static TrChapterProgress chapter(const TrProgress *p,unsigned slot,unsigned ch)
{
    TrChapterProgress result=p->chapters[ch][slot];
    if(result.token!=p->tokens[slot]){memset(&result,0,sizeof result);result.stage=(uint16_t)first[ch];result.token=p->tokens[slot];}
    return result;
}
static int complete(const TrProgress *p,unsigned slot)
{
    for(unsigned ch=0;ch<TR_CHAPTERS;++ch)if(included(p,slot,ch)){
        if(p->protected_records&records[ch])return 0;
        if(chapter(p,slot,ch).cleared!=cleared_all[ch])return 0;
    }
    return 1;
}
int tr_progress_load(TrProgress *p,const TrSram *s)
{
    if(!p||!s)return 0;memset(p,0,sizeof *p);p->next_token=1;
    if(!s->ready||s->read_only||!s->extensions_valid){p->protected_records=7;return fail(p,"Campaign extensions are unavailable; native SRAM remains separate");}
    unsigned version;size_t size;const uint8_t *data=tr_sram_get(s,"CAMP",&version,&size);
    if(data){
        p->present|=TR_RECORD_CAMP;
        if(version!=1||size!=CAMP_BYTES||!tr_campaign_decode(data,256,&p->campaign,&p->next_token,NULL,0)||!p->next_token)
            goto invalid_core;
        if(!decode_chapter(data+288,TR_CHAPTER_S3K,p->chapters[2]))goto invalid_core;
        for(unsigned i=0;i<TR_SLOTS;++i){p->tokens[i]=get32(data+256+i*4);
            if((p->tokens[i]!=0)!=(p->campaign.slots[i].state!=0)||p->tokens[i]>=p->next_token)goto invalid_core;
            if(p->tokens[i]!=p->chapters[2][i].token)goto invalid_core;
            for(unsigned j=0;j<i;++j)if(p->tokens[i]&&p->tokens[i]==p->tokens[j])goto invalid_core;
        }
        for(unsigned i=352;i<CAMP_BYTES;++i)if(data[i])goto invalid_core;
    }
    for(unsigned ch=0;ch<2;++ch){data=tr_sram_get(s,ch?"S2EX":"S1EX",&version,&size);if(!data)continue;
        p->present|=records[ch];
        if(version!=1||size!=CHAPTER_BYTES||!decode_chapter(data,ch,p->chapters[ch])){
            memset(p->chapters[ch],0,sizeof p->chapters[ch]);p->protected_records|=records[ch];
        }
    }
    if(p->protected_records)snprintf(p->error,sizeof p->error,"Unsupported chapter record retained; that chapter is unavailable");
    return 1;
invalid_core:
    memset(&p->campaign,0,sizeof p->campaign);memset(p->tokens,0,sizeof p->tokens);
    p->protected_records=7;return fail(p,"Unsupported campaign record retained; campaign edits disabled");
}
int tr_progress_store(TrProgress *p,TrSram *s)
{
    if(!p||!s)return 0;if(!p->dirty)return 1;
    if(p->dirty&p->protected_records)return fail(p,"Cannot replace an unsupported campaign record");
    uint8_t core[CAMP_BYTES]={0},s1[CHAPTER_BYTES],s2[CHAPTER_BYTES];
    TrSramRecord changes[3];size_t count=0;
    if(p->dirty&TR_RECORD_CAMP){
        if(!tr_campaign_encode(&p->campaign,p->next_token,core))return fail(p,"Invalid campaign state");
        for(unsigned i=0;i<TR_SLOTS;++i)put32(core+256+i*4,p->tokens[i]);
        encode_chapter(p->chapters[2],core+288);
        TrSramRecord r={{'C','A','M','P'},1,core,sizeof core};changes[count++]=r;
    }
    if(p->dirty&TR_RECORD_S1){encode_chapter(p->chapters[0],s1);TrSramRecord r={{'S','1','E','X'},1,s1,sizeof s1};changes[count++]=r;}
    if(p->dirty&TR_RECORD_S2){encode_chapter(p->chapters[1],s2);TrSramRecord r={{'S','2','E','X'},1,s2,sizeof s2};changes[count++]=r;}
    if(!tr_sram_set_many(s,changes,count))return fail(p,s->error);
    p->present|=p->dirty;p->dirty=0;p->error[0]=0;return 1;
}
int tr_progress_new(TrProgress *p,unsigned slot,unsigned mask,unsigned character)
{
    if(!p||slot>=TR_SLOTS||mask>3||character>TR_KNUCKLES)return 0;
    unsigned required=TR_RECORD_CAMP|((mask&1)?TR_RECORD_S1:0)|((mask&2)?TR_RECORD_S2:0);
    if((required&p->protected_records)||!p->next_token||p->next_token==UINT32_MAX)return fail(p,"Campaign slot cannot be replaced safely");
    tr_campaign_new(&p->campaign,slot,mask,character);p->tokens[slot]=p->next_token++;
    for(unsigned ch=0;ch<TR_CHAPTERS;++ch){
        if(p->protected_records&records[ch])continue;
        TrChapterProgress *c=&p->chapters[ch][slot];memset(c,0,sizeof *c);
        if(included(p,slot,ch)){c->stage=(uint16_t)first[ch];c->token=p->tokens[slot];}
        if((p->present|required)&records[ch])p->dirty|=records[ch];
    }
    return 1;
}
int tr_progress_delete(TrProgress *p,unsigned slot)
{
    if(!p||slot>=TR_SLOTS||(p->protected_records&TR_RECORD_CAMP))return 0;
    tr_campaign_delete(&p->campaign,slot);p->tokens[slot]=0;p->dirty|=TR_RECORD_CAMP;
    for(unsigned ch=0;ch<TR_CHAPTERS;++ch)if(!(p->protected_records&records[ch])){
        memset(&p->chapters[ch][slot],0,sizeof p->chapters[ch][slot]);
        if(p->present&records[ch])p->dirty|=records[ch];
    }
    return 1;
}
int tr_progress_attach_native(TrProgress *p,unsigned slot,const TrSlot *native)
{
    if(!native||!tr_slot_valid(native)||!native->state||native->packs||chapter_of(native->stage)!=2)return 0;
    if(!tr_progress_new(p,slot,0,native->character))return 0;
    p->campaign.slots[slot]=*native;p->chapters[2][slot].stage=native->stage;
    p->chapters[2][slot].cleared=native->state==TR_COMPLETE;return 1;
}
int tr_progress_add_pack(TrProgress *p,unsigned slot,unsigned pack)
{
    if(!active(p,slot)||(pack!=TR_PACK_S1&&pack!=TR_PACK_S2))return 0;
    unsigned ch=pack==TR_PACK_S1?0:1;if(p->protected_records&records[ch])return 0;
    TrSlot *s=&p->campaign.slots[slot];if(s->packs&pack)return 1;
    s->packs|=(uint8_t)pack;s->state=TR_ACTIVE;
    TrChapterProgress *c=&p->chapters[ch][slot];memset(c,0,sizeof *c);c->stage=(uint16_t)first[ch];c->token=p->tokens[slot];
    p->dirty|=TR_RECORD_CAMP|records[ch];return 1;
}
int tr_progress_resolve(const TrProgress *p,unsigned slot,unsigned available,TrResume *out)
{
    if(!active(p,slot)||available>3||!out)return 0;
    TrResume next={0};const TrSlot *s=&p->campaign.slots[slot];
    for(unsigned ch=0;ch<2;++ch)if(included(p,slot,ch)&&(!(available&packs[ch])||(p->protected_records&records[ch])))
        next.unavailable_packs|=(uint8_t)packs[ch];
    if(s->state==TR_COMPLETE&&complete(p,slot)){
        const TrStage *st=tr_stage(s->stage);
        if(!(st->pack&next.unavailable_packs)){next.stage=s->stage;*out=next;return 1;}
    }
    for(unsigned ch=0;ch<TR_CHAPTERS;++ch)if(included(p,slot,ch)&&!(packs[ch]&next.unavailable_packs)){
        TrChapterProgress c=chapter(p,slot,ch);
        if(c.cleared!=cleared_all[ch]||ch==TR_CHAPTER_S3K){next.stage=c.stage;next.checkpoint=c.checkpoint;*out=next;return 1;}
    }
    return 0;
}
int tr_progress_checkpoint(TrProgress *p,unsigned slot,unsigned id,unsigned checkpoint)
{
    if(!active(p,slot)||checkpoint>255)return 0;
    unsigned ch=chapter_of(id);if(ch>=TR_CHAPTERS||!included(p,slot,ch)||(p->protected_records&records[ch]))return 0;
    if(!tr_campaign_advance(&p->campaign.slots[slot],id))return 0;
    TrChapterProgress c=chapter(p,slot,ch);c.stage=(uint16_t)id;c.checkpoint=(uint8_t)checkpoint;
    p->chapters[ch][slot]=c;p->dirty|=TR_RECORD_CAMP|records[ch];return 1;
}
int tr_progress_finish_import(TrProgress *p,unsigned slot,unsigned available,TrResume *next)
{
    if(!active(p,slot)||available>3||!next)return 0;
    unsigned id=p->campaign.slots[slot].stage,ch=chapter_of(id);
    if(ch>=2||!(available&packs[ch])||(p->protected_records&records[ch]))return 0;
    TrChapterProgress c=chapter(p,slot,ch);const TrStage *st=tr_stage(id);
    c.cleared|=(uint8_t)(1u<<(st->act-1));c.checkpoint=0;
    if(ch==0&&id<0x1002)c.stage=(uint16_t)(id+1);
    p->chapters[ch][slot]=c;p->dirty|=TR_RECORD_CAMP|records[ch];
    p->campaign.slots[slot].stage=(uint16_t)tr_next_import(id,p->campaign.slots[slot].packs&available);
    if(complete(p,slot))p->campaign.slots[slot].state=TR_COMPLETE;
    if(!tr_progress_resolve(p,slot,available,next))return 0;
    p->campaign.slots[slot].stage=next->stage;return 1;
}
int tr_progress_finish_native(TrProgress *p,unsigned slot)
{
    if(!active(p,slot)||chapter_of(p->campaign.slots[slot].stage)!=2)return 0;
    p->chapters[2][slot]=chapter(p,slot,2);p->chapters[2][slot].cleared=1;
    if(complete(p,slot))p->campaign.slots[slot].state=TR_COMPLETE;
    p->dirty|=TR_RECORD_CAMP;return 1;
}
int tr_progress_select_zone(TrProgress *p,unsigned slot,unsigned id,unsigned available)
{
    if(!active(p,slot)||available>3||!complete(p,slot))return 0;
    const TrStage *s=tr_stage(id);if(!s||(s->pack&&!(s->pack&available)))return 0;
    if(!tr_campaign_select_zone(&p->campaign.slots[slot],id))return 0;
    p->dirty|=TR_RECORD_CAMP;return 1;
}
int tr_progress_collect(TrProgress *p,unsigned slot,unsigned chaos,unsigned super)
{
    if(!active(p,slot)||!tr_campaign_collect(&p->campaign.slots[slot],chaos,super))return 0;
    p->dirty|=TR_RECORD_CAMP;return 1;
}
int tr_native_slot_decode(const uint8_t bytes[10],TrSlot *out)
{
    /* LevelList_DA6E, sub_DA1E and SaveGame in the matched S3K disassembly. */
    static const uint16_t stages[]={0x3000,0x3010,0x3020,0x3030,0x3050,0x3060,
        0x3070,0x3040,0x3080,0x3090,0x3161,0x30A0,0x30B0,0x30C0};
    if(!bytes||!out)return 0;TrSlot next={0};
    if(bytes[0]&128){*out=next;return 1;}
    unsigned character=bytes[2]>>4,code=bytes[3],emeralds=(bytes[6]<<8)|bytes[7];
    if(character>TR_KNUCKLES||code>14||(bytes[0]&~3u))return 0;
    next.character=(uint8_t)character;next.state=(bytes[0]&3)?TR_COMPLETE:TR_ACTIVE;
    for(unsigned i=0;i<7;++i){unsigned value=(emeralds>>(14-i*2))&3;
        if(value)next.chaos|=(uint8_t)(1u<<i);
        if(value==3)next.super|=(uint8_t)(1u<<i);
        if(value>=2)next.entered_sk=1;
    }
    if(code>=6)next.entered_sk=1;
    /* Completion can store the code *after* a character's final stage. */
    unsigned last=character==TR_KNUCKLES?11:character==TR_TAILS||next.chaos!=127?12:13;
    if(code>last){if(next.state!=TR_COMPLETE)return 0;code=last;}
    next.stage=character==TR_KNUCKLES&&code==11?0x30A1:stages[code];
    if(!tr_slot_valid(&next))return 0;*out=next;return 1;
}
int tr_progress_sync_native(TrProgress *p,unsigned slot,const uint8_t bytes[10])
{
    if(!active(p,slot))return 0;
    TrSlot native;if(!tr_native_slot_decode(bytes,&native))return 0;
    if(!native.state)return tr_progress_delete(p,slot);
    if(native.character!=p->campaign.slots[slot].character)return tr_progress_attach_native(p,slot,&native);
    TrProgress before=*p;TrSlot *s=&p->campaign.slots[slot];
    s->chaos=native.chaos;s->super=native.super;s->entered_sk|=native.entered_sk;
    TrChapterProgress c=chapter(p,slot,TR_CHAPTER_S3K);
    if(c.stage!=native.stage)c.checkpoint=0;
    c.stage=native.stage;c.cleared=native.state==TR_COMPLETE;
    p->chapters[2][slot]=c;
    /* A cleared slot's browser choice includes individual acts. Native SRAM
     * has only a zone card, and its launch-time write must not replace that
     * choice with the previous native card (often Doomsday). */
    if(s->state!=TR_COMPLETE&&!tr_stage(s->stage)->pack)s->stage=native.stage;
    s->state=complete(p,slot)?TR_COMPLETE:TR_ACTIVE;
    if(memcmp(&before.campaign,&p->campaign,sizeof p->campaign)||
       memcmp(before.chapters[2],p->chapters[2],sizeof p->chapters[2]))p->dirty|=TR_RECORD_CAMP;
    return 1;
}
