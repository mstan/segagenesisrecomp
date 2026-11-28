#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "backup.h"
#include "../host_atomic_file.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
    (void)mode;
    wchar_t*w=wide_path(path);if(!w)return NULL;
    FILE*f=_wfopen(w,L"rb");free(w);return f;
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
    return genesis_atomic_file_store(path,data,bytes);
}
