/* Host sprites join the sprite layer behind every SAT sprite and obey plane
 * priority, without touching the hardware overflow/collision flags. */
#include "genesis_vdp.h"
#include "genesis_dac.h"
#include "genesis_machine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
static GVDP v;
static int calls, high, color = 2;
int g_snd_trace;
unsigned long g_snd_frame;
unsigned g_snd_line;
/* The real scheduler/VDP run below without a ROM, CPU program or sound bus. */
void gbus_init(GenesisBus *b,GVDP *p){memset(b,0,sizeof *b);b->vdp=p;}
uint16_t gbus_read16(GenesisBus *b,uint32_t a){(void)b;(void)a;return 0;}
uint8_t gbus_z80_read(GenesisBus *b,uint16_t a){(void)b;(void)a;return 0;}
void gbus_z80_write(GenesisBus *b,uint16_t a,uint8_t x){(void)b;(void)a;(void)x;}
void glue_run_game_chunk(uint32_t cycles){(void)cycles;}
int glue_own_vint_service_latched(GVDP *p){(void)p;return 0;}
static int split_line,last_row,hints,vints,raster_calls,has_output;
static void raster_color(uint16_t c)
{
    g_machine.vdp.cram[0]=c;
    g_machine.vdp.colour_updated(NULL,0,c);
}
void glue_own_interrupt(int level,GVDP *p)
{
    if(level==4){
        CHECK(p->scanline==split_line && (!has_output || last_row==split_line));
        if(split_line==0)CHECK(p->sprite_collision); /* status already evaluated */
        ++hints;raster_color(0xE00);
    }else{
        CHECK(level==6 && p->scanline==224 && (!has_output || last_row==223));
        ++vints;
    }
}
static void raster_sprites(void *user,const GVDP *p,const GVDPSpriteLayer *layer)
{(void)user;(void)p;(void)layer;++raster_calls;}
static void raster_sink(void *user,int line,const uint32_t *pixels,int width)
{
    (void)user;CHECK(line==last_row+1 && width==320);
    CHECK(pixels[width-1]==genesis_dac_cram_to_argb(line<=split_line?0x00E:0xE00,GENESIS_DAC_NORMAL));
    last_row=line;
}
static int raster_enabled(void *user){(void)user;return 3;}
static void raster_custom(void *user,const GVDP *p,int line,int width,
                          GenesisNativeRow native,void *ctx,const GVDPRasterRow *row)
{
    (void)p;CHECK(row);const uint32_t *pixels=native(ctx);CHECK(native(ctx)==pixels);
    raster_sink(user,line,pixels,width); /* deferred native composition is cached */
}
static void raster_order(void)
{
    const GenesisScanlineProducer producer={raster_enabled,raster_custom};
    const int splits[]={0,100,223};
    for(int mode=0;mode<3;++mode)for(unsigned n=0;n<sizeof splits/sizeof *splits;++n){
        machine_init();GVDP *p=&g_machine.vdp;
        split_line=splits[n];last_row=-1;hints=vints=raster_calls=0;has_output=mode!=2;
        p->reg[0]=0x10;p->reg[1]=0x60;p->reg[5]=0x7C;p->reg[10]=255;
        p->reg[12]=1;p->hint_counter=(uint8_t)split_line;raster_color(0x00E);
        memset(p->vram+32,0x11,32);
        for(int s=0;s<2;++s){unsigned a=0xF800+s*8;
            p->vram[a+1]=128;p->vram[a+3]=(uint8_t)(s?0:1);
            p->vram[a+5]=1;p->vram[a+7]=128;
        }
        gvdp_set_host_sprites(raster_sprites,NULL);
        machine_run_frame_ex(has_output?raster_sink:NULL,NULL,mode==1?&producer:NULL);
        CHECK(hints==1 && vints==1 && p->sprite_collision);
        CHECK(raster_calls==(has_output?224:0));
        if(has_output)CHECK(last_row==223);
    }
    gvdp_set_host_sprites(NULL,NULL);
}
static void word(unsigned a,unsigned n) { v.vram[a]=n>>8; v.vram[a+1]=n; }
static void draw(void *user, const GVDP *vdp, const GVDPSpriteLayer *layer)
{
    CHECK(user==&calls && vdp==&v && layer->total==320 && layer->offset==0);
    ++calls;
    for (int x=0;x<32;++x) {
        if (layer->opaque[x]) continue;
        layer->index[x]=(uint8_t)color; layer->opaque[x]=1; layer->high[x]=(uint8_t)high;
    }
}
static void private_planes(void)
{
    uint8_t markers[2048]={0},pixels[GVDP_MAX_WIDTH],composed[GVDP_MAX_WIDTH];
    uint16_t top[GVDP_EXT_LINES*16]={0},bottom[GVDP_EXT_LINES*16]={0};
    GVDPRasterRow captured;
    markers[1]=1;markers[2]=2;markers[4]=GVDP_EXT_SPRITE_FORCE|3;markers[7]=3;
    top[1]=0xE;bottom[1]=0xE00;top[18]=0xE0;bottom[18]=0xEEE;
    gvdp_set_ext_tiles(markers);gvdp_set_ext_colors(top,bottom,8);
    /* Same expected native priority and source-palette contract in scalar
     * and batched builds, including window replacement and operator sprites. */
    for(unsigned test=0;test<1024;++test){
        unsigned ap=test&3,bp=(test>>2)&3,ah=(test>>4)&1,bh=(test>>5)&1;
        unsigned sh=(test>>6)&1,win=(test/128)%3,spr=(test/29)%7,off=(test/11)%2;
        int line=(test&1)?8:0;
        memset(&v,0,sizeof v);v.reg[1]=64;v.reg[2]=0x30;v.reg[3]=0x2C;
        v.reg[4]=7;v.reg[5]=0x7C;v.reg[12]=(uint8_t)(1|sh*8);v.reg[13]=0x3F;
        if(win==1)v.reg[17]=1;
        if(win==2)v.reg[18]=0x80;
        memset(v.vram+32,0x11,32);memset(v.vram+64,0x22,32);
        memset(v.vram+128,0x33,32);memset(v.vram+160,0x33,32);
        memset(v.vram+192,spr==3?0xEE:0xFF,32);memset(v.vram+224,0x44,32);
        for(unsigned cell=0;cell<64*4;++cell){
            word(0xC000+cell*2,(ap<<13)|(ah<<15)|(off?3:1));
            word(0xE000+cell*2,(bp<<13)|(bh<<15)|2);
            word(0xB000+cell*2,(ap<<13)|(ah<<15)|(win==2?4:off?3:2));
        }
        if(spr){
            word(0xF800,(unsigned)(128+line));word(0xF806,128);
            word(0xF804,spr>=5?(spr==6?0x8007:7):spr>=3?0x6006:spr==2?0xA005:0x2005);
        }
        CHECK(gvdp_render_scanline(&v,line,pixels)==320);
        for(unsigned x=0;x<320;++x){
            unsigned tile=(win==2||(win==1&&x<16))?(win==2?4:off?3:2):off?3:1;
            unsigned ao=tile!=3,ai=tile==4?ap*16+3:ap?ap*16+tile:
                GVDP_HOST_PALETTE_BASE+(tile==2?16:0)+tile;
            unsigned bi=bp?bp*16+2:GVDP_HOST_PALETTE_BASE+18;
            unsigned so=x<8&&(spr==1||spr==2||spr>=5||(!sh&&spr>=3)),hi=spr==2||spr==6;
            unsigned si=spr>=5?GVDP_HOST_PALETTE_BASE+36:spr==3?62:spr==4?63:19;
            unsigned expected=so&&hi?si:ao&&ah?ai:bh?bi:so?si:ao?ai:bi;
            if(sh&&expected<64){
                unsigned shade=!((ao&&ah)||bh||(so&&hi)),light=0;
                if(x<8&&spr==4)shade=1;
                if(x<8&&spr==3){if(shade)shade=0;else light=1;}
                expected+=light?128:shade?64:0;
            }
            CHECK(pixels[x]==expected);
        }
        CHECK(gvdp_capture_presented_scanline(&v,line,&captured)==320 && captured.private_planes);
        CHECK(captured.host_palette[1]==genesis_dac_cram_to_argb(line<8?0xE:0xE00,GENESIS_DAC_NORMAL));
        CHECK(gvdp_compose_captured(&captured,composed)==320 && !memcmp(pixels,composed,320));
    }
    /* A presented packet keeps plane indices/colors after all providers and
     * the complete live VRAM image have been retired or overwritten. */
    gvdp_set_ext_tiles(NULL);memset(markers,0,sizeof markers);memset(&v,0,sizeof v);
    memset(top,0,sizeof top);memset(bottom,0,sizeof bottom);gvdp_set_ext_colors(top,bottom,0);
    CHECK(gvdp_compose_captured(&captured,composed)==320 && !memcmp(pixels,composed,320));
    CHECK(captured.host_palette[1]==genesis_dac_cram_to_argb(0xE00,GENESIS_DAC_NORMAL));
}
int main(void)
{
    uint8_t row[320];
    gvdp_init(&v);
    v.reg[1]=64; v.reg[2]=0x30; v.reg[4]=7; v.reg[5]=0x7C;
    v.reg[12]=1; v.reg[13]=0x3F;
    memset(v.vram+32,0x11,128);
    word(0xF800,128); v.vram[0xF802]=0; v.vram[0xF803]=0;  /* one 8x8 native sprite */
    word(0xF804,0x0001); word(0xF806,128);
    word(0xC004,0xA002);                                  /* plane A x=16..23: high, palette 1 */
    gvdp_render_scanline(&v,0,row);
    CHECK(row[4]==1 && row[12]==0 && row[20]==17);
    gvdp_set_host_sprites(draw,&calls);
    gvdp_render_scanline(&v,0,row);
    CHECK(calls==1);
    CHECK(row[4]==1);          /* native sprite keeps precedence */
    CHECK(row[12]==2);         /* host pixel fills an empty sprite column */
    CHECK(row[20]==17);        /* low-priority host pixel stays behind a high plane tile */
    high=1; gvdp_render_scanline(&v,0,row);
    CHECK(row[20]==2);         /* high-priority host pixel wins, like hardware */
    CHECK(!v.sprite_overflow && !v.sprite_collision);
    const uint32_t colors[] = { 0xFFFF1020u, 0xFF12AB34u };
    CHECK(!gvdp_host_palette());
    CHECK(gvdp_set_host_palette(colors, 2));
    CHECK(gvdp_host_palette()[0] == colors[0] && gvdp_host_palette()[1] == colors[1]);
    CHECK(gvdp_host_palette()[GVDP_HOST_PALETTE_SIZE-1] == 0xFF000000u);
    CHECK(!gvdp_set_host_palette(colors, GVDP_HOST_PALETTE_SIZE+1));
    CHECK(gvdp_host_palette()[0] == colors[0]);
    color=GVDP_HOST_PALETTE_BASE+1;
    high=0; v.reg[12]|=8; /* Host colors must not wrap in shadow/highlight mode. */
    gvdp_render_scanline(&v,0,row);
    CHECK(row[4]==1+GVDP_PALETTE_SHADOW && row[12]==color && row[20]==17);
    CHECK(gvdp_host_palette()[row[12]-GVDP_HOST_PALETTE_BASE] == colors[1]);
    high=1; gvdp_render_scanline(&v,0,row); CHECK(row[20]==color);
    uint32_t full_palette[GVDP_HOST_PALETTE_SIZE];
    for (unsigned i=0;i<GVDP_HOST_PALETTE_SIZE;++i) full_palette[i]=0xFF000000u | i;
    CHECK(gvdp_set_host_palette(full_palette,GVDP_HOST_PALETTE_SIZE));
    color=255; high=0; gvdp_render_scanline(&v,0,row);
    CHECK(row[12]==255 && gvdp_host_palette()[255-GVDP_HOST_PALETTE_BASE]==full_palette[GVDP_HOST_PALETTE_SIZE-1]);
    CHECK(v.cram[1]==0 && !v.sprite_overflow && !v.sprite_collision);
    CHECK(gvdp_set_host_palette(NULL,0) && !gvdp_host_palette());
    CHECK(gvdp_set_host_palette(colors,2));
    gvdp_set_host_sprites(NULL,NULL);
    CHECK(!gvdp_host_palette());
    v.reg[12]&=~8;
    gvdp_render_scanline(&v,0,row); CHECK(calls==5 && row[12]==0);
    uint8_t ext_tiles[2048]={0};
    uint16_t top[GVDP_EXT_LINES*16]={0}, bottom[GVDP_EXT_LINES*16]={0};
    top[1]=0xE;bottom[1]=0xE00;ext_tiles[1]=1;
    CHECK(!gvdp_ext_active() && !gvdp_ext_line(1));
    gvdp_set_ext_colors(top,bottom,8);gvdp_set_ext_tiles(ext_tiles);
    gvdp_render_scanline(&v,0,row);CHECK(row[4]==GVDP_HOST_PALETTE_BASE+1);
    CHECK(gvdp_ext_color(7,1,1)==0xE && gvdp_ext_color(8,1,1)==0xE00);
    CHECK(v.cram[1]==0 && !v.sprite_overflow && !v.sprite_collision);
    /* Ordinary extra lines remain line-0-only. An explicit sprite marker
     * can isolate imported UI on another source line, without coloring a
     * plane that uses the exact same pattern or changing CRAM. */
    word(0xF804,0x2001);gvdp_render_scanline(&v,0,row);CHECK(row[4]==17);
    CHECK(!gvdp_sprite_palette_line(0,1,1));
    ext_tiles[1]=GVDP_EXT_SPRITE_FORCE|2;word(0xC004,0xA001);
    gvdp_render_scanline(&v,0,row);
    CHECK(row[4]==GVDP_HOST_PALETTE_BASE+16+1 && row[20]==17);
    CHECK(gvdp_sprite_palette_line(0,1,1)==2 && !gvdp_ext_line(1));
    CHECK(v.cram[17]==0 && !v.sprite_overflow && !v.sprite_collision);
    ext_tiles[1]=3;word(0xF804,1);gvdp_render_scanline(&v,0,row);
    CHECK(row[4]==GVDP_HOST_PALETTE_BASE+32+1);
    ext_tiles[1]=1;word(0xC004,0xA002);
    gvdp_set_ext_tiles(NULL);gvdp_render_scanline(&v,0,row);CHECK(row[4]==1);
    /* An explicit marker selects private art without touching VRAM or plane
     * pixels, at native width as well as the game's custom renderer width. */
    uint8_t patterns[64]={0},pattern_lines[2]={0,1};
    memset(patterns+32,0x33,32);
    CHECK(!gvdp_set_sprite_patterns(patterns,63,pattern_lines));
    CHECK(gvdp_set_sprite_patterns(patterns,sizeof patterns,pattern_lines));
    unsigned bytes=99;CHECK(!gvdp_sprite_patterns(0,&bytes) && bytes==0);
    CHECK(gvdp_sprite_patterns(0x80,&bytes)==patterns && bytes==64);
    uint8_t second[64],second_lines[2]={2,2};memset(second,0x44,sizeof second);
    CHECK(!gvdp_set_sprite_patterns_bank(2,second,sizeof second,second_lines));
    CHECK(gvdp_set_sprite_patterns_bank(1,second,sizeof second,second_lines));
    CHECK(gvdp_sprite_patterns(0xC0,&bytes)==second && bytes==64);
    CHECK(gvdp_sprite_pattern_line(0xC0,1)==2 && gvdp_sprite_pattern_line(0x80,1)==1);
    gvdp_render_scanline(&v,0,row);CHECK(row[4]==1);
    v.vram[0xF802]=0x80;gvdp_render_scanline(&v,0,row);
    CHECK(row[4]==GVDP_HOST_PALETTE_BASE+3 && row[20]==17);
    CHECK(v.vram[32]==0x11 && !v.sprite_overflow && !v.sprite_collision);
    word(0xF804,0x2001);gvdp_render_scanline(&v,0,row);CHECK(row[4]==19);
    word(0xF804,0x0002);gvdp_render_scanline(&v,0,row);CHECK(row[4]==0);
    word(0xF804,1);CHECK(gvdp_set_sprite_patterns(NULL,0,NULL));
    CHECK(gvdp_sprite_patterns(0xC0,&bytes)==second && bytes==64);
    v.vram[0xF802]=0xC0;gvdp_render_scanline(&v,0,row);
    CHECK(row[4]==GVDP_HOST_PALETTE_BASE+16+4 && v.vram[32]==0x11);
    CHECK(gvdp_set_sprite_patterns_bank(1,NULL,0,NULL));v.vram[0xF802]=0x80;
    gvdp_render_scanline(&v,0,row);CHECK(row[4]==1);
    /* Presented captures own callback pixels, private art and final colors.
     * The older strict capture must still reject every provider before doing
     * any callback/device work. Capture itself invokes the callback once. */
    GVDPRasterRow captured,untouched;GVDP before=v;
    memset(&captured,0xA6,sizeof captured);untouched=captured;
    gvdp_set_host_sprites(draw,&calls);CHECK(gvdp_set_host_palette(colors,2));
    color=GVDP_HOST_PALETTE_BASE+1;high=1;
    CHECK(gvdp_set_sprite_patterns(patterns,sizeof patterns,pattern_lines));
    CHECK(gvdp_set_sprite_patterns_bank(1,second,sizeof second,second_lines));
    gvdp_set_ext_tiles(ext_tiles);gvdp_set_ext_colors(top,bottom,8);
    int old_calls=calls;
    CHECK(!gvdp_capture_scanline(&v,0,&captured));
    CHECK(calls==old_calls && !memcmp(&v,&before,sizeof v) && !memcmp(&captured,&untouched,sizeof captured));
    CHECK(gvdp_capture_presented_scanline(&v,0,&captured)==320 && calls==old_calls+1);
    CHECK(captured.host_palette[1]==colors[1] && captured.opaque[12] && captured.high[12]);
    uint8_t expected[320],composed[320];
    gvdp_render_scanline(&v,0,expected);
    CHECK(gvdp_compose_captured(&captured,composed)==320 && !memcmp(expected,composed,320));
    CHECK(gvdp_set_host_palette(NULL,0));
    pattern_lines[1]=GVDP_EXT_SPRITE_FORCE|3;top[35]=0xE0;word(0xF804,0x2001);
    gvdp_set_ext_colors(top,bottom,8);color=2;
    old_calls=calls;
    CHECK(gvdp_capture_presented_scanline(&v,0,&captured)==320 && calls==old_calls+1);
    CHECK(captured.sprite[4]==GVDP_HOST_PALETTE_BASE+35 && captured.host_palette[35]==0xFF00FF00u);
    CHECK(gvdp_compose_captured(&captured,expected)==320);
    gvdp_set_host_sprites(NULL,NULL);gvdp_set_ext_tiles(NULL);
    CHECK(gvdp_set_sprite_patterns(NULL,0,NULL) && gvdp_set_sprite_patterns_bank(1,NULL,0,NULL));
    memset(patterns,0,sizeof patterns);memset(second,0,sizeof second);memset(&v,0,sizeof v);
    memset(top,0,sizeof top);gvdp_set_ext_colors(top,top,0);
    CHECK(gvdp_compose_captured(&captured,composed)==320 && !memcmp(expected,composed,320));
    CHECK(captured.host_palette[35]==genesis_dac_cram_to_argb(0xE0,GENESIS_DAC_NORMAL));
    private_planes();raster_order();
    puts("host sprites composite behind native sprites with plane priority");
    return 0;
}
