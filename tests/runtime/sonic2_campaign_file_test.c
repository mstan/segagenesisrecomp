#include "../../sonicthehedgehog2/sonic2_campaign_file.h"
#include "../../sonicthehedgehog2/sonic2_party.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define getcwd _getcwd
#define chdir _chdir
#define mkdir(p,m) _mkdir(p)
#define rmdir _rmdir
#else
#include <unistd.h>
#endif

static int exists(const char *path) { struct stat st; return !stat(path,&st); }
static void load(const char *settings)
{ s2_party_load(settings); s2_campaign_file_load(settings); }
static void read_bytes(const char *path,uint8_t b[S2_SAVE_BYTES])
{
    FILE *f=fopen(path,"rb"); assert(f);
    assert(fread(b,1,S2_SAVE_BYTES,f)==S2_SAVE_BYTES && fgetc(f)==EOF); assert(!fclose(f));
}
static void write_bytes(const char *path,const void *b,size_t size)
{
    FILE *f=fopen(path,"wb"); assert(f);
    assert(fwrite(b,1,size,f)==size); assert(!fclose(f));
}
int main(void)
{
    char root[128],cwd[1024],external[1024];
#ifdef _WIN32
    snprintf(root,sizeof root,"campaign-path-%lu-%llu",(unsigned long)GetCurrentProcessId(),(unsigned long long)GetTickCount64());
#else
    snprintf(root,sizeof root,"campaign-path-%lu",(unsigned long)getpid());
#endif
    assert(!mkdir(root,0700) && !chdir(root));
    assert(!mkdir("game",0700) && !mkdir("library",0700) && !mkdir("legacy",0700) && !mkdir("failure",0700));
    assert(getcwd(cwd,sizeof cwd));
    snprintf(external,sizeof external,"%s/library/my progress.sav",cwd);
    const S2CampaignStore *s=s2_campaign_file_store();
    load("game/settings.ini");
    assert(!s2_party.campaign_path[0] && s->ready && !s->exists);
    assert(!exists("game/sonic2-campaign.srm") && !exists("game/sonic2-party.ini"));
    S2CampaignData d={0}; assert(s2_campaign_new(&d,0));
    assert(s2_campaign_file_commit(&d));
    assert(!strcmp(s2_party.campaign_path,"sonic2-campaign.srm"));
    assert(exists("game/sonic2-campaign.srm") && !exists("sonic2-campaign.srm"));
    uint8_t original[S2_SAVE_BYTES],bytes[S2_SAVE_BYTES];
    read_bytes("game/sonic2-campaign.srm",original);
    load("game/settings.ini");
    assert(!strcmp(s2_party.campaign_path,"sonic2-campaign.srm") && s->data.slots[0].state==1);

    /* A picker selection updates the actual external file, including backup. */
    write_bytes(external,original,sizeof original);
    assert(s2_campaign_file_select(external));
    assert(strstr(s2_party.campaign_path,"library/my progress.sav"));
    assert(s2_campaign_advance(&d.slots[0],1));
    assert(s2_campaign_file_commit(&d));
    load("game/settings.ini"); assert(s->data.slots[0].stage==1);
    assert(exists("library/my progress.sav.bak"));
    read_bytes("game/sonic2-campaign.srm",bytes); assert(!memcmp(bytes,original,sizeof bytes));
    char selected[1024]; strcpy(selected,s2_party.campaign_path);
    write_bytes("library/not-campaign.srm","raw S3K SRAM",12);
    assert(!s2_campaign_file_select("../library/not-campaign.srm"));
    assert(!strcmp(selected,s2_party.campaign_path) && s->data.slots[0].stage==1);
    assert(!s2_campaign_file_select("../library/missing.srm"));
    assert(!strcmp(selected,s2_party.campaign_path));
    assert(s2_campaign_file_select(""));
    assert(!s2_party.campaign_path[0] && s->data.slots[0].stage==0);
    assert(exists(external)); /* Clearing a selection never deletes it. */

    /* Absolute selection inside the executable folder persists relatively. */
    snprintf(selected,sizeof selected,"%s/game/sonic2-campaign.srm",cwd);
    assert(s2_campaign_file_select(selected));
    assert(!strcmp(s2_party.campaign_path,"sonic2-campaign.srm"));

    /* A configured missing file is created at that location, never fallback. */
    strcpy(s2_party.campaign_path,"../library/new.srm"); assert(s2_party_save());
    load("game/settings.ini"); assert(!s->exists && s->ready);
    assert(s2_campaign_file_commit(&d) && exists("library/new.srm"));
    strcpy(s2_party.campaign_path,"../missing-directory/no.srm"); assert(s2_party_save());
    load("game/settings.ini"); assert(!s2_campaign_file_commit(&d));
    assert(!strcmp(s2_party.campaign_path,"../missing-directory/no.srm"));
    read_bytes("game/sonic2-campaign.srm",bytes); assert(!memcmp(bytes,original,sizeof bytes));
    strcpy(s2_party.campaign_path,"../library/not-campaign.srm"); assert(s2_party_save());
    load("game/settings.ini"); assert(s->read_only && !s->ready);
    assert(!s2_campaign_file_commit(&d));

    /* Keep a legacy save in place and remember it without conversion. */
    write_bytes("legacy/sonic2-campaign.sav",original,sizeof original);
    load("legacy/settings.ini");
    assert(!strcmp(s2_party.campaign_path,"sonic2-campaign.sav") && s->data.slots[0].stage==0);
    assert(s2_campaign_file_commit(&d) && !exists("legacy/sonic2-campaign.srm"));
    load("legacy/settings.ini"); assert(s->data.slots[0].stage==1);
    /* Clearing configuration does not bypass a damaged legacy primary. */
    write_bytes("legacy/sonic2-campaign.sav","bad",3);
    assert(s2_campaign_file_select("")); assert(s->recovered && s->read_only);
    assert(!s2_campaign_file_commit(&d) && !exists("legacy/sonic2-campaign.srm"));

    /* A successful data write followed by a failed settings write can retry
     * without switching destinations or rewriting/incrementing the save. */
    load("failure/settings.ini"); assert(!mkdir("failure/sonic2-party.ini.tmp",0700));
    assert(!s2_campaign_file_commit(&d) && s->exists && s->sequence==1);
    assert(strstr(s2_campaign_file_error(),"Campaign saved"));
    assert(!rmdir("failure/sonic2-party.ini.tmp"));
    assert(s2_campaign_file_commit(&d) && s->sequence==1);
    load("failure/settings.ini");
    assert(!strcmp(s2_party.campaign_path,"sonic2-campaign.srm") && s->data.slots[0].stage==1);
    puts("campaign paths: lazy creation, in-place selection, executable anchoring, legacy preservation and failed writes passed");
    return 0;
}
