/* Semantic hit/invalidation tests reuse the existing independent frozen-head
 * fixtures, not the new job-key/decode implementation. */
#define main vdp_existing_differential_main
#include "vdp_render_diff_test.c"
#undef main
#include "../../runner/video/genesis_dac.h"
void cand_gvdp_render_reuse_stats(uint64_t out[6]);
void cand_gvdp_render_reuse_clear(void);
size_t cand_gvdp_render_reuse_cache_bytes(void);
int head_gvdp_capture_scanline(GVDP *,int,GVDPRasterRow *);
int head_gvdp_compose_captured(const GVDPRasterRow *,uint8_t *);
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
    v->vram[0x84]=0x98;v->vram[0x85]=0x76;v->vram[0x86]=0x54;v->vram[0x87]=0x32;
    v->sprite_collision=v->sprite_overflow=0;
}
static unsigned pair(GVDP *a,GVDP *b,int line,uint8_t *x,uint8_t *y)
{
    memset(x,0xa5,GVDP_MAX_WIDTH);memset(y,0xa5,GVDP_MAX_WIDTH);
    int wa=head_gvdp_render_scanline(a,line,x),wb=cand_gvdp_render_scanline(b,line,y);
    CHECK(wa==wb && wa>0);CHECK(!memcmp(x,y,GVDP_MAX_WIDTH));CHECK(!memcmp(a,b,sizeof *a));return (unsigned)wa;
}
static unsigned capture_pair(GVDP *a,GVDP *b,int line,uint8_t *x,uint8_t *y)
{
    GVDPRasterRow ha,hb;memset(&ha,0xa6,sizeof ha);memset(&hb,0xa6,sizeof hb);
    int wa=head_gvdp_capture_scanline(a,line,&ha),wb=cand_gvdp_capture_scanline(b,line,&hb);
    CHECK(wa==wb && wa>0);CHECK(!memcmp(&ha,&hb,sizeof ha));CHECK(!memcmp(a,b,sizeof *a));
    memset(x,0xa5,GVDP_MAX_WIDTH);memset(y,0xa5,GVDP_MAX_WIDTH);
    CHECK(head_gvdp_compose_captured(&ha,x)==wa);CHECK(cand_gvdp_compose_captured(&hb,y)==wb);
    CHECK(!memcmp(x,y,GVDP_MAX_WIDTH));return (unsigned)wa;
}
static void status_and_backoff(void)
{
    presentation();GVDP a,b;scene(&a);b=a;uint8_t x[GVDP_MAX_WIDTH],y[GVDP_MAX_WIDTH];uint64_t before[6],after[6];
    capture_pair(&a,&b,9,x,y);CHECK(a.sprite_collision && b.sprite_collision);
    a.sprite_collision=b.sprite_collision=a.sprite_overflow=b.sprite_overflow=0;
    cand_gvdp_render_reuse_stats(before);capture_pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(after);
    CHECK(b.sprite_collision && after[1]==before[1]+1 && after[5]==before[4]+1);
    /* Same-pointer direct art write must change visible output and miss. */
    a.vram[4*32+4]^=0xf0;b.vram[4*32+4]^=0xf0;
    capture_pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(before);CHECK(before[2]==2);
    for(unsigned n=0;n<31;++n)capture_pair(&a,&b,9,x,y);
    cand_gvdp_render_reuse_stats(after);CHECK(after[3]==before[3]+31);
    capture_pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(after);CHECK(after[1]==before[1]+1);
    /* Re-import a different current device at the same address, then restore. */
    GVDP saved=a;memset(a.vram+0x80,0x45,32);b=a;capture_pair(&a,&b,9,x,y);
    a=b=saved;for(unsigned n=0;n<34;++n)capture_pair(&a,&b,9,x,y);
    /* Raise the hardware line limit again after the guest clears status. */
    presentation();scene(&a);uint8_t entry[8];memcpy(entry,a.vram+0x6000,8);
    for(unsigned n=0;n<21;++n) {
        unsigned off=0x6000+n*8;memcpy(a.vram+off,entry,8);
        a.vram[off+3]=(uint8_t)(n==20?0:n+1);
        unsigned xpos=140+(n>1?n*8:0);a.vram[off+6]=(uint8_t)(xpos>>8);a.vram[off+7]=(uint8_t)xpos;
    }
    b=a;capture_pair(&a,&b,9,x,y);CHECK(a.sprite_overflow && a.sprite_collision);
    a.sprite_overflow=b.sprite_overflow=a.sprite_collision=b.sprite_collision=0;
    cand_gvdp_render_reuse_stats(before);capture_pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(after);
    CHECK(b.sprite_overflow && b.sprite_collision && after[1]==before[1]+1);
}
static void capture_and_palette(void)
{
    presentation();GVDP src,a,b;scene(&src);a=b=src;
    uint8_t x[GVDP_MAX_WIDTH],y[GVDP_MAX_WIDTH],prior[GVDP_MAX_WIDTH];uint64_t before[6],after[6];
    pair(&a,&b,9,x,y);pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(before);
    for(unsigned i=0;i<6;++i)CHECK(before[i]==0);
    CHECK(cand_gvdp_render_reuse_cache_bytes()==0); /* Native never allocates or asks the cache. */
    a=b=src;capture_pair(&a,&b,9,x,y);CHECK(cand_gvdp_render_reuse_cache_bytes()>0);
    a=b=src;capture_pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(before);CHECK(before[5]==1 && before[4]==0);
    /* SAT-only mutation: planes hit while current sprite pixels/status change. */
    memcpy(prior,x,sizeof prior);a.sprite_collision=b.sprite_collision=0;
    a.vram[0x6007]^=8;b.vram[0x6007]^=8;capture_pair(&a,&b,9,x,y);
    cand_gvdp_render_reuse_stats(after);CHECK(after[5]==before[5]+1);
    CHECK(!a.sprite_collision && !b.sprite_collision);CHECK(memcmp(prior,x,sizeof prior));
    /* Backdrop, shadow, diagnostic and current CRAM are metadata, not cached planes. */
    cand_gvdp_render_reuse_stats(before);unsigned color=4;
    uint32_t rgb=genesis_dac_cram_to_argb(a.cram[color],0);
    a.cram[color]^=0xeee;b.cram[color]^=0xeee;
    a.reg[7]^=3;b.reg[7]^=3;a.reg[0xc]^=8;b.reg[0xc]^=8;
    head_gvdp_set_bgdiag(1);cand_gvdp_set_bgdiag(1);capture_pair(&a,&b,9,x,y);
    CHECK(genesis_dac_cram_to_argb(b.cram[color],0)!=rgb);
    cand_gvdp_render_reuse_stats(after);CHECK(after[5]==before[5]+1);
    /* Tile ID and Vflip are already resolved into pattern bytes. */
    cand_gvdp_render_reuse_stats(before);
    for(unsigned row=0;row<8;++row){memcpy(a.vram+5*32+(7-row)*4,a.vram+4*32+row*4,4);memcpy(b.vram+5*32+(7-row)*4,b.vram+4*32+row*4,4);}
    for(unsigned pos=0xc000;pos<0xf000;pos+=2){a.vram[pos]=b.vram[pos]=0x10;a.vram[pos+1]=b.vram[pos+1]=5;}
    capture_pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(after);CHECK(after[5]==before[5]+1);
    /* Palette LINE, priority and Hflip still affect raw packed plane bytes. */
    for(unsigned bit=0;bit<3;++bit) {
        presentation();scene(&a);b=a;capture_pair(&a,&b,9,x,y);capture_pair(&a,&b,9,x,y);
        GVDPRasterRow old,changed;memset(&old,0xa6,sizeof old);cand_gvdp_capture_scanline(&b,9,&old);
        unsigned attr=bit==0?0x20u:bit==1?0x80u:8u;
        for(unsigned pos=0xc000;pos<0xf000;pos+=2){a.vram[pos]^=(uint8_t)attr;b.vram[pos]^=(uint8_t)attr;}
        cand_gvdp_render_reuse_stats(before);capture_pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(after);
        CHECK(after[2]==before[2]+1 && after[5]==before[5]);
        memset(&changed,0xa6,sizeof changed);cand_gvdp_capture_scanline(&b,9,&changed);
        CHECK(memcmp(old.plane_a,changed.plane_a,GVDP_MAX_WIDTH) || memcmp(old.plane_b,changed.plane_b,GVDP_MAX_WIDTH));
    }
    /* Retained packet owns its bytes after heap clear, imports and global replacement. */
    presentation();a=b=src;GVDPRasterRow owned;memset(&owned,0xa6,sizeof owned);
    CHECK(cand_gvdp_capture_scanline(&b,9,&owned)==320);memset(x,0xa5,sizeof x);CHECK(cand_gvdp_compose_captured(&owned,x)==320);
    cand_gvdp_render_reuse_clear();CHECK(!cand_gvdp_render_reuse_cache_bytes());
    memset(&b,0xd6,sizeof b);scene(&b);b.vram[0x80]^=0xff;
    cand_gvdp_set_ws_extra(64);cand_gvdp_set_ws_canvas(511);cand_gvdp_set_bgdiag(1);
    memset(y,0xa5,sizeof y);CHECK(cand_gvdp_compose_captured(&owned,y)==320);CHECK(!memcmp(x,y,sizeof x));
    presentation();a=b=src;capture_pair(&a,&b,9,x,y);
    GVDP other=a,other_b=b;capture_pair(&other,&other_b,9,x,y);
    other.vsram[0]=other_b.vsram[0]=17;capture_pair(&other,&other_b,9,x,y);
    a=b=src;for(unsigned n=0;n<34;++n)capture_pair(&a,&b,9,x,y);
    /* Valid destination storage inside live VRAM must keep original write order. */
    presentation();a=b=src;capture_pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(before);
    CHECK(head_gvdp_capture_scanline(&a,9,(GVDPRasterRow *)(void *)(a.vram+0x80))==cand_gvdp_capture_scanline(&b,9,(GVDPRasterRow *)(void *)(b.vram+0x80)));
    CHECK(!memcmp(&a,&b,sizeof a));cand_gvdp_render_reuse_stats(after);CHECK(after[0]==before[0]);
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
        GVDPRasterRow rejected,copy;memset(&rejected,0xc5,sizeof rejected);copy=rejected;
        CHECK(cand_gvdp_capture_scanline(&b,9,&rejected)==0);CHECK(!memcmp(&rejected,&copy,sizeof copy));CHECK(!cand_gvdp_render_reuse_cache_bytes());
        pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(before);pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(after);CHECK(after[0]==before[0] && !cand_gvdp_render_reuse_cache_bytes());
        if(mode==2) {
            uint16_t top[48];for(unsigned n=0;n<48;++n)top[n]=(uint16_t)(n*2);
            head_gvdp_set_ext_colors(top,NULL,100);cand_gvdp_set_ext_colors(top,NULL,100);
            pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(before);
            top[0]^=0xeee;head_gvdp_set_ext_colors(top,NULL,100);cand_gvdp_set_ext_colors(top,NULL,100);
            pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(after);
            CHECK(after[0]==before[0] && !cand_gvdp_render_reuse_cache_bytes() && head_gvdp_ext_color(9,1,0)==top[0] && cand_gvdp_ext_color(9,1,0)==top[0]);
        }
        if(mode<2){art_a[4*32+4]^=0xff;art_b[4*32+4]^=0xff;}
        tiles_a[4]=tiles_b[4]=(uint8_t)(tiles_a[4]%3+1);
        pair(&a,&b,9,x,y);
    }
    /* Native host palettes remain live without invoking or allocating the plane cache. */
    presentation();scene(&a);b=a;Mutation ha={0,0,0,0,NULL,tiles_a},hb={1,0,0,0,NULL,tiles_b};
    head_gvdp_set_host_sprites(mutation_provider,&ha);cand_gvdp_set_host_sprites(mutation_provider,&hb);
    uint32_t colors[6]={0xff010203,0,0,0,0,0xff123456};CHECK(head_gvdp_set_host_palette(colors,6));CHECK(cand_gvdp_set_host_palette(colors,6));
    pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(before);colors[5]=0xffaabbcc;CHECK(head_gvdp_set_host_palette(colors,6));CHECK(cand_gvdp_set_host_palette(colors,6));pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(after);
    CHECK(after[0]==before[0] && !cand_gvdp_render_reuse_cache_bytes() && ha.calls==2 && hb.calls==2);CHECK(head_gvdp_host_palette()[5]==colors[5] && cand_gvdp_host_palette()[5]==colors[5]);
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
    capture_pair(&a,&b,9,x,y);capture_pair(&a,&b,9,x,y);memcpy(prior,x,sizeof prior);cand_gvdp_render_reuse_stats(before);CHECK(before[1]>0);
    a.reg[15]=b.reg[15]=2;
    head_gvdp_write_control(&a,0x4084);cand_gvdp_write_control(&b,0x4084);
    head_gvdp_write_control(&a,0);cand_gvdp_write_control(&b,0);
    head_gvdp_write_data(&a,0x1234);cand_gvdp_write_data(&b,0x1234);capture_pair(&a,&b,9,x,y);cand_gvdp_render_reuse_stats(after);
    CHECK(memcmp(prior,x,sizeof prior) && after[2]==before[2]+1 && after[1]==before[1]);
    /* Rewarm before the separate DMA writer: fallback is not invalidation proof. */
    for(unsigned n=0;n<32;++n)capture_pair(&a,&b,9,x,y);
    cand_gvdp_render_reuse_stats(before);CHECK(before[1]>after[1]);memcpy(prior,x,sizeof prior);
    a.reg[1]|=0x10;b.reg[1]|=0x10;a.reg[0x13]=b.reg[0x13]=3;a.reg[0x14]=b.reg[0x14]=0;a.reg[0x17]=b.reg[0x17]=0x80;
    head_gvdp_write_control(&a,0x4084);cand_gvdp_write_control(&b,0x4084);
    head_gvdp_write_control(&a,0x80);cand_gvdp_write_control(&b,0x80);
    CHECK(a.dma_fill_pending && b.dma_fill_pending);
    head_gvdp_write_data(&a,0x789a);cand_gvdp_write_data(&b,0x789a);CHECK(!a.dma_fill_pending && !b.dma_fill_pending);capture_pair(&a,&b,9,x,y);
    cand_gvdp_render_reuse_stats(after);CHECK(memcmp(prior,x,sizeof prior) && after[2]==before[2]+1 && after[1]==before[1]);
}

int main(void)
{
    status_and_backoff();capture_and_palette();providers_and_aliases();port_and_dma();
    printf("Capture plane jobs: %u semantic assertions, fresh sprites/status/meta, hot direct/port/DMA invalidation, canonical inputs, alias/provider/lifecycle fallback and native zero-allocation pass\n",checks);return 0;
}
