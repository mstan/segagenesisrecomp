#include "sonic2_campaign.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <share.h>
#else
#include <unistd.h>
#endif

static int fail(S2CampaignStore *s, const char *why)
{ snprintf(s->error,sizeof s->error,"%s",why); return 0; }
/* 1=exact file, 0=absent, -1=unreadable/wrong size. Never consume an unbounded file. */
static int read_file(const char *path, uint8_t bytes[S2_SAVE_BYTES])
{
    FILE *f=fopen(path,"rb");
    if (!f) return errno==ENOENT?0:-1;
    size_t n=fread(bytes,1,S2_SAVE_BYTES,f);
    int extra=fgetc(f), ok=n==S2_SAVE_BYTES && extra==EOF && !ferror(f);
    if (fclose(f)) ok=0;
    return ok?1:-1;
}
int s2_campaign_open(S2CampaignStore *s, const char *path)
{
    if (!s) return 0;
    memset(s,0,sizeof *s);
    s->read_only=1;
    if (!path || !*path || strlen(path)>=sizeof s->path) return fail(s,"Invalid campaign save path");
    strcpy(s->path,path);
    int result=read_file(path,s->original);
    s->exists=result!=0;
    if (result>0 && s2_campaign_decode(s->original,S2_SAVE_BYTES,&s->data,&s->sequence,s->error,sizeof s->error)) {
        s->ready=1; s->read_only=0; return 1;
    }
    if (result<0) fail(s,"Campaign file is unreadable or truncated; file preserved");
    char backup[1040]; uint8_t bytes[S2_SAVE_BYTES];
    snprintf(backup,sizeof backup,"%s.bak",path);
    int backup_result=read_file(backup,bytes);
    if (backup_result==1 && s2_campaign_decode(bytes,sizeof bytes,&s->data,&s->sequence,NULL,0)) {
        s->ready=1; s->recovered=1;
        fail(s,"Primary save is invalid; backup opened read-only. Original files preserved");
        return 1;
    }
    if (!result && !backup_result) { s->ready=1; s->read_only=0; return 1; }
    if (!result) fail(s,"Primary save is missing and backup is invalid; files preserved");
    return 0;
}
static int replace_file(const char *from, const char *to)
{
#ifdef _WIN32
    return MoveFileExA(from,to,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    return rename(from,to)==0;
#endif
}
/* Exclusive temporary creation never truncates another run's temp file.
 * Flush file contents before same-directory atomic replacement. */
static int atomic_write(const char *path, const uint8_t *bytes)
{
    char temp[1100]; FILE *f=NULL;
#ifdef _WIN32
    int fd=-1;
    for (unsigned i=0;i<100;++i) {
        snprintf(temp,sizeof temp,"%s.%lu.%u.tmp",path,(unsigned long)GetCurrentProcessId(),i);
        errno_t e=_sopen_s(&fd,temp,_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY,_SH_DENYRW,_S_IREAD|_S_IWRITE);
        if (!e) break;
        if (e!=EEXIST) return 0;
    }
    if (fd<0) return 0;
    f=_fdopen(fd,"wb");
    if (!f) { _close(fd); remove(temp); return 0; }
#else
    snprintf(temp,sizeof temp,"%s.XXXXXX",path);
    int fd=mkstemp(temp);
    if (fd<0) return 0;
    f=fdopen(fd,"wb");
    if (!f) { close(fd); remove(temp); return 0; }
#endif
    int ok=fwrite(bytes,1,S2_SAVE_BYTES,f)==S2_SAVE_BYTES && fflush(f)==0;
#ifdef _WIN32
    if (ok) ok=_commit(_fileno(f))==0;
#else
    if (ok) ok=fsync(fileno(f))==0;
#endif
    if (fclose(f)) ok=0;
    if (ok) ok=replace_file(temp,path);
    if (!ok) remove(temp);
    return ok;
}
int s2_campaign_commit(S2CampaignStore *s, const S2CampaignData *candidate)
{
    if (!s) return 0;
    if (!s->ready || s->read_only) return fail(s,"Campaign save is protected; no files changed");
    if (!s2_campaign_valid(candidate)) return fail(s,"Invalid campaign data; no files changed");
    if (s->sequence==UINT32_MAX) return fail(s,"Save sequence exhausted; file preserved");
    uint8_t current[S2_SAVE_BYTES], bytes[S2_SAVE_BYTES];
    int result=read_file(s->path,current);
    if ((s->exists && (result!=1 || memcmp(current,s->original,sizeof current))) ||
        (!s->exists && result!=0)) {
        s->read_only=1;
        return fail(s,"Save file changed externally; reload before saving");
    }
    if (s->exists && !memcmp(candidate,&s->data,sizeof *candidate)) { s->error[0]=0; return 1; }
    s2_campaign_encode(candidate,s->sequence+1,bytes);
    if (s->exists) {
        char backup[1040]; snprintf(backup,sizeof backup,"%s.bak",s->path);
        if (!atomic_write(backup,s->original)) return fail(s,"Cannot preserve campaign backup; original save unchanged");
    }
    if (!atomic_write(s->path,bytes)) return fail(s,"Cannot write campaign save; original save unchanged");
    s->data=*candidate; ++s->sequence; s->exists=1;
    memcpy(s->original,bytes,sizeof bytes); s->error[0]=0;
    return 1;
}
