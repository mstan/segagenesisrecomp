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
#define GVDP_MAX_WIDTH      512                /* H40 320 + up to 96px widescreen margin/side */
#define GVDP_MAX_HEIGHT     240                /* V30 active lines (PAL)       */
#define GVDP_SPRITES_MAX    80                 /* H40 sprite table entries     */

/* CRAM index space the scanline callback emits: 64 colours * 3 brightnesses
 * (normal / shadow / highlight). The display path maps these to ARGB. */
#define GVDP_PALETTE_NORMAL     0
#define GVDP_PALETTE_SHADOW     64
#define GVDP_PALETTE_HIGHLIGHT  128
#define GVDP_TOTAL_PALETTE      192

/* Widescreen pillarbox bar sentinel: a synthetic index one past the real
 * palette that the display path maps to a fixed bar colour (opaque black).
 * Emitted by gvdp_render_scanline for pillarbox columns ONLY when black-bar
 * mode is selected; backdrop-bar mode emits the real backdrop index instead,
 * so this stays branch-free in the index->ARGB conversion (the host sizes its
 * ARGB cache to GVDP_TOTAL_PALETTE+1 and seeds this slot with black). */
#define GVDP_WS_BAR_INDEX       GVDP_TOTAL_PALETTE

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

/* ---- Always-on VDP event ring ---------------------------------------------
 * Port-level flight recorder: every register write, completed control
 * command, DMA execution/decline, and direct data-port write. Always
 * compiled in (Release too); probes query backward via the debug server
 * (cmd `vdp_events`) — never arm-then-record. Ring storage is file-static
 * in genesis_vdp.c (exposed via these globals) so the GVDP struct and the
 * raw save-state image keep their layout. */
enum {
    GVDP_EVT_REG          = 1,   /* code=reg index, value=reg value           */
    GVDP_EVT_CMD          = 2,   /* completed control pair: code/addr/inc     */
    GVDP_EVT_DATA         = 3,   /* data-port write: code/addr/value/inc      */
    GVDP_EVT_DMA_68K      = 4,   /* executed 68k->VDP DMA: addr/inc/len/src   */
    GVDP_EVT_DMA_FILL_ARM = 5,   /* VRAM fill armed                           */
    GVDP_EVT_DMA_COPY     = 6,   /* executed VRAM copy                        */
    GVDP_EVT_DMA_SKIP     = 7    /* DMA-bit command declined (see reason)     */
};
enum {
    GVDP_SKIP_DMA_OFF      = 1,  /* reg1 M1 (DMA enable) clear                */
    GVDP_SKIP_NO_BUSREAD   = 2,  /* no bus_read hook wired                    */
    GVDP_SKIP_FILL_TRIGGER = 3   /* DATA event doubled as a fill trigger      */
};
typedef struct {
    uint32_t seq;
    uint8_t  kind;               /* GVDP_EVT_*                                */
    uint8_t  code;               /* VDP code bits (or reg index for REG)      */
    uint8_t  reason;             /* GVDP_SKIP_* where applicable              */
    uint8_t  in_vblank;
    uint16_t scanline;
    uint16_t addr;               /* VDP address at the event                  */
    uint16_t value;              /* data word / reg value / cmd second half   */
    uint16_t inc;                /* auto-increment (reg 15) at the event      */
    uint16_t len;                /* DMA length in words                       */
    uint32_t src;                /* DMA source (68K byte address)             */
} GVdpEvent;
#define GVDP_EVENT_CAP 16384
extern GVdpEvent g_gvdp_events[GVDP_EVENT_CAP];
extern uint32_t  g_gvdp_event_seq;

/* ---- Geometry / mode queries (from registers) ----------------------------- */
int gvdp_screen_width (const GVDP *v);          /* 256 (H32) or 320 (H40)      */
int gvdp_screen_height(const GVDP *v);          /* 224 (V28) or 240 (V30) — RASTER lines */

/* ---- Widescreen (16:9) opt-in --------------------------------------------- */
/* Set the per-side extra pixels rendered around the authentic active window.
 * 0 (default) = authentic 4:3; the value is clamped to the output buffer.
 * NOT part of VDP state (excluded from raw-struct save images). */
void gvdp_set_ws_extra(int extra_px);
/* Arm a FIXED 16:9 output canvas (in pixels) used for EVERY frame while
 * widescreen is on, decoupled from the per-frame content margin set by
 * gvdp_set_ws_extra(). Gameplay frames fill the canvas with widened content;
 * non-gameplay frames (menus/title/intro) center the authentic 320/256 view
 * and pillarbox the sides, so the output width — and thus the window — never
 * changes. 0 (default) disarms: output reverts to screen_width + 2*extra.
 * NOT part of VDP state (excluded from raw-struct save images). */
void gvdp_set_ws_canvas(int canvas_w);
/* Pillarbox bar colour: 0 = per-scanline backdrop colour (seamless, looks like
 * one wider background); 1 = opaque black (classic letterbox). The host picks
 * the user-facing default (see GENESIS_WS_BARS in main.c). */
void gvdp_set_ws_bar_black(int black);
/* Active output width including widescreen margins (the armed canvas when set,
 * else screen_width + 2*extra). */
int  gvdp_active_width(const GVDP *v);
/* Diagnostic: force Plane B hscroll constant (line 0) + mark its wrap column. */
void gvdp_set_bgdiag(int on);
int gvdp_display_enabled(const GVDP *v);
int gvdp_interlace_double(const GVDP *v);       /* 1 = interlace mode 2        */
int gvdp_output_height(const GVDP *v);          /* raster height x2 in IM2     */

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
