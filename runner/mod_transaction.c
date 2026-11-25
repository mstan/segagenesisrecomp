#include "mod_transaction.h"
#include "genesis_runtime.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static char game_id[128],image_hash[65],error[256];
static size_t cartridge_size;
static void hex(const uint8_t hash[32],char out[65]) {
 static const char digits[]="0123456789abcdef";
 for(unsigned i=0;i<32;++i){out[i*2]=digits[hash[i]>>4];out[i*2+1]=digits[hash[i]&15];}out[64]=0;
}
int genesis_mod_initialize(const char *game,const void *original,size_t size) {
 uint8_t hash[32];if(!game||!*game||strlen(game)>=sizeof game_id||!original||!size||!rmod_sha256_bytes(original,size,hash))return 0;
 strcpy(game_id,game);hex(hash,image_hash);cartridge_size=size;error[0]=0;return 1;
}
static uint8_t *region(const char *space,uint64_t a,size_t n) {
 if(!space)return NULL;
 if(!strcmp(space,"ram"))return a<=sizeof g_ram&&n<=sizeof g_ram-a?g_ram+(size_t)a:NULL;
 if(!strcmp(space,"cartridge"))return a<=cartridge_size&&n<=cartridge_size-a&&a<=sizeof g_rom&&n<=sizeof g_rom-a?g_rom+(size_t)a:NULL;
 return NULL;
}
static int begin(void *u){(void)u;return 1;}
static int read_bytes(void *u,const char *space,uint64_t a,uint8_t*out,size_t n){(void)u;uint8_t*p=region(space,a,n);if(!p)return 0;memcpy(out,p,n);return 1;}
static int write_bytes(void *u,const char *space,uint64_t a,const uint8_t*in,size_t n){(void)u;uint8_t*p=region(space,a,n);if(!p)return 0;memcpy(p,in,n);return 1;}
static void finish(void*u,int committed){(void)u;(void)committed;}
int genesis_mod_apply(const char *plan,const rmod_patch *patches,size_t count) {
 uint8_t digest[32],identity[32];char plan_hash[65];
 if(!game_id[0]||!plan||!rmod_sha256_bytes(plan,strlen(plan),digest))return 0;
 hex(digest,plan_hash);rmod_context *ctx=rmod_create();if(!ctx)return 0;
 rmod_engine engine={NULL,game_id,image_hash,begin,read_bytes,write_bytes,NULL,finish};
 int result=rmod_bind_plan(ctx,game_id,image_hash,plan_hash)?rmod_apply(ctx,patches,count,NULL,0,&engine,identity):0;
 snprintf(error,sizeof error,"%s",rmod_error(ctx));rmod_destroy(ctx);
 if(result<0){fputs("Mod transaction could not restore guest memory\n",stderr);abort();}
 return result;
}
const char *genesis_mod_error(void){return error;}
