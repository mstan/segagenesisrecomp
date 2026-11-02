/* Exercise actual positioned worker I/O with deterministic completion barriers. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../runner/engine_worker.h"
static int allocation_failure;
static void *test_malloc(size_t n){return allocation_failure?NULL:malloc(n);}
#define malloc test_malloc
#include "../../runner/cd/disc_image.c"
#undef malloc
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {GenesisWorker *worker;SDL_sem *gate;void(*task)(void*);void*user;int skip;} TestWorker;
static int refuse_start,fail_create,skip_read;static unsigned destroyed,off_thread;
static SDL_threadID owner;
static void gated_job(void *user)
{
    TestWorker*w=user;CHECK(SDL_SemWait(w->gate)==0);
    CHECK(SDL_ThreadID()!=owner);++off_thread;
    if(!w->skip)w->task(w->user);
}
static void *create_worker(void)
{
    if(fail_create)return NULL;
    TestWorker*w=calloc(1,sizeof *w);CHECK(w);
    w->worker=genesis_worker_create();w->gate=SDL_CreateSemaphore(0);
    CHECK(w->worker&&w->gate);return w;
}
static int start_worker(void *user,void(*task)(void*),void*context)
{
    TestWorker*w=user;if(refuse_start)return 0;
    CHECK(!genesis_worker_busy(w->worker));
    w->task=task;w->user=context;w->skip=skip_read;
    return genesis_worker_try_start(w->worker,gated_job,w);
}
static int busy_worker(void*user){return genesis_worker_busy(((TestWorker*)user)->worker);}
static void finish(SegaCDImage *image)
{
    TestWorker*w=image->prefetch.worker;
    if(w&&genesis_worker_busy(w->worker))CHECK(SDL_SemPost(w->gate)==0);
    if(w)genesis_worker_join(w->worker);
}
static void destroy_worker(void*user)
{
    TestWorker*w=user;CHECK(SDL_SemPost(w->gate)==0);genesis_worker_destroy(w->worker);
    SDL_DestroySemaphore(w->gate);free(w);++destroyed;
}
static const SegaCDImageAsyncHost host={create_worker,start_worker,busy_worker,destroy_worker};
static void put32(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void put64(uint8_t*p,uint64_t n){put32(p,(uint32_t)n);put32(p+4,(uint32_t)(n>>32));}
static void fill(uint8_t*p,int32_t lba)
{
    for(int i=0;i<SECTOR_BYTES;i++){int n=(lba*17+i)%251;p[i]=(uint8_t)(n<0?n+251:n);}
    put32(p,(uint32_t)lba);
}
static void fixture(const char*path)
{
    uint8_t header[160]={0},sector[SECTOR_BYTES];
    static const uint8_t digest[32]={0xae,0x7e,0xa5,0xc5,0xda,0xd7,0x81,0xee,0xad,0x0f,0xc8,0x14,0x5e,0x12,0x2f,0x0d,0x63,0x84,0x05,0x06,0x48,0x98,0x09,0x2c,0xb0,0x65,0xf9,0xd0,0x13,0x23,0xa5,0xa0};
    memcpy(header,"SGCDIMG\0",8);put32(header+8,1);put32(header+12,160);
    put32(header+16,SECTOR_BYTES);put32(header+20,2);put32(header+24,(uint32_t)-150);
    put32(header+28,1700);put64(header+32,160);put64(header+40,1850u*SECTOR_BYTES);
    memcpy(header+48,digest,32);
    put32(header+96,1);put32(header+100,1);put32(header+104,(uint32_t)-150);
    put32(header+108,0);put32(header+112,1);
    put32(header+128,2);put32(header+132,0);put32(header+136,1);
    put32(header+140,1);put32(header+144,1700);
    FILE*f=fopen(path,"wb");CHECK(f&&fwrite(header,1,sizeof header,f)==sizeof header);
    for(int32_t lba=-150;lba<1700;lba++){fill(sector,lba);CHECK(fwrite(sector,1,sizeof sector,f)==sizeof sector);}
    CHECK(!fclose(f));
}
static SegaCDImage *open_image(const char*path)
{
    char error[128];SegaCDImage*i=segacd_image_open(path,error,sizeof error);CHECK(i);
    segacd_image_set_async_host(i,&host);return i;
}
static void read_sector(SegaCDImage*i,int32_t lba)
{
    uint8_t got[SECTOR_BYTES],expected[SECTOR_BYTES];fill(expected,lba);
    CHECK(segacd_image_read(i,lba,got)&&!memcmp(got,expected,sizeof got));
}
static void forbidden(void*user){(void)user;CHECK(0);}
int main(void)
{
    SDL_SetMainReady();CHECK(!SDL_Init(SDL_INIT_TIMER));owner=SDL_ThreadID();
    CHECK(!genesis_worker_try_start(NULL,forbidden,NULL)&&!genesis_worker_busy(NULL));
    const char*path="prefetch-fixture.sgcd";fixture(path);
    SegaCDImage*i=open_image(path);read_sector(i,1);read_sector(i,2);
    CHECK(i->prefetch.pending&&busy_worker(i->prefetch.worker));
    TestWorker*w=i->prefetch.worker;
    CHECK(!genesis_worker_try_start(w->worker,forbidden,NULL));
    read_sector(i,3);CHECK(i->raw_reads==3&&i->prefetch_hits==0); /* busy miss never joins */
    finish(i);read_sector(i,4);CHECK(i->prefetch_hits==1);
    for(int lba=5;lba<1700;lba++) {
        read_sector(i,lba);finish(i);
        if(lba==387)CHECK(i->prefetch.ready_first==3&&i->prefetch.pending);
    }
    CHECK(i->prefetch_hits==1696&&i->prefetch_jobs==4&&i->prefetch_failures==0);
    CHECK(i->prefetch.job_first+(int32_t)i->prefetch.job_count==1700);
    uint8_t raw[SECTOR_BYTES];memset(raw,0xa5,sizeof raw);
    CHECK(!segacd_image_read(i,1700,raw));for(size_t n=0;n<sizeof raw;n++)CHECK(raw[n]==0xa5);
    SegaCDImage*j=open_image(path);read_sector(j,1);read_sector(j,2);
    read_sector(i,900);read_sector(i,901);CHECK(i->prefetch.pending);
    segacd_image_prefetch_cancel(i);finish(i);read_sector(i,902);
    CHECK(i->prefetch.ready_count==0&&i->prefetch_hits==1696);
    finish(j);read_sector(j,3);CHECK(j->prefetch_hits==1&&j->prefetch.ready_first==3);
    segacd_image_close(j);
    /* Destruction must release a still-blocked real worker before freeing inputs. */
    read_sector(i,903);CHECK(i->prefetch.pending);unsigned before=destroyed;
    segacd_image_close(i);CHECK(destroyed==before+1);
    for(unsigned failure=0;failure<4;failure++) {
        i=open_image(path);allocation_failure=failure==0;fail_create=failure==1;
        refuse_start=failure==2;skip_read=failure==3;
        read_sector(i,1);read_sector(i,2);finish(i);read_sector(i,3);
        CHECK(!i->prefetch_hits&&i->raw_reads==3);
        if(failure==3)CHECK(i->prefetch_failures==1);
        allocation_failure=fail_create=refuse_start=skip_read=0;segacd_image_close(i);
    }
    CHECK(off_thread>=6&&destroyed==4);CHECK(!remove(path));SDL_Quit();
    puts("prefetch windows, bytes, busy/seek/cancel/tail/lifetime and failure fallbacks passed");return 0;
}
