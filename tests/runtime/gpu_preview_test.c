#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#define _POSIX_C_SOURCE 200809L
#include "../../runner/local_states.h"
#include "../../runner/rb_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define MKDIR(p) _mkdir(p)
#define PID() _getpid()
#else
#include <sys/stat.h>
#include <unistd.h>
#define MKDIR(p) mkdir(p,0700)
#define PID() getpid()
#endif
#define CHECK(x) do{if(!(x)){fprintf(stderr,"preview %d: %s\n",__LINE__,#x);exit(1);}}while(0)
void original_genesis_local_states_configure(const char *,int,int,int);
void original_genesis_local_states_note_frame(const uint32_t *,int,int,int);
void original_genesis_local_states_slot_saved(int);
void original_genesis_local_states_clear(void);
void original_genesis_local_states_shutdown(void);
int original_genesis_local_states_open_rewind(void);
int original_genesis_local_states_overlay(const uint32_t **,int *,int *);
void original_genesis_local_states_move(int);
int original_genesis_local_states_accept(void);
static uint64_t state_value;
size_t genesis_rb_bound(void){return sizeof state_value;}
size_t genesis_rb_save(void *out,size_t cap){if(cap<sizeof state_value)return 0;memcpy(out,&state_value,sizeof state_value);return sizeof state_value;}
int genesis_rb_load(const void *in,size_t n){if(n!=sizeof state_value)return 0;memcpy(&state_value,in,n);return 1;}
int runner_save_state_file(const char *path){FILE *f=fopen(path,"wb");if(!f)return 0;int ok=fwrite(&state_value,sizeof state_value,1,f)==1;fclose(f);return ok;}
int runner_load_state_file(const char *path){FILE *f=fopen(path,"rb");if(!f)return 0;int ok=fread(&state_value,sizeof state_value,1,f)==1;fclose(f);return ok;}
typedef struct Presented { uint32_t image[512*480];unsigned calls;int alive,fail; } Presented;
static int preview(void *u,uint32_t *out,int w,int h,int sw,int sh)
{
    Presented *p=(Presented *)u;CHECK(p->alive);++p->calls;
    if(p->fail){memset(out,0xa3,(size_t)w*(size_t)h*4);return 0;}
    for(int y=0;y<h;y++)for(int x=0;x<w;x++)out[(size_t)y*(size_t)w+x]=p->image[(size_t)(y*sh/h)*512+x*sw/w];
    return 1;
}
static void env(const char *key,const char *value)
{
#ifdef _WIN32
    CHECK(_putenv_s(key,value)==0);
#else
    CHECK(setenv(key,value,1)==0);
#endif
}
static void pixels(Presented *p,unsigned frame)
{for(unsigned i=0;i<512*480;i++)p->image[i]=(i*2654435761u)^(frame*0x378911u);}
static void compare_saved(const char *a,const char *b)
{
    char pa[700],pb[700];CHECK(snprintf(pa,sizeof pa,"%snative_save_1.bin",a)>0);CHECK(snprintf(pb,sizeof pb,"%snative_save_1.bin",b)>0);
    CHECK(runner_save_state_file(pa)&&runner_save_state_file(pb));
    original_genesis_local_states_slot_saved(1);genesis_local_states_slot_saved(1);
    CHECK(snprintf(pa,sizeof pa,"%snative_save_1.bin.thumb",a)>0);CHECK(snprintf(pb,sizeof pb,"%snative_save_1.bin.thumb",b)>0);
    FILE *fa=fopen(pa,"rb"),*fb=fopen(pb,"rb");CHECK(fa&&fb);size_t n=0;
    for(;;){int x=fgetc(fa),y=fgetc(fb);CHECK(x==y);if(x==EOF)break;++n;}
    CHECK(n==8+128u*90u*4u);fclose(fa);fclose(fb);
}
int main(int argc,char **argv)
{
    CHECK(argc==2);char root[600],a[640],b[640];
    CHECK(snprintf(root,sizeof root,"%s-%lu-%lu",argv[1],(unsigned long)time(NULL),(unsigned long)PID())>0);
    CHECK(MKDIR(root)==0);CHECK(snprintf(a,sizeof a,"%s/A",root)>0);CHECK(snprintf(b,sizeof b,"%s/B",root)>0);
    CHECK(MKDIR(a)==0&&MKDIR(b)==0);strcat(a,"/");strcat(b,"/");
    Presented *p=(Presented *)calloc(1,sizeof *p);CHECK(p);p->alive=1;
    env("GENESISRECOMP_REWIND","0");env("GENESISRECOMP_REWIND_DEPTH","4");env("GENESISRECOMP_REWIND_INTERVAL","2");
    original_genesis_local_states_configure(a,0,4,2);genesis_local_states_configure(b,0,4,2);
    for(unsigned frame=1;frame<=20;frame++) {
        pixels(p,frame);original_genesis_local_states_note_frame(p->image,512,frame&1?256:320,224);
        genesis_local_states_note_frame_source(preview,p,frame&1?256:320,224);
    }
    CHECK(p->calls==0);compare_saved(a,b);CHECK(p->calls==1);compare_saved(a,b);CHECK(p->calls==1);
    env("GENESISRECOMP_REWIND","1");original_genesis_local_states_configure(a,1,4,2);genesis_local_states_configure(b,1,4,2);
    unsigned before=p->calls;
    for(unsigned frame=1;frame<=6;frame++) {
        state_value=frame*11u;pixels(p,frame+20);
        original_genesis_local_states_note_frame(p->image,512,320,frame&1?224:448);
        genesis_local_states_note_frame_source(preview,p,320,frame&1?224:448);
    }
    CHECK(p->calls==before+3);CHECK(original_genesis_local_states_open_rewind()&&genesis_local_states_open_rewind());
    original_genesis_local_states_move(-1);genesis_local_states_move(-1);
    const uint32_t *oa,*ob;int wa,ha,wb,hb;
    CHECK(original_genesis_local_states_overlay(&oa,&wa,&ha)&&genesis_local_states_overlay(&ob,&wb,&hb));
    CHECK(wa==wb&&ha==hb&&!memcmp(oa,ob,(size_t)wa*(size_t)ha*4));
    state_value=999;CHECK(original_genesis_local_states_accept());uint64_t old_state=state_value;
    state_value=777;CHECK(genesis_local_states_accept()&&state_value==old_state&&old_state==44);
    pixels(p,77);original_genesis_local_states_note_frame(p->image,512,256,224);
    genesis_local_states_note_frame_source(preview,p,256,224);
    original_genesis_local_states_clear();genesis_local_states_clear();compare_saved(a,b);
    env("GENESISRECOMP_REWIND","0");original_genesis_local_states_configure(a,0,4,2);genesis_local_states_configure(b,0,4,2);
    p->fail=1;genesis_local_states_note_frame_source(preview,p,256,224);
    original_genesis_local_states_note_frame(NULL,512,256,224);compare_saved(a,b);p->fail=0;
    pixels(p,88);original_genesis_local_states_note_frame(p->image,512,320,224);
    genesis_local_states_note_frame_source(preview,p,320,224);
    original_genesis_local_states_shutdown();genesis_local_states_shutdown();p->alive=0;
    original_genesis_local_states_configure(a,0,4,2);genesis_local_states_configure(b,0,4,2);compare_saved(a,b);
    original_genesis_local_states_shutdown();genesis_local_states_shutdown();free(p);
    printf("lazy preview matched frozen native thumb files, real rewind ring images/restore, clear/reconfigure/shutdown and atomic failure; evidence %s\n",root);return 0;
}
