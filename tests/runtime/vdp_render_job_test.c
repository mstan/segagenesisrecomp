/* Semantic hit/invalidation tests reuse the existing independent frozen-head
 * fixtures, not the new job-key/decode implementation. */
#define main vdp_existing_differential_main
#include "vdp_render_diff_test.c"
#undef main
#include "../../runner/video/genesis_dac.h"
void cand_gvdp_render_reuse_stats(uint64_t out[6]);
void cand_gvdp_render_reuse_clear(void);
void head_gvdp_set_unlimited_sprites(int);
void cand_gvdp_set_unlimited_sprites(int);
void head_gvdp_write_control(GVDP *,uint16_t);
void cand_gvdp_write_control(GVDP *,uint16_t);
void head_gvdp_write_data(GVDP *,uint16_t);
void cand_gvdp_write_data(GVDP *,uint16_t);
int head_gvdp_set_host_palette(const uint32_t *,unsigned);
int cand_gvdp_set_host_palette(const uint32_t *,unsigned);
const uint32_t *head_gvdp_host_palette(void);
const uint32_t *cand_gvdp_host_palette(void);
void head_gvdp_set_ext_colors(const uint16_t *,const uint16_t *,int);
void cand_gvdp_set_ext_colors(const uint16_t *,const uint16_t *,int);
uint16_t head_gvdp_ext_color(int,int,int);
uint16_t cand_gvdp_ext_color(int,int,int);
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"render job line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
static void presentation(void)
{
    head_gvdp_set_ws_extra(0);cand_gvdp_set_ws_extra(0);
    head_gvdp_set_ws_canvas(0);cand_gvdp_set_ws_canvas(0);
    head_gvdp_set_ws_bar_black(0);cand_gvdp_set_ws_bar_black(0);
    head_gvdp_set_bgdiag(0);cand_gvdp_set_bgdiag(0);
    head_gvdp_set_host_sprites(NULL,NULL);cand_gvdp_set_host_sprites(NULL,NULL);
    head_gvdp_set_ext_tiles(NULL);cand_gvdp_set_ext_tiles(NULL);
    head_gvdp_set_unlimited_sprites(0);cand_gvdp_set_unlimited_sprites(0);
    for(unsigned bank=0;bank<2;++bank){CHECK(head_gvdp_set_sprite_patterns_bank(bank,NULL,0,NULL));CHECK(cand_gvdp_set_sprite_patterns_bank(bank,NULL,0,NULL));}
    cand_gvdp_render_reuse_clear();
}
static void scene(GVDP *v)
{
    fill_random(v,1,1,0,1,0,0,0);v->reg[5]=0x30;
    /* Stable simple planes use visible stamp4, distinct from SAT/scroll. */
    v->reg[2]=0x30;v->reg[3]=0x3c;v->reg[4]=7;v->reg[0xd]=0x20;
    v->reg[0x10]=0;v->reg[0x11]=v->reg[0x12]=0;v->vsram[0]=v->vsram[1]=0;
    memset(v->vram+0x8000,0,1024);
    for(unsigned pos=0xc000;pos<0xf000;pos+=2){v->vram[pos]=0;v->vram[pos+1]=4;}
    install_directed_sprite(v,1,0,0);
    memcpy(v->vram+0x6008,v->vram+0x6000,8);v->vram[0x600b]=0;
    v->sprite_collision=v->sprite_overflow=0;
}
static unsigned pair(GVDP *a,GVDP *b,int line,uint8_t *x,uint8_t *y)
{
    memset(x,0xa5,GVDP_MAX_WIDTH);memset(y,0xa5,GVDP_MAX_WIDTH);
    int wa=head_gvdp_render_scanline(a,line,x),wb=cand_gvdp_render_scanline(b,line,y);
    CHECK(wa==wb && wa>0);CHECK(!memcmp(x,y,GVDP_MAX_WIDTH));CHECK(!memcmp(a,b,sizeof *a));return (unsigned)wa;
}
static void status_and_backoff(void)
{
    presentation();GVDP a,b;scene(&a);b=a;uint8_t x[GVDP_MAX_WIDTH],y[GVDP_MAX_WIDTH];uint64_t before[6],after[6];
    pair(&a,&b,9,x,y);CHECK(a.sprite_collision && b.sprite_collision);
    a.sprite_collision=b.sprite_collision=a.sprite_overflow=b.sprite_overflow=0;
    cand_gvdp_render_reuse_stats(before);pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(after);
    CHECK(b.sprite_collision && after[1]==before[1]+1 && after[4]==before[4]+1);
    /* Same-pointer direct art write must change visible output and miss. */
    a.vram[4*32+4]^=0xf0;b.vram[4*32+4]^=0xf0;
    pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(before);CHECK(before[2]==2);
    for(unsigned n=0;n<31;++n)pair(&a,&b,9,x,y);
    cand_gvdp_render_reuse_stats(after);CHECK(after[3]==before[3]+31);
    pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(after);CHECK(after[1]==before[1]+1);
    /* Re-import a different current device at the same address, then restore. */
    GVDP saved=a;memset(a.vram+0x80,0x45,32);b=a;pair(&a,&b,9,x,y);
    a=b=saved;for(unsigned n=0;n<34;++n)pair(&a,&b,9,x,y);
    /* Raise the hardware line limit again after the guest clears status. */
    presentation();scene(&a);uint8_t entry[8];memcpy(entry,a.vram+0x6000,8);
    for(unsigned n=0;n<21;++n) {
        unsigned off=0x6000+n*8;memcpy(a.vram+off,entry,8);
        a.vram[off+3]=(uint8_t)(n==20?0:n+1);
        unsigned xpos=140+(n>1?n*8:0);a.vram[off+6]=(uint8_t)(xpos>>8);a.vram[off+7]=(uint8_t)xpos;
    }
    b=a;pair(&a,&b,9,x,y);CHECK(a.sprite_overflow && a.sprite_collision);
    a.sprite_overflow=b.sprite_overflow=a.sprite_collision=b.sprite_collision=0;
    cand_gvdp_render_reuse_stats(before);pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(after);
    CHECK(b.sprite_overflow && b.sprite_collision && after[1]==before[1]+1);
}
static void capture_and_palette(void)
{
    presentation();GVDP src,a,b;scene(&src);a=b=src;
    GVDPRasterRow first,second;uint8_t x[GVDP_MAX_WIDTH],y[GVDP_MAX_WIDTH];uint64_t stats[6];
    memset(x,0xa5,sizeof x);head_gvdp_render_scanline(&a,9,x);
    memset(&first,0xa6,sizeof first);CHECK(cand_gvdp_capture_scanline(&b,9,&first)==320);CHECK(!memcmp(&a,&b,sizeof a));
    b=src;memset(y,0xa5,sizeof y);CHECK(cand_gvdp_render_scanline(&b,9,y)==320);CHECK(!memcmp(x,y,sizeof x));
    cand_gvdp_render_reuse_stats(stats);CHECK(stats[1]==1 && stats[4]==1);
    b=src;memset(&second,0xa6,sizeof second);CHECK(cand_gvdp_capture_scanline(&b,9,&second)==320);CHECK(!memcmp(&first,&second,sizeof first));
    cand_gvdp_render_reuse_stats(stats);CHECK(stats[5]==1);
    /* Retained input remains owned after device/global replacement and eviction. */
    memset(&b,0xd6,sizeof b);scene(&b);b.vram[0x80]^=0xff;
    memset(y,0xa5,sizeof y);cand_gvdp_render_scanline(&b,9,y);
    cand_gvdp_set_ws_extra(64);cand_gvdp_set_ws_canvas(511);cand_gvdp_set_bgdiag(1);
    memset(y,0xa5,sizeof y);CHECK(cand_gvdp_compose_captured(&first,y)==320);CHECK(!memcmp(x,y,sizeof x));
    presentation();a=b=src;pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(stats);uint64_t hits=stats[1];
    unsigned pos=0;while(pos<320 && x[pos]>=192)++pos;CHECK(pos<320);unsigned index=x[pos],color=index%64;
    uint32_t old=genesis_dac_cram_to_argb(a.cram[color],(int)(index/64));
    a.cram[color]^=0xeee;b.cram[color]^=0xeee;pair(&a,&b,9,x,y);
    CHECK(genesis_dac_cram_to_argb(b.cram[color],(int)(index/64))!=old);
    cand_gvdp_render_reuse_stats(stats);CHECK(stats[1]==hits+1);
    /* A different VDP at a different pointer may reuse only identical values. */
    GVDP other=a,other_b=b;pair(&other,&other_b,9,x,y);
    other.vsram[0]=other_b.vsram[0]=17;pair(&other,&other_b,9,x,y);
}
typedef struct Mutation { int candidate,drop,mutate;unsigned calls;uint8_t *retained;uint8_t *tiles; } Mutation;
static void mutation_provider(void *user,const GVDP *v,const GVDPSpriteLayer *layer)
{
    Mutation *m=(Mutation *)user;++m->calls;m->retained=layer->index;
    for(int x=2;x<layer->total;x+=19){layer->index[x]=GVDP_HOST_PALETTE_BASE+5;layer->opaque[x]=0x80;layer->high[x]=255;}
    if(m->mutate) {
        GVDP *w=(GVDP *)(void *)v;w->reg[2]^=8;w->reg[0xb]^=4;w->vsram[0]^=13;w->vram[0xc000]^=0x80;
        if(m->candidate){cand_gvdp_set_ws_extra(8);cand_gvdp_set_ws_canvas(449);cand_gvdp_set_ext_tiles(m->tiles);}
        else {head_gvdp_set_ws_extra(8);head_gvdp_set_ws_canvas(449);head_gvdp_set_ext_tiles(m->tiles);}
    }
    if(m->drop){if(m->candidate)cand_gvdp_set_host_sprites(NULL,NULL);else head_gvdp_set_host_sprites(NULL,NULL);}
}
static void providers_and_aliases(void)
{
    static uint8_t art_a[65536],art_b[65536],tiles_a[2048],tiles_b[2048];
    for(unsigned i=0;i<sizeof art_a;++i)art_a[i]=art_b[i]=(uint8_t)(i*37+0x19);
    for(unsigned i=0;i<sizeof tiles_a;++i)tiles_a[i]=tiles_b[i]=(uint8_t)(i%3+1);
    uint8_t x[GVDP_MAX_WIDTH],y[GVDP_MAX_WIDTH];uint64_t before[6],after[6];GVDP a,b;
    for(unsigned mode=0;mode<3;++mode) {
        presentation();scene(&a);b=a;
        if(mode<2){unsigned sat=(a.reg[5]&0x7fu)<<9;a.vram[sat+2]=b.vram[sat+2]=(uint8_t)(mode?0xc0:0x80);a.vram[sat+10]=b.vram[sat+10]=(uint8_t)(mode?0xc0:0x80);CHECK(head_gvdp_set_sprite_patterns_bank(mode,art_a,sizeof art_a,tiles_a));CHECK(cand_gvdp_set_sprite_patterns_bank(mode,art_b,sizeof art_b,tiles_b));}
        else {head_gvdp_set_ext_tiles(tiles_a);cand_gvdp_set_ext_tiles(tiles_b);}
        pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(before);pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(after);CHECK(after[1]==before[1]+1);
        if(mode==2) {
            uint16_t top[48];for(unsigned n=0;n<48;++n)top[n]=(uint16_t)(n*2);
            head_gvdp_set_ext_colors(top,NULL,100);cand_gvdp_set_ext_colors(top,NULL,100);
            pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(before);
            top[0]^=0xeee;head_gvdp_set_ext_colors(top,NULL,100);cand_gvdp_set_ext_colors(top,NULL,100);
            pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(after);
            CHECK(after[1]==before[1]+1 && head_gvdp_ext_color(9,1,0)==top[0] && cand_gvdp_ext_color(9,1,0)==top[0]);
        }
        if(mode<2){art_a[4*32+4]^=0xff;art_b[4*32+4]^=0xff;}
        tiles_a[4]=tiles_b[4]=(uint8_t)(tiles_a[4]%3+1);
        pair(&a,&b,9,x,y);
    }
    /* Host palette changes must not freeze an indexed hit's final colors. */
    presentation();scene(&a);b=a;Mutation ha={0,0,0,0,NULL,tiles_a},hb={1,0,0,0,NULL,tiles_b};
    head_gvdp_set_host_sprites(mutation_provider,&ha);cand_gvdp_set_host_sprites(mutation_provider,&hb);
    uint32_t colors[6]={0xff010203,0,0,0,0,0xff123456};CHECK(head_gvdp_set_host_palette(colors,6));CHECK(cand_gvdp_set_host_palette(colors,6));
    pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(before);colors[5]=0xffaabbcc;CHECK(head_gvdp_set_host_palette(colors,6));CHECK(cand_gvdp_set_host_palette(colors,6));pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(after);
    CHECK(after[1]==before[1]+1 && ha.calls==2 && hb.calls==2);CHECK(head_gvdp_host_palette()[5]==colors[5] && cand_gvdp_host_palette()[5]==colors[5]);
    /* Retained sprite-lane output aliases require original read/write ordering. */
    cand_gvdp_render_reuse_stats(before);unsigned wa=(unsigned)head_gvdp_render_scanline(&a,9,ha.retained),wb=(unsigned)cand_gvdp_render_scanline(&b,9,hb.retained);CHECK(wa==wb && !memcmp(ha.retained,hb.retained,wa) && !memcmp(&a,&b,sizeof a));cand_gvdp_render_reuse_stats(after);CHECK(after[0]==before[0]);
    /* Opaque callback changes regs/globals/VRAM and unregisters in this row. */
    presentation();scene(&a);b=a;ha=(Mutation){0,1,1,0,NULL,tiles_a};hb=(Mutation){1,1,1,0,NULL,tiles_b};
    for(unsigned n=0;n<4;++n){head_gvdp_set_host_sprites(mutation_provider,&ha);cand_gvdp_set_host_sprites(mutation_provider,&hb);pair(&a,&b,9,x,y);}
    CHECK(ha.calls==4 && hb.calls==4);
    /* Current GVDP and caller-owned extension data can also be the sink. */
    presentation();scene(&a);b=a;pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(before);
    wa=(unsigned)head_gvdp_render_scanline(&a,9,a.vram+0x80);wb=(unsigned)cand_gvdp_render_scanline(&b,9,b.vram+0x80);CHECK(wa==wb && !memcmp(&a,&b,sizeof a));cand_gvdp_render_reuse_stats(after);CHECK(after[0]==before[0]);
    presentation();scene(&a);b=a;memset(tiles_a,1,sizeof tiles_a);memcpy(tiles_b,tiles_a,sizeof tiles_b);head_gvdp_set_ext_tiles(tiles_a);cand_gvdp_set_ext_tiles(tiles_b);pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(before);
    wa=(unsigned)head_gvdp_render_scanline(&a,9,tiles_a);wb=(unsigned)cand_gvdp_render_scanline(&b,9,tiles_b);CHECK(wa==wb && !memcmp(tiles_a,tiles_b,sizeof tiles_a) && !memcmp(&a,&b,sizeof a));cand_gvdp_render_reuse_stats(after);CHECK(after[0]==before[0]);
    presentation();
}
static void port_and_dma(void)
{
    presentation();GVDP a,b;scene(&a);b=a;uint8_t x[GVDP_MAX_WIDTH],y[GVDP_MAX_WIDTH],prior[GVDP_MAX_WIDTH];uint64_t before[6],after[6];
    pair(&a,&b,9,x,y);pair(&a,&b,9,x,y);memcpy(prior,x,sizeof prior);cand_gvdp_render_reuse_stats(before);CHECK(before[1]>0);
    a.reg[15]=b.reg[15]=2;
    head_gvdp_write_control(&a,0x4084);cand_gvdp_write_control(&b,0x4084);
    head_gvdp_write_control(&a,0);cand_gvdp_write_control(&b,0);
    head_gvdp_write_data(&a,0x1234);cand_gvdp_write_data(&b,0x1234);pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(after);
    CHECK(memcmp(prior,x,sizeof prior) && after[2]==before[2]+1 && after[1]==before[1]);
    /* Rewarm before the separate DMA writer: fallback is not invalidation proof. */
    for(unsigned n=0;n<32;++n)pair(&a,&b,9,x,y);
    cand_gvdp_render_reuse_stats(before);CHECK(before[1]>after[1]);memcpy(prior,x,sizeof prior);
    a.reg[1]|=0x10;b.reg[1]|=0x10;a.reg[0x13]=b.reg[0x13]=3;a.reg[0x14]=b.reg[0x14]=0;a.reg[0x17]=b.reg[0x17]=0x80;
    head_gvdp_write_control(&a,0x4084);cand_gvdp_write_control(&b,0x4084);
    head_gvdp_write_control(&a,0x80);cand_gvdp_write_control(&b,0x80);
    CHECK(a.dma_fill_pending && b.dma_fill_pending);
    head_gvdp_write_data(&a,0x789a);cand_gvdp_write_data(&b,0x789a);CHECK(!a.dma_fill_pending && !b.dma_fill_pending);pair(&a,&b,9,x,y);
    cand_gvdp_render_reuse_stats(after);CHECK(memcmp(prior,x,sizeof prior) && after[2]==before[2]+1 && after[1]==before[1]);
}

int main(void)
{
    status_and_backoff();capture_and_palette();providers_and_aliases();port_and_dma();
    printf("Complete indexed jobs: %u semantic assertions, actual native/capture/private/palette hits, status-clear, aliases, lifecycle, callbacks and31call recovery pass\n",checks);return 0;
}
