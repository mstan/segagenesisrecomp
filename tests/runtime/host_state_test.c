/* Exercise the production save envelope and writer with deterministic storage
 * stalls/failures. The tiny adapter makes guest mutation observable. */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "../../runner/state_crc.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(2); } } while(0)
#define RECOMP_LAUNCHER 0
uint8_t g_ram[65536],g_rom[0x400000];
static uint8_t adapter[16];
static uint32_t machine=17,glue=23,fm=29,psg=31,resumed;
static int boundary,requested,discarded,replaying;
static int genesis_replay_playing(void) { return replaying; }
static void genesis_local_states_save_finished(const char *path,int ok) { (void)path;(void)ok; }
static size_t state_size(void) { return sizeof adapter; }
static int state_save(void *p,size_t n) { memcpy(p,adapter,n);return 1; }
static int state_load(const void *p,size_t n,int apply) {
    if(n!=sizeof adapter)return 0;
    if(apply)memcpy(adapter,p,n);
    return 1;
}
static const struct {
    const char *display_name,*state_build_id;
    size_t expected_rom_size;
    size_t (*state_size)(void);
    int (*state_save)(void *,size_t);
    int (*state_load)(const void *,size_t,int);
    uint32_t resume_main_loop_pc;
    uint32_t (*save_resume_pc)(uint8_t);
} g_game_spec={"Writer test","test-build",sizeof g_rom,state_size,state_save,state_load,0x1000,NULL};
static const struct { uint32_t game_mode_addr; } g_game_layout={0xfff600};
static uint32_t rom_crc32(const uint8_t *p,size_t n) { return ~genesis_state_crc32_update(UINT32_MAX,p,n); }
static void glue_save_state(FILE *f) { (void)fwrite(&glue,sizeof glue,1,f); }
static void glue_load_state(FILE *f) { (void)fread(&glue,sizeof glue,1,f); }
static int machine_save_state(FILE *f) { return fwrite(&machine,sizeof machine,1,f)==1; }
static int machine_load_state(FILE *f) { return fread(&machine,sizeof machine,1,f)==1; }
static int ym2612_save_state(FILE *f) { uint32_t n=sizeof fm;return fwrite(&n,sizeof n,1,f)==1&&fwrite(&fm,sizeof fm,1,f)==1; }
static int ym2612_load_state(FILE *f) { uint32_t n;return fread(&n,sizeof n,1,f)==1&&n==sizeof fm&&fread(&fm,sizeof fm,1,f)==1; }
static int psg_save_state(FILE *f) { return fwrite(&psg,sizeof psg,1,f)==1; }
static int psg_load_state(FILE *f) { return fread(&psg,sizeof psg,1,f)==1; }
static void glue_state_boundary_request(int b) { requested=b; }
static int glue_state_boundary_ready(void) { return boundary; }
static uint8_t glue_peek8(uint32_t p) { return g_ram[p&65535]; }
static void glue_restart_game_fiber(uint32_t pc) { resumed=pc; }
static void audio_discard_playback(void) { ++discarded; }

static SDL_sem *entered,*release_write;
static int block_commit,fail_commit,fail_thread;
static int test_commit(const char *from,const char *to) {
    if(block_commit) {
        SDL_SemPost(entered);
        if(SDL_SemWaitTimeout(release_write,5000))return 0;
    }
    if(fail_commit)return 0;
#ifdef _WIN32
    return MoveFileExA(from,to,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    return rename(from,to)==0;
#endif
}
static SDL_Thread *test_create_thread(SDL_ThreadFunction fn,const char *name,void *p) {
    return fail_thread?NULL:SDL_CreateThread(fn,name,p);
}
static int release_commit(void *unused) {
    (void)unused;SDL_Delay(10);SDL_SemPost(release_write);return 0;
}
#undef SDL_CreateThread
#define SDL_CreateThread test_create_thread
#ifdef _WIN32
#define MoveFileExA(from,to,flags) test_commit(from,to)
#else
#define rename(from,to) (test_commit(from,to)?0:-1)
#endif
#include "../../runner/host_state.h"
#undef SDL_CreateThread
#ifdef _WIN32
#undef MoveFileExA
#else
#undef rename
#endif

static uint32_t file_crc(const char *p) {
    FILE *f=fopen(p,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);
    rewind(f);uint32_t crc;CHECK(genesis_state_crc32_file(f,(size_t)n,&crc));CHECK(!fclose(f));return crc;
}
static void set_guest(uint8_t value) { memset(adapter,value,sizeof adapter);memset(g_ram,value,sizeof g_ram);machine=value; }
static void check_guest(uint8_t value) {
    CHECK(adapter[0]==value&&adapter[15]==value&&g_ram[0]==value&&g_ram[65535]==value&&machine==value);
}
int main(int argc,char **argv) {
    CHECK(argc==2);CHECK(!SDL_Init(SDL_INIT_TIMER));
    entered=SDL_CreateSemaphore(0);release_write=SDL_CreateSemaphore(0);CHECK(entered&&release_write);
    char path[512],temporary[640];CHECK(snprintf(path,sizeof path,"%s/host-state-writer-test.bin",argv[1])>0);
    set_guest(1);CHECK(host_state_save_now(path));CHECK(host_state_collect(1));
    uint32_t original=file_crc(path);

    /* Storage stalls cannot mutate the producer or replace the prior slot.
     * A second request is rejected, and the immutable snapshot wins later. */
    block_commit=1;set_guest(2);CHECK(host_state_save_now(path));
    CHECK(!SDL_SemWaitTimeout(entered,5000));CHECK(host_state_writer);
    strcpy(temporary,host_state_writer->temporary);set_guest(3);
    CHECK(!host_state_collect(0));CHECK(!host_state_save(path));CHECK(!*host_state_pending);
    CHECK(file_crc(path)==original);check_guest(3);
    SDL_SemPost(release_write);CHECK(host_state_collect(1));block_commit=0;
    CHECK(!fopen(temporary,"rb"));CHECK(host_state_load(path));check_guest(2);
    CHECK(resumed==0x1000&&discarded==1);uint32_t second=file_crc(path);CHECK(second!=original);

    /* Pending capture cannot silently load the stale previous slot. A mode
     * without a resumable boundary times out while retaining that slot. */
    set_guest(3);boundary=0;CHECK(host_state_save(path));CHECK(requested);
    CHECK(!host_state_load(path));check_guest(3);
    for(int i=0;i<120;++i)host_state_tick();
    CHECK(!requested&&!*host_state_pending&&file_crc(path)==second);

    /* A failed atomic replacement leaves the previous bytes loadable and
     * rejects a load paired with that failed request exactly once. */
    fail_commit=1;set_guest(4);CHECK(host_state_save_now(path));
    strcpy(temporary,host_state_writer->temporary);
    CHECK(!host_state_load(path));check_guest(4);fail_commit=0;
    CHECK(!fopen(temporary,"rb")&&file_crc(path)==second);
    CHECK(host_state_load(path));check_guest(2);

    /* Shutdown drains an accepted snapshot; thread creation failure uses the
     * same validated synchronous writer rather than losing the request. */
    set_guest(5);CHECK(host_state_save_now(path));host_state_shutdown();CHECK(!host_state_writer);
    CHECK(host_state_load(path));check_guest(5);
    fail_thread=1;set_guest(6);CHECK(host_state_save_now(path));CHECK(!host_state_writer);fail_thread=0;
    CHECK(host_state_load(path));check_guest(6);

    /* Uncapped replay can reach another recorded accepted request before the
     * prior worker completes. Wait without advancing/changing the guest. */
    block_commit=1;set_guest(7);CHECK(host_state_save_now(path));
    CHECK(!SDL_SemWaitTimeout(entered,5000));set_guest(8);replaying=1;
    SDL_Thread *release=test_create_thread(release_commit,"test-release",NULL);CHECK(release);
    CHECK(host_state_save(path));SDL_WaitThread(release,NULL);replaying=0;block_commit=0;
    CHECK(*host_state_pending&&requested);check_guest(8);
    boundary=1;host_state_tick();CHECK(!*host_state_pending);CHECK(host_state_collect(1));
    CHECK(host_state_load(path));check_guest(8);
    CHECK(!remove(path));SDL_DestroySemaphore(entered);SDL_DestroySemaphore(release_write);SDL_Quit();
    puts("Host state writer PASS: immutable capture, busy/pending, timeout, atomic failure, load, shutdown, fallback, replay ordering");
    return 0;
}
