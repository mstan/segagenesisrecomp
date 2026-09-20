/* First-stage integration harness. Imported stage resources are decoded from
 * verified owner ROMs; stock generated code remains the player simulation. */
#include "trilogy_runtime.h"
#include "trilogy_assets.h"
#include "trilogy_campaign.h"
#include "trilogy_progress.h"
#include "genesis_runtime.h"
#include "video/genesis_machine.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TrStageAssets *stage;
static int selected,active;
static TrProgress progress;
void tr_runtime_sram_loaded(void)
{tr_progress_load(&progress,tr_sram_current());}
static void sync_native_saves(uint32_t pc)
{
    if(!(progress.present&TR_RECORD_CAMP)||(progress.protected_records&TR_RECORD_CAMP))return;
    if(pc==0xC3E4){
        unsigned mode=g_ram[0xF600]&127;
        /* SRAM_Load may repair bad native checksums at boot. An initialized
         * empty native slot is not evidence that the player deleted a chapter. */
        if(!mode)return;
        for(unsigned i=0;i<TR_SLOTS;++i)if(progress.tokens[i]){
            const uint8_t *native=g_ram+0xE6AC+i*10;
            if((native[0]&128)&&mode!=0x4C)continue;
            tr_progress_sync_native(&progress,i,native);
        }
    }else if(pc==0xD624){
        unsigned address=g_cpu.A[1]&65535;
        if(address>=0xE6AC&&address<0xE6FC&&(address-0xE6AC)%10==0)
            tr_progress_delete(&progress,(address-0xE6AC)/10);
    }
    if(progress.dirty&&!tr_progress_store(&progress,tr_sram_current()))
        fprintf(stderr,"[Trilogy SRAM] %s\n",progress.error);
}
static unsigned word(const uint8_t *p){return (unsigned)p[0]<<8|p[1];}
static unsigned ram(unsigned a){return word(g_ram+(a&65535));}
static void put(unsigned a,unsigned n){g_ram[a]=(uint8_t)(n>>8);g_ram[a+1]=(uint8_t)n;}
static void putlong(unsigned a,uint32_t n){put(a,n>>16);put(a+2,n);}
static int hash_matches(const uint8_t *p,size_t n,const char *expected)
{
    uint8_t digest[32];char hex[65];recompui_sha256_compute(p,n,digest);
    for(unsigned i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",digest[i]);return !strcmp(hex,expected);
}
void tr_runtime_settings(const char *settings)
{
    (void)settings;free(stage);stage=NULL;selected=active=0;
    /* Development-only until object and campaign integration is certified.
     * An ordinary launch is unaffected, including native SRAM behavior. */
    const char *id=getenv("SONIC_TRILOGY_STAGE"),*path=getenv("SONIC_TRILOGY_ROM");
    if(!id||!path)return;
    char *end;unsigned wanted=(unsigned)strtoul(id,&end,16);
    if(*end||!tr_stage(wanted)||!tr_stage(wanted)->pack)return;
    size_t size=wanted==0x2000?0x100000:0x80000;
    FILE *f=fopen(path,"rb");if(!f){fprintf(stderr,"[Trilogy] Cannot open donor\n");return;}
    uint8_t *r=malloc(size);if(!r){fclose(f);return;}
    size_t n=fread(r,1,size,f);int extra=fgetc(f),io=ferror(f);fclose(f);
    const char *hash=wanted==0x2000?"193bc4064ce0daf27ea9e908ed246d87ec576cc294833badebb590b6ad8e8f6b":
        "46160baa06362c711c9f1a5017cb7371026444936c8af5e93a78996cf32ff2a6";
    if(n!=size||extra!=EOF||io||!hash_matches(r,size,hash)){
        fprintf(stderr,"[Trilogy] Donor revision/size does not match the verified source\n");free(r);return;}
    TrStageAssets *candidate=malloc(sizeof *candidate);char error[192];
    if(candidate&&tr_stage_decode(wanted,r,size,candidate,error,sizeof error)){
        stage=candidate;selected=1;fprintf(stderr,"[Trilogy] Verified stage %04X decoded\n",wanted);
    }else{free(candidate);fprintf(stderr,"[Trilogy] Stage conversion failed\n");}
    free(r);
}
const char *tr_runtime_state_reason(void)
{return selected?"Trilogy stage experiment has host state; machine snapshots are unavailable":NULL;}
int tr_runtime_netplay_allowed(void){return !selected;}
int tr_runtime_read16(uint32_t a,uint16_t *value)
{
    if(!active)return 0;
    const uint8_t *p=NULL;unsigned offset=0,size=0;
    if(a>=0x400000&&a<0x400C00){p=stage->collision;offset=a-0x400000;size=sizeof stage->collision;}
    else if(a>=0x96000&&a<0x96100){p=stage->angles;offset=a-0x96000;size=sizeof stage->angles;}
    else if(a>=0x96100&&a<0x97100){p=stage->heights;offset=a-0x96100;size=sizeof stage->heights;}
    else if(a>=0x97100&&a<0x98100){p=stage->widths;offset=a-0x97100;size=sizeof stage->widths;}
    if(!p||offset+1>=size)return 0;*value=(uint16_t)word(p+offset);return 1;
}
static uint16_t tile(unsigned x,unsigned y,unsigned plane)
{
    unsigned w=word(stage->layout+plane*2),h=word(stage->layout+4+plane*2);
    if(!w||!h)return 0;
    if(plane){x%=w*128;y%=h*128;}else if(x>=w*128||y>=h*128)return 0;
    unsigned row=word(stage->layout+8+(y/128)*4+plane*2)-0x8000;
    unsigned chunk=stage->layout[row+x/128];
    unsigned block=word(stage->chunks+chunk*128+(y&112)+(x&112)/8);
    unsigned tx=((x>>3)&1)^((block>>10)&1),ty=((y>>3)&1)^((block>>11)&1);
    return (uint16_t)(word(stage->blocks+(block&1023)*8+(ty*2+tx)*2)^((block&0xC00)<<1));
}
static void screen(void)
{
    GVDP *v=&g_machine.vdp;unsigned x=ram(0xEE78),y=ram(0xEE7C),bgx=x/4,bgy=y/8;
    put(0xEE80,x);put(0xEE84,y);put(0xEE8C,bgx);put(0xEE90,bgy);
    put(0xF616,y);put(0xF618,bgy);put(0xF100,0);putlong(0xEF74,0);
    for(unsigned line=0;line<224;++line){put(0xE000+line*4,0u-x);put(0xE002+line*4,0u-bgx);}
    for(unsigned plane=0;plane<2;++plane){
        unsigned px=plane?bgx:x,py=plane?bgy:y,base=plane?0xE000:0xC000;
        for(unsigned yy=py/8;yy<py/8+29;++yy)for(unsigned xx=px/8;xx<px/8+41;++xx){
            unsigned a=base+((yy&31)*64+(xx&63))*2,value=tile(xx*8,yy*8,plane);
            v->vram[a]=(uint8_t)(value>>8);v->vram[a+1]=(uint8_t)value;
        }
    }
}
static void size_start(void)
{
    for(unsigned a=0xEE0C;a<=0xEE20;a+=8){put(a,0);put(a+2,stage->max_x);put(a+4,0);put(a+6,stage->max_y);}
    put(0xEEA8,65535);put(0xEEAA,0xFFF);put(0xEEAC,0xFF0);put(0xEEAE,0x7C);
    /* Checkpoint restoration is connected after the first terrain gate. */
    put(0xB010,stage->start_x);put(0xB014,stage->start_y);
    put(0xEE78,stage->start_x>160?stage->start_x-160:0);
    put(0xEE7C,stage->start_y>96?stage->start_y-96:0);
    put(0xEE28,96);put(0xEE2A,96);
}
int tr_runtime_hook(uint32_t pc)
{
    if(pc==0xC3E4||pc==0xD624){sync_native_saves(pc);return 0;}
    if(!selected)return 0;
    if(pc==0x5FB2 && (g_ram[0xF600]&127)==12 && !ram(0xF660)){
        if(!active&&!hash_matches(g_rom,0x400000,"fba0677fde9f76df93f3e98d6310d8af68b9847bde16e253d73cd4dd8134ed23")){
            selected=0;fprintf(stderr,"[Trilogy] Unsupported base ROM\n");return 0;}
        active=1;put(0xFE10,1);put(0xEE4E,1);
        /* Preserve native SRAM while the terrain harness is running. */
        putlong(0xE660,0);
    }
    if(!active)return 0;
    switch(pc){
    case 0x1BC60:size_start();return 1;
    case 0x7812:memcpy(g_machine.vdp.vram,stage->tiles,stage->tile_bytes);return 1;
    case 0x1C2B0:
        memcpy(g_ram,stage->chunks,sizeof stage->chunks);memcpy(g_ram+0x8000,stage->layout,sizeof stage->layout);
        memcpy(g_ram+0x9000,stage->blocks,sizeof stage->blocks);
        for(unsigned i=0;i<48;++i)put(0xFCA0+i*2,stage->palette[i]);return 1;
    case 0x76A6:putlong(0xF7B4,0x400000);putlong(0xF7B8,0x400600);putlong(0xF796,0x400000);return 1;
    case 0x7892:g_ram[0xF730]=0;return 1;
    case 0x4E35C:g_ram[0xF664]=0;put(0xEEAA,0xFFF);put(0xEEAC,0xFF0);put(0xEEAE,0x7C);screen();return 1;
    case 0x4E408:screen();return 1;
    case 0x3BB8:return ram(0xEE50)==0; /* retain the native fade-in, suppress AIZ cycling */
    case 0x1C38A:case 0x28C80:case 0x27758:case 0x4F33C:
    case 0x1B690:case 0x1B7F2:case 0xE8AA:case 0x85FDE:return 1;
    case 0xEFF0:{
        unsigned x=(uint16_t)g_cpu.D[3],y=(uint16_t)g_cpu.D[2],w=word(stage->layout),h=word(stage->layout+4);
        unsigned a=0;
        if(x<w*128&&y<h*128){
            unsigned object=g_cpu.A[0]&65535;
            const uint8_t *layout=object>=0xB000 && object<0xCFCC && g_ram[object+0x46]==14?stage->alternate_layout:stage->layout;
            unsigned row=word(layout+8+(y/128)*4)-0x8000;
            a=layout[row+x/128]*128+(y&112)+(x&112)/8;
        }
        g_cpu.A[1]=0xFF0000+a;return 1;}
    default:return 0;
    }
}
