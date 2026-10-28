#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../runner/video/genesis_vdp.h"

int head_gvdp_render_scanline(GVDP *v, int line, uint8_t *out);
void head_gvdp_reset(GVDP *v);
void head_gvdp_set_ws_extra(int extra_px);
void head_gvdp_set_ws_canvas(int canvas_w);
void head_gvdp_set_ws_bar_black(int black);
void head_gvdp_set_bgdiag(int on);

int cand_gvdp_render_scanline(GVDP *v, int line, uint8_t *out);
int cand_gvdp_capture_scanline(GVDP *v, int line, GVDPRasterRow *row);
int cand_gvdp_compose_captured(const GVDPRasterRow *row, uint8_t *out);
void cand_gvdp_reset(GVDP *v);
void cand_gvdp_evaluate_scanline(GVDP *v, int line);
void cand_gvdp_set_ws_extra(int extra_px);
void cand_gvdp_set_ws_canvas(int canvas_w);
void cand_gvdp_set_ws_bar_black(int black);
void cand_gvdp_set_bgdiag(int on);
void cand_gvdp_set_host_sprites(GVDPHostSprites draw,void *user);
void cand_gvdp_set_ext_tiles(const uint8_t *tiles);
int cand_gvdp_set_sprite_patterns_bank(unsigned,const uint8_t *,unsigned,const uint8_t *);

static uint32_t rng_state = 0x13572468u;

static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static void fill_random(GVDP *v, int display, int h40, int im2, int sh,
                        int hmode, int vmode_2cell, int plane_code)
{
    memset(v, 0, sizeof(*v));
    for (int i = 0; i < GVDP_VRAM_SIZE; i++)
        v->vram[i] = (uint8_t)rnd();
    for (int i = 0; i < GVDP_VSRAM_ENTRIES; i++)
        v->vsram[i] = (uint16_t)rnd();
    for (int i = 0; i < GVDP_CRAM_ENTRIES; i++)
        v->cram[i] = (uint16_t)rnd();

    v->reg[0x01] = display ? 0x40 : 0x00;
    v->reg[0x02] = (uint8_t)((rnd() & 0x07u) << 3);
    v->reg[0x03] = (uint8_t)(rnd() & (h40 ? 0x3Eu : 0x3Fu));
    v->reg[0x04] = (uint8_t)(rnd() & 0x07u);
    v->reg[0x05] = (uint8_t)(rnd() & 0x7Fu);
    v->reg[0x07] = (uint8_t)(rnd() & 0x3Fu);
    v->reg[0x0B] = (uint8_t)((hmode & 3) | (vmode_2cell ? 4 : 0));
    v->reg[0x0C] = (uint8_t)((h40 ? 1 : 0) | (im2 ? 6 : 0) | (sh ? 8 : 0));
    v->reg[0x0D] = (uint8_t)(rnd() & 0x3Fu);
    v->reg[0x10] = (uint8_t)plane_code;
    v->reg[0x11] = (uint8_t)(rnd() & 0x9Fu);
    v->reg[0x12] = (uint8_t)(rnd() & 0x9Fu);
    v->sprite_overflow = (uint8_t)(rnd() & 1u);
    v->sprite_collision = (uint8_t)(rnd() & 1u);
}

static void install_directed_sprite(GVDP *v, int h40, int im2, int sh_op)
{
    uint16_t sat = (uint16_t)((v->reg[0x05] & 0x7F) << 9);
    int cell_h_shift = im2 ? 4 : 3;
    int tile_bytes = im2 ? 64 : 32;
    int y_origin = im2 ? 256 : 128;
    int y = y_origin + 8;
    int x = 128 + 12;
    int tile = 4;
    int pal = sh_op ? 3 : 2;
    int nib = sh_op ? 14 : 9;
    uint16_t attr = (uint16_t)((1u << 15) | (pal << 13) | tile);
    uint16_t pa = (uint16_t)(tile * tile_bytes + 2 * 4);
    int max_sprites = h40 ? 80 : 64;

    for (int i = 0; i < max_sprites * 8; i++)
        v->vram[(uint16_t)(sat + i)] = 0;
    v->vram[(uint16_t)(sat + 0)] = (uint8_t)(y >> 8);
    v->vram[(uint16_t)(sat + 1)] = (uint8_t)y;
    v->vram[(uint16_t)(sat + 2)] = 0x00;
    v->vram[(uint16_t)(sat + 3)] = 0x01;
    v->vram[(uint16_t)(sat + 4)] = (uint8_t)(attr >> 8);
    v->vram[(uint16_t)(sat + 5)] = (uint8_t)attr;
    v->vram[(uint16_t)(sat + 6)] = (uint8_t)(x >> 8);
    v->vram[(uint16_t)(sat + 7)] = (uint8_t)x;

    v->vram[(uint16_t)(sat + 8)] = (uint8_t)(y >> 8);
    v->vram[(uint16_t)(sat + 9)] = (uint8_t)y;
    v->vram[(uint16_t)(sat + 10)] = 0x00;
    v->vram[(uint16_t)(sat + 11)] = 0x00;
    v->vram[(uint16_t)(sat + 12)] = (uint8_t)(attr >> 8);
    v->vram[(uint16_t)(sat + 13)] = (uint8_t)attr;
    v->vram[(uint16_t)(sat + 14)] = (uint8_t)(x >> 8);
    v->vram[(uint16_t)(sat + 15)] = (uint8_t)x;

    for (int row = 0; row < (1 << cell_h_shift); row++)
        for (int col = 0; col < 4; col++)
            v->vram[(uint16_t)(tile * tile_bytes + row * 4 + col)] =
                (uint8_t)((nib << 4) | nib);
    v->vram[pa] = (uint8_t)((nib << 4) | nib);
}

static int compare_case(const char *label, const GVDP *src, int line,
                        int ws_extra, int ws_canvas, int bar_black, int bgdiag)
{
    GVDP a = *src;
    GVDP b = *src;
    uint8_t out_a[GVDP_MAX_WIDTH];
    uint8_t out_b[GVDP_MAX_WIDTH];
    memset(out_a, 0xA5, sizeof(out_a));
    memset(out_b, 0xA5, sizeof(out_b));

    head_gvdp_set_ws_extra(ws_extra);
    head_gvdp_set_ws_canvas(ws_canvas);
    head_gvdp_set_ws_bar_black(bar_black);
    head_gvdp_set_bgdiag(bgdiag);
    cand_gvdp_set_ws_extra(ws_extra);
    cand_gvdp_set_ws_canvas(ws_canvas);
    cand_gvdp_set_ws_bar_black(bar_black);
    cand_gvdp_set_bgdiag(bgdiag);

    int wa = head_gvdp_render_scanline(&a, line, out_a);
    int wb = cand_gvdp_render_scanline(&b, line, out_b);
    if (wa != wb) {
        fprintf(stderr, "%s width mismatch line=%d head=%d cand=%d\n",
                label, line, wa, wb);
        return 1;
    }
    if (memcmp(out_a, out_b, sizeof(out_a)) != 0) {
        for (int i = 0; i < GVDP_MAX_WIDTH; i++) {
            if (out_a[i] != out_b[i]) {
                fprintf(stderr,
                        "%s pixel mismatch line=%d x=%d head=%u cand=%u width=%d\n",
                        label, line, i, out_a[i], out_b[i], wa);
                break;
            }
        }
        return 1;
    }
    if (memcmp(&a, &b, sizeof(GVDP)) != 0) {
        fprintf(stderr,
                "%s GVDP mutation mismatch line=%d overflow %u/%u collision %u/%u\n",
                label, line, a.sprite_overflow, b.sprite_overflow,
                a.sprite_collision, b.sprite_collision);
        return 1;
    }
    /* Deferred composition owns all row inputs. It must survive replacement
     * of the complete device image and presentation parameters after capture. */
    GVDP captured=*src;GVDPRasterRow packet;
    int wc=cand_gvdp_capture_scanline(&captured,line,&packet);
    if(wc!=wa || memcmp(&captured,&a,sizeof a)) {
        fprintf(stderr,"%s capture state mismatch line=%d\n",label,line);return 1;
    }
    memset(&captured,0xd6,sizeof captured);
    cand_gvdp_set_ws_extra(0);cand_gvdp_set_ws_canvas(0);
    cand_gvdp_set_ws_bar_black(!bar_black);cand_gvdp_set_bgdiag(!bgdiag);
    memset(out_b,0xa5,sizeof out_b);
    wc=cand_gvdp_compose_captured(&packet,out_b);
    cand_gvdp_set_ws_extra(ws_extra);cand_gvdp_set_ws_canvas(ws_canvas);
    cand_gvdp_set_ws_bar_black(bar_black);cand_gvdp_set_bgdiag(bgdiag);
    if(wc!=wa || memcmp(out_a,out_b,sizeof out_a)) {
        fprintf(stderr,"%s deferred row mismatch line=%d\n",label,line);return 1;
    }
    /* A discarded replay row must have exactly the full renderer's device
     * effects. Use direct state images, then repeat the view: neither writes
     * bypassing VDP ports nor reset/restore may require cache invalidation. */
    GVDP discarded = *src;
    cand_gvdp_evaluate_scanline(&discarded, line);
    if (memcmp(&a, &discarded, sizeof(GVDP)) != 0) {
        fprintf(stderr, "%s discarded-row device mismatch line=%d\n", label, line);
        return 1;
    }
    cand_gvdp_evaluate_scanline(&discarded, line);
    if (memcmp(&a, &discarded, sizeof(GVDP)) != 0) {
        fprintf(stderr, "%s repeated discarded-row mismatch line=%d\n", label, line);
        return 1;
    }
    /* Lazy presentation can request original pixels after status evaluation.
     * That fallback must reproduce both the image and device state. */
    memset(out_b, 0xA5, sizeof(out_b));
    wb = cand_gvdp_render_scanline(&discarded, line, out_b);
    if (wb != wa || memcmp(out_a, out_b, sizeof(out_a)) ||
        memcmp(&a, &discarded, sizeof(GVDP))) {
        fprintf(stderr, "%s lazy native fallback mismatch line=%d\n", label, line);
        return 1;
    }
    return 0;
}

static int run_directed(void)
{
    int failures = 0;
    GVDP v;
    const int plane_codes[] = { 0x00, 0x01, 0x03, 0x10, 0x30, 0x33 };
    for (int h40 = 0; h40 <= 1; h40++) {
        for (int im2 = 0; im2 <= 1; im2++) {
            for (int sh = 0; sh <= 1; sh++) {
                for (int display = 0; display <= 1; display++) {
                    for (int pc = 0; pc < (int)(sizeof(plane_codes) / sizeof(plane_codes[0])); pc++) {
                        fill_random(&v, display, h40, im2, sh, pc & 3, pc & 1, plane_codes[pc]);
                        install_directed_sprite(&v, h40, im2, sh);
                        failures += compare_case("directed-native", &v, im2 ? 17 : 9, 0, 0, 0, 0);
                        failures += compare_case("directed-pillar-black", &v, im2 ? 17 : 9, 0, 448, 1, 0);
                        failures += compare_case("directed-wide-bgdiag", &v, im2 ? 17 : 9, 64, 448, 0, 1);
                    }
                }
            }
        }
    }
    return failures;
}

static int run_random(void)
{
    int failures = 0;
    const int plane_codes[] = { 0x00, 0x01, 0x03, 0x10, 0x11, 0x13, 0x30, 0x31, 0x33 };
    for (int i = 0; i < 4000; i++) {
        GVDP v;
        int h40 = (int)(rnd() & 1u);
        int im2 = (int)(rnd() & 1u);
        int display = (int)(rnd() & 1u);
        int sh = (int)(rnd() & 1u);
        int hmode = (int)(rnd() & 3u);
        int vmode = (int)(rnd() & 1u);
        int plane_code = plane_codes[rnd() % (sizeof(plane_codes) / sizeof(plane_codes[0]))];
        int max_line = (display && im2) ? 448 : 240;
        int line = (int)(rnd() % (uint32_t)max_line);
        int ws_extra = (int[]){0, 8, 64, 96, 140}[rnd() % 5];
        int ws_canvas = (int[]){0, 320, 448, 512}[rnd() % 4];
        int bar_black = (int)(rnd() & 1u);
        int bgdiag = (int)(rnd() & 1u);
        char label[64];
        fill_random(&v, display, h40, im2, sh, hmode, vmode, plane_code);
        if ((i & 7) == 0)
            install_directed_sprite(&v, h40, im2, sh);
        snprintf(label, sizeof(label), "random-%d", i);
        failures += compare_case(label, &v, line, ws_extra, ws_canvas, bar_black, bgdiag);
        if (failures > 12)
            return failures;
    }
    return failures;
}

static int run_odd_canvas_cases(void)
{
    int failures = 0;
    GVDP v;
    const int canvases[] = { 449, 511 };
    const int lines[] = { 0, 9, 223, 239, 447 };

    for (int h40 = 0; h40 <= 1; h40++) {
        for (int im2 = 0; im2 <= 1; im2++) {
            for (int display = 0; display <= 1; display++) {
                for (int bar_black = 0; bar_black <= 1; bar_black++) {
                    for (int bgdiag = 0; bgdiag <= 1; bgdiag++) {
                        fill_random(&v, display, h40, im2, 0, 0, 0, 0x00);
                        if (display)
                            install_directed_sprite(&v, h40, im2, 0);
                        for (int ci = 0; ci < (int)(sizeof(canvases) / sizeof(canvases[0])); ci++) {
                            for (int li = 0; li < (int)(sizeof(lines) / sizeof(lines[0])); li++) {
                                if (!im2 && lines[li] >= 240)
                                    continue;
                                char label[96];
                                snprintf(label, sizeof(label),
                                         "odd-canvas-%d-h%d-im2%d-display%d-bar%d-bgdiag%d-line%d",
                                         canvases[ci], h40 ? 40 : 32, im2, display,
                                         bar_black, bgdiag, lines[li]);
                                failures += compare_case(label, &v, lines[li],
                                                         canvases[ci] - (h40 ? 320 : 256),
                                                         canvases[ci], bar_black, bgdiag);
                            }
                        }
                    }
                }
            }
        }
    }
    return failures;
}

static int run_import_sequence(void)
{
    int failures = 0;
    GVDP a, b, c;
    fill_random(&a, 1, 1, 0, 0, 0, 0, 0x00);
    fill_random(&b, 1, 1, 1, 1, 3, 1, 0x33);
    fill_random(&c, 0, 0, 0, 0, 2, 0, 0x11);
    install_directed_sprite(&b, 1, 1, 1);
    failures += compare_case("import-seq-a", &a, 1, 0, 0, 0, 0);
    failures += compare_case("import-seq-b", &b, 223, 0, 0, 0, 0);
    failures += compare_case("import-seq-c", &c, 12, 0, 512, 1, 0);
    failures += compare_case("import-seq-a-again-wide", &a, 33, 64, 448, 0, 1);
    failures += compare_case("import-seq-b-again-native", &b, 447, 0, 0, 0, 0);
    return failures;
}

static int run_reset_restore(void)
{
    GVDP saved, a, b;
    int failures = 0;
    fill_random(&saved, 1, 1, 1, 1, 3, 1, 0x33);
    install_directed_sprite(&saved, 1, 1, 1);
    a = b = saved;
    head_gvdp_reset(&a);
    cand_gvdp_reset(&b);
    if (memcmp(&a, &b, sizeof a)) {
        fprintf(stderr, "reset state differs\n");
        failures++;
    }
    failures += compare_case("after-reset", &b, 0, 0, 0, 0, 0);
    b = saved; /* Current-build import can replace every byte directly. */
    failures += compare_case("after-reset-restore", &b, 16, 64, 448, 0, 0);
    b.cram[0] ^= 0xEEE;
    b.vsram[0] ^= 31;
    b.reg[0x0B] = 0;
    failures += compare_case("after-live-palette-scroll-write", &b, 17, 0, 0, 0, 0);
    return failures;
}

static unsigned rejected_callbacks;
static void rejected_sprite(void *user,const GVDP *v,const GVDPSpriteLayer *layer)
{(void)user;(void)v;(void)layer;++rejected_callbacks;}
static int run_capture_rejection(void)
{
    GVDP v,before;GVDPRasterRow row,saved;
    uint8_t art[32]={0},tiles[2048]={0},out[GVDP_MAX_WIDTH],guard[GVDP_MAX_WIDTH];
    fill_random(&v,1,1,0,0,0,0,0);before=v;
    memset(&row,0xa6,sizeof row);saved=row;
    for(unsigned kind=0;kind<7;++kind) {
        if(kind==0)cand_gvdp_set_host_sprites(rejected_sprite,NULL);
        if(kind==1)cand_gvdp_set_ext_tiles(tiles);
        if(kind==2||kind==3)if(!cand_gvdp_set_sprite_patterns_bank(kind-2,art,sizeof art,NULL))return 1;
        int line=kind==4?-1:kind==5?GVDP_MAX_HEIGHT*2:0;
        int width=cand_gvdp_capture_scanline(kind==6?NULL:&v,line,&row);
        cand_gvdp_set_host_sprites(NULL,NULL);cand_gvdp_set_ext_tiles(NULL);
        if(!cand_gvdp_set_sprite_patterns_bank(0,NULL,0,NULL)||
           !cand_gvdp_set_sprite_patterns_bank(1,NULL,0,NULL))return 1;
        if(width||memcmp(&v,&before,sizeof v)||memcmp(&row,&saved,sizeof row)||rejected_callbacks) {
            fprintf(stderr,"capture rejection mutated inputs kind=%u\n",kind);return 1;
        }
    }
    if(cand_gvdp_capture_scanline(&v,0,NULL))return 1;
    memset(out,0xb9,sizeof out);memcpy(guard,out,sizeof out);
    for(unsigned kind=0;kind<4;++kind) {
        memset(&row,0,sizeof row);row.width=320;row.count=320;
        if(kind==0)row.width=0;
        if(kind==1)row.width=GVDP_MAX_WIDTH+1;
        if(kind==2)row.start=321;
        if(kind==3)row.count=321;
        if(cand_gvdp_compose_captured(&row,out)||memcmp(out,guard,sizeof out)) {
            fprintf(stderr,"invalid capture packet wrote output kind=%u\n",kind);return 1;
        }
    }
    if(cand_gvdp_compose_captured(NULL,out)||cand_gvdp_compose_captured(&row,NULL))return 1;
    return 0;
}

int main(void)
{
    int failures = 0;
    failures += run_directed();
    failures += run_random();
    failures += run_odd_canvas_cases();
    failures += run_import_sequence();
    failures += run_reset_restore();
    failures += run_capture_rejection();
    if (failures) {
        fprintf(stderr, "vdp_render_diff_test failed: %d mismatches\n", failures);
        return 1;
    }
    printf("vdp_render_diff_test passed: directed, 4000 random, odd-canvas, import/reset/restore and live-write cases matched\n");
    return 0;
}
