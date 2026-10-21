#include "sonic_cd_render.h"
#include "../video/genesis_dac.h"

#include <string.h>

static uint16_t be16(const uint8_t *p)
{ return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }

static uint16_t vword(const GVDP *v, unsigned address)
{ return (uint16_t)(((uint16_t)v->vram[address & 0xffffu] << 8) |
                    v->vram[(address + 1u) & 0xffffu]); }

static int plane_tiles(unsigned code)
{ return code == 1 ? 64 : code == 3 ? 128 : 32; }

/* Lift a wrapped VDP coordinate into the original camera's world interval.
 * Use integer arithmetic with a specified tie direction on every host. */
static int lift(int wrapped, int target, int period)
{
    int delta = target - wrapped;
    int rounds = delta >= 0 ? (delta + period / 2) / period :
                              -((-delta + period / 2) / period);
    return wrapped + rounds * period;
}

/* Packed layer pixel: index[5:0], opaque[6], high priority[7]. */
static uint8_t pattern_pixel(const GVDP *v, uint16_t attr, int x, int y)
{
    unsigned fx = (unsigned)x & 7u, fy = (unsigned)y & 7u;
    if (attr & 0x0800u) fx ^= 7u;
    if (attr & 0x1000u) fy ^= 7u;
    unsigned address = (attr & 0x07ffu) * 32u + fy * 4u + fx / 2u;
    unsigned byte = v->vram[address & 0xffffu];
    unsigned nib = fx & 1u ? byte & 15u : byte >> 4;
    return (uint8_t)(((attr >> 9) & 0x30u) | nib |
                     (nib ? 0x40u : 0u) | ((attr >> 8) & 0x80u));
}

static uint8_t world_pixel(const SegaCDWorldMap *world, const GVDP *v,
                            SegaCDWorldPlane plane, int x, int y, int *valid)
{
    uint16_t attr = 0;
    /* The foreground has authored horizontal ends. Vertical addressing keeps
     * the original game's 0x800-pixel wrap, including objects crossing it. */
    if (plane == SEGACD_WORLD_FOREGROUND &&
        (x < 0 || x >= (int)world->plane_width[0] * 256)) return 0;
    SegaCDWorldResult result = segacd_world_pattern_at_native(world, plane,
                                                              x, y, &attr);
    if (result == SEGACD_WORLD_INVALID) *valid=0;
    if (result != SEGACD_WORLD_OK && result != SEGACD_WORLD_EMPTY) return 0;
    return pattern_pixel(v, attr, x, y);
}

static void sprites(const SegaCDRender *r, const GVDP *v, unsigned line)
{
    memset(r->sprite_line, 0, r->width);
    int margin = ((int)r->width - 320) / 2;
    for (size_t i = 0; i < r->scene->piece_count; ++i) {
        const SegaCDScenePiece *p = &r->scene->pieces[i];
        int w = (((p->size >> 2) & 3) + 1) * 8;
        int h = ((p->size & 3) + 1) * 8;
        int sy = (int)line - p->y;
        if (sy < 0 || sy >= h) continue;
        int left = p->x + (p->hud_slot && p->screen_relative ? 0 : margin);
        int first = left < 0 ? -left : 0;
        int end = (int)r->width - left;
        if (end > w) end = w;
        if (p->tile_attribute & 0x1000u) sy = h - 1 - sy;
        for (int dx = first; dx < end; ++dx) {
            int out = left + dx;
            if (r->sprite_line[out] & 0x40u) continue;
            int sx = p->tile_attribute & 0x0800u ? w - 1 - dx : dx;
            unsigned tile = ((p->tile_attribute & 0x07ffu) +
                              (unsigned)(sx / 8) * (unsigned)(h / 8) +
                              (unsigned)(sy / 8)) & 0x07ffu;
            unsigned address = tile * 32u + ((unsigned)sy & 7u) * 4u +
                                ((unsigned)sx & 7u) / 2u;
            unsigned byte = v->vram[address];
            unsigned nib = sx & 1 ? byte & 15u : byte >> 4;
            if (nib) r->sprite_line[out] = (uint8_t)(0x40u | nib |
                ((p->tile_attribute >> 9) & 0x30u) |
                ((p->tile_attribute >> 8) & 0x80u));
        }
    }
}

int segacd_render_scanline(const SegaCDRender *r, const GVDP *v,
                           unsigned line, uint32_t *argb, size_t pixels)
{
    SegaCDWorldBackgroundBand band;
    if (!r || !v || !argb || !r->world || !r->world->valid ||
        !r->scene || r->scene->status != SEGACD_SCENE_OK ||
        !r->sprite_line || r->width < 320 ||
        r->width > SEGACD_SONIC_CD_MAX_VIEW_WIDTH ||
        r->sprite_line_bytes < r->width || pixels < r->width || line >= 224)
        return 0;
    /* Standard H40, noninterlaced, full-plane vertical scroll. Other original
     * display modes continue through the unmodified native VDP renderer. */
    if (!(v->reg[12] & 1) || (v->reg[12] & 6) || (v->reg[11] & 4) ||
        v->reg[17] || v->reg[18] ||
        segacd_world_background_band_at(r->world, (int32_t)line, &band) !=
            SEGACD_WORLD_OK) return 0;
    int wpx = plane_tiles(v->reg[16] & 3u) * 8;
    int hpx = plane_tiles((v->reg[16] >> 4) & 3u) * 8;
    unsigned hmode = v->reg[11] & 3u;
    unsigned hrow = hmode == 0 ? 0 : hmode == 2 ? line & ~7u : line;
    unsigned hbase = (v->reg[13] & 63u) << 10;
    int hsa = vword(v, hbase + hrow * 4u) & 0x3ff;
    int hsb = vword(v, hbase + hrow * 4u + 2u) & 0x3ff;
    int vya = (int)line + (v->vsram[0] & 0x3ff);
    int vyb = (int)line + (v->vsram[1] & 0x3ff);
    int camera_x = be16(r->world->main_ram + 0x1926);
    int camera_y = be16(r->world->main_ram + 0x192a);
    int world_y_a = lift(vya & (hpx - 1), camera_y + (int)line, hpx);
    int world_y_b = lift(vyb & (hpx - 1), band.camera_y + (int)line, hpx);
    unsigned backdrop = v->reg[7] & 63u;
    int margin = ((int)r->width - 320) / 2;
    int shadow_mode = (v->reg[12] & 8) != 0;
    int valid=1;
    sprites(r, v, line);
    for (unsigned out = 0; out < r->width; ++out) {
        int x = (int)out - margin;
        int xa = lift((x - hsa) & (wpx - 1), camera_x + x, wpx);
        int xb = (x - hsb) & (wpx - 1);
        if (!band.static_x) xb = lift(xb, band.camera_x + x, wpx);
        uint8_t a = world_pixel(r->world, v, SEGACD_WORLD_FOREGROUND,
                                xa, world_y_a,&valid);
        uint8_t b = world_pixel(r->world, v, SEGACD_WORLD_BACKGROUND,
                                xb, world_y_b,&valid);
        uint8_t s = r->sprite_line[out];
        int highlight_operator = shadow_mode && (s & 0x7fu) == 0x7eu;
        int shadow_operator = shadow_mode && (s & 0x7fu) == 0x7fu;
        int sprite_opaque = (s & 0x40u) && !highlight_operator && !shadow_operator;
        unsigned color;
        if (sprite_opaque && (s & 0x80u)) color = s & 63u;
        else if ((a & 0xc0u) == 0xc0u) color = a & 63u;
        else if ((b & 0xc0u) == 0xc0u) color = b & 63u;
        else if (sprite_opaque) color = s & 63u;
        else if (a & 0x40u) color = a & 63u;
        else if (b & 0x40u) color = b & 63u;
        else color = backdrop;
        int shading = GENESIS_DAC_NORMAL;
        if (shadow_mode) {
            int shadow = !((sprite_opaque && (s & 0x80u)) ||
                            (a & 0xc0u) == 0xc0u || (b & 0xc0u) == 0xc0u);
            if (shadow_operator) shadow = 1;
            if (highlight_operator) {
                if (shadow) shadow = 0;
                else shading = GENESIS_DAC_HIGHLIGHT;
            }
            if (shadow) shading = GENESIS_DAC_SHADOW;
        }
        if (!(v->reg[1] & 0x40u)) { color = backdrop; shading = GENESIS_DAC_NORMAL; }
        argb[out] = genesis_dac_cram_to_argb(v->cram[color], shading);
    }
    return valid;
}
