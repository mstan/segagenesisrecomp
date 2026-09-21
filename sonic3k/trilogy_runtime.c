/* First-stage integration harness. Imported stage resources are decoded from
 * verified owner ROMs; stock generated code remains the player simulation. */
#include "trilogy_runtime.h"
#include "trilogy_assets.h"
#include "trilogy_campaign.h"
#include "trilogy_progress.h"
#include "trilogy_objects.h"
#include "trilogy_music.h"
#include "sonic3_video.h"
#include "genesis_runtime.h"
#include "video/genesis_machine.h"
#include "video/genesis_dac.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TrStageAssets *stage;
static int selected,active;
static TrStageAssets *imports[4];
static unsigned available,forced_stage,pending_stage,resume_checkpoint;
static int session_started,session_slot=-1;
static unsigned menu_stage[TR_SLOTS];
static uint8_t menu_tiles[4][0x8C0],menu_text_map[256];
static uint16_t menu_palette[4][16];
static unsigned menu_text_object;
static uint8_t title_tiles[0x1000],title_map[128],title_three[512];
static unsigned title_bytes;
static void prepare_previews(void);
static TrProgress progress;
/* SPCL v2 is optional, independent of chapter availability. Eight entries:
 * slot token:u32 and four act entrance masks:u32, all big endian. */
static uint8_t special_records[TR_SLOTS*20];
static uint32_t special_no_save[4];
static int special_read_only;
static unsigned special_return,special_restore_objects,special_ring_guest;
static unsigned special_goal_entered,special_after_goal;
static unsigned ram(unsigned a);
static void put(unsigned a,unsigned n);
static void putlong(unsigned a,uint32_t n);
static void save_import(void);
static void store_progress(void)
{
    if(progress.dirty&&!tr_progress_store(&progress,tr_sram_current()))
        fprintf(stderr,"[Trilogy SRAM] %s\n",progress.error);
}
void tr_runtime_sram_loaded(void)
{
    TrSram *s=tr_sram_current();tr_progress_load(&progress,s);
    unsigned version;size_t size;const uint8_t *p=tr_sram_get(s,"SPCL",&version,&size);
    memset(special_records,0,sizeof special_records);
    special_read_only=s->read_only||!s->extensions_valid||(p&&((version!=1&&version!=2)||size!=sizeof special_records));
    /* v1 used synthetic start/checkpoint rings. Their bits do not identify
     * the restored donor entrances; migrate lazily to an empty v2 mask. */
    if(p&&!special_read_only&&version==2)memcpy(special_records,p,size);
}
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
    store_progress();
}
static unsigned word(const uint8_t *p){return (unsigned)p[0]<<8|p[1];}
static unsigned ram(unsigned a){return word(g_ram+(a&65535));}
static void put(unsigned a,unsigned n){g_ram[a]=(uint8_t)(n>>8);g_ram[a+1]=(uint8_t)n;}
static void putlong(unsigned a,uint32_t n){put(a,n>>16);put(a+2,n);}
static uint32_t getlong(const uint8_t *p){return (uint32_t)word(p)*65536+word(p+2);}
static void savelong(uint8_t *p,uint32_t n){p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n;}
static unsigned special_index(void){return stage->id==0x2000?3:stage->id-0x1000;}
static uint32_t special_mask(void)
{
    if(session_slot<0)return special_no_save[special_index()];
    const uint8_t *p=special_records+session_slot*20;
    return getlong(p)==progress.tokens[session_slot]?getlong(p+4+special_index()*4):0;
}
static void special_used(unsigned number)
{
    if(number>=32)return;uint32_t mask=special_mask()|(1u<<number);
    if(session_slot<0)special_no_save[special_index()]=mask;
    else if(!special_read_only){
        uint8_t *p=special_records+session_slot*20;
        if(getlong(p)!=progress.tokens[session_slot]){memset(p,0,20);savelong(p,progress.tokens[session_slot]);}
        savelong(p+4+special_index()*4,mask);
        if(!tr_sram_set(tr_sram_current(),"SPCL",2,special_records,sizeof special_records))
            fprintf(stderr,"[Trilogy special] Cannot save ring collection: %s\n",tr_sram_current()->error);
    }
    putlong(0xFF92,mask);
}
static void special_spawn(void)
{
    if(stage->id>=0x2000||(session_slot>=0&&special_read_only)||tr_objects_results_started()||special_goal_entered)return;
    if(special_ring_guest){unsigned a=special_ring_guest;uint32_t c=getlong(g_ram+a);
        if((c>=0x6166A&&c<0x61990)||(c==0x85AD2&&getlong(g_ram+a+0x34)==0x61682))return;
        special_ring_guest=0;
    }
    /* Use S1's actual object $4B placements: GHZ1/2 goals only, no boss-act
     * ring. The shared S3 emerald set has seven entries instead of S1's six. */
    if(ram(0xFE20)<50||g_ram[0xFFB0]>=7||special_mask()&1)return;
    for(unsigned i=0;i<stage->object_count;++i){
        const TrPlacement *p=stage->objects+i;if(p->id!=0x4B)continue;int x=p->x,y=p->y;
        if(x<(int)ram(0xEE78)-64||x>(int)ram(0xEE78)+384||y<(int)ram(0xEE7C)-32||y>(int)ram(0xEE7C)+256)continue;
        for(unsigned a=0xB0DE;a<0xCAE2;a+=0x4A)if(!getlong(g_ram+a)){
            memset(g_ram+a,0,0x4A);putlong(a,0x6166A);put(a+0x10,x);put(a+0x14,y);g_ram[a+0x2C]=0;
            special_ring_guest=a;return;
        }
    }
}
void tr_runtime_checkpoint_special(unsigned guest,unsigned number)
{
    if(!active||!stage||stage->id!=0x2000||g_ram[0xF600]!=12||special_return||g_ram[0xB005]>=6)return;
    M68KState saved=g_cpu;
    special_used(number);save_import();tr_objects_special_save();special_return=stage->id;
    /* S2's stars switch straight into the special stage. Use the checkpoint
     * position for S3's return bookkeeping, and native Blue Spheres itself. */
    g_cpu.A[0]=0xFF0000|guest;recomp_call_addr(0x2D1CC);
    put(0xFE4C,ram(0xFE2E));put(0xFE4E,ram(0xFE30)-20);
    recomp_call_addr(0x1BB7E);g_ram[0xFFBB]=0;g_ram[0xFE48]=1;g_ram[0xFF97]=1;g_ram[0xF600]=0x34;
    fprintf(stderr,"[Trilogy special] Checkpoint %u -> Blue Spheres from 2000\n",number);g_cpu=saved;
}
static void special_ring_display(void)
{
    unsigned a=g_cpu.A[0]&65535;M68KState saved=g_cpu;
    int x=(int)ram(a+0x10)-(int)ram(0xEE78),y=(int)ram(a+0x14)-(int)ram(0xEE7C);
    if((g_ram[a+0x38]&32)||x < -256||x>576||y < -128||y>384){
        /* Native cleanup also edits AIZ's palette. Restore only its shared
         * explosion graphics here, keeping the donor's terrain colors. */
        tr_nemesis(g_rom+0x19200A,0x400000-0x19200A,g_machine.vdp.vram+0x5A0*32,0x40*32);
        recomp_call_addr(0x851CC);
    }else{
        if(g_ram[a+4]&128){g_cpu.A[2]=0x619B2;recomp_call_addr(0x85022);}
        g_cpu.A[0]=saved.A[0];recomp_call_addr(0x1ABC6);
    }
    g_cpu=saved;
}
static int hash_matches(const uint8_t *p,size_t n,const char *expected)
{
    uint8_t digest[32];char hex[65];recompui_sha256_compute(p,n,digest);
    for(unsigned i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",digest[i]);return !strcmp(hex,expected);
}
static int load_donor(const char *path,unsigned pack)
{
    if(!path||!*path)return 0;
    size_t size=pack==TR_PACK_S2?0x100000:0x80000;
    FILE *f=fopen(path,"rb");if(!f){fprintf(stderr,"[Trilogy] Cannot open donor\n");return 0;}
    uint8_t *r=malloc(size);if(!r){fclose(f);return 0;}
    size_t n=fread(r,1,size,f);int extra=fgetc(f),io=ferror(f);fclose(f);
    const char *hash=pack==TR_PACK_S2?"193bc4064ce0daf27ea9e908ed246d87ec576cc294833badebb590b6ad8e8f6b":
        "46160baa06362c711c9f1a5017cb7371026444936c8af5e93a78996cf32ff2a6";
    if(n!=size||extra!=EOF||io||!hash_matches(r,size,hash)){
        fprintf(stderr,"[Trilogy] Donor revision/size does not match the verified source\n");free(r);return 0;}
    unsigned first=pack==TR_PACK_S1?0:3,end=pack==TR_PACK_S1?3:4;int ok=1;
    for(unsigned i=first;i<end;++i){
        TrStageAssets *candidate=malloc(sizeof *candidate);char error[192];unsigned id=i==3?0x2000:0x1000+i;
        if(candidate&&tr_stage_decode(id,r,size,candidate,error,sizeof error))imports[i]=candidate;
        else{free(candidate);ok=0;break;}
    }
    if(ok)ok=tr_audio_load(pack,r,size);
    free(r);
    if(!ok){for(unsigned i=first;i<end;++i){free(imports[i]);imports[i]=NULL;}return 0;}
    available|=pack;fprintf(stderr,"[Trilogy] Verified Sonic %u chapter decoded\n",pack==1?1:2);return 1;
}
/* Private donor paths stay in local settings.ini, never in a patch or save. */
void tr_runtime_settings(const char *settings)
{
    tr_audio_reset();
    special_return=special_restore_objects=special_ring_guest=0;memset(special_no_save,0,sizeof special_no_save);
    special_goal_entered=special_after_goal=0;
    tr_objects_reset(NULL);for(unsigned i=0;i<4;++i){free(imports[i]);imports[i]=NULL;}
    stage=NULL;selected=active=session_started=0;available=forced_stage=pending_stage=resume_checkpoint=0;session_slot=-1;
    char paths[2][1024]={{0}},line[1200];int enabled=0,section=0;
    FILE *f=settings?fopen(settings,"r"):NULL;
    if(f){while(fgets(line,sizeof line,f)){
        char *p=line;while(*p==' '||*p=='\t')++p;
        if(*p=='['){section=!strncmp(p,"[trilogy]",9);continue;}
        if(!section)continue;char *equal=strchr(p,'=');if(!equal)continue;*equal++=0;
        char *last=p+strlen(p);while(last>p&&(last[-1]==' '||last[-1]=='\t'))*--last=0;
        while(*equal==' '||*equal=='\t')++equal;equal[strcspn(equal,"\r\n")]=0;
        if(!strcmp(p,"enabled"))enabled=atoi(equal)!=0;
        else if(!strcmp(p,"sonic1_rom"))snprintf(paths[0],sizeof paths[0],"%s",equal);
        else if(!strcmp(p,"sonic2_rom"))snprintf(paths[1],sizeof paths[1],"%s",equal);
    }fclose(f);}
    const char *s1=getenv("SONIC_TRILOGY_S1_ROM"),*s2=getenv("SONIC_TRILOGY_S2_ROM");
    if(s1||s2)enabled=1;
    const char *id=getenv("SONIC_TRILOGY_STAGE"),*path=getenv("SONIC_TRILOGY_ROM");
    if(id&&path){char *end;unsigned wanted=(unsigned)strtoul(id,&end,16);
        if(!*end&&tr_stage(wanted)&&tr_stage(wanted)->pack){
            forced_stage=wanted;enabled=1;if(wanted==0x2000)s2=path;else s1=path;
        }
    }
    if(!enabled)return;
    load_donor(s1?s1:paths[0],TR_PACK_S1);load_donor(s2?s2:paths[1],TR_PACK_S2);
    prepare_previews();
    selected=1;
}
const char *tr_runtime_state_reason(void)
{return NULL;}
static void runtime_state(TrStateIO *io)
{
    unsigned header[7]={0x54524932,1,sizeof(TrProgress),available,stage?stage->id:0,progress.protected_records,(unsigned)special_read_only};
    if(io->mode&&io->data&&io->size>=sizeof header)memcpy(header,io->data,sizeof header);
    if(header[0]!=0x54524932||header[1]!=1||header[2]!=sizeof(TrProgress)||header[3]!=available||!selected||header[5]!=progress.protected_records||header[6]!=(unsigned)special_read_only)io->ok=0;
    TrStageAssets *restored=NULL;for(unsigned i=0;i<4;++i)if(imports[i]&&imports[i]->id==header[4])restored=imports[i];
    if(header[4]&&!restored)io->ok=0;
    TR_STATE(io,header);if(io->mode==2)stage=restored;
    TR_STATE(io,active);TR_STATE(io,forced_stage);TR_STATE(io,pending_stage);TR_STATE(io,resume_checkpoint);
    TR_STATE(io,session_started);TR_STATE(io,session_slot);TR_STATE(io,menu_stage);TR_STATE(io,menu_text_object);
    TR_STATE(io,menu_text_map);TR_STATE(io,title_tiles);TR_STATE(io,title_map);TR_STATE(io,title_three);TR_STATE(io,title_bytes);
    TR_STATE(io,progress);TR_STATE(io,special_records);TR_STATE(io,special_no_save);TR_STATE(io,special_read_only);
    TR_STATE(io,special_return);TR_STATE(io,special_restore_objects);TR_STATE(io,special_ring_guest);
    TR_STATE(io,special_goal_entered);TR_STATE(io,special_after_goal);
    tr_objects_state(io,imports);tr_audio_state(io);s3_video_state(io);
}
size_t tr_runtime_state_size(void)
{if(!selected)return 0;TrStateIO io={0};runtime_state(&io);return io.pos;}
int tr_runtime_state_save(void *data,size_t size)
{
    unsigned mode=g_ram[0xF600];
    if((mode!=12&&mode!=0x34&&mode!=0x48)||size!=tr_runtime_state_size())return 0;
    TrStateIO io={(uint8_t *)data,size,0,0,1};runtime_state(&io);return io.ok&&io.pos==size;
}
int tr_runtime_state_load(const void *data,size_t size,int apply)
{
    if(!selected||size!=tr_runtime_state_size())return 0;
    TrStateIO io={(uint8_t *)data,size,0,1,1};runtime_state(&io);
    if(!io.ok||io.pos!=size)return 0;
    if(apply){io.pos=0;io.mode=2;runtime_state(&io);
        /* A quickload rewinds native SRAM and its understood extensions as
         * one save image. Keep the current file path, writer baseline and
         * unrelated/unknown extension records. */
        progress.dirty|=progress.present&~progress.protected_records;store_progress();
        if(!special_read_only)tr_sram_set(tr_sram_current(),"SPCL",2,special_records,sizeof special_records);
    }
    return io.ok;
}
int tr_runtime_netplay_allowed(void){return !selected;}
unsigned tr_runtime_stage_id(void){return active&&stage?stage->id:0;}
int tr_runtime_scene_required(void){return active&&stage&&(g_ram[0xF600]&127)==12;}
const uint32_t *tr_runtime_sprite_palette(uint32_t mapping)
{
    if(!tr_runtime_scene_required())return NULL;
    /* Terrain keeps the exact donor CRAM. Its sprites keep their donor
     * palette too; S3 players/common objects have a separate presentation
     * palette instead of forcing either game's pixels into the other's. */
    static uint32_t donor[64],native[64];
    static uint16_t previous[64];static const TrStageAssets *previous_stage;
    const uint16_t *cram=g_machine.vdp.cram;
    if(previous_stage!=stage||memcmp(previous,cram,sizeof previous)){
        previous_stage=stage;memcpy(previous,cram,sizeof previous);
        for(unsigned i=0;i<64;++i){
            unsigned d=i<16?stage->sprite_palette[i]:cram[i];
            unsigned n=i<16?cram[i]:word(g_rom+0xA8B7C+(i-16)*2);
            /* Native fade-in/out uses palette zero's white as its ceiling. */
            unsigned white=cram[1],df=0,nf=0;
            for(unsigned shift=0;shift<=8;shift+=4){unsigned limit=(white>>shift)&14;
                unsigned dc=(d>>shift)&14,nc=(n>>shift)&14;
                df|=(dc<limit?dc:limit)<<shift;nf|=(nc<limit?nc:limit)<<shift;}
            donor[i]=genesis_dac_cram_to_argb((uint16_t)df,GENESIS_DAC_NORMAL);
            native[i]=genesis_dac_cram_to_argb((uint16_t)nf,GENESIS_DAC_NORMAL);
        }
    }
    return mapping>=0x410000&&mapping<0x480000?donor:native;
}
int tr_runtime_read16(uint32_t a,uint16_t *value)
{
    if(selected&&(g_ram[0xF600]&127)==0x4C&&a>=0x480000&&a+1<0x480000+sizeof menu_text_map){*value=(uint16_t)word(menu_text_map+a-0x480000);return 1;}
    if(!active||(g_ram[0xF600]&127)!=12)return 0;
    if(a>=0x480200&&a+1<0x480200+sizeof title_map){*value=(uint16_t)word(title_map+a-0x480200);return 1;}
    if(tr_objects_read16(a,value))return 1;
    const uint8_t *p=NULL;unsigned offset=0,size=0;
    if(a>=0x400000&&a<0x400C00){p=stage->collision;offset=a-0x400000;size=sizeof stage->collision;}
    else if(a>=0x96000&&a<0x96100){p=stage->angles;offset=a-0x96000;size=sizeof stage->angles;}
    else if(a>=0x96100&&a<0x97100){p=stage->heights;offset=a-0x96100;size=sizeof stage->heights;}
    else if(a>=0x97100&&a<0x98100){p=stage->widths;offset=a-0x97100;size=sizeof stage->widths;}
    if(!p||offset+1>=size)return 0;*value=(uint16_t)word(p+offset);return 1;
}
static uint16_t asset_tile(const TrStageAssets *data,unsigned x,unsigned y,unsigned plane)
{
    unsigned w=word(data->layout+plane*2),h=word(data->layout+4+plane*2);
    if(!w||!h)return 0;
    if(plane){x%=w*128;y%=h*128;}else if(x>=w*128||y>=h*128)return 0;
    unsigned row=word(data->layout+8+(y/128)*4+plane*2)-0x8000;
    unsigned chunk=data->layout[row+x/128];
    unsigned block=word(data->chunks+chunk*128+(y&112)+(x&112)/8);
    unsigned tx=((x>>3)&1)^((block>>10)&1),ty=((y>>3)&1)^((block>>11)&1);
    return (uint16_t)(word(data->blocks+(block&1023)*8+(ty*2+tx)*2)^((block&0xC00)<<1));
}
static unsigned preview_pixel(const TrStageAssets *data,unsigned x,unsigned y,unsigned plane)
{
    unsigned t=asset_tile(data,x,y,plane),tx=(x&7)^((t&0x800)?7:0),ty=(y&7)^((t&0x1000)?7:0);
    unsigned offset=(t&0x7FF)*32+ty*4+tx/2;if(offset>=data->tile_bytes)return 0;
    unsigned pixel=(data->tiles[offset]>>((tx&1)?0:4))&15;
    return pixel?((t>>13)&3)*16+pixel:0;
}
static unsigned color_distance(unsigned a,unsigned b)
{
    int r=(int)(a&14)-(int)(b&14),g=(int)((a>>4)&14)-(int)((b>>4)&14),blue=(int)((a>>8)&14)-(int)((b>>8)&14);
    return (unsigned)(r*r+g*g+blue*blue);
}
static void prepare_previews(void)
{
    for(unsigned index=0;index<4;++index){TrStageAssets *data=imports[index];if(!data)continue;
        uint16_t pixels[80*56];unsigned frequency[512]={0};
        unsigned top=data->start_y>144?data->start_y-144:0;
        for(unsigned y=0;y<56;++y)for(unsigned x=0;x<80;++x){
            unsigned worldx=x*3,worldy=top+y*3,p=preview_pixel(data,worldx,worldy,0);
            if(!p)p=preview_pixel(data,worldx,data->id==0x2000?y*3:38-top/32+y*3,1);
            unsigned c=p>=16?data->palette[p-16]:0;
            pixels[y*80+x]=(uint16_t)c;
            unsigned compact=((c>>1)&7)|((c>>2)&56)|((c>>3)&448);++frequency[compact];
        }
        menu_palette[index][0]=0;frequency[0]=0;
        for(unsigned p=1;p<16;++p){unsigned best=0;
            for(unsigned c=1;c<512;++c)if(frequency[c]>frequency[best])best=c;
            menu_palette[index][p]=(uint16_t)(((best&7)<<1)|((best&56)<<2)|((best&448)<<3));frequency[best]=0;
        }
        /* The stock save card is six sprite pieces: 4x4,4x4,2x4,
         * followed by 4x3,4x3,2x3 tiles, each in VDP column order. */
        for(unsigned y=0;y<56;++y)for(unsigned x=0;x<80;++x){
            unsigned best=0,distance=~0u;
            for(unsigned p=0;p<16;++p){unsigned d=color_distance(pixels[y*80+x],menu_palette[index][p]);if(d<distance){best=p;distance=d;}}
            unsigned tx=x/8,ty=y/8,height=ty<4?4:3;
            unsigned base=ty<4?0:40;if(tx>=8)base+=height*8;else if(tx>=4)base+=height*4;
            unsigned t=base+(tx%4)*height+(ty<4?ty:ty-4),offset=t*32+(y&7)*4+(x&7)/2;
            if(x&1)menu_tiles[index][offset]=(uint8_t)((menu_tiles[index][offset]&0xF0)|best);
            else menu_tiles[index][offset]=(uint8_t)(best<<4);
        }
    }
}
static void prepare_title(void)
{
    uint8_t source[3][4096],font[4096];
    static const unsigned addresses[]={0x39BDC8,0x39BEDA,0x39C02C};
    for(unsigned i=0;i<3;++i)if(!tr_kosinski(g_rom+addresses[i]+2,0x400000-addresses[i]-2,source[i],sizeof source[i]))return;
    static const struct {char letter;unsigned source,tile,width;} glyphs[]={
        {'G',0,0x559,2},{'R',1,0x562,2},{'H',1,0x559,2},{'I',0,0x55F,1},
        {'L',0,0x562,1},{'M',2,0x568,3},{'A',2,0x54D,2},{'D',2,0x559,2}};
    unsigned tiles[26]={0},width[26]={0};title_bytes=0;
    for(unsigned i=0;i<sizeof glyphs/sizeof *glyphs;++i){unsigned n=glyphs[i].width*3*32,c=glyphs[i].letter-'A';
        tiles[c]=0x854D+title_bytes/32;width[c]=glyphs[i].width*8;
        memcpy(title_tiles+title_bytes,source[glyphs[i].source]+(glyphs[i].tile-0x54D)*32,n);title_bytes+=n;
    }
    tiles['E'-'A']=0x851C;width['E'-'A']=16;tiles['N'-'A']=0x8522;width['N'-'A']=16;
    const char *name=tr_stage(stage->id)->name;unsigned total=0;
    for(unsigned i=0;name[i];++i)total+=name[i]==' '?16:width[name[i]-'A'];
    /* Native title elements use multi-draw: main frame zero means hidden.
     * Frame one keeps its original bounds/culling and exit choreography. */
    memset(title_map,0,sizeof title_map);title_map[1]=title_map[3]=4;int x=128-(int)total;unsigned count=0;
    for(unsigned i=0;name[i];++i){if(name[i]==' '){x+=16;continue;}
        unsigned c=name[i]-'A',at=6+count++*6,t=tiles[c];
        title_map[at+1]=(uint8_t)(((width[c]/8-1)<<2)|2);
        title_map[at+2]=(uint8_t)(t>>8);title_map[at+3]=(uint8_t)t;
        title_map[at+4]=(uint8_t)(x>>8);title_map[at+5]=(uint8_t)x;x+=(int)width[c];
    }
    title_map[5]=(uint8_t)count;
    memset(title_three,0,sizeof title_three);
    if(tr_nemesis(g_rom+0xCA5E0,0x400000-0xCA5E0,font,sizeof font)){
        /* S3 has title-card digits 1 and 2. Its own menu numeral supplies 3. */
        for(unsigned y=0;y<32;++y)for(unsigned x=4;x<28;++x){
            unsigned px=(x-4)/3,py=y/4,pixel=(font[3*32+py*4+px/2]>>((px&1)?0:4))&15;
            unsigned offset=((x/8)*4+y/8)*32+(y&7)*4+(x&7)/2;
            if(pixel)title_three[offset]|=(uint8_t)((x&1)?15:0xF0);
        }
    }
}
static void title_art(void)
{
    memcpy(g_machine.vdp.vram+0x54D*32,title_tiles,title_bytes);
    if(stage->id==0x1002)memcpy(g_machine.vdp.vram+0x53D*32,title_three,sizeof title_three);
}
static void title_name(void)
{
    unsigned a=g_cpu.A[0]&65535;M68KState saved=g_cpu;
    title_art();
    putlong(a,0x2D95C);putlong(a+0xC,0x480200);put(a+0xA,0);g_ram[a+0x22]=1;
    recomp_call_addr(0x2D95C);g_cpu=saved;
}
static int bg_scroll(unsigned id,unsigned x,unsigned line,unsigned bgy,unsigned frame)
{
    if(id!=0x2000){
        unsigned y=line+bgy;
        if(y<112)return (int)x*3/8;
        if(y<152)return (int)x/2;
        /* Verified Sonic 1 REV00 water-perspective fixed-point formula. */
        int hill=(int)x/2,step=((int)x-512-hill)*256/104;
        return hill+(int)((int64_t)step*(y-152)>>8);
    }
    static const int ripple[]={1,2,1,3,1,2,2,1,2,3,1,2,1,2,0,0,2,0,3,2,2,3,2,2,1,3,0,0,1,0,1,3};
    if(line<22)return 0;
    int slow=((int)x+63)/64;
    if(line<80)return slow;
    if(line<101)return slow-ripple[(line-80-(frame/8+1))&31];
    if(line<112)return 0;
    if(line<128)return ((int)x+15)/16;
    if(line<144){int n=-((int)x+15)/16;return -(n+(n>>1));}
    int n=-(int)x,step=((n>>1)-(n>>3))*256/48,index=(int)line-144;
    if(index>=33)index=33+(index-33)/3*3;else if(index>=15)index=15+(index-15)/2*2;
    return -((n>>3)+(int)((int64_t)step*index>>8));
}
static void screen(void)
{
    GVDP *v=&g_machine.vdp;unsigned x=ram(0xEE78),y=ram(0xEE7C),frame=ram(0xFE04);
    unsigned bgy=stage->id==0x2000?0:38-((y&2047)>>5),bgx=x*3/8;
    if(bgy>38)bgy=0;
    tr_stage_animate(stage,frame,v->vram);
    put(0xEE80,x);put(0xEE84,y);put(0xEE8C,bgx);put(0xEE90,bgy);
    put(0xF616,y);put(0xF618,bgy);put(0xF100,0);putlong(0xEF74,0);
    for(unsigned line=0;line<224;++line){put(0xE000+line*4,0u-x);put(0xE002+line*4,-bg_scroll(stage->id,x,line,bgy,frame));}
    for(unsigned plane=0;plane<2;++plane){
        unsigned px=plane?bgx:x,py=plane?bgy:y,base=plane?0xE000:0xC000;
        for(unsigned yy=py/8;yy<py/8+29;++yy){
            int start=(int)px/8,count=41;
            if(plane){unsigned line=yy*8>py?yy*8-py:0;start=bg_scroll(stage->id,x,line,bgy,frame)/8-8;count=64;}
            for(int xx=start;xx<start+count;++xx){
            unsigned a=base+((yy&31)*64+(xx&63))*2,value=asset_tile(stage,xx*8,yy*8,plane);
            v->vram[a]=(uint8_t)(value>>8);v->vram[a+1]=(uint8_t)value;
        }}
    }
}
static void palette_cycle(void)
{
    unsigned frame=ram(0xFE04),cycle=(frame/(stage->id==0x2000?8:6))&3;
    if(stage->id==0x2000){
        put(0xFC26,stage->water_palette[cycle*4]);put(0xFC28,stage->water_palette[cycle*4+1]);
        put(0xFC3C,stage->water_palette[cycle*4+2]);put(0xFC3E,stage->water_palette[cycle*4+3]);
    }else for(unsigned i=0;i<4;++i)put(0xFC50+i*2,stage->water_palette[cycle*4+i]);
}
static void size_start(void)
{
    g_ram[0xEE08]=g_ram[0xEE0A]=g_ram[0xEE0B]=g_ram[0xEE30]=g_ram[0xEE33]=g_ram[0xEE39]=0;
    put(0xF660,0);put(0xF662,0);
    for(unsigned a=0xEE0C;a<=0xEE20;a+=8){put(a,0);put(a+2,stage->max_x);put(a+4,0);put(a+6,stage->max_y);}
    put(0xEEA8,65535);put(0xEEAA,0xFFF);put(0xEEAC,0xFF0);put(0xEEAE,0x7C);
    put(0xB010,stage->start_x);put(0xB014,stage->start_y);
    if(resume_checkpoint){
        for(unsigned i=0;i<stage->object_count;++i){const TrPlacement *p=stage->objects+i;
            if(p->id==0x79&&(p->subtype&127)==resume_checkpoint){put(0xB010,p->x);put(0xB014,p->y-20);break;}}
        /* The first resumed attempt starts at the donor post. Native post
         * bookkeeping takes over on touch, including a later death/retry. */
        resume_checkpoint=0;g_ram[0xFE2A]=0;
    }else if(g_ram[0xFE2A]){M68KState saved=g_cpu;recomp_call_addr(0x2D234);g_cpu=saved;}
    unsigned start_x=ram(0xB010),start_y=ram(0xB014);
    put(0xEE78,start_x>160?start_x-160:0);
    unsigned camera_y=start_y>96?start_y-96:0;
    put(0xEE7C,camera_y>stage->max_y?stage->max_y:camera_y);
    put(0xEE2C,96);put(0xEE2E,96);
}
static void resize(void)
{
    unsigned x=ram(0xEE78),y=ram(0xEE7C),target=stage->max_y;
    if(stage->id==0x1000)target=x<0x1780?0x300:0x400;
    else if(stage->id==0x1001)target=x<0xED0?0x300:x<0x1600?0x200:x<0x1D60?0x400:0x300;
    else if(stage->id==0x1002){
        if(g_ram[0xEE33]&&x<0x960)g_ram[0xEE33]=0;
        if(!g_ram[0xEE33]){
            target=x<0x380?0x300:0x310;
            if(x>=0x960){
                if(y<0x280||x>=0x1700){target=0x300;g_ram[0xEE33]=2;}
                else if(x<0x1380){target=0x4C0;put(0xEE1A,target);}else target=0x400;
            }
        }
    }
    put(0xEE12,target);
    int current=(int)ram(0xEE1A),delta=(int)target-current;
    if(delta){
        if(delta<0){if(y>target)current=(int)(y&~1u);current-=2;}
        else{current+=(y+8>=(unsigned)current&&(g_ram[0xB02A]&2))?8:2;}
        put(0xEE1A,current);g_ram[0xEE32]=1;
    }
}
static int native_slot(void)
{
    unsigned address=ram(0xE662);
    return address>=0xE6AC&&address<0xE6FC&&(address-0xE6AC)%10==0?(int)(address-0xE6AC)/10:-1;
}
static void begin_level(void)
{
    if(ram(0xFFD0)){active=0;tr_objects_reset(NULL);return;} /* native attract/demo */
    if(!session_started){
        if(!hash_matches(g_rom,0x400000,"fba0677fde9f76df93f3e98d6310d8af68b9847bde16e253d73cd4dd8134ed23")){
            selected=active=0;fprintf(stderr,"[Trilogy] Unsupported base ROM\n");return;}
        session_slot=forced_stage?-1:native_slot();session_started=1;
        if(forced_stage)pending_stage=forced_stage;
        else if(session_slot>=0&&progress.tokens[session_slot]){
            TrResume resume;
            if(tr_progress_resolve(&progress,(unsigned)session_slot,available,&resume)){
                pending_stage=resume.stage;resume_checkpoint=resume.checkpoint;
                if(resume.unavailable_packs)fprintf(stderr,"[Trilogy] Chapter unavailable; retaining its progress and resuming %04X\n",resume.stage);
            }
        }else if(session_slot<0)pending_stage=tr_first_stage(available);
    }
    if(special_after_goal&&pending_stage){
        special_after_goal=special_goal_entered=0;g_ram[0xFE2A]=g_ram[0xFE48]=g_ram[0xFF97]=0;resume_checkpoint=0;
    }
    if(pending_stage){
        unsigned wanted=pending_stage;pending_stage=0;const TrStage *target=tr_stage(wanted);
        stage=wanted>=0x1000&&wanted<=0x1002?imports[wanted-0x1000]:wanted==0x2000?imports[3]:NULL;
        active=stage!=NULL;
        if(active){put(0xFE10,1);put(0xEE4E,tr_stage(stage->id)->act-1);prepare_title();}
        else if(target&&!target->pack){put(0xFE10,target->native_id);put(0xEE4E,target->native_id);tr_objects_reset(NULL);}
        else{fprintf(stderr,"[Trilogy] Requested stage unavailable\n");active=0;}
        fprintf(stderr,"[Trilogy] Starting stage %04X slot %d\n",wanted,session_slot);
    }
    if(active){
        putlong(0xE660,0); /* native chapter stays at its own checkpoint */
        putlong(0xFF92,special_mask());special_ring_guest=0;
        if(special_return==stage->id){special_return=0;special_restore_objects=1;
            fprintf(stderr,"[Trilogy special] Returning to %04X\n",stage->id);}
    }
    else if(session_slot>=0)putlong(0xE660,0xFFFFE6AC+session_slot*10);
}
static void save_import(void)
{
    if(session_slot<0||!stage)return;
    unsigned slot=(unsigned)session_slot,cp=g_ram[0xFE2A]&127,ch=stage->id==0x2000?TR_CHAPTER_S2:TR_CHAPTER_S1;
    if(!special_after_goal&&(progress.chapters[ch][slot].stage!=stage->id||progress.chapters[ch][slot].checkpoint!=cp)){
        if(cp||!progress.chapters[ch][slot].checkpoint)tr_progress_checkpoint(&progress,slot,stage->id,cp);
    }
    unsigned chaos=0,supers=0;
    for(unsigned i=0;i<7;++i){if(g_ram[0xFFB2+i])chaos|=1u<<i;if(g_ram[0xFFB2+i]==3)supers|=1u<<i;}
    if(chaos!=progress.campaign.slots[slot].chaos||supers!=progress.campaign.slots[slot].super)
        tr_progress_collect(&progress,slot,chaos,supers);
    unsigned native=0xE6AC+slot*10,emeralds=0;
    for(unsigned i=0;i<7;++i)emeralds|=(g_ram[0xFFB2+i]&3)<<(14-i*2);
    unsigned special=(g_ram[native+2]&0xF0)|(g_ram[0xFE16]&15);
    if(ram(native+6)!=emeralds||g_ram[native+8]!=g_ram[0xFE12]||g_ram[native+2]!=special||g_ram[native+9]!=g_ram[0xFE18]){
        put(native+6,emeralds);g_ram[native+8]=g_ram[0xFE12];g_ram[native+2]=(uint8_t)special;g_ram[native+9]=g_ram[0xFE18];
        M68KState saved=g_cpu;recomp_call_addr(0xC3E4);g_cpu=saved;
    }
    store_progress();
}
static void finish_level(void)
{
    if(!stage)return;
    save_import();unsigned next=tr_next_import(stage->id,available);
    if(session_slot>=0){TrResume resume;if(tr_progress_finish_import(&progress,(unsigned)session_slot,available,&resume))next=resume.stage;else return;store_progress();}
    pending_stage=next;g_ram[0xFE2A]=0;put(0xFE02,1);
    g_ram[0xFAA8]=0;g_ram[0xF7CA]=g_ram[0xF7CB]=0;
    fprintf(stderr,"[Trilogy] Stage %04X cleared; next %04X\n",stage->id,next);
    if(special_goal_entered){
        special_after_goal=next;put(0xFE02,0);g_ram[0xFFBB]=0;g_ram[0xFE48]=1;
        put(0xFE4A,ram(0xFE10));g_ram[0xF600]=0x34;
        fprintf(stderr,"[Trilogy special] Goal -> Blue Spheres after %04X; next %04X\n",stage->id,next);
    }
}
static unsigned native_card(unsigned id)
{
    static const unsigned ids[]={0x3000,0x3010,0x3020,0x3030,0x3050,0x3060,0x3070,0x3040,0x3080,0x3090,0x3161,0x30A0,0x30B0,0x30C0};
    for(unsigned i=0;i<14;++i)if((id&0xFFF0)==(ids[i]&0xFFF0))return i;
    return 0;
}
static unsigned menu_cycle(unsigned slot,unsigned current,int direction)
{
    unsigned index=0;for(unsigned i=0;i<tr_stage_count;++i)if(tr_stages[i].id==current)index=i;
    for(unsigned n=0;n<tr_stage_count;++n){
        index=(index+tr_stage_count+direction)%tr_stage_count;
        const TrStage *s=tr_stages+index;TrSlot candidate=progress.campaign.slots[slot];
        if((!s->pack||(s->pack&available))&&tr_campaign_select_zone(&candidate,s->id))return s->id;
    }
    return current;
}
static void menu_input(void)
{
    unsigned focused=g_ram[0xEF4B];
    if(!(g_ram[0xF605]&0x10)||!available||focused<1||focused>TR_SLOTS||ram(0xB078)||ram(0xEEE4))return;
    unsigned slot=focused-1,native=0xE6AC+slot*10;
    if((g_ram[native]&128)||(progress.protected_records&TR_RECORD_CAMP))return;
    if(progress.tokens[slot]&&!(available&~progress.campaign.slots[slot].packs))return;
    if(!progress.tokens[slot]){TrSlot old;if(tr_native_slot_decode(g_ram+native,&old))tr_progress_attach_native(&progress,slot,&old);}
    if(!progress.tokens[slot])return;
    if(available&1)tr_progress_add_pack(&progress,slot,1);
    if(available&2)tr_progress_add_pack(&progress,slot,2);
    TrResume r;if(tr_progress_resolve(&progress,slot,available,&r))menu_stage[slot]=r.stage;
    store_progress();
    /* The stock menu handles B as Back before processing save cards. Consume
     * only the advertised enrollment action before that handler sees it. */
    g_ram[0xF605]&=(uint8_t)~0x10;
}
static int menu_slot_hook(void)
{
    unsigned a=g_cpu.A[0]&65535,slot=g_ram[a+0x2E];if(slot>=TR_SLOTS)return 0;
    unsigned native=0xE6AC+slot*10;if(g_ram[native]&128)return 0;
    int focused=g_ram[0xEF4B]==slot+1;
    int can_control=focused&&!ram(0xB078)&&!ram(0xEEE4);
    unsigned pressed=can_control?g_ram[0xF605]:0;
    if(!progress.tokens[slot]||(progress.protected_records&TR_RECORD_CAMP))return 0;
    M68KState saved=g_cpu;
    TrSlot *campaign=progress.campaign.slots+slot;
    if(!menu_stage[slot]){TrResume r;if(tr_progress_resolve(&progress,slot,available,&r))menu_stage[slot]=r.stage;}
    if(campaign->state==TR_COMPLETE){
        if(pressed&1)menu_stage[slot]=menu_cycle(slot,menu_stage[slot],1);
        if(pressed&2)menu_stage[slot]=menu_cycle(slot,menu_stage[slot],-1);
    }
    if(pressed&0xE0){
        if(campaign->state==TR_COMPLETE)tr_progress_select_zone(&progress,slot,menu_stage[slot],available);
        store_progress();
        /* Reuse the stock slot launch for character, emeralds, lives, sound,
         * SRAM and the normal menu-to-level fade. Only routing is extended. */
        g_cpu.A[0]=0xFF0000|a;g_cpu.A[1]=0xFF0000|native;g_cpu.D[0]=g_ram[native+3];
        recomp_call_addr(0xD57A);g_cpu=saved;return 1;
    }
    g_ram[a+0x3B]=0;put(a+0x36,native_card(menu_stage[slot]));
    g_ram[a+0x22]=(uint8_t)(4+ram(a+0x34));
    g_ram[a+0x1D]=(uint8_t)(focused&&campaign->state==TR_COMPLETE&&!(ram(0xFE04)&16)?0x1A:0);
    g_ram[a+0x23]=0x17;put(a+0x16,focused?2:0);
    if(focused){
        unsigned id=menu_stage[slot],index=id==0x2000?3:id>=0x1000&&id<=0x1002?id-0x1000:4;
        if(index<4&&imports[index]){
            memcpy(g_machine.vdp.vram+0xB740,menu_tiles[index],0x8C0);
            for(unsigned i=0;i<16;++i)put(0xFC60+i*2,menu_palette[index][i]);
        }else{g_cpu.D[1]=native_card(id);g_cpu.A[0]=0xFF0000|a;g_cpu.A[1]=0xFF0000|native;recomp_call_addr(0xD96A);}
    }
    g_cpu.A[0]=0xFF0000|a;recomp_call_addr(0xD69E);g_cpu=saved;return 1;
}
static unsigned font_tile(unsigned c)
{
    if(c>='0'&&c<='9')return 0xA562+c-'0';
    if(c>='A'&&c<='Z')return 0xA570+c-'A';
    if(c==':')return 0xA56E;
    return 0x8000;
}
static void menu_caption(unsigned address,const char *text,unsigned width)
{
    unsigned length=(unsigned)strlen(text);
    for(unsigned i=0;i<width;++i){unsigned t=font_tile(i<length?(unsigned char)text[i]:' '),a=address+i*2;
        g_machine.vdp.vram[a]=(uint8_t)(t>>8);g_machine.vdp.vram[a+1]=(uint8_t)t;}
}
static void menu_frame(void)
{
    char footer[40]="";unsigned focused=g_ram[0xEF4B];
    for(unsigned slot=0;slot<TR_SLOTS;++slot){
        if(!progress.tokens[slot]||(g_ram[0xE6AC+slot*10]&128))continue;
        const TrStage *s=tr_stage(menu_stage[slot]);if(!s)continue;
        char caption[12];
        if(s->pack)snprintf(caption,sizeof caption,"%s %u",s->pack==1?"GHZ":"EHZ",s->act);
        else{static const char *names[]={"AIZ","HCZ","MGZ","CNZ","ICZ","LBZ","MHZ","FBZ","SOZ","LRZ","HPZ","SSZ","DEZ","DDZ"};
            snprintf(caption,sizeof caption,"%s %u",names[native_card(s->id)],s->act);}
        menu_caption(0xCA1E+slot*26,caption,7);
    }
    if(focused>=1&&focused<=8){unsigned slot=focused-1,native=0xE6AC+slot*10;
        if(progress.tokens[slot]&&!(g_ram[native]&128)){
            if(available&~progress.campaign.slots[slot].packs)snprintf(footer,sizeof footer,"B ADD CHAPTERS");
            else if(progress.campaign.slots[slot].state==TR_COMPLETE)snprintf(footer,sizeof footer,"UP DOWN SELECT STAGE");
        }else if(!(g_ram[native]&128)&&available)snprintf(footer,sizeof footer,"B ADD CHAPTERS");
    }
    /* Reuse the original title area for the enrollment hint, without
     * covering Data Select or introducing another menu. */
    g_ram[0xB022]=(uint8_t)(*footer?0:3);
    if(!menu_text_object||ram(menu_text_object)!=1||ram(menu_text_object+2)!=0xABC6){
        menu_text_object=0;
        for(unsigned a=0xB0DE;a<0xCAE2;a+=0x4A)if(!ram(a)&&!ram(a+2)){
            menu_text_object=a;memset(g_ram+a,0,0x4A);putlong(a,0x1ABC6);putlong(a+0xC,0x480000);
            put(a+8,0x80);put(a+0x10,236);put(a+0x14,332);g_ram[a+7]=128;g_ram[a+6]=8;break;
        }
    }
    memset(menu_text_map,0,sizeof menu_text_map);menu_text_map[1]=2;
    unsigned count=0;
    for(unsigned i=0;footer[i]&&i<32;++i){if(footer[i]==' ')continue;
        unsigned at=4+count++*6,t=font_tile((unsigned char)footer[i]);
        menu_text_map[at+2]=(uint8_t)(t>>8);menu_text_map[at+3]=(uint8_t)t;
        menu_text_map[at+4]=(uint8_t)((i*8)>>8);menu_text_map[at+5]=(uint8_t)(i*8);
    }
    menu_text_map[3]=(uint8_t)count;
}
int tr_runtime_hook(uint32_t pc)
{
    if(pc==0xC3E4||pc==0xD624){
        sync_native_saves(pc);
        if(pc==0xD624&&selected&&!forced_stage&&available){
            unsigned address=g_cpu.A[1]&65535,a=g_cpu.A[0]&65535;
            if(address>=0xE6AC&&address<0xE6FC&&(address-0xE6AC)%10==0){
                tr_progress_new(&progress,(address-0xE6AC)/10,available,ram(a+0x34));store_progress();
            }
        }
        return 0;
    }
    if(!selected)return 0;
    if(pc==0xC4D2&&active&&stage){save_import();fprintf(stderr,"[Trilogy special] Results saved, emerald mask %02X\n",session_slot<0?0:progress.campaign.slots[session_slot].chaos);return 1;}
    if(pc==0x1358){tr_audio_cue(active&&stage&&(g_ram[0xF600]&127)==12?tr_stage(stage->id)->pack:0,g_cpu.D[0]&255);return 0;}
    if(session_started&&(g_ram[0xF600]&127)==4){
        active=session_started=0;stage=NULL;pending_stage=resume_checkpoint=0;session_slot=-1;tr_objects_reset(NULL);
        special_return=special_restore_objects=special_ring_guest=0;memset(special_no_save,0,sizeof special_no_save);
        special_goal_entered=special_after_goal=0;
    }
    if(pc==0xC812&&!forced_stage){menu_input();return 0;}
    if(pc==0xD42C&&!forced_stage)return menu_slot_hook();
    if(pc==0xC818&&!forced_stage){menu_frame();return 0;}
    if(pc==0xC570){
        active=session_started=0;pending_stage=resume_checkpoint=0;session_slot=-1;tr_objects_reset(NULL);
        special_return=special_restore_objects=special_ring_guest=0;memset(special_no_save,0,sizeof special_no_save);
        special_goal_entered=special_after_goal=0;
        menu_text_object=0;
        for(unsigned i=0;i<TR_SLOTS;++i){TrResume r;menu_stage[i]=tr_progress_resolve(&progress,i,available,&r)?r.stage:0;}
        return 0;
    }
    if(pc==0x5FB2 && (g_ram[0xF600]&127)==12)begin_level();
    if(!active||(g_ram[0xF600]&127)!=12)return 0;
    switch(pc){
    case 0x6170A:if(g_ram[0xB005]<6)special_used(g_ram[(g_cpu.A[0]&65535)+0x2C]);return 0;
    case 0x6185C:
        if(stage->id<0x2000){
            special_goal_entered=1;save_import();tr_objects_start_results();
            M68KState saved=g_cpu;recomp_call_addr(0x851D8);g_cpu=saved;
            fprintf(stderr,"[Trilogy special] Goal ring collected in %04X; act results first\n",stage->id);return 1;
        }
        return 0;
    case 0x618FC:special_ring_display();return 1;
    case 0x2D3C8:
        if(stage->id==0x2000&&ram(0xFE20)>=50&&g_ram[0xFFB0]<7&&!(session_slot>=0&&special_read_only)){
            unsigned a=g_cpu.A[0]&65535,id=g_ram[a+0x2C]&127;
            if(id<32&&!(special_mask()&(1u<<id)))tr_objects_checkpoint_stars(a);
        }
        return 1;
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
    case 0x3BB8:if(ram(0xEE50))return 0;palette_cycle();return 1;
    case 0x1B690:
        if(!g_ram[0xF76C]){tr_objects_reset(stage);if(special_restore_objects){tr_objects_special_restore();special_restore_objects=0;}g_ram[0xF76C]=4;}
        tr_objects_load();special_spawn();save_import();return 1;
    case 0x1B7F2:tr_objects_load();return 1;
    case 0x1C38A:resize();return 1;
    case 0x28C80:case 0x27758:case 0x4F33C:case 0x2F77C:
    case 0xE8AA:case 0x85FDE:return 1;
    case 0x2DCE2:if(tr_objects_results_started()){finish_level();return 1;}return 0;
    case 0x2D92C:title_name();return 1;
    case 0x2D95C:if(ram((g_cpu.A[0]&65535)+0xC)==0x48)title_art();return 0;
    case 0x2DC36:if(stage->id==0x1002)memcpy(g_machine.vdp.vram+0x53D*32,title_three,sizeof title_three);return 0;
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
