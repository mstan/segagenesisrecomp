/*
 * genesis_vdp.h — clean-room Sega Genesis / Mega Drive VDP (315-5313).
 *
 * AGPL-free runtime initiative, teardown item #4 (see LICENSING.md). This is an
 * ORIGINAL implementation written from documented hardware behaviour — the VDP
 * register map, the control-port address/code mechanism, DMA semantics, the
 * plane/sprite rendering rules, and H/V interrupt timing are all public
 * hardware facts (Sega's VDP documentation, the genvdp/Charles MacDonald notes,
 * and hardware test ROMs). NO code is copied or translated from any other
 * emulator: not clownmdemu (AGPL), not Genesis Plus GX (non-commercial), not
 * BlastEm/jgenesis (GPL). Permissively-licensed emulators (ares ISC, MAME
 * BSD-3) may be consulted ONLY as a last-resort reference for an obscure
 * hardware detail; their expression is not reproduced here.
 *
 * Output model matches what the runner's display path already consumes
 * (main.c colour_updated_cb / scanline_rendered_cb): per scanline we emit a row
 * of palette INDICES (0..63 + brightness), and CRAM writes raise colour-updated
 * events. The recompiled 68K and superzazu Z80 drive register writes through the
 * bus; this module owns only the VDP.
 */
#ifndef GENESIS_VDP_H
#define GENESIS_VDP_H

#include <stdint.h>
#include <stddef.h>

/* ---- Hardware constants --------------------------------------------------- */
#define GVDP_VRAM_SIZE      0x10000            /* 64 KB byte-addressed         */
#define GVDP_CRAM_ENTRIES   64                 /* 4 palette lines * 16 colours */
#define GVDP_VSRAM_ENTRIES  40                 /* 40 scroll words (16-bit MD)  */
#define GVDP_NUM_REGISTERS  24                 /* $00..$17                     */
#define GVDP_MAX_WIDTH      320                /* H40 active pixels            */
#define GVDP_MAX_HEIGHT     240                /* V30 active lines (PAL)       */
#define GVDP_SPRITES_MAX    80                 /* H40 sprite table entries     */

/* CRAM index space the scanline callback emits: 64 colours * 3 brightnesses
 * (normal / shadow / highlight). The display path maps these to ARGB. */
#define GVDP_PALETTE_NORMAL     0
#define GVDP_PALETTE_SHADOW     64
#define GVDP_PALETTE_HIGHLIGHT  128
#define GVDP_TOTAL_PALETTE      192

/* ---- Callbacks the VDP needs from its host ------------------------------- */
/* DMA 68k->VRAM/CRAM/VSRAM must read source words from the main bus. The host
 * supplies a reader over the 68K address space (word reads). */
typedef uint16_t (*GVDP_BusReadWord)(void *user, uint32_t address_68k);

/* Raised when a CRAM entry changes, so the host can recompute its ARGB cache
 * (mirrors clownmdemu's colour_updated callback). value = 9-bit BGR (0xBGR). */
typedef void (*GVDP_ColourUpdated)(void *user, unsigned index, uint16_t bgr9);

/* ---- VDP state ------------------------------------------------------------ */
typedef struct GVDP {
    /* Memories */
    uint8_t  vram[GVDP_VRAM_SIZE];
    uint16_t cram[GVDP_CRAM_ENTRIES];          /* stored as 9-bit BGR          */
    uint16_t vsram[GVDP_VSRAM_ENTRIES];

    /* Register file ($00..$17). */
    uint8_t  reg[GVDP_NUM_REGISTERS];

    /* Control-port address/code FSM. The control port takes two 16-bit writes;
     * together they form a 5-bit code (read/write target + DMA) and a 16-bit
     * (plus 2 high) address that auto-increments by reg[15] after each access. */
    uint8_t  control_pending;                  /* second-write latch           */
    uint8_t  code;                             /* 6-bit CD0..CD5               */
    uint16_t address;                          /* current VRAM/CRAM/VSRAM addr */
    uint8_t  address_hi;                        /* CD bits / 128KB addr extend  */

    /* Status register bits (assembled on read). */
    uint8_t  in_vblank;
    uint8_t  in_hblank;
    uint8_t  dma_active;
    uint8_t  sprite_overflow;
    uint8_t  sprite_collision;
    uint8_t  vint_pending;                     /* status bit 7 (F flag)        */

    /* Interrupt timing. */
    uint16_t hint_counter;                     /* counts down reg[10] per line */
    uint16_t scanline;                         /* current raster line          */

    /* Pending DMA (fill/copy) state, serviced incrementally. */
    uint8_t  dma_fill_pending;

    /* Host hooks. */
    GVDP_BusReadWord   bus_read;
    void              *bus_read_user;
    GVDP_ColourUpdated colour_updated;
    void              *colour_updated_user;
} GVDP;

/* ---- Lifecycle ------------------------------------------------------------ */
void gvdp_init(GVDP *v);
void gvdp_reset(GVDP *v);

/* ---- 68K port interface ($C00000 data, $C00004 control) ------------------ */
void     gvdp_write_data   (GVDP *v, uint16_t value);
void     gvdp_write_control(GVDP *v, uint16_t value);
uint16_t gvdp_read_data    (GVDP *v);
uint16_t gvdp_read_control (GVDP *v);          /* status register             */

/* H/V counter read ($C00008). */
uint16_t gvdp_read_hv_counter(const GVDP *v);

/* Fetch-and-clear the 68K freeze cycles owed by the last 68K->VDP DMA
 * (hardware freezes the 68K for the whole transfer; fill/copy run in
 * background and don't stall). Kept OUTSIDE the GVDP struct: it is transient
 * (consumed right after the triggering port write) and the save states
 * snapshot GVDP as a raw struct image — a new field would break existing
 * save files. */
uint32_t gvdp_consume_68k_stall(GVDP *v);

/* ---- Geometry / mode queries (from registers) ----------------------------- */
int gvdp_screen_width (const GVDP *v);          /* 256 (H32) or 320 (H40)      */
int gvdp_screen_height(const GVDP *v);          /* 224 (V28) or 240 (V30)      */
int gvdp_display_enabled(const GVDP *v);

/* ---- Rendering ------------------------------------------------------------ */
/* Render one active scanline into `out` as palette indices (see GVDP_PALETTE_*
 * for shadow/highlight offsets). `out` must hold at least gvdp_screen_width()
 * entries. Returns the number of pixels written. */
int gvdp_render_scanline(GVDP *v, int line, uint8_t *out);

/* ---- Per-frame raster timing ---------------------------------------------- */
/* Called by the scheduler at the start of each scanline; advances the H-int
 * counter and sets/clears V-int pending at the vblank boundary. Returns a mask
 * of interrupts that should fire this line. */
enum { GVDP_IRQ_HBLANK = 1, GVDP_IRQ_VBLANK = 2 };
unsigned gvdp_begin_scanline(GVDP *v, int line);

#endif /* GENESIS_VDP_H */
