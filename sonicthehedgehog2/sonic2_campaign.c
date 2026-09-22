#include "sonic2_campaign.h"
#include <stdio.h>
#include <string.h>

/* s2disasm 65ddcc2: LevelOrder + ObjB2_SCZ_Finished/ObjB2_Start_DEZ.
 * Unused, debug and two-player destinations are intentionally absent. */
const S2CampaignStage s2_campaign_stages[S2_CAMPAIGN_STAGES] = {
    {0x0000,0,1,"EMERALD HILL"}, {0x0001,0,2,"EMERALD HILL"},
    {0x0D00,1,1,"CHEMICAL PLANT"}, {0x0D01,1,2,"CHEMICAL PLANT"},
    {0x0F00,2,1,"AQUATIC RUIN"}, {0x0F01,2,2,"AQUATIC RUIN"},
    {0x0C00,3,1,"CASINO NIGHT"}, {0x0C01,3,2,"CASINO NIGHT"},
    {0x0700,4,1,"HILL TOP"}, {0x0701,4,2,"HILL TOP"},
    {0x0B00,5,1,"MYSTIC CAVE"}, {0x0B01,5,2,"MYSTIC CAVE"},
    {0x0A00,6,1,"OIL OCEAN"}, {0x0A01,6,2,"OIL OCEAN"},
    {0x0400,7,1,"METROPOLIS"}, {0x0401,7,2,"METROPOLIS"},
    {0x0500,7,3,"METROPOLIS"}, {0x1000,8,1,"SKY CHASE"},
    {0x0600,9,1,"WING FORTRESS"}, {0x0E00,10,1,"DEATH EGG"}
};
int s2_campaign_stage(uint16_t id)
{
    for (unsigned i=0;i<S2_CAMPAIGN_STAGES;++i)
        if (s2_campaign_stages[i].native_id==id) return (int)i;
    return -1;
}
int s2_campaign_zone_start(unsigned zone)
{
    for (unsigned i=0;i<S2_CAMPAIGN_STAGES;++i)
        if (s2_campaign_stages[i].zone==zone) return (int)i;
    return -1;
}
unsigned s2_campaign_emerald_count(unsigned mask)
{
    unsigned n=0;
    for (unsigned i=0;i<7;++i) n+=(mask>>i)&1;
    return n;
}
static int valid_slot(const S2CampaignSlot *s)
{
    return s && s->state<=S2_SAVE_COMPLETE && s->stage<S2_CAMPAIGN_STAGES &&
        s->emeralds<=0x7F && (s->state!=S2_SAVE_EMPTY ||
        (!s->stage && !s->emeralds && !s->lives && !s->continues));
}
int s2_campaign_valid(const S2CampaignData *d)
{
    if (!d) return 0;
    for (unsigned i=0;i<S2_SAVE_SLOTS;++i) if (!valid_slot(&d->slots[i])) return 0;
    return 1;
}
int s2_campaign_new(S2CampaignData *d, unsigned i)
{
    if (!d || i>=S2_SAVE_SLOTS || d->slots[i].state!=S2_SAVE_EMPTY) return 0;
    d->slots[i]=(S2CampaignSlot){S2_SAVE_ACTIVE,0,0,3,0}; return 1;
}
int s2_campaign_delete(S2CampaignData *d, unsigned i)
{
    if (!d || i>=S2_SAVE_SLOTS) return 0;
    d->slots[i]=(S2CampaignSlot){0}; return 1;
}
int s2_campaign_advance(S2CampaignSlot *s, uint16_t next)
{
    if (!valid_slot(s) || !s->state || s->stage+1>=S2_CAMPAIGN_STAGES ||
        s2_campaign_stage(next)!=s->stage+1) return 0;
    ++s->stage; return 1;
}
int s2_campaign_collect(S2CampaignSlot *s, unsigned mask)
{
    if (!valid_slot(s) || !s->state || mask>0x7F) return 0;
    s->emeralds|=(uint8_t)mask; return 1;
}
int s2_campaign_complete(S2CampaignSlot *s)
{
    if (!valid_slot(s) || !s->state || s->stage!=S2_CAMPAIGN_STAGES-1) return 0;
    s->state=S2_SAVE_COMPLETE; return 1;
}
int s2_campaign_select_zone(S2CampaignSlot *s, unsigned zone)
{
    int stage=s2_campaign_zone_start(zone);
    return stage>=0 && s2_campaign_select_stage(s,(unsigned)stage);
}
int s2_campaign_select_stage(S2CampaignSlot *s, unsigned stage)
{
    if (!valid_slot(s) || s->state!=S2_SAVE_COMPLETE || stage>=S2_CAMPAIGN_STAGES) return 0;
    s->stage=(uint8_t)stage; return 1;
}
static uint32_t read32(const uint8_t *p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static void write32(uint8_t *p, uint32_t v)
{ p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v; }
/* Exact stock guest SHA-256, independently verified against the local REV01.
 * Donor hashes do not belong in the campaign identity. */
static const uint8_t identity[32]={
    0x19,0x3B,0xC4,0x06,0x4C,0xE0,0xDA,0xF2,0x7E,0xA9,0xE9,0x08,0xED,0x24,0x6D,0x87,
    0xEC,0x57,0x6C,0xC2,0x94,0x83,0x3B,0xAD,0xEB,0xB5,0x90,0xB6,0xAD,0x8E,0x8F,0x6B};
static uint32_t checksum(const uint8_t *p)
{
    uint32_t crc=0xFFFFFFFF;
    for (unsigned i=0;i<S2_SAVE_BYTES;++i) {
        crc^=(i>=60 && i<64)?0:p[i];
        for (unsigned bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xEDB88320:0);
    }
    return ~crc;
}
int s2_campaign_encode(const S2CampaignData *d, uint32_t seq, uint8_t b[S2_SAVE_BYTES])
{
    if (!b || !s2_campaign_valid(d)) return 0;
    memset(b,0,S2_SAVE_BYTES); memcpy(b,"S2SAVE\r\n",8);
    write32(b+8,2); write32(b+12,S2_SAVE_BYTES); write32(b+16,S2_SAVE_SLOTS);
    write32(b+20,seq); memcpy(b+24,identity,32);
    for (unsigned i=0;i<S2_SAVE_SLOTS;++i) {
        b[64+8*i]=d->slots[i].state; b[65+8*i]=d->slots[i].stage; b[66+8*i]=d->slots[i].emeralds;
        b[67+8*i]=d->slots[i].lives; b[68+8*i]=d->slots[i].continues;
    }
    write32(b+60,checksum(b)); return 1;
}
int s2_campaign_decode(const uint8_t *b, size_t size, S2CampaignData *d,
                       uint32_t *seq, char *error, size_t cap)
{
    const char *why=NULL;
    S2CampaignData temp={0};
    if (!b || !d || !seq || size!=S2_SAVE_BYTES) why="Save size is invalid";
    else if (memcmp(b,"S2SAVE\r\n",8)) why="Unrecognized save file";
    else if (read32(b+8)!=1 && read32(b+8)!=2) why="Unsupported save version; file preserved";
    else if (memcmp(b+24,identity,32)) why="Save belongs to a different game or ROM revision";
    else if (read32(b+12)!=S2_SAVE_BYTES || read32(b+16)!=S2_SAVE_SLOTS || read32(b+56)) why="Invalid save header";
    else if (read32(b+60)!=checksum(b)) why="Save checksum mismatch";
    else {
        for (unsigned i=0;i<S2_SAVE_SLOTS;++i) {
            const uint8_t *p=b+64+8*i;
            unsigned version=read32(b+8);
            temp.slots[i]=(S2CampaignSlot){p[0],p[1],p[2],
                version==1?(p[0]?3:0):p[3],version==1?0:p[4]};
            for (unsigned j=version==1?3:5;j<8;++j) if (p[j]) why="Unsupported save slot fields";
        }
        if (!s2_campaign_valid(&temp)) why="Invalid campaign destination or Emerald state";
    }
    if (error && cap) snprintf(error,cap,"%s",why?why:"");
    if (why) return 0;
    *d=temp; *seq=read32(b+20); return 1;
}
