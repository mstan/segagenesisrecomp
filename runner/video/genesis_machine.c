/*
 * genesis_machine.c — clean-room own-backend scheduler (see genesis_machine.h).
 * Per-scanline frame loop driving the recompiled 68K (glue fiber), the superzazu
 * Z80, and our VDP, delivering V/H interrupts. Original implementation.
 */
#include "genesis_machine.h"
#include <string.h>

GenesisMachine g_machine;

/* glue.c hooks (recompiled-68K fiber + own-backend interrupt delivery). */
extern void glue_run_game_chunk(uint32_t cycles);
extern void glue_own_interrupt(int level, GVDP *vdp);

/* ARGB palette cache (normal/shadow/highlight), fed by VDP CRAM writes. */
static uint32_t s_cram_argb[GVDP_TOTAL_PALETTE];

static int clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
static uint32_t bgr9_to_argb(uint16_t c, int lvl)
{
    int r = (c >> 1) & 7, g = (c >> 5) & 7, b = (c >> 9) & 7, scale, off;
    if      (lvl == 1) { scale = 18; off = 0;   }
    else if (lvl == 2) { scale = 18; off = 130; }
    else               { scale = 36; off = 0;   }
    return 0xFF000000u
         | ((uint32_t)clamp8(off + r * scale) << 16)
         | ((uint32_t)clamp8(off + g * scale) << 8)
         |  (uint32_t)clamp8(off + b * scale);
}
static void colour_cb(void *u, unsigned idx, uint16_t bgr9)
{
    (void)u;
    if (idx >= GVDP_CRAM_ENTRIES) return;
    s_cram_argb[idx]                          = bgr9_to_argb(bgr9, 0);
    s_cram_argb[idx + GVDP_PALETTE_SHADOW]    = bgr9_to_argb(bgr9, 1);
    s_cram_argb[idx + GVDP_PALETTE_HIGHLIGHT] = bgr9_to_argb(bgr9, 2);
}

/* VDP DMA source reads come from our own bus. */
static uint16_t vdp_bus_read(void *u, uint32_t a) { (void)u; return gbus_read16(&g_machine.bus, a); }

/* Z80 core memory callbacks -> Z80-side bus. */
static cc_u16f z80_read (void *u, cc_u16f a)            { return gbus_z80_read((GenesisBus *)u, (uint16_t)a); }
static void    z80_write(void *u, cc_u16f a, cc_u16f v) { gbus_z80_write((GenesisBus *)u, (uint16_t)a, (uint8_t)v); }

void machine_init(void)
{
    memset(&g_machine, 0, sizeof(g_machine));
    gvdp_init(&g_machine.vdp);
    g_machine.vdp.bus_read       = vdp_bus_read;
    g_machine.vdp.colour_updated = colour_cb;
    gbus_init(&g_machine.bus, &g_machine.vdp);
    ClownZ80_Constant_Initialise();
    ClownZ80_State_Initialise(&g_machine.z80);
    g_machine.z80_cb.read      = z80_read;
    g_machine.z80_cb.write     = z80_write;
    g_machine.z80_cb.user_data = &g_machine.bus;
}

void machine_set_pad(int port, uint8_t buttons)
{
    if (port >= 0 && port < 2) g_machine.bus.pad[port] = buttons;
}

/* NTSC raster timing. */
#define LINES_TOTAL     262
#define MASTER_PER_LINE 3420u
#define M68K_PER_LINE   488u
#define Z80_PER_LINE    228u

static void step_z80(GenesisMachine *m, uint32_t target)
{
    if (!m->bus.z80_reset_off || m->bus.z80_busreq) return;   /* Z80 halted */
    uint32_t done = m->z80_cycle_debt;
    while (done < target)
        done += ClownZ80_DoInstruction(&m->z80, &m->z80_cb);
    m->z80_cycle_debt = done - target;
}

void machine_run_frame(GenesisScanlineSink sink, void *user)
{
    GenesisMachine *m = &g_machine;
    int active_h = gvdp_screen_height(&m->vdp);
    static uint8_t  idxbuf[GVDP_MAX_WIDTH];
    static uint32_t rowbuf[GVDP_MAX_WIDTH];

    for (int line = 0; line < LINES_TOTAL; line++) {
        unsigned irq = gvdp_begin_scanline(&m->vdp, line);

        /* Advance the recompiled 68K ~one scanline (it parks at WaitForVBlank). */
        glue_run_game_chunk(M68K_PER_LINE);

        /* Step the Z80 (SMPS driver) for its share of the line. */
        step_z80(m, Z80_PER_LINE);

        /* Deliver interrupts to the 68K (and the Z80 at vblank). */
        if (irq & GVDP_IRQ_HBLANK) glue_own_interrupt(4, &m->vdp);
        if (irq & GVDP_IRQ_VBLANK) {
            ClownZ80_Interrupt(&m->z80, 1);
            glue_own_interrupt(6, &m->vdp);
            ClownZ80_Interrupt(&m->z80, 0);
        }

        /* Render + emit active scanlines. */
        if (line < active_h && sink) {
            int n = gvdp_render_scanline(&m->vdp, line, idxbuf);
            for (int x = 0; x < n; x++) rowbuf[x] = s_cram_argb[idxbuf[x]];
            sink(user, line, rowbuf, n);
        }

        m->master_cycle += MASTER_PER_LINE;
    }
}
