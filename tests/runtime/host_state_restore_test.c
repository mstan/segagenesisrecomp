#define _GNU_SOURCE
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "game_spec.h"
#include "state_crc.h"

/* Exercise the real envelope and transactional loader with observable fake
 * machine/chip state. No game window, retail data or owner save is opened. */
uint8_t g_ram[65536],g_rom[0x400000];
static uint32_t host_value,cpu_value,machine_value,fm_value,psg_value;
static int fail_machine_once,temporary_calls;
static const struct {uint32_t game_mode_addr;} g_game_layout={0};
static uint32_t rom_crc32(const uint8_t *data,size_t size)
{return ~genesis_state_crc32_update(UINT32_MAX,data,size);}
static size_t state_size(void){return sizeof host_value;}
static int state_save(void *data,size_t size)
{if(size!=sizeof host_value)return 0;memcpy(data,&host_value,size);return 1;}
static int state_load(const void *data,size_t size,int apply)
{if(size!=sizeof host_value)return 0;if(apply)memcpy(&host_value,data,size);return 1;}
const GameSpec g_game_spec={.display_name="State fixture",.state_build_id="host-state-contract-v1",
    .expected_rom_size=32,.state_size=state_size,.state_save=state_save,.state_load=state_load};
static void glue_save_state(FILE *f){(void)fwrite(&cpu_value,sizeof cpu_value,1,f);}
static void glue_load_state(FILE *f){if(fread(&cpu_value,sizeof cpu_value,1,f)!=1)cpu_value=UINT32_MAX;}
static int machine_save_state(FILE *f){return fwrite(&machine_value,sizeof machine_value,1,f)==1;}
static int machine_load_state(FILE *f){
    int ok=fread(&machine_value,sizeof machine_value,1,f)==1;
    if(fail_machine_once){fail_machine_once=0;return 0;}return ok;
}
static int ym2612_save_state(FILE *f){uint32_t n=sizeof fm_value;return fwrite(&n,4,1,f)==1&&fwrite(&fm_value,4,1,f)==1;}
static int ym2612_load_state(FILE *f){uint32_t n;return fread(&n,4,1,f)==1&&n==4&&fread(&fm_value,4,1,f)==1;}
static int psg_save_state(FILE *f){return fwrite(&psg_value,4,1,f)==1;}
static int psg_load_state(FILE *f){return fread(&psg_value,4,1,f)==1;}
static int genesis_replay_playing(void){return 0;}
static void genesis_local_states_save_finished(const char *p,int ok){(void)p;(void)ok;}
static void glue_state_boundary_request(int on){(void)on;}
static int glue_state_boundary_ready(void){return 1;}
static uint8_t glue_peek8(uint32_t at){return g_ram[at&65535];}
static void glue_restart_game_fiber(uint32_t pc){(void)pc;}
static void audio_discard_playback(void){}
#ifdef GENESIS_TEST_DENY_TMPFILES
FILE *__wrap_tmpfile(void){++temporary_calls;errno=ENOENT;return NULL;}
#endif
#include "host_state.h"

#define CHECK(expr) do{if(!(expr)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expr);return 1;}}while(0)
static void install(uint32_t value){host_value=cpu_value=machine_value=fm_value=psg_value=value;memset(g_ram,(int)value,sizeof g_ram);}
static int matches(uint32_t value){
    return host_value==value&&cpu_value==value&&machine_value==value&&fm_value==value&&psg_value==value&&g_ram[0]==(uint8_t)value&&g_ram[65535]==(uint8_t)value;
}
static int flip_byte(const char *path,long offset){
    FILE *f=fopen(path,"r+b");if(!f)return 0;
    int ok=!fseek(f,offset,SEEK_SET);int c=ok?fgetc(f):EOF;
    ok=ok&&c!=EOF&&!fseek(f,offset,SEEK_SET)&&fputc(c^1,f)!=EOF;
    return fclose(f)==0&&ok;
}
int main(int argc,char **argv){
    CHECK(argc==2);CHECK(SDL_Init(0)==0);
    const char *path=argv[1];
    install(17);CHECK(host_state_save_now(path));CHECK(host_state_collect(1));
    install(29);CHECK(host_state_load(path));CHECK(matches(17));
    /* A decoder failure after partial CPU/RAM application restores all of B. */
    install(43);fail_machine_once=1;CHECK(!host_state_load(path));CHECK(matches(43));
    CHECK(flip_byte(path,(long)sizeof(HostStateHeader)+3));
    CHECK(!host_state_load(path));CHECK(matches(43));
    CHECK(flip_byte(path,(long)sizeof(HostStateHeader)+3));
    CHECK(flip_byte(path,8));CHECK(!host_state_load(path));CHECK(matches(43));
    CHECK(flip_byte(path,8));CHECK(host_state_load(path));CHECK(matches(17));
    CHECK(!temporary_calls);CHECK(remove(path)==0);SDL_Quit();
    puts("PASS current-build restore, transactional rollback and checksum/build rejection");
#ifdef GENESIS_TEST_DENY_TMPFILES
    puts("PASS restore succeeds with temporary files unavailable");
#endif
    return 0;
}
