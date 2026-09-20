#include "trilogy_sram.h"
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

enum { HEADER=24, RECORD=16, MAX_RECORDS=256 };
static uint32_t get32(const uint8_t *p)
{return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static unsigned get16(const uint8_t *p){return (unsigned)p[0]<<8|p[1];}
static void put32(uint8_t *p,uint32_t n)
{p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n;}
static void put16(uint8_t *p,unsigned n){p[0]=(uint8_t)(n>>8);p[1]=(uint8_t)n;}
static uint32_t crc(const uint8_t *p,size_t n)
{uint32_t c=~0u;while(n--){c^=*p++;for(unsigned j=0;j<8;++j)c=(c>>1)^((0u-(c&1))&0xEDB88320u);}return ~c;}
static int fail(TrSram *s,const char *why)
{snprintf(s->error,sizeof s->error,"%s",why);return 0;}
static int valid_tail(const uint8_t *p,size_t size)
{
    if(!size)return 1;
    if(size<HEADER||memcmp(p,"S3KEXT01",8)||get32(p+8)!=1||get32(p+12)!=size||
       get32(p+16)>MAX_RECORDS||get32(p+20)!=crc(p+HEADER,size-HEADER))return 0;
    size_t pos=HEADER, offsets[MAX_RECORDS];unsigned count=get32(p+16);
    for(unsigned i=0;i<count;++i){
        if(pos>size||size-pos<RECORD)return 0;
        const uint8_t *r=p+pos;size_t len=get32(r+8);
        if(len>size-pos-RECORD || !get16(r+4) || get16(r+6) ||
           get32(r+12)!=crc(r+RECORD,len))return 0;
        for(unsigned j=0;j<i;++j)if(!memcmp(r,p+offsets[j],4))return 0;
        offsets[i]=pos;pos+=RECORD+len;
    }
    return pos==size;
}
void tr_sram_dispose(TrSram *s)
{if(s){free(s->original);free(s->tail);memset(s,0,sizeof *s);}}
/* 0 missing, 1 read, -1 unreadable/oversized. Size bounded before allocation. */
static int read_file(const char *path,uint8_t **out,size_t *size)
{
    FILE *f=fopen(path,"rb");if(!f)return errno==ENOENT?0:-1;
    if(fseek(f,0,SEEK_END)){fclose(f);return -1;}
    long n=ftell(f);if(n<0||n>TR_SRAM_LIMIT||fseek(f,0,SEEK_SET)){fclose(f);return -1;}
    uint8_t *p=malloc(n?(size_t)n:1);if(!p){fclose(f);return -1;}
    size_t got=fread(p,1,(size_t)n,f);int extra=fgetc(f),ok=got==(size_t)n&&extra==EOF&&!ferror(f);
    if(fclose(f))ok=0;if(!ok){free(p);return -1;}*out=p;*size=(size_t)n;return 1;
}
int tr_sram_open(TrSram *s,const char *path,uint8_t base[TR_SRAM_BASE])
{
    if(!s||!base||!path||!*path)return 0;
    /* Caller initializes the store to zero; re-open explicitly disposes it. */
    tr_sram_dispose(s);s->read_only=1;
    if(strlen(path)>=sizeof s->path)return fail(s,"SRAM path is too long");
    strcpy(s->path,path);
    int result=read_file(path,&s->original,&s->original_size);s->exists=result!=0;
    if(!result){s->ready=s->extensions_valid=1;s->read_only=0;return 1;}
    if(result<0)return fail(s,"SRAM file cannot be read safely; original preserved");
    if(s->original_size<TR_SRAM_BASE)return fail(s,"Truncated native SRAM; original preserved");
    size_t size=s->original_size-TR_SRAM_BASE;
    if(size){s->tail=malloc(size);if(!s->tail)return fail(s,"Cannot allocate SRAM extensions");
        memcpy(s->tail,s->original+TR_SRAM_BASE,size);s->tail_size=size;}
    memcpy(base,s->original,TR_SRAM_BASE);s->ready=1;s->read_only=0;
    s->extensions_valid=valid_tail(s->tail,s->tail_size);
    if(!s->extensions_valid)fail(s,"Unknown or damaged extension tail preserved; native SRAM remains usable");
    return 1;
}
const uint8_t *tr_sram_get(const TrSram *s,const char tag[4],unsigned *version,size_t *size)
{
    if(!s||!tag||!s->extensions_valid||!s->tail_size)return NULL;
    for(size_t pos=HEADER;pos<s->tail_size;){const uint8_t *r=s->tail+pos;size_t n=get32(r+8);
        if(!memcmp(r,tag,4)){if(version)*version=get16(r+4);if(size)*size=n;return r+RECORD;}
        pos+=RECORD+n;}
    return NULL;
}
int tr_sram_set(TrSram *s,const char tag[4],unsigned version,const void *data,size_t size)
{
    if(!s||!tag||(!data&&size))return 0;
    if(!s->ready||s->read_only||!s->extensions_valid)return fail(s,"SRAM extension writes are protected");
    if(!version||version>65535||size>TR_SRAM_LIMIT-TR_SRAM_BASE-HEADER-RECORD)
        return fail(s,"Invalid SRAM extension size or version");
    size_t old_pos=s->tail_size?s->tail_size:HEADER,old_len=0;
    unsigned count=s->tail_size?get32(s->tail+16):0;
    for(size_t pos=HEADER;pos<s->tail_size;){const uint8_t *r=s->tail+pos;size_t n=get32(r+8);
        if(!memcmp(r,tag,4)){old_pos=pos;old_len=RECORD+n;
            if(get16(r+4)==version&&n==size&&(!size||!memcmp(r+RECORD,data,size)))return 1;break;}
        pos+=RECORD+n;}
    if(!old_len&&count==MAX_RECORDS)return fail(s,"Too many SRAM extensions");
    size_t total=(s->tail_size?s->tail_size:HEADER)-old_len+RECORD+size;
    if(total>TR_SRAM_LIMIT-TR_SRAM_BASE)return fail(s,"SRAM extensions exceed size limit");
    uint8_t *next=calloc(1,total);if(!next)return fail(s,"Cannot allocate SRAM extension");
    if(s->tail_size){memcpy(next,s->tail,old_pos);
        if(old_len)memcpy(next+old_pos+RECORD+size,s->tail+old_pos+old_len,s->tail_size-old_pos-old_len);}
    memcpy(next,"S3KEXT01",8);put32(next+8,1);put32(next+12,(uint32_t)total);
    put32(next+16,count+(old_len?0:1));
    uint8_t *r=next+old_pos;memcpy(r,tag,4);put16(r+4,version);put32(r+8,(uint32_t)size);
    if(size)memcpy(r+RECORD,data,size);put32(r+12,crc(r+RECORD,size));
    put32(next+20,crc(next+HEADER,total-HEADER));
    free(s->tail);s->tail=next;s->tail_size=total;++s->generation;s->error[0]=0;return 1;
}
int tr_sram_set_many(TrSram *s,const TrSramRecord *records,size_t count)
{
    if(!s||(!records&&count)||count>MAX_RECORDS)return 0;
    if(!s->ready||s->read_only||!s->extensions_valid)return fail(s,"SRAM extension writes are protected");
    TrSram next=*s;next.tail=NULL;
    if(s->tail_size){next.tail=malloc(s->tail_size);if(!next.tail)return fail(s,"Cannot allocate SRAM transaction");
        memcpy(next.tail,s->tail,s->tail_size);}
    for(size_t i=0;i<count;++i){
        for(size_t j=0;j<i;++j)if(!memcmp(records[i].tag,records[j].tag,4)){
            free(next.tail);return fail(s,"Duplicate record in SRAM transaction");}
        if(!tr_sram_set(&next,records[i].tag,records[i].version,records[i].data,records[i].size)){
            free(next.tail);return fail(s,next.error);}
    }
    int changed=next.generation!=s->generation;
    free(s->tail);s->tail=next.tail;s->tail_size=next.tail_size;
    if(changed)++s->generation;s->error[0]=0;return 1;
}
static int atomic_write(const char *path,const uint8_t *data,size_t size)
{
    char temp[1100];FILE *f=NULL;
#ifdef _WIN32
    int fd=-1;
    for(unsigned i=0;i<100;++i){snprintf(temp,sizeof temp,"%s.%lu.%u.tmp",path,(unsigned long)GetCurrentProcessId(),i);
        errno_t e=_sopen_s(&fd,temp,_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY,_SH_DENYRW,_S_IREAD|_S_IWRITE);
        if(!e)break;if(e!=EEXIST)return 0;}
    if(fd<0)return 0;f=_fdopen(fd,"wb");if(!f){_close(fd);remove(temp);return 0;}
#else
    snprintf(temp,sizeof temp,"%s.XXXXXX",path);int fd=mkstemp(temp);if(fd<0)return 0;
    f=fdopen(fd,"wb");if(!f){close(fd);remove(temp);return 0;}
#endif
    int ok=fwrite(data,1,size,f)==size&&fflush(f)==0;
#ifdef _WIN32
    if(ok)ok=_commit(_fileno(f))==0;
#else
    if(ok)ok=fsync(fileno(f))==0;
#endif
    if(fclose(f))ok=0;
#ifdef _WIN32
    if(ok)ok=MoveFileExA(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    if(ok)ok=rename(temp,path)==0;
#endif
    if(!ok)remove(temp);return ok;
}
static int commit_locked(TrSram *s,const uint8_t base[TR_SRAM_BASE])
{
    uint8_t *disk=NULL;size_t disk_size=0;int result=read_file(s->path,&disk,&disk_size);
    int changed=s->exists?(result!=1||disk_size!=s->original_size||memcmp(disk,s->original,disk_size)):(result!=0);
    free(disk);if(changed){s->read_only=1;return fail(s,"SRAM changed externally; reload before saving");}
    size_t total=TR_SRAM_BASE+s->tail_size;
    uint8_t *next=malloc(total);if(!next)return fail(s,"Cannot allocate SRAM write buffer");
    memcpy(next,base,TR_SRAM_BASE);if(s->tail_size)memcpy(next+TR_SRAM_BASE,s->tail,s->tail_size);
    if(s->exists&&total==s->original_size&&!memcmp(next,s->original,total)){free(next);return 1;}
    if(s->exists){char backup[1040];snprintf(backup,sizeof backup,"%s.bak",s->path);
        int preserve_backup=0;
        if(!valid_tail(s->original+TR_SRAM_BASE,s->original_size-TR_SRAM_BASE)){
            /* A native-only save of a damaged container must not replace the
             * previous recoverable copy with the damaged one. */
            uint8_t *prior=NULL;size_t prior_size=0;
            preserve_backup=read_file(backup,&prior,&prior_size)!=0;free(prior);
        }
        if(!preserve_backup&&!atomic_write(backup,s->original,s->original_size)){
            free(next);return fail(s,"Cannot preserve SRAM backup; original unchanged");}}
    if(!atomic_write(s->path,next,total)){free(next);return fail(s,"SRAM write failed; original unchanged");}
    free(s->original);s->original=next;s->original_size=total;s->exists=1;return 1;
}
int tr_sram_commit(TrSram *s,const uint8_t base[TR_SRAM_BASE])
{
    if(!s||!base)return 0;
    if(!s->ready||s->read_only)return fail(s,"SRAM file is protected; no files changed");
    /* Serialize the compare/backup/replace sequence across updated runners.
     * Keep the lock file: unlinking it creates a race between different inodes. */
    char lock_path[1040];snprintf(lock_path,sizeof lock_path,"%s.lock",s->path);
    int fd=-1;
#ifdef _WIN32
    if(_sopen_s(&fd,lock_path,_O_WRONLY|_O_CREAT|_O_BINARY,_SH_DENYRW,_S_IREAD|_S_IWRITE))
        return fail(s,"SRAM is busy or its lock cannot be opened; retry later");
#else
    fd=open(lock_path,O_WRONLY|O_CREAT,0600);
    if(fd<0)return fail(s,"Cannot open SRAM lock; retry later");
    struct flock lock={0};lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;
    if(fcntl(fd,F_SETLK,&lock)<0){close(fd);return fail(s,"SRAM is busy; retry later");}
#endif
    int ok=commit_locked(s,base);
#ifdef _WIN32
    _close(fd);
#else
    close(fd);
#endif
    if(ok)s->error[0]=0;
    return ok;
}
static TrSram current;
TrSram *tr_sram_current(void){return &current;}
uint64_t tr_sram_generation(void){return current.generation;}
int tr_sram_load(const char *path,uint8_t *base,size_t size)
{
    if(size!=TR_SRAM_BASE)return 0;
    int ok=tr_sram_open(&current,path,base);
    if(current.error[0])fprintf(stderr,"[SRAM] %s\n",current.error);
    return ok;
}
int tr_sram_save(const char *path,const uint8_t *base,size_t size)
{
    if(size!=TR_SRAM_BASE||strcmp(path,current.path))return 0;
    int ok=tr_sram_commit(&current,base);
    if(!ok)fprintf(stderr,"[SRAM] %s\n",current.error);
    return ok;
}
