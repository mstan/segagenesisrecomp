#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
/* Actual scheduler/VDP/Z80 code versus frozen426. Only guest glue and bus I/O
 * are fixture stubs: this is adapter/raster qualification, not game execution. */
#include "genesis_machine.h"
#include "genesis_frame_pixels.h"
#include "genesis_tile_packet.h"
#include "chip_trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"indexed line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
extern GenesisMachine original_g_machine;
void original_machine_init(void);
void original_machine_run_frame_ex(GenesisScanlineSink,void *,const GenesisScanlineProducer *);
size_t original_machine_rb_save(void *,size_t);
unsigned long g_snd_frame,g_snd_vint,g_snd_mc;
unsigned g_snd_line,g_snd_pcz;
int g_snd_trace;

enum { STRIDE=1024, ROWS=480 };
typedef struct Result {
    uint32_t frame[ROWS][STRIDE];
    unsigned widths[ROWS],argb,indexed,accepts,producer,host,guest,interrupts,packets,capture_calls;
    uint64_t status_trace;
    uint8_t state[sizeof(GenesisMachine)+128];size_t state_size;
} Result;
typedef struct Context { Result *result;unsigned cookie;int route,mode; } Context;
static GenesisMachine *active_machine;
static Context *active_context;

void gbus_init(GenesisBus *b,GVDP *v){memset(b,0,sizeof *b);b->vdp=v;}
uint16_t gbus_read16(GenesisBus *b,uint32_t a){(void)b;(void)a;return 0;}
uint8_t gbus_z80_read(GenesisBus *b,uint16_t a){(void)b;(void)a;return 0;}
void gbus_z80_write(GenesisBus *b,uint16_t a,uint8_t v){(void)b;(void)a;(void)v;}
void glue_run_game_chunk(uint32_t cycles)
{
    CHECK(cycles==488);GVDP *v=&active_machine->vdp;unsigned line=v->scanline;
    ++active_context->result->guest;
    unsigned entry=line%64;uint16_t color=(uint16_t)((line*39u)&0xeeeu);
    v->cram[entry]=color;v->colour_updated(v->colour_updated_user,entry,color);
    if(line%11==0)active_context->result->status_trace=active_context->result->status_trace*131u+gvdp_read_control(v);
}
void glue_own_interrupt(int level,GVDP *v)
{
    CHECK(v==&active_machine->vdp && (level==4 || level==6));
    ++active_context->result->interrupts;
}
int glue_own_vint_service_latched(GVDP *v){CHECK(v==&active_machine->vdp);return 0;}

static void put16(uint8_t *p,unsigned value){p[0]=(uint8_t)(value>>8);p[1]=(uint8_t)value;}
static void host_sprites(void *user,const GVDP *v,const GVDPSpriteLayer *layer)
{
    Context *c=(Context *)user;CHECK(c==active_context && v==&active_machine->vdp);++c->result->host;
    uint32_t palette[GVDP_HOST_PALETTE_SIZE];
    for(unsigned i=0;i<GVDP_HOST_PALETTE_SIZE;++i)palette[i]=0x80000000u|((unsigned)layer->line*0x010307u+i*0x030503u);
    gvdp_set_host_palette(palette,GVDP_HOST_PALETTE_SIZE);
    if(layer->total>9){layer->index[9]=GVDP_HOST_PALETTE_BASE;layer->opaque[9]=1;layer->high[9]=1;}
}
static void setup(Context *c,int reference)
{
    active_machine=reference?&original_g_machine:&g_machine;active_context=c;
    g_snd_frame=0;g_snd_line=0;gvdp_set_host_sprites(NULL,NULL);gvdp_set_host_palette(NULL,0);
    gvdp_set_ws_extra(0);gvdp_set_unlimited_sprites(0);gvdp_rb_set_pending_stall(0);
    if(reference)original_machine_init();else machine_init();
    GVDP *v=&active_machine->vdp;v->reg[0]=0x10;v->reg[1]=(uint8_t)(0x60|(c->mode==4?8:0));
    v->reg[2]=0x30;v->reg[3]=0x2c;v->reg[4]=7;v->reg[5]=0x68;v->reg[10]=5;
    v->reg[12]=(uint8_t)((c->mode==1?0:1)|(c->mode==2?6:0)|(c->mode==4?8:0));v->reg[16]=1;
    uint32_t seed=17;
    for(unsigned i=0;i<sizeof v->vram;++i){seed=seed*1664525u+1013904223u;v->vram[i]=(uint8_t)(seed>>24);}
    memset(v->vram+64,0x11,64);
    for(unsigned i=0;i<22;++i){uint8_t *sat=v->vram+0xd000+i*8;put16(sat,c->mode==2?256:128);put16(sat+2,i==21?0:i+1);put16(sat+4,2);put16(sat+6,128);}
    for(unsigned i=0;i<64;++i){uint16_t color=(uint16_t)((i*71)&0xeeeu);v->cram[i]=color;v->colour_updated(v->colour_updated_user,i,color);}
    if(c->mode==3)gvdp_set_ws_extra(16);
    if(c->mode>=3)gvdp_set_host_sprites(host_sprites,c);
}
static Context *context(void *user)
{Context *c=(Context *)user;CHECK(c==active_context && c->cookie==0x95b3u);return c;}
static void store(Context *c,int line,const uint32_t *argb,int width)
{
    CHECK(line>=0 && line<ROWS && width>0 && width<=STRIDE);c->result->widths[line]=(unsigned)width;
    memcpy(c->result->frame[line],argb,(size_t)width*4);
    /* Identical active-only overlay after expansion; one application per row. */
    c->result->frame[line][width-1]^=0x00453319u;
}
static void argb_sink(void *user,int line,const uint32_t *argb,int width)
{Context *c=context(user);++c->result->argb;store(c,line,argb,width);}
static void drop_sink(void *user,int line,const uint32_t *argb,int width)
{(void)user;(void)line;(void)argb;(void)width;}
static int accepts(void *user,int line,int width)
{
    Context *c=context(user);CHECK(width>0);++c->result->accepts;
    return c->route==1 || (c->route==3 && !(line&1));
}
static void indexed_sink(void *user,int line,const uint8_t *indices,int width,const uint32_t *palette)
{
    Context *c=context(user);uint32_t row[STRIDE];++c->result->indexed;
    genesis_pixels_expand(row,indices,width,palette,GVDP_OUTPUT_PALETTE_SIZE,0xff000000u);store(c,line,row,width);
}
typedef struct CaptureContext { Context *original;unsigned cookie; } CaptureContext;
static int packet_sink(void *user,GVDP *v,int line)
{
    CaptureContext *capture=(CaptureContext *)user;CHECK(capture&&capture->cookie==0x7138u);
    Context *c=context(capture->original);CHECK(v==&active_machine->vdp);++c->result->capture_calls;
    if(c->route==12 || (c->route==13 && (line&1)))return 0;
    GVDPTileRasterRow row;int width=c->route==11?gvdp_capture_scanline(v,line,&row.raster):
                                                   gvdp_capture_tile_scanline(v,line,&row);
    if(!width)return 0;
    uint8_t indices[GVDP_MAX_WIDTH];uint32_t argb[STRIDE];
    CHECK((c->route==11?gvdp_compose_captured(&row.raster,indices):gvdp_compose_tile_captured(&row,indices))==width);
    for(int x=0;x<width;x++)argb[x]=indices[x]<GVDP_TOTAL_PALETTE?
        genesis_dac_cram_to_argb(v->cram[indices[x]&63],indices[x]>>6):0xff000000u;
    ++c->result->packets;store(c,line,argb,width);return 1;
}
static int producer_enabled(void *user){Context *c=context(user);return c->route==5 || c->route==6 || c->route==14;}
static void produce(void *user,const GVDP *v,int line,int width,GenesisNativeRow get,void *ctx)
{
    Context *c=context(user);CHECK(v==&active_machine->vdp);++c->result->producer;
    if(c->route==5 || c->route==14){const uint32_t *a=get(ctx);CHECK(get(ctx)==a);argb_sink(c,line,a,gvdp_active_width(v));}
    else {uint32_t row[STRIDE];CHECK(width<=STRIDE);for(int x=0;x<width;++x)row[x]=0xff332211u+(unsigned)x;store(c,line,row,width);}
}
static void run(Result *result,int reference,int route,int mode)
{
    memset(result,0,sizeof *result);Context c={result,0x95b3u,route,mode};setup(&c,reference);
    GenesisScanlineProducer producer={producer_enabled,produce};
    GenesisIndexedSink adapter={&c,route==8?NULL:argb_sink,route>=4?NULL:indexed_sink,accepts};
    CaptureContext capture={&c,0x7138u};GenesisRasterSink raster={adapter,route==16?NULL:packet_sink,&capture};
    GenesisScanlineSink sink=argb_sink;void *user=&c;
    if(route==7)sink=NULL;
    else if(route==15){sink=reference?drop_sink:genesis_raster_sink_adapter;user=NULL;}
    else if(route>=10){if(!reference){sink=genesis_raster_sink_adapter;user=&raster;}}
    else if(route>=8){sink=reference?drop_sink:genesis_indexed_sink_adapter;user=reference?NULL:route==9?NULL:&adapter;}
    else if(!reference && route){sink=genesis_indexed_sink_adapter;user=&adapter;}
    if(reference)original_machine_run_frame_ex(sink,user,route==5||route==6||route==7||route==14?&producer:NULL);
    else machine_run_frame_ex(sink,user,route==5||route==6||route==7||route==14?&producer:NULL);
    CHECK(result->guest==262 && active_machine->master_cycle==262u*3420u && g_snd_frame==1);
    CHECK(result->producer==(route==5||route==6||route==14?(mode==2?448u:mode==4?240u:224u):0));
    if(!reference && route==1){
#if GENESIS_FINAL_PIXEL_PIPELINE
        CHECK(result->indexed>0 && result->argb==0);
#else
        CHECK(result->indexed==0 && result->argb>0);
#endif
    }
    if(!reference && route==2)CHECK(result->indexed==0 && result->argb>0);
    if(!reference && (route==10 || route==11) && mode<3)CHECK(result->packets>0&&result->argb==0);
    if(!reference && route==12)CHECK(result->packets==0&&result->argb>0);
    if(!reference && (route==14 || route==16))CHECK(result->capture_calls==0);
    result->state_size=reference?original_machine_rb_save(result->state,sizeof result->state):machine_rb_save(result->state,sizeof result->state);CHECK(result->state_size>0);
    gvdp_set_host_sprites(NULL,NULL);gvdp_set_host_palette(NULL,0);
}
static void current_restore_generation(void)
{
    machine_init();uint64_t generation=machine_presentation_generation();
    size_t n=machine_rb_save(NULL,0);void *data=malloc(n),*after=malloc(n);CHECK(data&&after);
    CHECK(machine_rb_save(data,n)==n&&machine_rb_load(data,n));
    CHECK(machine_presentation_generation()!=generation);generation=machine_presentation_generation();
    CHECK(machine_rb_save(after,n)==n&&!memcmp(data,after,n));
    FILE *f=tmpfile();CHECK(f&&machine_save_state(f));rewind(f);CHECK(machine_load_state(f));fclose(f);
    CHECK(machine_presentation_generation()!=generation);generation=machine_presentation_generation();
    f=tmpfile();CHECK(f&&!machine_load_state(f));fclose(f);CHECK(machine_presentation_generation()==generation);
    free(after);free(data);
}
int main(void)
{
    Result *a=(Result *)malloc(sizeof *a),*b=(Result *)malloc(sizeof *b);CHECK(a&&b);
    for(int mode=0;mode<6;++mode)for(int route=0;route<17;++route){
        run(a,1,route,mode);run(b,0,route,mode);
        CHECK(a->state_size==b->state_size && !memcmp(a->state,b->state,a->state_size));
        CHECK(!memcmp(a->frame,b->frame,sizeof a->frame) && !memcmp(a->widths,b->widths,sizeof a->widths));
        CHECK(a->status_trace==b->status_trace && a->host==b->host && a->guest==b->guest && a->interrupts==b->interrupts);
    }
    current_restore_generation();
    free(a);free(b);puts("102 frozen-machine indexed/raster adapter/context/status/palette/interlace/producer cases and current restore generation exact");return 0;
}
