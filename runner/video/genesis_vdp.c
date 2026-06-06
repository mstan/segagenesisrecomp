/*
 * genesis_vdp.c — clean-room Sega Genesis VDP (315-5313): core + DMA.
 *
 * Original implementation from documented hardware behaviour (see the header for
 * the clean-room statement). This file implements the register file, the
 * control-port two-write address/code state machine, data-port access to
 * VRAM/CRAM/VSRAM with auto-increment, the status register, the HV counter, and
 * the DMA engine (68k->VDP / VRAM fill / VRAM copy). Rendering and full raster
 * timing live in companion units / later increments.
 *
 * References (hardware facts, not code): Sega "Genesis Software Manual" VDP
 * chapter, Charles MacDonald's "genvdp.txt" hardware notes, and VDP test ROMs.
 */
#include "genesis_vdp.h"
#include <string.h>

/* ---- Register accessors (named for readability; all clean-room) ----------- */
#define R(v, n)            ((v)->reg[(n)])
#define REG_MODE1(v)       R(v, 0x00)
#define REG_MODE2(v)       R(v, 0x01)
#define REG_PLANEA_NT(v)   R(v, 0x02)
#define REG_WINDOW_NT(v)   R(v, 0x03)
#define REG_PLANEB_NT(v)   R(v, 0x04)
#define REG_SPRITE_NT(v)   R(v, 0x05)
#define REG_BACKDROP(v)    R(v, 0x07)
#define REG_HINT_RATE(v)   R(v, 0x0A)
#define REG_MODE3(v)       R(v, 0x0B)
#define REG_MODE4(v)       R(v, 0x0C)
#define REG_HSCROLL_NT(v)  R(v, 0x0D)
#define REG_AUTOINC(v)     R(v, 0x0F)
#define REG_PLANE_SIZE(v)  R(v, 0x10)
#define REG_WINDOW_X(v)    R(v, 0x11)
#define REG_WINDOW_Y(v)    R(v, 0x12)
#define REG_DMA_LEN_LO(v)  R(v, 0x13)
#define REG_DMA_LEN_HI(v)  R(v, 0x14)
#define REG_DMA_SRC_LO(v)  R(v, 0x15)
#define REG_DMA_SRC_MID(v) R(v, 0x16)
#define REG_DMA_SRC_HI(v)  R(v, 0x17)

/* Mode-bit helpers. */
#define MODE2_DISPLAY_ON(v) (REG_MODE2(v) & 0x40)
#define MODE2_VINT_ON(v)    (REG_MODE2(v) & 0x20)
#define MODE2_DMA_ON(v)     (REG_MODE2(v) & 0x10)
#define MODE2_V30(v)        (REG_MODE2(v) & 0x08)
#define MODE1_HINT_ON(v)    (REG_MODE1(v) & 0x10)
#define MODE4_H40(v)        (REG_MODE4(v) & 0x01)
#define MODE4_SHI(v)        (REG_MODE4(v) & 0x08)   /* shadow/highlight        */

/* Control-port code field: low nibble selects target+direction. */
enum {
    CODE_VRAM_READ  = 0x0,
    CODE_VRAM_WRITE = 0x1,
    CODE_CRAM_WRITE = 0x3,
    CODE_VSRAM_READ = 0x4,
    CODE_VSRAM_WRITE= 0x5,
    CODE_CRAM_READ  = 0x8
};
#define CODE_DMA_BIT 0x20

/* ---- Lifecycle ------------------------------------------------------------ */
void gvdp_init(GVDP *v)
{
    memset(v, 0, sizeof(*v));
    gvdp_reset(v);
}

void gvdp_reset(GVDP *v)
{
    /* Clear volatile FSM/timing state but keep memories (matches a soft reset
     * leaving VRAM intact). */
    v->control_pending = 0;
    v->code = 0;
    v->address = 0;
    v->address_hi = 0;
    v->in_vblank = v->in_hblank = 0;
    v->dma_active = 0;
    v->sprite_overflow = v->sprite_collision = 0;
    v->vint_pending = 0;
    v->hint_counter = 0;
    v->scanline = 0;
    v->dma_fill_pending = 0;
    /* Sensible auto-increment default; games set it explicitly. */
    if (REG_AUTOINC(v) == 0) REG_AUTOINC(v) = 2;
}

/* ---- Geometry / mode queries ---------------------------------------------- */
int gvdp_screen_width(const GVDP *v)  { return MODE4_H40(v) ? 320 : 256; }
int gvdp_screen_height(const GVDP *v) { return MODE2_V30(v) ? 240 : 224; }
int gvdp_display_enabled(const GVDP *v){ return MODE2_DISPLAY_ON(v) ? 1 : 0; }

/* ---- VRAM/CRAM/VSRAM word access ------------------------------------------ */
/* VRAM word write carries the documented byte-swap on odd addresses. */
static void vram_write_word(GVDP *v, uint16_t addr, uint16_t value)
{
    uint16_t a = addr & 0xFFFE;
    if (addr & 1) { v->vram[a] = (uint8_t)value; v->vram[(uint16_t)(a + 1)] = (uint8_t)(value >> 8); }
    else          { v->vram[a] = (uint8_t)(value >> 8); v->vram[(uint16_t)(a + 1)] = (uint8_t)value; }
}

static uint16_t vram_read_word(const GVDP *v, uint16_t addr)
{
    uint16_t a = addr & 0xFFFE;
    return (uint16_t)((v->vram[a] << 8) | v->vram[(uint16_t)(a + 1)]);
}

static void cram_write(GVDP *v, uint16_t addr, uint16_t value)
{
    unsigned idx = (addr >> 1) & (GVDP_CRAM_ENTRIES - 1);
    uint16_t bgr9 = value & 0x0EEE;     /* 3 bits each B,G,R                   */
    v->cram[idx] = bgr9;
    if (v->colour_updated) v->colour_updated(v->colour_updated_user, idx, bgr9);
}

static void vsram_write(GVDP *v, uint16_t addr, uint16_t value)
{
    unsigned idx = (addr >> 1);
    if (idx < GVDP_VSRAM_ENTRIES) v->vsram[idx] = value & 0x03FF;
}

/* ---- DMA ------------------------------------------------------------------ */
static uint32_t dma_length(const GVDP *v)
{
    uint32_t len = (uint32_t)REG_DMA_LEN_LO(v) | ((uint32_t)REG_DMA_LEN_HI(v) << 8);
    return len ? len : 0x10000;     /* 0 means full 64K transfer             */
}

/* 68K source address (word address << 1). Bit 23 selects fill/copy. */
static uint32_t dma_source_68k(const GVDP *v)
{
    uint32_t s = (uint32_t)REG_DMA_SRC_LO(v)
               | ((uint32_t)REG_DMA_SRC_MID(v) << 8)
               | ((uint32_t)(REG_DMA_SRC_HI(v) & 0x7F) << 16);
    return s << 1;
}

static void dma_store(GVDP *v, uint16_t value)
{
    switch (v->code & 0x0F) {
        case CODE_VRAM_WRITE: vram_write_word(v, v->address, value); break;
        case CODE_CRAM_WRITE: cram_write(v, v->address, value);      break;
        case CODE_VSRAM_WRITE:vsram_write(v, v->address, value);     break;
        default:              vram_write_word(v, v->address, value); break;
    }
    v->address = (uint16_t)(v->address + REG_AUTOINC(v));
}

static void dma_run_68k_to_vdp(GVDP *v)
{
    uint32_t len = dma_length(v);
    uint32_t src = dma_source_68k(v);
    if (!v->bus_read) return;
    while (len--) {
        uint16_t word = v->bus_read(v->bus_read_user, src);
        dma_store(v, word);
        /* Source increments within its 128KB bank (low 17 bits wrap). */
        src = (src & 0xFE0000u) | ((src + 2) & 0x01FFFFu);
    }
}

static void dma_run_vram_copy(GVDP *v)
{
    uint32_t len = dma_length(v);
    uint16_t src = (uint16_t)(((uint32_t)REG_DMA_SRC_LO(v)) | ((uint32_t)REG_DMA_SRC_MID(v) << 8));
    while (len--) {
        v->vram[v->address & 0xFFFF] = v->vram[src & 0xFFFF];
        src = (uint16_t)(src + 1);
        v->address = (uint16_t)(v->address + REG_AUTOINC(v));
    }
}

/* VRAM fill is armed here and completed by the next data-port write, which
 * supplies the fill byte (hardware behaviour). */
static void dma_run_vram_fill_data(GVDP *v, uint16_t value)
{
    uint32_t len = dma_length(v);
    uint8_t fill = (uint8_t)(value >> 8);
    /* The triggering write itself lands normally first. */
    vram_write_word(v, v->address, value);
    v->address = (uint16_t)(v->address + REG_AUTOINC(v));
    while (len--) {
        v->vram[(v->address ^ 1) & 0xFFFF] = fill;
        v->address = (uint16_t)(v->address + REG_AUTOINC(v));
    }
    v->dma_fill_pending = 0;
}

static void maybe_start_dma(GVDP *v)
{
    if (!(v->code & CODE_DMA_BIT) || !MODE2_DMA_ON(v)) return;
    /* reg[23] bits 7-6 select the DMA flavour. */
    uint8_t mode = REG_DMA_SRC_HI(v) & 0xC0;
    if ((mode & 0x80) == 0) {            /* 68k -> VDP                         */
        v->dma_active = 1;
        dma_run_68k_to_vdp(v);
        v->dma_active = 0;
    } else if ((mode & 0x40) == 0) {     /* VRAM fill (waits for data write)   */
        v->dma_fill_pending = 1;
    } else {                             /* VRAM copy                          */
        v->dma_active = 1;
        dma_run_vram_copy(v);
        v->dma_active = 0;
    }
}

/* ---- 68K port interface --------------------------------------------------- */
static void set_register(GVDP *v, unsigned index, uint8_t value)
{
    if (index < GVDP_NUM_REGISTERS) v->reg[index] = value;
}

void gvdp_write_control(GVDP *v, uint16_t value)
{
    if (!v->control_pending) {
        if ((value & 0xC000) == 0x8000) {
            /* Register write: 10rr rrrr dddd dddd */
            set_register(v, (value >> 8) & 0x1F, (uint8_t)value);
            v->code = 0;            /* register writes reset the code         */
            return;
        }
        /* First half of an address/code set. */
        v->address = (uint16_t)((v->address & 0xC000) | (value & 0x3FFF));
        v->code = (uint8_t)((v->code & 0x3C) | ((value >> 14) & 0x03));
        v->control_pending = 1;
    } else {
        /* Second half: A15-A14 in bits 1-0, CD5-CD2 in bits 7-4. */
        v->address = (uint16_t)((v->address & 0x3FFF) | ((value & 0x0003) << 14));
        v->code = (uint8_t)((v->code & 0x03) | ((value >> 2) & 0x3C));
        v->control_pending = 0;
        maybe_start_dma(v);
    }
}

void gvdp_write_data(GVDP *v, uint16_t value)
{
    v->control_pending = 0;
    if (v->dma_fill_pending) { dma_run_vram_fill_data(v, value); return; }

    switch (v->code & 0x0F) {
        case CODE_VRAM_WRITE:  vram_write_word(v, v->address, value); break;
        case CODE_CRAM_WRITE:  cram_write(v, v->address, value);      break;
        case CODE_VSRAM_WRITE: vsram_write(v, v->address, value);     break;
        default:               vram_write_word(v, v->address, value); break;
    }
    v->address = (uint16_t)(v->address + REG_AUTOINC(v));
}

uint16_t gvdp_read_data(GVDP *v)
{
    uint16_t out;
    v->control_pending = 0;
    switch (v->code & 0x0F) {
        case CODE_CRAM_READ:
            out = v->cram[(v->address >> 1) & (GVDP_CRAM_ENTRIES - 1)];
            break;
        case CODE_VSRAM_READ: {
            unsigned idx = (v->address >> 1);
            out = (idx < GVDP_VSRAM_ENTRIES) ? v->vsram[idx] : 0;
            break;
        }
        case CODE_VRAM_READ:
        default:
            out = vram_read_word(v, v->address);
            break;
    }
    v->address = (uint16_t)(v->address + REG_AUTOINC(v));
    return out;
}

uint16_t gvdp_read_control(GVDP *v)
{
    /* Reading the control port resets the write FSM and clears the V-int flag. */
    v->control_pending = 0;
    uint16_t s = 0x3400;                 /* open-bus pattern in the high bits  */
    s |= 0x0200;                         /* FIFO empty                         */
    if (v->vint_pending)     s |= 0x0080;
    if (v->sprite_overflow)  s |= 0x0040;
    if (v->sprite_collision) s |= 0x0020;
    if (v->in_vblank)        s |= 0x0008;
    if (v->in_hblank)        s |= 0x0004;
    if (v->dma_active)       s |= 0x0002;
    v->vint_pending = 0;
    /* Advance a phantom H-blank between status reads so 68K routines that pace
     * on the H-blank flag (e.g. Sonic 1's SEGA-chant DAC feeder, which polls
     * $C00004 bit 2 between samples) make progress under our per-whole-line
     * renderer instead of spinning forever. */
    v->in_hblank ^= 1;
    return s;
}

uint16_t gvdp_read_hv_counter(const GVDP *v)
{
    /* Approximate: V counter in the high byte, H counter low. Refined during
     * raster-timing validation. */
    return (uint16_t)(((v->scanline & 0xFF) << 8) | (v->in_hblank ? 0x00 : 0x80));
}

/* ---- Per-scanline timing -------------------------------------------------- */
unsigned gvdp_begin_scanline(GVDP *v, int line)
{
    unsigned irq = 0;
    int active_h = gvdp_screen_height(v);

    v->scanline = (uint16_t)line;

    if (line < active_h) {
        /* H-int counter: reloads with reg[10] and fires when it underflows. */
        if (v->hint_counter == 0) {
            v->hint_counter = REG_HINT_RATE(v);
            if (MODE1_HINT_ON(v)) irq |= GVDP_IRQ_HBLANK;
        } else {
            v->hint_counter--;
        }
    } else {
        v->hint_counter = REG_HINT_RATE(v);
        if (line == active_h) {        /* entering vblank                     */
            v->in_vblank = 1;
            v->vint_pending = 1;
            if (MODE2_VINT_ON(v)) irq |= GVDP_IRQ_VBLANK;
        }
    }
    if (line == 0) v->in_vblank = 0;
    return irq;
}

/* ---- Rendering ------------------------------------------------------------ */
/* Per-scanline sprite layer, filled by the sprite engine and read by the
 * compositor below. */
static uint8_t s_spr_idx[GVDP_MAX_WIDTH];   /* palette index 0..63            */
static uint8_t s_spr_op [GVDP_MAX_WIDTH];   /* opaque sprite pixel            */
static uint8_t s_spr_hi [GVDP_MAX_WIDTH];   /* sprite priority bit            */
static uint8_t s_spr_shadow_op[GVDP_MAX_WIDTH]; /* operator: force shadow     */
static uint8_t s_spr_hilite_op[GVDP_MAX_WIDTH]; /* operator: highlight        */

/* Sprite engine: evaluate the sprite attribute table in link-list order for
 * `line`, fill the per-pixel sprite layer, and update overflow/collision. In
 * shadow/highlight mode, palette-line-3 colours 14/15 are operator pixels
 * (transparent, they modulate the underlying pixel) rather than drawn colours.
 *
 * Sprite cell ordering is column-major (cells go down a column, then across),
 * the tile layout the Genesis hardware uses. */
static void sprite_render_line(GVDP *v, int line, int w)
{
    memset(s_spr_op, 0, (size_t)w);
    memset(s_spr_shadow_op, 0, (size_t)w);
    memset(s_spr_hilite_op, 0, (size_t)w);

    int h40 = MODE4_H40(v);
    int max_sprites   = h40 ? 80 : 64;
    int max_per_line  = h40 ? 20 : 16;
    int sh_mode       = MODE4_SHI(v) != 0;
    uint16_t sat = (uint16_t)((REG_SPRITE_NT(v) & 0x7F) << 9);

    /* Tracks the first opaque sprite owner per pixel (first-wins; a second
     * opaque hit raises the collision flag). */
    static uint8_t drawn[GVDP_MAX_WIDTH];
    memset(drawn, 0, (size_t)w);

    int link = 0, on_line = 0, pixels_budget = w;

    for (int n = 0; n < max_sprites; n++) {
        uint16_t e = (uint16_t)(sat + link * 8);
        int y      = (vram_read_word(v, e) & 0x3FF) - 128;
        uint8_t sz = v->vram[(uint16_t)(e + 2)];
        int hsz    = ((sz >> 2) & 3) + 1;     /* width  in cells (1..4)        */
        int vsz    = (sz & 3) + 1;            /* height in cells (1..4)        */
        int height = vsz * 8;
        int next   = v->vram[(uint16_t)(e + 3)] & 0x7F;

        if (line >= y && line < y + height) {
            if (++on_line > max_per_line) { v->sprite_overflow = 1; break; }

            uint16_t attr = vram_read_word(v, (uint16_t)(e + 4));
            int xword     = vram_read_word(v, (uint16_t)(e + 6)) & 0x1FF;
            int screen_x  = xword - 128;

            /* Sprite masking: an x==0 sprite after another on-line sprite
             * masks the rest of the line. */
            if (xword == 0 && on_line > 1) break;

            int tile = attr & 0x07FF;
            int pal  = (attr >> 13) & 0x3;
            int pri  = (attr & 0x8000) != 0;
            int hf   = (attr & 0x0800) != 0;
            int vf   = (attr & 0x1000) != 0;
            int width = hsz * 8;

            int iy = vf ? (height - 1 - (line - y)) : (line - y);
            int celly = iy >> 3, fy = iy & 7;

            for (int lx = 0; lx < width; lx++) {
                int sx = screen_x + lx;
                if (sx < 0 || sx >= w) continue;
                int ix = hf ? (width - 1 - lx) : lx;
                int cellx = ix >> 3, fx = ix & 7;
                int tileno = (tile + cellx * vsz + celly) & 0x07FF;
                uint16_t pa = (uint16_t)(tileno * 32 + fy * 4 + (fx >> 1));
                uint8_t byte = v->vram[pa & 0xFFFF];
                int nib = (fx & 1) ? (byte & 0x0F) : (byte >> 4);
                if (nib == 0) continue;            /* transparent             */

                if (drawn[sx]) { v->sprite_collision = 1; continue; } /* first wins */

                if (sh_mode && pal == 3 && nib == 14) {        /* highlight op */
                    s_spr_hilite_op[sx] = 1;
                } else if (sh_mode && pal == 3 && nib == 15) { /* shadow op    */
                    s_spr_shadow_op[sx] = 1;
                } else {
                    s_spr_idx[sx] = (uint8_t)(pal * 16 + nib);
                    s_spr_op[sx]  = 1;
                    s_spr_hi[sx]  = (uint8_t)pri;
                    drawn[sx] = 1;
                }
            }

            pixels_budget -= width;
            if (pixels_budget <= 0) { v->sprite_overflow = 1; break; }
        }

        if (next == 0) break;     /* link 0 terminates the list */
        link = next;
    }
}

/* Plane geometry: tiles wide/high from reg[16] (00=32,01=64,11=128). */
static int plane_tiles(unsigned code2)
{
    switch (code2 & 0x3) { case 1: return 64; case 3: return 128; default: return 32; }
}

/* Fetch one plane pixel at plane-space (px,py). Returns palette index 0..63,
 * opaque flag (pixel != 0), and the tile's priority bit. */
static void fetch_plane_pixel(const GVDP *v, uint16_t base, int wt, int ht,
                              int px, int py, uint8_t *idx, int *opaque, int *hi)
{
    int wpx = wt * 8, hpx = ht * 8;
    px &= (wpx - 1); py &= (hpx - 1);
    uint16_t nt = (uint16_t)(base + (uint16_t)(((py >> 3) * wt + (px >> 3)) * 2));
    uint16_t e  = vram_read_word(v, nt);
    int ti = e & 0x07FF;
    int fx = px & 7, fy = py & 7;
    if (e & 0x0800) fx ^= 7;   /* H flip */
    if (e & 0x1000) fy ^= 7;   /* V flip */
    uint16_t pa = (uint16_t)(ti * 32 + fy * 4 + (fx >> 1));
    uint8_t byte = v->vram[pa & 0xFFFF];
    int nib = (fx & 1) ? (byte & 0x0F) : (byte >> 4);
    *opaque = (nib != 0);
    *hi     = (e & 0x8000) != 0;
    *idx    = (uint8_t)(((e >> 13) & 0x3) * 16 + nib);
}

/* Is screen pixel (x,line) inside the window region? Window X is in 2-cell
 * (16px) units, Y in 1-cell (8px) units; bit7 flips the covered side. */
static int in_window(const GVDP *v, int x, int line)
{
    unsigned wx = REG_WINDOW_X(v), wy = REG_WINDOW_Y(v);
    int cell_x = x >> 3;
    int hx = (wx & 0x1F) * 2;                 /* boundary in cells            */
    int vy = (wy & 0x1F) * 8;                  /* boundary in lines            */
    int h_in = (wx & 0x80) ? (cell_x >= hx) : (cell_x < hx);
    int v_in = (wy & 0x80) ? (line   >= vy) : (line   < vy);
    return h_in || v_in;
}

int gvdp_render_scanline(GVDP *v, int line, uint8_t *out)
{
    int w = gvdp_screen_width(v);
    uint8_t backdrop = (uint8_t)(REG_BACKDROP(v) & 0x3F);

    if (!gvdp_display_enabled(v)) {
        for (int x = 0; x < w; x++) out[x] = backdrop;
        return w;
    }

    /* Plane geometry. */
    int wt = plane_tiles(REG_PLANE_SIZE(v) & 0x3);
    int ht = plane_tiles((REG_PLANE_SIZE(v) >> 4) & 0x3);
    uint16_t base_a = (uint16_t)((REG_PLANEA_NT(v) & 0x38) << 10);
    uint16_t base_b = (uint16_t)((REG_PLANEB_NT(v) & 0x07) << 13);
    int win_wt = MODE4_H40(v) ? 64 : 32;
    uint16_t base_w = MODE4_H40(v) ? (uint16_t)((REG_WINDOW_NT(v) & 0x3E) << 10)
                                   : (uint16_t)((REG_WINDOW_NT(v) & 0x3F) << 10);

    /* Horizontal scroll for this line. */
    uint16_t hbase = (uint16_t)((REG_HSCROLL_NT(v) & 0x3F) << 10);
    int hmode = REG_MODE3(v) & 0x3;
    int hidx  = (hmode == 0) ? 0 : (hmode == 2) ? (line & ~7) : line; /* 3=line */
    int hs_a = vram_read_word(v, (uint16_t)(hbase + hidx * 4 + 0)) & 0x3FF;
    int hs_b = vram_read_word(v, (uint16_t)(hbase + hidx * 4 + 2)) & 0x3FF;

    int vmode_2cell = (REG_MODE3(v) & 0x4) != 0;

    /* Sprite layer for this line. */
    sprite_render_line(v, line, w);

    for (int x = 0; x < w; x++) {
        /* Vertical scroll per plane (full, or per-16px-column 2-cell). */
        int vs_a, vs_b;
        if (vmode_2cell) {
            int col = (x >> 4) * 2;
            vs_a = v->vsram[(col + 0) % GVDP_VSRAM_ENTRIES] & 0x3FF;
            vs_b = v->vsram[(col + 1) % GVDP_VSRAM_ENTRIES] & 0x3FF;
        } else {
            vs_a = v->vsram[0] & 0x3FF;
            vs_b = v->vsram[1] & 0x3FF;
        }

        uint8_t a_idx, b_idx; int a_op, a_hi, b_op, b_hi;

        /* Plane A — or the window plane (unscrolled) where the window covers. */
        if (in_window(v, x, line)) {
            fetch_plane_pixel(v, base_w, win_wt, ht, x, line, &a_idx, &a_op, &a_hi);
        } else {
            fetch_plane_pixel(v, base_a, wt, ht,
                              (x - hs_a), (line + vs_a), &a_idx, &a_op, &a_hi);
        }
        /* Plane B. */
        fetch_plane_pixel(v, base_b, wt, ht,
                          (x - hs_b), (line + vs_b), &b_idx, &b_op, &b_hi);

        /* Sprite layer. */
        int s_op = s_spr_op[x], s_hi = s_spr_hi[x];
        uint8_t s_idx = s_spr_idx[x];

        /* Genesis priority order: S-hi > A-hi > B-hi > S-lo > A-lo > B-lo > bg. */
        uint8_t px;
        if      (s_op && s_hi) px = s_idx;
        else if (a_op && a_hi) px = a_idx;
        else if (b_op && b_hi) px = b_idx;
        else if (s_op)         px = s_idx;
        else if (a_op)         px = a_idx;
        else if (b_op)         px = b_idx;
        else                   px = backdrop;

        /* Shadow/highlight: when enabled, a pixel with no high-priority layer
         * is shadowed; operator sprites (pal3 colours 14/15) force highlight or
         * shadow on the underlying pixel. */
        if (MODE4_SHI(v)) {
            int hi_present = (s_op && s_hi) || (a_op && a_hi) || (b_op && b_hi);
            int sh = !hi_present;
            int hl = 0;
            if (s_spr_shadow_op[x]) sh = 1;
            if (s_spr_hilite_op[x]) { if (sh) sh = 0; else hl = 1; }
            if (hl)      px += GVDP_PALETTE_HIGHLIGHT;
            else if (sh) px += GVDP_PALETTE_SHADOW;
        }

        out[x] = px;
    }
    return w;
}
