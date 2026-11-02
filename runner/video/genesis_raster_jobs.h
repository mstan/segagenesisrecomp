#ifndef GENESIS_RASTER_JOBS_H
#define GENESIS_RASTER_JOBS_H
#include <stdint.h>
#include "genesis_vdp.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Native-density immutable jobs. Pattern sources are the original four packed
 * bytes of an eight-pixel tile row, interned by CONTENT rather than guest
 * address. Every writer, DMA, opaque direct store and state restore therefore
 * takes effect at the same native fetch; no stale address-generation cache is
 * consulted. No enhanced pixels or scale-sized CPU allocations exist here. */
#define GVDP_HD_ROWS (GVDP_MAX_HEIGHT * 2)
#define GVDP_HD_PATTERNS 65536u
#define GVDP_HD_PALETTES 1024u
#define GVDP_HD_ROW_SPANS (GVDP_MAX_WIDTH / 2 + GVDP_SPRITES_MAX * 4 + 8)
#define GVDP_HD_SPANS (GVDP_HD_ROWS * GVDP_HD_ROW_SPANS)
#define GVDP_HD_OP_WORDS (GVDP_HD_ROWS * (GVDP_MAX_WIDTH / 8))

/* Four uints, directly usable as one RGBA32UI GPU texel.
 * run: count[7:0], first source nibble[15:8], horizontal flip[16].
 * color: palette base[7:0], priority[8], visible destination mask[23:16].
 * Sample k = floor(output_native_x)-x; nibble = (first+k) XOR (flip?7:0).
 * A zero nibble is transparent; visible mask is mandatory for sprite spans.
 * Plane spans have mask255. Spans are sorted by x within every row/layer. */
typedef struct GVDPHdSpan {
    uint32_t x, source, run, color;
} GVDPHdSpan;

/* Twenty uints/five RGBA32UI texels. Native y is the row array index.
 * Each plane range covers [start,start+count) with ordered nonoverlapping
 * spans. Sprite visible masks are disjoint; scan backwards from the last
 * span with x<=pixel until x<pixel-7 (at most15 nonempty visible spans).
 * operator_words[ops_first+x/8]: shadow bits[7:0], highlight bits[15:8]. */
typedef struct GVDPHdRow {
    uint32_t width, start, count, backdrop;
    uint32_t bar, shadow_mode, diagnostic, diag_offset;
    uint32_t diag_mask, palette, a_first, a_count;
    uint32_t b_first, b_count, s_first, s_count;
    uint32_t ops_first, display, reserved0, reserved1;
} GVDPHdRow;

/* Entries at used_patterns/used_palettes are the only source slots this frame
 * references. Their numeric values and palette rows are owned by this frame.
 * A backend retains its own per-slot shadow and uploads only differing source
 * slots/ranges. Skipped frames, reset, rewind and backend re-creation cannot
 * make a reused slot impersonate old content: compare the owned value.
 * ARGB palettes are the exact table used by the native row sink, including
 * brightness, bars and supported host colors. */
typedef struct GVDPRasterFrame {
    uint64_t serial;
    uint32_t width, height, span_count, pattern_count, palette_count;
    uint32_t valid, row_count;
    GVDPHdRow rows[GVDP_HD_ROWS];
    GVDPHdSpan spans[GVDP_HD_SPANS];
    uint32_t operator_words[GVDP_HD_OP_WORDS];
    uint32_t pattern_values[GVDP_HD_PATTERNS];
    uint32_t used_patterns[GVDP_HD_PATTERNS];
    uint32_t palette_values[GVDP_HD_PALETTES][GVDP_OUTPUT_PALETTE_SIZE];
    uint32_t used_palettes[GVDP_HD_PALETTES];
} GVDPRasterFrame;

/* Producer enables capture before rendering a frame. Native status, pixels and
 * guest state always run normally. The native ARGB sink binds its exact row
 * palette after gvdp_render_scanline, before presentation. Unrepresentable
 * opaque custom drawing makes the whole frame ineligible, retaining native. */
void gvdp_hd_enable(int enabled);
void gvdp_hd_reset(void);
void gvdp_hd_bind_palette(unsigned line, const uint32_t *argb, unsigned count);
/* Explicit bounded leases: a published frame never mutates while acquired.
 * Release after CPU/GPU source upload has copied all referenced input. If all
 * slots are retained the producer falls back for that frame without blocking
 * guest simulation or overwriting an immutable lease. NULL means fallback. */
const GVDPRasterFrame *gvdp_hd_acquire(void);
void gvdp_hd_release(const GVDPRasterFrame *frame);
/* Portable exact native-density job renderer; does not read any live device,
 * palette or shared cache. The destination stride is in uint32_t pixels. */
int gvdp_hd_render_argb(const GVDPRasterFrame *, uint32_t *, unsigned stride);

/* Native-producer hooks. Layer0=A/window,1=B,2=normal sprites. */
void gvdp_hd_begin_row(const GVDP *, unsigned line, unsigned width,
                      unsigned start, unsigned count, unsigned backdrop,
                      unsigned bar, unsigned display, unsigned shadow_mode);
void gvdp_hd_set_diagnostic(unsigned enabled, unsigned offset, unsigned mask);
void gvdp_hd_emit(unsigned layer, unsigned x, unsigned count, uint32_t bits,
                  unsigned first, unsigned flip, unsigned palette_base,
                  unsigned high, unsigned visible_mask);
void gvdp_hd_operator(unsigned x, unsigned highlight);
void gvdp_hd_ineligible(void);
void gvdp_hd_end_row(void);
extern unsigned g_gvdp_hd_capture_active;
static inline int gvdp_hd_capturing(void) { return g_gvdp_hd_capture_active!=0; }

#ifdef __cplusplus
}
#endif
#endif
