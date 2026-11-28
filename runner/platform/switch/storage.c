/* libnx rename does not replace an existing file. Supply the POSIX contract
 * expected by settings and native saves, retaining the old file until the
 * complete replacement succeeds and restoring it if the second move fails.
 * A process interrupted between moves is recovered on its next fopen. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef __SWITCH__
#include <switch.h>
static Mutex s_storage_lock;
#define STORAGE_LOCK() mutexLock(&s_storage_lock)
#define STORAGE_UNLOCK() mutexUnlock(&s_storage_lock)
#else
/* Host contract tests are single-threaded; this source ships only on Switch. */
#define STORAGE_LOCK() ((void)0)
#define STORAGE_UNLOCK() ((void)0)
#endif

int __real_rename(const char *from,const char *to);
FILE *__real_fopen(const char *path,const char *mode);
static const char s_backup_suffix[]=".genesis-replace-backup";

static char *backup_path(const char *path)
{
    size_t size=strlen(path)+sizeof s_backup_suffix;
    char *out=malloc(size);
    if(out)snprintf(out,size,"%s%s",path,s_backup_suffix);
    return out;
}
static int regular_file(const char *path)
{
    struct stat info;
    return !stat(path,&info) && S_ISREG(info.st_mode);
}

int __wrap_rename(const char *from,const char *to)
{
    if(!from || !to){errno=EINVAL;return -1;}
    if(!strcmp(from,to)){struct stat info;return stat(from,&info);}
    STORAGE_LOCK();
    int result=__real_rename(from,to),failure=errno;
    if(result==0 || (failure!=EEXIST && failure!=ENOTEMPTY) ||
       !regular_file(from) || !regular_file(to))goto done;
    char *backup=backup_path(to);
    if(!backup){failure=ENOMEM;goto done;}
    /* An existing destination is a committed complete replacement. An older
     * backup can remain if its post-commit cleanup was interrupted. */
    if(regular_file(backup) && remove(backup)){failure=errno;free(backup);goto done;}
    if(__real_rename(to,backup)){failure=errno;free(backup);goto done;}
    if(__real_rename(from,to)==0) {
        result=0;
        if(remove(backup))fprintf(stderr,"[storage] retained recovery file %s\n",backup);
    } else {
        failure=errno;
        if(__real_rename(backup,to))
            fprintf(stderr,"[storage] replacement failed; original retained at %s\n",backup);
    }
    free(backup);
done:
    STORAGE_UNLOCK();
    if(result)errno=failure;
    return result;
}

FILE *__wrap_fopen(const char *path,const char *mode)
{
    if(!path || !mode){errno=EINVAL;return NULL;}
    STORAGE_LOCK();
    struct stat info;
    if(stat(path,&info) && errno==ENOENT) {
        char *backup=backup_path(path);
        if(backup) {
            if(regular_file(backup)) {
                if(__real_rename(backup,path)) {
                    int failure=errno;
                    free(backup);STORAGE_UNLOCK();errno=failure;return NULL;
                }
                fprintf(stderr,"[storage] recovered %s\n",path);
            }
            free(backup);
        }
    }
    FILE *file=__real_fopen(path,mode);
    STORAGE_UNLOCK();
    return file;
}
