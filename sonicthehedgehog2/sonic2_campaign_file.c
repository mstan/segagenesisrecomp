#include "sonic2_campaign_file.h"
#include "sonic2_party.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

static S2CampaignStore store;
static char directory[1024], error[256];
static int remember_path;
static int fail(const char *why)
{ snprintf(error,sizeof error,"%s",why); return 0; }

static int absolute(const char *path)
{
#ifdef _WIN32
    return (path[0] && path[1]==':' && (path[2]=='/' || path[2]=='\\')) ||
        ((path[0]=='/' || path[0]=='\\') && path[1]==path[0]);
#else
    return path[0]=='/';
#endif
}
static int resolve(const char *path,char out[1024])
{
    if (!path || !*path || strpbrk(path,"\r\n") || strlen(path)>=1024)
        return fail("Invalid campaign SRAM path");
#ifdef _WIN32
    /* Reject drive-relative/root-relative forms whose meaning depends on CWD. */
    if (!absolute(path) && (strchr(path,':') || path[0]=='/' || path[0]=='\\'))
        return fail("Choose a full path or a path relative to the game folder");
#endif
    int n=snprintf(out,1024,"%s%s",absolute(path)?"":directory,path);
    if (n<0 || n>=1024) return fail("Campaign SRAM path is too long");
#ifdef _WIN32
    char full[1024]; DWORD size=GetFullPathNameA(out,sizeof full,full,NULL);
    if (!size || size>=sizeof full) return fail("Cannot resolve campaign SRAM path");
    strcpy(out,full);
    for (char *p=out;*p;++p) if (*p=='\\') *p='/';
#endif
    return 1;
}
static void configured_path(const char *path)
{
    size_t n=strlen(directory);
#ifdef _WIN32
    int local=!_strnicmp(path,directory,n);
#else
    int local=!strncmp(path,directory,n);
#endif
    snprintf(s2_party.campaign_path,sizeof s2_party.campaign_path,"%s",local?path+n:path);
}
static int present(const char *path)
{
    struct stat st; char backup[1040];
    snprintf(backup,sizeof backup,"%s.bak",path);
    return !stat(path,&st) || !stat(backup,&st);
}
static int open_default(S2CampaignStore *candidate)
{
    char path[1024];
    if (!resolve("sonic2-campaign.sav",path)) return 0;
    /* Preserve the original release's file, including protected/broken saves.
     * Never silently abandon existing progress for a new empty default. */
    if (!present(path) && !resolve("sonic2-campaign.srm",path)) return 0;
    s2_campaign_open(candidate,path);
    return 1;
}
void s2_campaign_file_load(const char *settings)
{
    memset(&store,0,sizeof store); store.read_only=1;
    directory[0]=error[0]=0; remember_path=0;
    char path[1024];
    if (!settings || !*settings) settings="settings.ini";
    if (!absolute(settings)) {
        char cwd[1024];
        if (!getcwd(cwd,sizeof cwd)) { fail("Cannot locate game settings directory"); return; }
        int n=snprintf(directory,sizeof directory,"%s/",cwd);
        if (n<0 || n>=(int)sizeof directory) { fail("Game settings path is too long"); return; }
    }
    if (!resolve(settings,path)) return;
    char *last=strrchr(path,'/');
    if (!last) { fail("Cannot locate game settings directory"); return; }
    last[1]=0; strcpy(directory,path);
    if (*s2_party.campaign_path) {
        if (!resolve(s2_party.campaign_path,path)) return;
        s2_campaign_open(&store,path);
    } else {
        if (!open_default(&store)) return;
        if (present(store.path)) { configured_path(store.path); remember_path=1; }
    }
}
const S2CampaignStore *s2_campaign_file_store(void) { return &store; }
const char *s2_campaign_file_error(void) { return *error?error:store.error; }
int s2_campaign_file_select(const char *path)
{
    error[0]=0;
    if (!*directory) return fail("Campaign SRAM storage is not initialized");
    S2CampaignStore candidate;
    if (path && *path) {
        char resolved[1024];
        if (!resolve(path,resolved)) return 0;
        if (!s2_campaign_open(&candidate,resolved) || !candidate.exists || candidate.recovered)
            return fail(*candidate.error?candidate.error:"Select an existing Sonic 2 campaign SRAM file");
        configured_path(candidate.path);
    } else {
        if (!open_default(&candidate)) return 0;
        s2_party.campaign_path[0]=0;
    }
    store=candidate; remember_path=1;
    return 1;
}
int s2_campaign_file_commit(const S2CampaignData *candidate)
{
    error[0]=0;
    if (!s2_campaign_commit(&store,candidate)) return 0;
    if (!*s2_party.campaign_path) { configured_path(store.path); remember_path=1; }
    if (remember_path) {
        if (!s2_party_save()) return fail("Campaign saved, but its path could not be saved in settings; retry");
        remember_path=0;
    }
    return 1;
}
