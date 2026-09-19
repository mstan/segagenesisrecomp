#include "../../sonicthehedgehog2/sonic2_campaign.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#endif

static void write_bytes(const char *path, const void *p, size_t size)
{
    FILE *f=fopen(path,"wb"); assert(f);
    assert(fwrite(p,1,size,f)==size); assert(!fclose(f));
}
static void read_bytes(const char *path, uint8_t bytes[S2_SAVE_BYTES])
{
    FILE *f=fopen(path,"rb"); assert(f);
    assert(fread(bytes,1,S2_SAVE_BYTES,f)==S2_SAVE_BYTES); assert(fgetc(f)==EOF); assert(!fclose(f));
}
static void repair_crc(uint8_t *b)
{
    uint32_t crc=0xFFFFFFFF;
    memset(b+60,0,4);
    for (unsigned i=0;i<S2_SAVE_BYTES;++i) {
        crc^=b[i];
        for (unsigned j=0;j<8;++j) crc=(crc>>1)^((crc&1)?0xEDB88320:0);
    }
    crc=~crc;
    b[60]=(uint8_t)(crc>>24); b[61]=(uint8_t)(crc>>16); b[62]=(uint8_t)(crc>>8); b[63]=(uint8_t)crc;
}
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    const uint16_t expected[]={0,1,0xD00,0xD01,0xF00,0xF01,0xC00,0xC01,0x700,0x701,
        0xB00,0xB01,0xA00,0xA01,0x400,0x401,0x500,0x1000,0x600,0xE00};
    for (unsigned i=0;i<S2_CAMPAIGN_STAGES;++i) {
        assert(s2_campaign_stages[i].native_id==expected[i]);
        assert(s2_campaign_stage(expected[i])==(int)i);
    }
    assert(s2_campaign_stage(0x402)==-1); /* MTZ3 is a separate native zone */
    assert(s2_campaign_stage(0x800)==-1); assert(s2_campaign_stage(0x200)==-1);
    assert(s2_campaign_stage(0x1001)==-1); assert(s2_campaign_zone_start(11)==-1);
    S2CampaignData d={0}, decoded={0};
    assert(s2_campaign_valid(&d)); assert(!s2_campaign_new(&d,8));
    assert(s2_campaign_new(&d,0)); assert(!s2_campaign_new(&d,0));
    assert(!s2_campaign_select_zone(&d.slots[0],10));
    assert(!s2_campaign_complete(&d.slots[0]));
    assert(!s2_campaign_advance(&d.slots[0],0xD00));
    assert(s2_campaign_collect(&d.slots[0],0x21));
    assert(s2_campaign_collect(&d.slots[0],0x01));
    assert(d.slots[0].emeralds==0x21); assert(s2_campaign_emerald_count(0x21)==2);
    assert(!s2_campaign_collect(&d.slots[0],0x80));
    for (unsigned i=1;i<S2_CAMPAIGN_STAGES;++i) assert(s2_campaign_advance(&d.slots[0],expected[i]));
    assert(!s2_campaign_advance(&d.slots[0],0));
    assert(s2_campaign_complete(&d.slots[0]));
    for (unsigned z=0;z<11;++z) {
        assert(s2_campaign_select_zone(&d.slots[0],z));
        assert(s2_campaign_stages[d.slots[0].stage].act==1);
        assert(d.slots[0].state==S2_SAVE_COMPLETE);
    }
    assert(s2_campaign_select_zone(&d.slots[0],0));
    assert(s2_campaign_collect(&d.slots[0],0x40));
    assert(s2_campaign_advance(&d.slots[0],1));
    assert(d.slots[0].state==S2_SAVE_COMPLETE && d.slots[0].emeralds==0x61);
    assert(s2_campaign_new(&d,7));
    assert(s2_campaign_advance(&d.slots[7],1)); /* independent Act 2 resume */
    uint8_t bytes[S2_SAVE_BYTES], changed[S2_SAVE_BYTES]; uint32_t seq=0;
    char error[192];
    assert(s2_campaign_encode(&d,27,bytes));
    assert(s2_campaign_decode(bytes,sizeof bytes,&decoded,&seq,error,sizeof error));
    assert(!memcmp(&d,&decoded,sizeof d) && seq==27 && !*error);
    for (unsigned i=0;i<S2_SAVE_BYTES;++i) {
        memcpy(changed,bytes,sizeof bytes); changed[i]^=0x40;
        assert(!s2_campaign_decode(changed,sizeof changed,&decoded,&seq,error,sizeof error));
        assert(!memcmp(&d,&decoded,sizeof d) && seq==27 && *error);
    }
    assert(!s2_campaign_decode(bytes,sizeof bytes-1,&decoded,&seq,error,sizeof error));
    const unsigned invalid_offsets[]={64,65,66,67,56};
    for (unsigned i=0;i<sizeof invalid_offsets/sizeof *invalid_offsets;++i) {
        memcpy(changed,bytes,sizeof bytes); changed[invalid_offsets[i]]=255; repair_crc(changed);
        assert(!s2_campaign_decode(changed,sizeof changed,&decoded,&seq,error,sizeof error));
    }
    char dir[80], path[128], backup[140];
#ifdef _WIN32
    snprintf(dir,sizeof dir,"campaign-test-%lu-%llu",(unsigned long)GetCurrentProcessId(),(unsigned long long)GetTickCount64());
    assert(!_mkdir(dir));
#else
    snprintf(dir,sizeof dir,"campaign-test-%lu",(unsigned long)getpid());
    assert(!mkdir(dir,0700));
#endif
    snprintf(path,sizeof path,"%s/campaign.sav",dir); snprintf(backup,sizeof backup,"%s.bak",path);
    S2CampaignStore s, reopened;
    assert(s2_campaign_open(&s,path) && !s.exists && !s.read_only);
    assert(s2_campaign_commit(&s,&d) && s.sequence==1);
    assert(s2_campaign_open(&reopened,path) && !memcmp(&d,&reopened.data,sizeof d));
    assert(s2_campaign_commit(&s,&d) && s.sequence==1); /* no repeated frame writes */
    assert(s2_campaign_delete(&d,7));
    assert(s2_campaign_commit(&s,&d) && s.sequence==2);
    read_bytes(backup,bytes);
    assert(s2_campaign_decode(bytes,sizeof bytes,&decoded,&seq,error,sizeof error));
    assert(decoded.slots[7].stage==1 && decoded.slots[7].state==S2_SAVE_ACTIVE);
    assert(s2_campaign_open(&reopened,path) && !reopened.data.slots[7].state);
    assert(s2_campaign_new(&d,1));
    /* Directory occupying the backup filename makes replacement fail. */
    assert(!remove(backup));
#ifdef _WIN32
    assert(!_mkdir(backup));
#else
    assert(!mkdir(backup,0700));
#endif
    assert(!s2_campaign_commit(&s,&d) && s.sequence==2 && !s.data.slots[1].state);
    assert(s2_campaign_open(&reopened,path) && reopened.sequence==2);
#ifdef _WIN32
    assert(!_rmdir(backup));
    assert(SetFileAttributesA(path,FILE_ATTRIBUTE_READONLY));
    assert(!s2_campaign_commit(&s,&d) && s.sequence==2);
    assert(SetFileAttributesA(path,FILE_ATTRIBUTE_NORMAL));
#else
    assert(!rmdir(backup));
#endif
    assert(s2_campaign_commit(&s,&d));
    /* Primary corruption: backup is usable read-only; both files preserved. */
    write_bytes(path,"broken",6);
    assert(s2_campaign_open(&reopened,path) && reopened.recovered && reopened.read_only);
    assert(!s2_campaign_commit(&reopened,&d));
    assert(!s2_campaign_commit(&s,&d) && s.read_only); /* external edit detection */
    assert(!remove(path));
    assert(s2_campaign_open(&reopened,path) && reopened.recovered && reopened.read_only);
    assert(!remove(backup));
    /* Future version and foreign ROM are never silently initialized. */
    assert(s2_campaign_encode(&d,30,bytes)); bytes[11]=2; repair_crc(bytes);
    write_bytes(path,bytes,sizeof bytes);
    assert(!s2_campaign_open(&reopened,path) && reopened.read_only);
    assert(!s2_campaign_commit(&reopened,&d));
    read_bytes(path,changed); assert(!memcmp(bytes,changed,sizeof bytes));
    bytes[11]=1; bytes[24]^=1; repair_crc(bytes); write_bytes(path,bytes,sizeof bytes);
    assert(!s2_campaign_open(&reopened,path));
    assert(!remove(path));
#ifdef _WIN32
    assert(!_rmdir(dir));
#else
    assert(!rmdir(dir));
#endif
    puts("campaign: progression, Emeralds, schema, persistence, recovery and failed writes passed");
    return 0;
}
