#define SDL_MAIN_HANDLED
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include "../../runner/engine_worker.c"
static char test_root[256];
const char *exe_relative(const char *name)
{
    static char path[512]; snprintf(path, sizeof path, "%s/%s", test_root, name); return path;
}
static unsigned checks, commits;
static int fail_commit, fail_calloc;
static SDL_atomic_t block_open;
static SDL_sem *started, *allow;
static SDL_threadID producer;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);exit(2); } } while(0)
static FILE *controlled_open(const char *path, const char *mode)
{
    if (!strcmp(mode,"w") && SDL_ThreadID()!=producer && SDL_AtomicCAS(&block_open,1,0)) {
        SDL_SemPost(started); SDL_SemWait(allow);
    }
    return fopen(path,mode);
}
static int controlled_commit(int fd)
{
    ++commits;
    if (fail_commit) { errno=EIO; return -1; }
#ifdef _WIN32
    return _commit(fd);
#else
    return fsync(fd);
#endif
}
static void *controlled_calloc(size_t n,size_t size)
{
    if(fail_calloc){fail_calloc=0;return NULL;} return calloc(n,size);
}
#define fopen controlled_open
#define calloc controlled_calloc
#ifdef _WIN32
#define _commit controlled_commit
#else
#define fsync controlled_commit
#endif
#include "../../runner/runtime_evidence.c"
#undef fopen
#undef calloc
#ifdef _WIN32
#undef _commit
#else
#undef fsync
#endif
static char *file_text(const char *path)
{
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));
    long n=ftell(f);CHECK(n>=0);CHECK(!fseek(f,0,SEEK_SET));
    char *text=malloc((size_t)n+1);CHECK(text);CHECK(fread(text,1,(size_t)n,f)==(size_t)n);
    text[n]=0;CHECK(!fclose(f));return text;
}
static void add_entry(uint32_t address,const char *note)
{
    CHECK(runtime_evidence_add(RT_EVIDENCE_DISPATCH_MISS,address,s_session_frames,note));
}
int main(int argc,char **argv)
{
    CHECK(argc==2);CHECK(!SDL_Init(0));producer=SDL_ThreadID();
    started=SDL_CreateSemaphore(0);allow=SDL_CreateSemaphore(0);CHECK(started&&allow);
    snprintf(test_root,sizeof test_root,"%s",argv[1]);
    runtime_evidence_request_fresh();
    runtime_evidence_init("e35ownership","E35 ownership fixture",NULL,0);
    CHECK(!s_writer && !s_writer_attempted && !s_write && !s_requested);
    unsigned before=commits;
    for(int i=0;i<600;i++)runtime_evidence_tick();
    CHECK(commits==before); /* Default counters do not write every frame. */
    char target[512];snprintf(target,sizeof target,"%s/dispatch_misses.toml",test_root);
    char note[]="immutable published note";add_entry(2,note);
#if GENESIS_ENGINE_WORKERS
    SDL_AtomicSet(&block_open,1);
#endif
    runtime_evidence_sync(RT_EVIDENCE_DISPATCH_MISS);
    memset(note,'X',sizeof note-1);
#if GENESIS_ENGINE_WORKERS
    CHECK(s_writer&&s_write);CHECK(SDL_SemWaitTimeout(started,5000)==0);
    CHECK(s_write->file[0].n==1 && s_write->frames==600);
    CHECK(s_write->file[0].e!=s_files[0].e);
    CHECK(s_write->file[0].e[0].note!=s_files[0].e[0].note);
    CHECK(strstr(s_write->file[0].e[0].note,"immutable published note"));
    CHECK(s_files[0].dirty);
    EvEntry *published=s_write->file[0].e;
    for(uint32_t i=1;i<1024;i++)add_entry(2+i*2,"new while blocked");
    CHECK(s_write->file[0].e==published && s_write->file[0].n==1);
    runtime_evidence_sync(RT_EVIDENCE_DISPATCH_MISS);
    CHECK(s_requested&1u);
    for(int i=0;i<10;i++)runtime_evidence_tick();
    SDL_SemPost(allow);poll_write(1);
    CHECK(s_files[0].dirty); /* Old generation cannot acknowledge newer adds. */
    char *published_text=file_text(target);
    CHECK(strstr(published_text,"# entries:        1\r\n")||strstr(published_text,"# entries:        1\n"));
    CHECK(strstr(published_text,"# session_frames: 600 "));
    CHECK(strstr(published_text,"immutable published note")&&!strstr(published_text,"new while blocked"));free(published_text);
#else
    CHECK(!s_writer&&!s_write&&!s_files[0].dirty);
    for(uint32_t i=1;i<1024;i++)add_entry(2+i*2,"new while blocked");
    runtime_evidence_sync(RT_EVIDENCE_DISPATCH_MISS);
#endif
    runtime_evidence_flush();CHECK(!s_writer&&!s_write&&!s_requested&&!s_files[0].dirty);
    char *text=file_text(target);CHECK(strstr(text,"# entries:        1024"));free(text);

    /* Durable callback failure keeps the prior file and remains retryable. */
    char *prior=file_text(target);add_entry(0x8000,"retry commit failure");
    fail_commit=1;runtime_evidence_sync(RT_EVIDENCE_DISPATCH_MISS);poll_write(1);
    CHECK(s_files[0].dirty);text=file_text(target);CHECK(!strcmp(text,prior));free(text);
    before=commits;for(int i=0;i<10;i++)runtime_evidence_tick();CHECK(commits==before);
    fail_commit=0;runtime_evidence_sync(RT_EVIDENCE_DISPATCH_MISS);poll_write(1);
    CHECK(!s_files[0].dirty);text=file_text(target);CHECK(strstr(text,"retry commit failure"));free(text);free(prior);

    /* Open failure can be retried after the path becomes usable. */
    char valid_root[sizeof test_root];memcpy(valid_root,test_root,sizeof test_root);
    snprintf(test_root,sizeof test_root,"%s/absent-parent",argv[1]);
    add_entry(0x8002,"retry open failure");runtime_evidence_sync(RT_EVIDENCE_DISPATCH_MISS);poll_write(1);
    CHECK(s_files[0].dirty);memcpy(test_root,valid_root,sizeof test_root);
    runtime_evidence_sync(RT_EVIDENCE_DISPATCH_MISS);poll_write(1);CHECK(!s_files[0].dirty);
    text=file_text(target);CHECK(strstr(text,"retry open failure"));free(text);
#ifdef _WIN32
    prior=file_text(target);
    HANDLE hold=CreateFileA(target,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    CHECK(hold!=INVALID_HANDLE_VALUE);add_entry(0x8004,"retry replacement failure");
    runtime_evidence_sync(RT_EVIDENCE_DISPATCH_MISS);poll_write(1);CHECK(s_files[0].dirty);
    CloseHandle(hold);text=file_text(target);CHECK(!strcmp(text,prior));free(text);free(prior);
    runtime_evidence_sync(RT_EVIDENCE_DISPATCH_MISS);poll_write(1);CHECK(!s_files[0].dirty);
#endif
#if GENESIS_ENGINE_WORKERS
    /* Failing snapshot allocation must use the real synchronous fallback. */
    add_entry(0x8006,"allocation fallback");fail_calloc=1;
    runtime_evidence_sync(RT_EVIDENCE_DISPATCH_MISS);
    CHECK(!fail_calloc&&!s_write&&!s_files[0].dirty);
    text=file_text(target);CHECK(strstr(text,"allocation fallback"));free(text);
    /* Final flush waits for an older snapshot and includes newer unqueued data. */
    add_entry(0x8008,"shutdown accepted");SDL_AtomicSet(&block_open,1);
    runtime_evidence_sync(RT_EVIDENCE_DISPATCH_MISS);CHECK(SDL_SemWaitTimeout(started,5000)==0);
    add_entry(0x800a,"shutdown latest");SDL_SemPost(allow);
#else
    add_entry(0x800a,"shutdown latest");
#endif
    runtime_evidence_flush();CHECK(!s_write&&!s_writer&&!s_requested&&!s_files[0].dirty);
    text=file_text(target);CHECK(strstr(text,"shutdown latest"));free(text);
    before=commits;runtime_evidence_flush();CHECK(commits==before);
    /* Advance the real minute-expiry scheduling branch without sleeping.
     * Ordinary ticks still do no I/O; the due tick publishes all counters. */
    while(s_session_frames%FLUSH_CHECK_FRAMES!=FLUSH_CHECK_FRAMES-1)runtime_evidence_tick();
    s_last_flush=time(NULL)-FLUSH_MIN_SECONDS;
#if GENESIS_ENGINE_WORKERS
    SDL_AtomicSet(&block_open,1);
#endif
    runtime_evidence_tick();
#if GENESIS_ENGINE_WORKERS
    CHECK(s_write&&s_write->mask==EV_ALL);CHECK(SDL_SemWaitTimeout(started,5000)==0);
    CHECK(s_write->frames==s_session_frames);SDL_SemPost(allow);poll_write(1);
#else
    CHECK(!s_write&&!s_writer);
#endif
    CHECK(s_flushed_frames==s_session_frames);
    for(int k=0;k<RT_EVIDENCE_KIND_COUNT;k++) {
        char rel[200],path[512],counter[80];snprintf(rel,sizeof rel,"%s.toml",s_files[k].stem);
        snprintf(path,sizeof path,"%s",exe_relative(rel));
        snprintf(counter,sizeof counter,"# session_frames: %llu ",s_session_frames);
        text=file_text(path);CHECK(strstr(text,counter));free(text);
    }
    runtime_evidence_flush();CHECK(!s_writer&&!s_write);
    SDL_DestroySemaphore(started);SDL_DestroySemaphore(allow);
    printf("PASS evidence ownership workers=%d checks=%u\n",GENESIS_ENGINE_WORKERS,checks);
    SDL_Quit();return 0;
}
