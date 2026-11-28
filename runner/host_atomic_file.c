#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "host_atomic_file.h"
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
static wchar_t *atomic_wide_path(const char *path)
{
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,NULL,0);
    if(!n)return NULL;
    wchar_t *w=malloc((size_t)n*sizeof *w);
    if(w&&!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,w,n)){free(w);return NULL;}
    return w;
}
static FILE *atomic_open_path(const char *path,const char *mode)
{
    wchar_t*w=atomic_wide_path(path);if(!w)return NULL;
    if(!strcmp(mode,"rb")){FILE*f=_wfopen(w,L"rb");free(w);return f;}
    HANDLE handle=CreateFileW(w,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    free(w);
    if(handle==INVALID_HANDLE_VALUE){
        DWORD error=GetLastError();
        errno=(error==ERROR_FILE_EXISTS||error==ERROR_ALREADY_EXISTS)?EEXIST:EIO;
        return NULL;
    }
    int fd=_open_osfhandle((intptr_t)handle,_O_WRONLY|_O_BINARY);
    if(fd<0){CloseHandle(handle);return NULL;}
    FILE*f=_fdopen(fd,"wb");if(!f)_close(fd);return f;
}
#else
#include <unistd.h>
static FILE *atomic_open_path(const char *path,const char *mode){return fopen(path,mode);}
#endif
int genesis_atomic_file_store(const char *path,const void *data,size_t bytes)
{
    if(!path||!*path||(!data&&bytes)||strlen(path)>SIZE_MAX-64)return 0;
    size_t size=strlen(path)+64;char*temp=malloc(size);if(!temp)return 0;
#ifdef _WIN32
    unsigned long pid=GetCurrentProcessId();
#else
    unsigned long pid=(unsigned long)getpid();
#endif
    /* A live allocation is unique between concurrent writers. CREATE_NEW/x
     * also protects stale files from previous processes; bounded retries handle
     * address reuse without a shared counter/data race with UI settings saves. */
    FILE*f=NULL;
    for(unsigned attempt=0;attempt<16;++attempt) {
        (void)snprintf(temp,size,"%s.tmp-%lu-%p-%u",path,pid,(void*)temp,attempt);
        f=atomic_open_path(temp,"wbx");
        if(f||errno!=EEXIST)break;
    }
    if(!f){free(temp);return 0;}
    int ok=(!bytes||fwrite(data,1,bytes,f)==bytes)&&!fflush(f);
#ifdef _WIN32
    if(ok&&_commit(_fileno(f)))ok=0;
#else
    if(ok&&fsync(fileno(f)))ok=0;
#endif
    if(fclose(f))ok=0;
#ifdef _WIN32
    wchar_t*target=atomic_wide_path(path),*tmp=atomic_wide_path(temp);
    if(ok&&(!target||!tmp||!MoveFileExW(tmp,target,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)))ok=0;
    if(!ok&&tmp)(void)_wremove(tmp);
    free(target);free(tmp);
#else
    if(ok&&rename(temp,path))ok=0;
    if(!ok)(void)remove(temp);
#endif
    free(temp);return ok;
}
