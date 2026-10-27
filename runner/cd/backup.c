#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "backup.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
static wchar_t *wide_path(const char *path)
{
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,NULL,0);
    if(!n)return NULL;
    wchar_t *w=malloc((size_t)n*sizeof *w);
    if(w&&!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,w,n)){free(w);return NULL;}
    return w;
}
static FILE *open_path(const char *path,const char *mode)
{
    wchar_t*w=wide_path(path);if(!w)return NULL;
    if(!strcmp(mode,"rb")){FILE*f=_wfopen(w,L"rb");free(w);return f;}
    HANDLE handle=CreateFileW(w,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    free(w);
    if(handle==INVALID_HANDLE_VALUE)return NULL;
    int fd=_open_osfhandle((intptr_t)handle,_O_WRONLY|_O_BINARY);
    if(fd<0){CloseHandle(handle);return NULL;}
    FILE*f=_fdopen(fd,"wb");if(!f)_close(fd);return f;
}
#else
#include <unistd.h>
static FILE *open_path(const char *path,const char *mode){return fopen(path,mode);}
#endif
SegaCDBackupResult segacd_backup_load(const char *path,uint8_t backup[SEGACD_BACKUP_BYTES])
{
    if(!path||!*path||!backup)return SEGACD_BACKUP_ERROR;
    errno=0;FILE*f=open_path(path,"rb");
    if(!f)return errno==ENOENT?SEGACD_BACKUP_NEW:SEGACD_BACKUP_ERROR;
    uint8_t data[SEGACD_BACKUP_BYTES];
    int ok=fread(data,1,sizeof data,f)==sizeof data&&fgetc(f)==EOF&&!ferror(f);
    if(fclose(f))ok=0;
    if(!ok)return SEGACD_BACKUP_ERROR;
    memcpy(backup,data,sizeof data);return SEGACD_BACKUP_OK;
}
SegaCDBackupResult segacd_backup_store(const char *path,const uint8_t backup[SEGACD_BACKUP_BYTES])
{
    if(!backup)return SEGACD_BACKUP_ERROR;
    return segacd_atomic_file_store(path,backup,SEGACD_BACKUP_BYTES)?SEGACD_BACKUP_OK:SEGACD_BACKUP_ERROR;
}
int segacd_atomic_file_store(const char *path,const void *data,size_t bytes)
{
    static unsigned sequence;
    if(!path||!*path||(!data&&bytes)||strlen(path)>SIZE_MAX-64)return 0;
    size_t size=strlen(path)+64;char*temp=malloc(size);if(!temp)return 0;
#ifdef _WIN32
    unsigned long pid=GetCurrentProcessId();
#else
    unsigned long pid=(unsigned long)getpid();
#endif
    (void)snprintf(temp,size,"%s.tmp-%lu-%u",path,pid,++sequence);
    FILE*f=open_path(temp,"wbx");
    if(!f){free(temp);return 0;}
    int ok=(!bytes||fwrite(data,1,bytes,f)==bytes)&&!fflush(f);
#ifdef _WIN32
    if(ok&&_commit(_fileno(f)))ok=0;
#else
    if(ok&&fsync(fileno(f)))ok=0;
#endif
    if(fclose(f))ok=0;
#ifdef _WIN32
    wchar_t*target=wide_path(path),*tmp=wide_path(temp);
    if(ok&&(!target||!tmp||!MoveFileExW(tmp,target,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)))ok=0;
    if(!ok&&tmp)(void)_wremove(tmp);
    free(target);free(tmp);
#else
    if(ok&&rename(temp,path))ok=0;
    if(!ok)(void)remove(temp);
#endif
    free(temp);return ok;
}
