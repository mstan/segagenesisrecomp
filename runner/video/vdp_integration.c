/*
 * vdp_integration.c — splice the clean-room genesis_vdp into the runner.
 * See vdp_integration.h. Phase-1 shadow renderer: mirrored writes + scanline
 * substitution; clownmdemu still owns timing for now.
 */
#include "genesis_vdp.h"
#include "vdp_integration.h"
#include "genesis_dac.h"  /* authentic Genesis output-DAC color ladder */
#include <string.h>

/* Authoritative DMA-source word read, provided by glue.c (ROM + live work
 * RAM). g_ram is only a shadow, so we must not read it directly. */
extern uint16_t glue_bus_read_word(uint32_t addr);

static GVDP     g_gvdp;
static uint32_t g_cram_argb[GVDP_TOTAL_PALETTE];   /* 192 entries (N/SH/HL)   */
static int      g_inited;

/* Genesis 9-bit BGR -> ARGB8888 at one of three brightness levels
 * (0 normal, 1 shadow, 2 highlight) via the authentic nonlinear Genesis DAC
 * ladder (genesis_dac.h), matching BlastEm. */
static uint32_t bgr9_to_argb(uint16_t c, int level)
{
    return genesis_dac_cram_to_argb(c, level);
}

static void colour_cb(void *user, unsigned index, uint16_t bgr9)
{
    (void)user;
    if (index >= GVDP_CRAM_ENTRIES) return;
    g_cram_argb[index]                          = bgr9_to_argb(bgr9, 0);
    g_cram_argb[index + GVDP_PALETTE_SHADOW]    = bgr9_to_argb(bgr9, 1);
    g_cram_argb[index + GVDP_PALETTE_HIGHLIGHT] = bgr9_to_argb(bgr9, 2);
}

/* DMA source reader — delegate to glue's authoritative bus. */
static uint16_t bus_read_cb(void *user, uint32_t a)
{
    (void)user;
    return glue_bus_read_word(a);
}

void gvdp_integration_init(void)
{
    if (g_inited) return;
    gvdp_init(&g_gvdp);
    g_gvdp.bus_read           = bus_read_cb;
    g_gvdp.bus_read_user      = NULL;
    g_gvdp.colour_updated     = colour_cb;
    g_gvdp.colour_updated_user= NULL;
    g_inited = 1;
}

void gvdp_on_bus_write(uint32_t byte_addr, uint16_t value)
{
    if (!g_inited) gvdp_integration_init();
    byte_addr &= 0xFFFFFFu;
    if (byte_addr >= 0xC00000u && byte_addr <= 0xC00007u) {
        if (byte_addr < 0xC00004u) gvdp_write_data(&g_gvdp, value);
        else                       gvdp_write_control(&g_gvdp, value);
    }
}

int gvdp_render_substitute(int line, uint32_t *row, int max_w)
{
    static uint8_t buf[GVDP_MAX_WIDTH];
    if (!g_inited) return 0;
    int w = gvdp_render_scanline(&g_gvdp, line, buf);
    if (w > max_w) w = max_w;
    for (int x = 0; x < w; x++) row[x] = g_cram_argb[buf[x]];
    return w;
}
