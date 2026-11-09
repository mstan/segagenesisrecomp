/* Shadow/highlight merge, one case per scanline render: plane priority with
 * opaque and transparent tiles, sprite priority, sprite colour 14, and the
 * palette-3 colour 14/15 operators. Expected values match Genesis Plus GX and
 * ares, which agree on every case. Built for the scalar and the batched plane
 * renderer. */
#include "genesis_vdp.h"
#if GENESIS_HD_JOBS
#include "genesis_raster_jobs.h"
#endif
#if GVDP_TEST_GPU
#define SDL_MAIN_HANDLED
#include "genesis_hd_gpu.h"
static GenesisHdGpu *gpu;
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while (0)

enum { NONE, LO, HI };                 /* plane tile priority, or no tile     */
enum { T_EMPTY = 0, T_C1 = 1, T_C14 = 2, T_C15 = 3 };
enum { SH = GVDP_PALETTE_SHADOW, HL = GVDP_PALETTE_HIGHLIGHT };
enum { RED = 1, GREEN = 17, BLUE = 33, YELLOW = 14, CYAN = 30, MAGENTA = 46,
       BACKDROP = 50 };                /* pal0/1/2 colour 1 and 14, pal3 colour 2 */

typedef struct { int pri, pal, tile; } Sprite;
typedef struct {
    const char *name;
    int a, a_opaque, b, b_opaque;      /* plane A/B: NONE/LO/HI, opaque tile  */
    Sprite s[2];                       /* front to back; tile 0 = unused      */
    int expect;                        /* output index at the case's pixel    */
} Case;

static GVDP v;
static void word(unsigned a, unsigned n) { v.vram[a] = n >> 8; v.vram[a + 1] = n; }

static const Case cases[] = {
    { "low plane is shadowed",               LO,1, NONE,0, {{0}},                   RED + SH },
    { "high plane is normal",                HI,1, NONE,0, {{0}},                   RED },
    { "highlight op over normal",            NONE,0, HI,1, {{1,3,T_C14}},           GREEN + HL },
    { "A hi transparent over B lo",          HI,0, LO,1, {{0}},                     GREEN },
    { "A lo opaque, B hi transparent",       LO,1, HI,0, {{0}},                     RED },
    { "backdrop, B hi transparent",          LO,0, HI,0, {{0}},                     BACKDROP },
    { "backdrop, all low",                   LO,0, LO,0, {{0}},                     BACKDROP + SH },
    { "sprite lo over A lo",                 LO,1, NONE,0, {{0,2,T_C1}},            BLUE + SH },
    { "sprite hi over A lo",                 LO,1, NONE,0, {{1,2,T_C1}},            BLUE },
    { "sprite lo, A hi transparent, B lo",   HI,0, LO,1, {{0,2,T_C1}},              BLUE },
    { "sprite lo under A hi",                HI,1, NONE,0, {{0,2,T_C1}},            RED },
    { "colour 14 pal0 lo",                   LO,1, NONE,0, {{0,0,T_C14}},           YELLOW },
    { "colour 14 pal1 lo over backdrop",     NONE,0, NONE,0, {{0,1,T_C14}},         CYAN },
    { "colour 14 pal2 lo",                   LO,1, NONE,0, {{0,2,T_C14}},           MAGENTA },
    { "colour 14 pal0 hi",                   LO,1, NONE,0, {{1,0,T_C14}},           YELLOW },
    { "highlight op lo over shadowed",       LO,1, NONE,0, {{0,3,T_C14}},           RED },
    { "highlight op lo under A hi",          HI,1, NONE,0, {{0,3,T_C14}},           RED },
    { "highlight op lo, A hi transparent",   HI,0, LO,1, {{0,3,T_C14}},             GREEN + HL },
    { "shadow op lo over shadowed",          LO,1, NONE,0, {{0,3,T_C15}},           RED + SH },
    { "shadow op lo under A hi",             HI,1, NONE,0, {{0,3,T_C15}},           RED },
    { "shadow op hi over A hi",              HI,1, NONE,0, {{1,3,T_C15}},           RED + SH },
    { "highlight op hi over A hi",           HI,1, NONE,0, {{1,3,T_C14}},           RED + HL },
    { "highlight op lo over backdrop",       NONE,0, NONE,0, {{0,3,T_C14}},         BACKDROP },
    { "shadow op lo, B hi transparent",      NONE,0, HI,0, {{0,3,T_C15}},           BACKDROP + SH },
    { "highlight op hides sprite behind",    LO,1, NONE,0, {{0,3,T_C14},{0,2,T_C1}}, RED },
    { "shadow op hides sprite behind",       LO,1, NONE,0, {{0,3,T_C15},{1,2,T_C1}}, RED + SH },
    { "sprite hides operator behind",        LO,1, NONE,0, {{0,2,T_C1},{0,3,T_C14}}, BLUE + SH },
};

/* Plane tiles at screen column 0..7 of line 0; sprites as 8x8 at (0,0). */
static void setup(const Case *c)
{
    memset(v.vram + 0xC000, 0, 0x4000);
    if (c->a != NONE) word(0xC000, (c->a == HI ? 0x8000 : 0) | 0 << 13 | (c->a_opaque ? T_C1 : T_EMPTY));
    if (c->b != NONE) word(0xE000, (c->b == HI ? 0x8000 : 0) | 1 << 13 | (c->b_opaque ? T_C1 : T_EMPTY));
    int n = 0;
    while (n < 2 && c->s[n].tile) ++n;
    for (int i = 0; i < n; ++i) {
        unsigned e = 0xF800 + i * 8;
        word(e, 128);
        word(e + 2, i + 1 < n ? i + 1 : 0);              /* 8x8, link        */
        word(e + 4, c->s[i].pri << 15 | c->s[i].pal << 13 | c->s[i].tile);
        word(e + 6, 128);
    }
}

static int host_calls;
#if GENESIS_HD_JOBS
static void check_jobs(int expected)
{
    static uint32_t actual[GVDP_HD_ROWS][GVDP_MAX_WIDTH];
    uint32_t palette[256];uint8_t row[GVDP_MAX_WIDTH];
    for(unsigned i=0;i<256;++i)palette[i]=0xff000000u|i;
    gvdp_hd_enable(1);
    for(unsigned y=0;y<224;++y){
        gvdp_render_scanline(&v,(int)y,row);
        gvdp_hd_bind_palette(y,palette,256);
    }
    const GVDPRasterFrame *frame=gvdp_hd_acquire();CHECK(frame);
    CHECK(gvdp_hd_render_argb(frame,&actual[0][0],GVDP_MAX_WIDTH));
    CHECK(actual[0][4]==palette[expected]);
#if GVDP_TEST_GPU
    static uint32_t pixels[640*448];
    CHECK(genesis_hd_gpu_render(gpu,frame,2,NULL));
    CHECK(genesis_hd_gpu_readback(gpu,pixels,640*sizeof(uint32_t)));
    for(unsigned y=0;y<448;++y)for(unsigned x=0;x<640;++x)
        CHECK(pixels[y*640+x]==actual[y/2][x/2]);
#endif
    gvdp_hd_release(frame);gvdp_hd_enable(0);
}
#endif
static void host_blue(void *user, const GVDP *vdp, const GVDPSpriteLayer *layer)
{
    (void)user; (void)vdp;
    ++host_calls;
    for (int x = 0; x < 16; ++x)
        if (!layer->opaque[x]) { layer->index[x] = BLUE; layer->opaque[x] = 1; layer->high[x] = 0; }
}

int main(void)
{
#if GVDP_TEST_GPU
    SDL_SetMainReady();CHECK(SDL_Init(SDL_INIT_VIDEO)==0);
    SDL_Window *window=SDL_CreateWindow("Shadow highlight checks",0,0,640,448,SDL_WINDOW_HIDDEN);
    CHECK(window);
    SDL_Renderer *renderer=genesis_hd_create_renderer(window,SDL_RENDERER_ACCELERATED,2);CHECK(renderer);
    SDL_Texture *texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,320,224);CHECK(texture);
    gpu=genesis_hd_gpu_create(renderer,texture);CHECK(gpu);
#endif
    uint8_t row[GVDP_MAX_WIDTH];
    gvdp_init(&v);
    v.reg[1] = 0x44;                   /* display on, mode 5                  */
    v.reg[2] = 0x30; v.reg[4] = 0x07;  /* plane A $C000, plane B $E000         */
    v.reg[5] = 0x7C; v.reg[7] = 0x32;  /* SAT $F800, backdrop palette 3 col 2  */
    v.reg[12] = 0x81 | 0x08;           /* H40, shadow/highlight on            */
    v.reg[13] = 0x3F; v.reg[15] = 2;
    memset(v.vram + T_C1 * 32, 0x11, 32);
    memset(v.vram + T_C14 * 32, 0xEE, 32);
    memset(v.vram + T_C15 * 32, 0xFF, 32);

    int failed = 0;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        setup(&cases[i]);
        gvdp_render_scanline(&v, 0, row);
        if (row[4] != cases[i].expect) {
            fprintf(stderr, "case %zu (%s): got %d, want %d\n",
                    i, cases[i].name, row[4], cases[i].expect);
            failed = 1;
        }
        GVDPRasterRow captured;
        CHECK(gvdp_capture_scanline(&v,0,&captured)>0);
        CHECK(gvdp_compose_captured(&captured,row)>0);
        CHECK(row[4]==cases[i].expect);
#if GENESIS_HD_JOBS
        check_jobs(cases[i].expect);
#endif
    }
    CHECK(!failed);

    /* Host sprites sit behind every SAT sprite, operator pixels included;
     * past the 8x8 operator sprite they draw over the shadowed backdrop. */
    gvdp_set_host_sprites(host_blue, NULL);
    setup(&cases[15]);                 /* highlight op lo over a shadowed A    */
    gvdp_render_scanline(&v, 0, row);
    CHECK(host_calls == 1 && row[4] == RED && row[12] == BLUE + SH);
    GVDPRasterRow captured;
    CHECK(gvdp_capture_presented_scanline(&v,0,&captured)>0);
    CHECK(gvdp_compose_captured(&captured,row)>0);
    CHECK(host_calls == 2 && row[4]==RED && row[12]==BLUE+SH);
    gvdp_set_host_sprites(NULL, NULL);

    /* Without shadow/highlight the operator colours are ordinary colours. */
    v.reg[12] = 0x81;
    setup(&cases[15]);
    gvdp_render_scanline(&v, 0, row);
    CHECK(row[4] == 3 * 16 + 14);
#if GVDP_TEST_GPU
    genesis_hd_gpu_destroy(gpu);SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
#endif
    puts("vdp_shadow_highlight: all cases pass");
    return 0;
}
