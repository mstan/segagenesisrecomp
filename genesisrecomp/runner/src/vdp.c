/*
 * vdp.c — Sega Genesis VDP implementation.
 *
 * Implements: VRAM/CRAM/VSRAM writes, DMA (68K→VRAM, VRAM fill, VRAM copy),
 * and vdp_render_frame() (Scroll A + Scroll B + sprites, 4bpp tiles, CRAM palette).
 *
 * VDP Register Map (key registers):
 *   $01 Mode 2: display enable (bit6), DMA enable (bit4)
 *   $02 Scroll A nametable base: (bits5-3) × $2000
 *   $03 Window nametable base: varies
 *   $04 Scroll B nametable base: (bits2-0) × $2000
 *   $05 Sprite table base: (bits6-0) × $200
 *   $07 Background color: CRAM index (bits5-0)
 *   $0D H-scroll table base: (bits5-0) × $400
 *   $0F Auto-increment: added to VRAM addr after each data access
 *   $10 Scroll size: bits5-4=height, bits1-0=width (0=32, 1=64, 3=128 cells)
 *   $13 DMA length low
 *   $14 DMA length high
 *   $15 DMA src addr bits[8:1]
 *   $16 DMA src addr bits[16:9]
 *   $17 DMA type (bits7-6) + DMA src addr bits[22:17]
 *       Type: 00/01=68K→VRAM, 10=fill, 11=VRAM copy
 *
 * CRAM color format: 0b----BBBxGGGxRRRx (bits11-9=B, bits7-5=G, bits3-1=R, 3 bits each)
 * Nametable entry: bit15=priority, bits14-13=palette, bit12=vflip, bit11=hflip, bits10-0=tile_idx
 * Tile: 32 bytes, 8 rows × 4 bytes; each byte = two 4bpp pixels (high nibble left, low right)
 */
#include "vdp.h"
#include "genesis_runtime.h"
#include <string.h>
#include <stdio.h>
#include <SDL2/SDL.h>

/* Mutex protecting all VDP state — game thread and VBlank thread both access VDP */
static SDL_mutex *s_vdp_mutex = NULL;
void vdp_lock(void)   { if (s_vdp_mutex) SDL_LockMutex(s_vdp_mutex); }
void vdp_unlock(void) { if (s_vdp_mutex) SDL_UnlockMutex(s_vdp_mutex); }

int g_vram_log_next = 0;  /* set non-zero to log next N writes */

/* ---- VDP state ---- */
uint8_t  g_vdp_vram[VDP_VRAM_SIZE];
uint16_t g_vdp_cram[64];
uint16_t g_vdp_vsram[40];
uint8_t  g_vdp_regs[VDP_REG_COUNT];

static uint16_t s_ctrl_latch   = 0;
static int      s_ctrl_pending = 0;
static uint32_t s_addr         = 0;   /* current VRAM/CRAM/VSRAM address */
static int      s_code         = 0;   /* CD5-CD0 access code */
static uint16_t s_status       = VDP_STATUS_FIFO_EMPTY | VDP_STATUS_VBLANK;
static int      s_fill_pending = 0;   /* 1 = next data write triggers VRAM fill */

/* ---- Init ---- */

void vdp_init(void) {
    if (!s_vdp_mutex) s_vdp_mutex = SDL_CreateMutex();
    memset(g_vdp_vram,  0, sizeof(g_vdp_vram));
    memset(g_vdp_cram,  0, sizeof(g_vdp_cram));
    memset(g_vdp_vsram, 0, sizeof(g_vdp_vsram));
    memset(g_vdp_regs,  0, sizeof(g_vdp_regs));
    g_vdp_regs[15] = 2;  /* default auto-increment */
    s_ctrl_pending = 0;
    s_fill_pending = 0;
    s_status = VDP_STATUS_FIFO_EMPTY | VDP_STATUS_VBLANK;
}

/* ---- CRAM color conversion ---- */
/* Genesis CRAM: 0b0000BBBxGGGxRRRx  (bits 11-9=B, 7-5=G, 3-1=R) */
static uint32_t cram_to_argb(uint16_t cram) {
    unsigned r = (cram >> 1) & 7;
    unsigned g = (cram >> 5) & 7;
    unsigned b = (cram >> 9) & 7;
    /* Scale 0-7 → 0-255: v×36 approximates v/7×255 */
    r = (r << 5) | (r << 2) | (r >> 1);
    g = (g << 5) | (g << 2) | (g >> 1);
    b = (b << 5) | (b << 2) | (b >> 1);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* ---- DMA helpers ---- */

static void vdp_write_target(uint16_t val) {
    int dest = s_code & 0xF;
    uint32_t inc = g_vdp_regs[15];  /* auto-increment; 0 = no advance (valid on hardware) */
    {
        static int vwt_count = 0;
        if (++vwt_count <= 500)
            fprintf(stderr, "[VWT] #%d dest=%d s_code=%02X s_addr=%04X val=%04X inc=%u\n",
                    vwt_count, dest, s_code, s_addr, val, (unsigned)inc);
    }
    switch (dest) {
    case 0x1: /* VRAM write — big-endian word, wraps at 64KB boundary */
        {
            static int vram_tile_count = 0;
            if (s_addr < 0xF800 && ++vram_tile_count <= 100)
                fprintf(stderr, "[VRAM_TILE] #%d addr=%04X val=%04X inc=%u\n",
                        vram_tile_count, s_addr, val, (unsigned)g_vdp_regs[15]);
        }
        g_vdp_vram[ s_addr         & 0xFFFF] = (uint8_t)(val >> 8);
        g_vdp_vram[(s_addr + 1u)   & 0xFFFF] = (uint8_t)(val & 0xFF);
        break;
    case 0x3: /* CRAM write */
        if ((s_addr >> 1) < 64) {
            static int cram_dbg = 0;
            if (cram_dbg < 8) { fprintf(stderr, "[CRAM] addr=%04X val=%04X (entry %d)\n", s_addr, val, s_addr>>1); cram_dbg++; }
            g_vdp_cram[s_addr >> 1] = val;
        }
        break;
    case 0x5: /* VSRAM write */
        if ((s_addr >> 1) < 40) g_vdp_vsram[s_addr >> 1] = val;
        break;
    }
    s_addr = (s_addr + inc) & 0xFFFF;
}

static void vdp_do_dma(void) {
    int    dma_type = (g_vdp_regs[23] >> 6) & 3;
    uint32_t src    = ((uint32_t)(g_vdp_regs[23] & 0x3F) << 17)
                    | ((uint32_t) g_vdp_regs[22]          <<  9)
                    | ((uint32_t) g_vdp_regs[21]          <<  1);
    uint32_t len    = ((uint32_t)g_vdp_regs[20] << 8) | g_vdp_regs[19];
    if (len == 0) len = 0x10000;

    static int dma_dbg = 0;
    if (dma_dbg < 30) { dma_dbg++;
    fprintf(stderr, "[DMA] type=%d src=%06X len=%d dst_code=%02X dst_addr=%04X\n",
            dma_type, src, len, s_code, s_addr);
    }
    static int dma_done_dbg = 0;
    if (dma_type <= 1) {
        /* 68K → VRAM/CRAM/VSRAM */
        for (uint32_t i = 0; i < len; i++) {
            uint16_t word = m68k_read16(src);
            vdp_write_target(word);
            src += 2;
        }
        if (dma_done_dbg++ < 30)
            fprintf(stderr, "[DMA_DONE] src=%06X\n", src);
    } else if (dma_type == 2) {
        /* VRAM fill — triggered by next data write */
        s_fill_pending = 1;
        /* length stored for fill; save it back in case write clears regs */
        /* actual fill happens in vdp_write_data when s_fill_pending */
    } else {
        /* VRAM copy: source is VRAM byte address in regs 21-22 */
        uint32_t vsrc = ((uint32_t)g_vdp_regs[22] << 8) | g_vdp_regs[21];
        uint32_t inc  = g_vdp_regs[15];
        for (uint32_t i = 0; i < len; i++) {
            if (vsrc < VDP_VRAM_SIZE && s_addr < VDP_VRAM_SIZE)
                g_vdp_vram[s_addr] = g_vdp_vram[vsrc];
            vsrc++;
            s_addr = (s_addr + inc) & 0xFFFF;
        }
    }
}

/* ---- Control port ---- */

void vdp_write_ctrl(uint16_t val) {
    vdp_lock();
    /* A register write ($8xxx) always cancels any pending first-word latch,
     * regardless of s_ctrl_pending state. Real hardware confirmed by BlastEm/Exodus. */
    if ((val & 0xC000) == 0x8000) {
        s_ctrl_pending = 0;
        int reg = (val >> 8) & 0x1F;
        if (reg < VDP_REG_COUNT) g_vdp_regs[reg] = (uint8_t)(val & 0xFF);
        vdp_unlock();
        return;
    }
    if (!s_ctrl_pending) {
        s_ctrl_latch   = val;
        s_ctrl_pending = 1;
    } else {
        uint32_t w1 = s_ctrl_latch;
        uint32_t w2 = (uint32_t)val;
        s_code = (int)(((w1 >> 14) & 3) | ((w2 >> 2) & 0x3C));
        s_addr = (uint32_t)((w1 & 0x3FFF) | ((w2 & 3) << 14));
        s_ctrl_pending = 0;
        s_fill_pending = 0;
        {
            static int vram_set_count = 0;
            if ((s_code & 0xF) == 1 && ++vram_set_count <= 20)
                fprintf(stderr, "[VRAM_SET] #%d addr=0x%04X code=0x%02X\n", vram_set_count, s_addr, s_code);
        }

        /* DMA trigger: CD5 set */
        if (s_code & 0x20) {
            vdp_do_dma();
        }
    }
    vdp_unlock();
}

uint16_t vdp_read_status(void) {
    vdp_lock();
    /* Reading status clears the control-word latch (hardware confirmed) */
    s_ctrl_pending = 0;
    uint16_t r = s_status;
    vdp_unlock();
    return r;
}

/* Set or clear the VBLANK status bit to reflect actual display state.
 * Call with active=1 before the VBlank handler runs, active=0 when
 * active display resumes.  Do NOT toggle — games poll this in tight loops. */
void vdp_set_vblank(int active) {
    vdp_lock();
    if (active)
        s_status |=  VDP_STATUS_VBLANK;
    else
        s_status &= (uint16_t)~VDP_STATUS_VBLANK;
    vdp_unlock();
}

/* Set or clear the HBLANK status bit. Set at the start of each HBlank period,
 * cleared before the handler returns so active-display reads see 0. */
void vdp_set_hblank(int active) {
    vdp_lock();
    if (active)
        s_status |=  VDP_STATUS_HBLANK;
    else
        s_status &= (uint16_t)~VDP_STATUS_HBLANK;
    vdp_unlock();
}

/* ---- Data port ---- */

void vdp_write_data(uint16_t val) {
    vdp_lock();
    /* Any data port access clears the first/second word control latch (real hardware behavior) */
    s_ctrl_pending = 0;
    if (s_fill_pending) {
        /* VRAM fill — matches GPGX vdp_dma_fill() exactly:
         * Fill byte = HIGH byte of trigger word, written to (addr ^ 1) each iteration.
         * Verified against gpgx_vdp_ctrl.c:3214-3234. */
        uint32_t len = ((uint32_t)g_vdp_regs[20] << 8) | g_vdp_regs[19];
        if (len == 0) len = 0x10000;
        uint32_t inc = g_vdp_regs[15];
        uint8_t fill_byte = (uint8_t)(val >> 8);   /* HIGH byte — not low */
        fprintf(stderr, "[FILL] triggered by data=0x%04X fill_byte=0x%02X at addr=0x%04X len=%d\n",
                val, fill_byte, s_addr, (int)len);
        for (uint32_t i = 0; i < len; i++) {
            g_vdp_vram[(s_addr ^ 1) & 0xFFFF] = fill_byte;  /* to addr^1 each iteration */
            s_addr = (s_addr + inc) & 0xFFFF;
        }
        s_fill_pending = 0;
        vdp_unlock();
        return;
    }
    vdp_write_target(val);
    vdp_unlock();
}

uint16_t vdp_read_data(void) {
    vdp_lock();
    /* Any data port access clears the first/second word control latch */
    s_ctrl_pending = 0;
    uint16_t val = 0;
    int dest = s_code & 0xF;
    uint32_t inc = g_vdp_regs[15];
    switch (dest) {
    case 0x0: /* VRAM read */
        if (s_addr + 1 < VDP_VRAM_SIZE)
            val = (uint16_t)(g_vdp_vram[s_addr] << 8 | g_vdp_vram[s_addr + 1]);
        break;
    case 0x8: /* CRAM read */
        if ((s_addr >> 1) < 64)
            val = g_vdp_cram[s_addr >> 1];
        break;
    case 0x4: /* VSRAM read */
        if ((s_addr >> 1) < 40)
            val = g_vdp_vsram[s_addr >> 1];
        break;
    }
    s_addr = (s_addr + inc) & 0xFFFF;
    vdp_unlock();
    return val;
}

/* =========================================================================
 * vdp_render_frame — priority-correct composite:
 *   low-B → low-A/window → low-sprites → high-B → high-A/window → high-sprites
 * ========================================================================= */

/* Scroll size table: reg16 bits 1-0 / 5-4 */
static int scroll_size_cells(int bits2) {
    switch (bits2 & 3) {
    case 0: return 32;
    case 1: return 64;
    case 3: return 128;
    default: return 32;
    }
}

/* Ensure modulo result is non-negative (C % can return negative for negative operands) */
static int pos_mod(int a, int b) { return ((a % b) + b) % b; }

/* Draw one 8×8 tile at (px, py) into framebuf.
 * Transparent pixels (color index 0) leave framebuf unchanged.
 * Clip is 0 <= x < GENESIS_SCREEN_W, 0 <= y < GENESIS_SCREEN_H. */
static void draw_tile(uint32_t *framebuf, int px, int py,
                      uint16_t nametable_entry, int clip_x, int clip_y) {
    uint32_t tile_idx = nametable_entry & 0x7FF;
    int      palette  = (nametable_entry >> 13) & 3;
    int      hflip    = (nametable_entry >> 11) & 1;
    int      vflip    = (nametable_entry >> 12) & 1;

    uint32_t tile_off = tile_idx * 32;
    if (tile_off + 32 > VDP_VRAM_SIZE) return;

    for (int row = 0; row < 8; row++) {
        int screen_y = py + (vflip ? 7 - row : row);
        if (screen_y < clip_y || screen_y >= GENESIS_SCREEN_H) continue;

        uint32_t row_off = tile_off + (uint32_t)row * 4;
        for (int col = 0; col < 8; col++) {
            int screen_x = px + (hflip ? 7 - col : col);
            if (screen_x < clip_x || screen_x >= GENESIS_SCREEN_W) continue;

            int byte_idx  = col >> 1;
            uint8_t b     = g_vdp_vram[row_off + byte_idx];
            int pixel_val = (col & 1) ? (b & 0xF) : (b >> 4);
            if (pixel_val == 0) continue;  /* transparent — leave framebuf unchanged */

            int cram_idx = palette * 16 + pixel_val;
            framebuf[screen_y * GENESIS_SCREEN_W + screen_x] = cram_to_argb(g_vdp_cram[cram_idx]);
        }
    }
}

/* ---- H-scroll helper ---- */
/* hscroll_mode = reg11[1:0]:  0=full-screen, 2=per-cell, 3=per-line
 * Returns hscroll_a and hscroll_b for the given cell row cy (0-based).
 * The h-scroll table layout: 4 bytes per line (word A, word B), GENESIS_SCREEN_H entries.
 * Per-cell: only lines cy*8 are used (first line of each 8-line strip).
 * Per-line: each scanline has its own entry (we approximate using the first line of cy). */
static void get_hscroll_for_row(uint32_t hscroll_base, int hscroll_mode, int cy,
                                  int *hs_a, int *hs_b) {
    uint32_t off = hscroll_base;
    switch (hscroll_mode & 3) {
    case 2: off += (uint32_t)(cy * 8) * 4; break;  /* per-cell: 8 lines per strip */
    case 3: off += (uint32_t)(cy * 8) * 4; break;  /* per-line: approx with first scanline */
    default: break;                                   /* full-screen: always offset 0 */
    }
    if (off + 3 < VDP_VRAM_SIZE) {
        int16_t ha = (int16_t)((uint16_t)(g_vdp_vram[off]   << 8 | g_vdp_vram[off+1]));
        int16_t hb = (int16_t)((uint16_t)(g_vdp_vram[off+2] << 8 | g_vdp_vram[off+3]));
        /* Hardware: negative scroll word = scroll right. Negate to get "offset into plane". */
        *hs_a = -(int)ha;
        *hs_b = -(int)hb;
    } else {
        *hs_a = 0;
        *hs_b = 0;
    }
}

/* ---- Plane renderer (one priority pass, window-region-aware) ----
 * Renders tiles whose priority bit matches priority_filter.
 * For plane A (is_plane_a=1), cells in win_cells[] are skipped (window plane takes over).
 * win_cells: 40×28 uint8 array where 1 = this screen cell is in the window region.
 * vscroll_col[cx]: per-2-column vscroll value; pass same value to all for full-screen mode. */
static void render_plane_pass(uint32_t *framebuf, uint32_t nametable_base,
                               int plane_w, int plane_h,
                               uint32_t hscroll_base, int hscroll_mode,
                               const int16_t *vsram, int vscroll_mode,
                               int priority_filter,
                               int is_plane_a,
                               const uint8_t *win_cells) {
    int cells_x = (GENESIS_SCREEN_W / 8) + 2;
    int cells_y = (GENESIS_SCREEN_H / 8) + 2;

    for (int cy = 0; cy < cells_y; cy++) {
        int hs_a, hs_b;
        get_hscroll_for_row(hscroll_base, hscroll_mode, cy > 0 ? cy - 1 : 0, &hs_a, &hs_b);
        int hscroll = is_plane_a ? hs_a : hs_b;
        int fine_x  = pos_mod(hscroll, 8);

        for (int cx = 0; cx < cells_x; cx++) {
            /* Screen cell index (may be -1 for left overdraw) */
            int scr_cx = cx - 1;
            int scr_cy = cy - 1;

            /* For plane A, skip cells belonging to the window region */
            if (is_plane_a && win_cells && scr_cx >= 0 && scr_cx < 40 &&
                scr_cy >= 0 && scr_cy < 28) {
                if (win_cells[scr_cy * 40 + scr_cx]) continue;
            }

            /* Per-2-column vscroll (reg11 bit2): VSRAM[(cx/2)*2+plane] for plane A/B.
             * Full-screen (mode 0): use VSRAM[0] for A, VSRAM[1] for B. */
            int vscroll_idx = (vscroll_mode && scr_cx >= 0) ? ((scr_cx / 2) * 2 + (is_plane_a ? 0 : 1)) : (is_plane_a ? 0 : 1);
            if (vscroll_idx >= 40) vscroll_idx = is_plane_a ? 0 : 1;
            int vscroll = (int)vsram[vscroll_idx];

            int src_cx = pos_mod(cx * 8 - hscroll, plane_w * 8) / 8;
            int src_cy = pos_mod(cy * 8 + vscroll,  plane_h * 8) / 8;

            uint32_t entry_off = nametable_base + (uint32_t)(src_cy * plane_w + src_cx) * 2;
            if (entry_off + 1 >= VDP_VRAM_SIZE) continue;
            uint16_t entry = (uint16_t)(g_vdp_vram[entry_off] << 8 | g_vdp_vram[entry_off + 1]);
            /* Note: tile index 0 is NOT skipped — it can contain visible pixels. */
            if (((entry >> 15) & 1) != priority_filter) continue;

            int fine_y = pos_mod(vscroll, 8);
            int px = cx * 8 + fine_x - 8;
            int py = cy * 8 - fine_y - 8;
            draw_tile(framebuf, px, py, entry, 0, 0);
        }
    }
}

/* ---- Window plane renderer (one priority pass) ----
 * win_cells: 40×28 map where 1 = this cell is window.
 * win_w: width of window nametable in cells (64 for H40, 32 for H32). */
static void render_window_pass(uint32_t *framebuf, uint32_t win_base, int win_w,
                                int priority_filter, const uint8_t *win_cells) {
    int cells_x = GENESIS_SCREEN_W / 8;  /* 40 */
    int cells_y = GENESIS_SCREEN_H / 8;  /* 28 */
    for (int cy = 0; cy < cells_y; cy++) {
        for (int cx = 0; cx < cells_x; cx++) {
            if (!win_cells[cy * 40 + cx]) continue;
            uint32_t entry_off = win_base + (uint32_t)(cy * win_w + cx) * 2;
            if (entry_off + 1 >= VDP_VRAM_SIZE) continue;
            uint16_t entry = (uint16_t)(g_vdp_vram[entry_off] << 8 | g_vdp_vram[entry_off + 1]);
            if (((entry >> 15) & 1) != priority_filter) continue;
            draw_tile(framebuf, cx * 8, cy * 8, entry, 0, 0);
        }
    }
}

/* ---- Sprite renderer (one priority pass) ----
 * Multi-cell sprites: tile index increments column-major (left-to-right columns,
 * top-to-bottom within each column). hflip/vflip mirrors the draw position of cells
 * while the tile content itself is flipped inside draw_tile. */
static void render_sprites_pass(uint32_t *framebuf, uint32_t sat_base, int priority_filter) {
    int s = 0;
    for (int guard = 0; guard < 80; guard++) {
        uint32_t soff = sat_base + (uint32_t)s * 8;
        if (soff + 7 >= VDP_VRAM_SIZE) break;

        int16_t  sy   = (int16_t)((uint16_t)(g_vdp_vram[soff  ] << 8 | g_vdp_vram[soff+1])) - 128;
        uint8_t  sz   = g_vdp_vram[soff+2];
        uint8_t  link = g_vdp_vram[soff+3] & 0x7F;
        uint16_t attr = (uint16_t)(g_vdp_vram[soff+4] << 8 | g_vdp_vram[soff+5]);
        int16_t  sx   = (int16_t)((uint16_t)(g_vdp_vram[soff+6] << 8 | g_vdp_vram[soff+7])) - 128;

        int w_cells      = 1 + ((sz >> 2) & 3);
        int h_cells      = 1 + (sz & 3);
        uint32_t tile_start = attr & 0x7FF;
        int sprite_prio  = (attr >> 15) & 1;
        int hflip        = (attr >> 11) & 1;
        int vflip        = (attr >> 12) & 1;

        if (sprite_prio == priority_filter &&
            (int)sy > -(h_cells * 8) && (int)sy < GENESIS_SCREEN_H &&
            (int)sx > -(w_cells * 8) && (int)sx < GENESIS_SCREEN_W) {

            /* Column-major tile traversal; flip mirrors the draw position.
             * attr & 0xF800 preserves: priority(15), palette(14:13), vflip(12), hflip(11).
             * The individual tile's own flip bits tell draw_tile how to flip the 8×8 cell. */
            for (int tc = 0; tc < w_cells * h_cells; tc++) {
                int cell_col = tc / h_cells;
                int cell_row = tc % h_cells;
                /* Mirror the destination cell for multi-cell flip */
                int draw_col = hflip ? (w_cells - 1 - cell_col) : cell_col;
                int draw_row = vflip ? (h_cells - 1 - cell_row) : cell_row;
                uint16_t cell_entry = (uint16_t)((attr & 0xF800) |
                                      (uint16_t)((tile_start + tc) & 0x7FF));
                draw_tile(framebuf,
                          (int)sx + draw_col * 8,
                          (int)sy + draw_row * 8,
                          cell_entry, 0, 0);
            }
        }

        if (link == 0) break;
        s = link;
    }
}

void vdp_render_frame(uint32_t *framebuf) {
    vdp_lock();

    /* Debug: dump VDP state on first few frames */
    static int dbg_count = 0;
    dbg_count++;
    if (dbg_count <= 3 || (dbg_count % 30 == 0)) {
        int nz_tile = 0;
        for (int i = 0; i < 0xF800; i++) if (g_vdp_vram[i]) nz_tile++;
        fprintf(stderr, "[VDP] frame%d CRAM[0]=%04X nz_tile=%d VRAM[0..7]:",
                dbg_count, g_vdp_cram[0], nz_tile);
        for (int i = 0; i < 8; i++) fprintf(stderr, " %02X", g_vdp_vram[i]);
        fprintf(stderr, "\n");
    }

    /* Background color from CRAM[reg7 & 0x3F] */
    int bg_idx = g_vdp_regs[7] & 0x3F;
    uint32_t bg_color = cram_to_argb(g_vdp_cram[bg_idx]);
    for (int i = 0; i < GENESIS_SCREEN_W * GENESIS_SCREEN_H; i++)
        framebuf[i] = bg_color;

    /* Display enable: reg1 bit6 */
    if (!(g_vdp_regs[1] & 0x40)) { vdp_unlock(); return; }

    /* Plane scroll dimensions */
    int plane_w = scroll_size_cells(g_vdp_regs[16] & 3);
    int plane_h = scroll_size_cells((g_vdp_regs[16] >> 4) & 3);

    /* Nametable bases */
    uint32_t base_a = (uint32_t)((g_vdp_regs[2] >> 3) & 7) << 13;
    uint32_t base_b = (uint32_t)(g_vdp_regs[4] & 7) << 13;
    uint32_t sat_base = (uint32_t)(g_vdp_regs[5] & 0x7F) << 9;

    /* H40 vs H32 mode (reg12 bit0) */
    int h40 = (g_vdp_regs[12] & 1);

    /* Window nametable base: H40 = (reg3 << 10) & 0xF000, H32 = (reg3 << 10) & 0xF800 */
    uint32_t base_w = h40 ? ((uint32_t)(g_vdp_regs[3] << 10) & 0xF000u)
                           : ((uint32_t)(g_vdp_regs[3] << 10) & 0xF800u);
    int win_w = h40 ? 64 : 32;

    /* H-scroll table */
    uint32_t hscroll_base = (uint32_t)(g_vdp_regs[13] & 0x3F) << 10;
    int hscroll_mode = g_vdp_regs[11] & 3;  /* reg11[1:0]: 0=full, 2=per-cell, 3=per-line */

    /* V-scroll mode: reg11 bit2 (0=full-screen, 1=per-2-column) */
    int vscroll_mode = (g_vdp_regs[11] >> 2) & 1;

    /* Window region: reg17 (H boundary), reg18 (V boundary).
     * reg17: bit7=RGTSCROLL (1=window on right), bits[4:0]=column boundary (units of 2 cells).
     * reg18: bit7=DWNSCROLL (1=window on bottom), bits[4:0]=row boundary (cells).
     * A screen cell is in the window if v_match OR h_match (both must be 0 for pure Scroll A). */
    int win_right = (g_vdp_regs[17] >> 7) & 1;
    int win_col   = (g_vdp_regs[17] & 0x1F) * 2;   /* boundary in cells */
    int win_down  = (g_vdp_regs[18] >> 7) & 1;
    int win_row   = (g_vdp_regs[18] & 0x1F);         /* boundary in cells */

    /* Build 40×28 window cell map */
    uint8_t win_cells[40 * 28];
    {
        int scr_w = h40 ? 40 : 32;
        int scr_h = GENESIS_SCREEN_H / 8;   /* 28 */
        for (int cy = 0; cy < scr_h; cy++) {
            int v_match = win_down ? (cy >= win_row) : (cy < win_row);
            for (int cx = 0; cx < 40; cx++) {
                int in_screen = (cx < scr_w);
                int h_match   = win_right ? (cx >= win_col) : (cx < win_col);
                win_cells[cy * 40 + cx] = (in_screen && (v_match || h_match)) ? 1 : 0;
            }
        }
        /* Disable window entirely if both boundaries are 0 and their flags off */
        if (!win_right && win_col == 0 && !win_down && win_row == 0)
            memset(win_cells, 0, sizeof(win_cells));
    }

    /* Cast VSRAM to int16_t for vscroll reads */
    const int16_t *vsram16 = (const int16_t *)g_vdp_vsram;

    /* 6-pass priority-correct composite (back-to-front, transparent pixels skip):
     *   pass 0 (prio=0): Scroll B → Scroll A + Window → Sprites
     *   pass 1 (prio=1): Scroll B → Scroll A + Window → Sprites  */
    for (int prio = 0; prio <= 1; prio++) {
        render_plane_pass(framebuf, base_b, plane_w, plane_h,
                          hscroll_base, hscroll_mode, vsram16, vscroll_mode,
                          prio, 0 /*plane B*/, NULL);

        render_plane_pass(framebuf, base_a, plane_w, plane_h,
                          hscroll_base, hscroll_mode, vsram16, vscroll_mode,
                          prio, 1 /*plane A*/, win_cells);

        render_window_pass(framebuf, base_w, win_w, prio, win_cells);

        render_sprites_pass(framebuf, sat_base, prio);
    }

    vdp_unlock();
}
