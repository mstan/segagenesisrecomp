/* color_lut.h — present-time screen-color simulation (Genesis 9-bit → ARGB8888).
 *
 * PRESENT-TIME ONLY. This never touches the emulation, the framebuffer the VDP
 * produces, or the differential-verify path — diff / [FBHASH] / golden-image
 * comparisons stay defined on the raw ARGB8888 the runner builds in s_framebuf
 * (the ×36 3→8 expansion in main.c). The LUT is a 512-entry table applied to a
 * COPY of the frame at SDL-upload time, and it defaults to Raw (exact
 * passthrough of that same ×36 expansion), so default behavior and every
 * hashed/verified frame are byte-identical unless a screen model is opted in
 * via GENESIS_SCREEN={raw,crt,trinitron,composite,linear}.
 *
 * Genesis color is 9-bit (BGR, 3 bits each) carried in CRAM as 0x0EEE:
 *   bits  3:1 = Red (0-7), bits 7:5 = Green (0-7), bits 11:9 = Blue (0-7).
 * The display model is a CRT/composite NTSC monitor, NOT an LCD panel — so we
 * use a CRT-phosphor primaries + power-law model here, and we DO NOT fake
 * composite-encoding artifacts (dot crawl, rainbowing, NTSC notch-filter
 * blur): those are spatial/temporal effects of the composite signal chain, not
 * a per-pixel color transform, and inventing them would be guessing hardware
 * behavior. See SHADOW_ENHANCEMENTS.md "Composite-artifact nuance".
 *
 * The math is first-principles CIE colorimetry (xyY→XYZ, primaries→matrix,
 * Bradford adaptation, sRGB OETF). The colorimetry core is lifted verbatim
 * from the gbarecomp/snesrecomp color_lut; the Genesis 9-bit indexing, the CRT
 * primaries, and the ARGB8888 output path are this port's.
 *
 * ── Attribution ───────────────────────────────────────────────────
 * Colorimetry core ported from JRickey/gba-recomp
 * (crates/screen/src/{color,profile,lut}.rs) via the gbarecomp C++ port
 * (src/runtime/color_lut.*), © Jrickey, MIT OR Apache-2.0, used with
 * permission. C port + 9-bit Genesis index + CRT model + ARGB8888 present
 * path are ours. See THIRD-PARTY-LICENSES.md.
 */
#ifndef VIDEO_COLOR_LUT_H
#define VIDEO_COLOR_LUT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Which display model to simulate. */
typedef enum {
  SCREEN_RAW = 0,    /* ×36 3→8 expansion, untouched (default; passthrough) */
  SCREEN_CRT,        /* generic NTSC CRT phosphor + gamma */
  SCREEN_TRINITRON,  /* Sony Trinitron-ish primaries (common reference monitor) */
  SCREEN_COMPOSITE,  /* CRT model with a slightly warmer/softer gamma; NO fake
                      * composite spatial artifacts (see header note) */
  SCREEN_LINEAR,     /* linear-light decode, sRGB re-encode (clean reference) */
} ScreenKind;

/* Parse a config/env token; returns false if unrecognized. */
bool screen_kind_from_name(const char* name, ScreenKind* out);

/* A baked 9-bit-Genesis → ARGB8888 table. Build once per settings change;
 * apply per frame as one indexed lookup per pixel. 512 entries (2^9). */
typedef struct {
  uint32_t table[512];  /* table[bgr9 index] = 0xFFRRGGBB */
  bool passthrough;     /* true for SCREEN_RAW (bit-identical to md_colour_to_argb) */
} ColorLut;

/* Build the LUT for a given screen model. `darken` < 0 selects the per-model
 * default. Safe to call repeatedly. */
void color_lut_build(ColorLut* lut, ScreenKind screen, double darken);

static inline bool color_lut_is_passthrough(const ColorLut* lut) {
  return lut->passthrough;
}

/* Map a 9-bit Genesis CRAM value (0x0EEE-masked) to ARGB8888. */
static inline uint32_t color_lut_map_cram(const ColorLut* lut, uint16_t cram) {
  /* Recover the 9-bit index: R(0-7) | G<<3 | B<<6, from the 0x0EEE layout. */
  uint32_t idx = (uint32_t)(((cram >> 1) & 7u) |
                            (((cram >> 5) & 7u) << 3) |
                            (((cram >> 9) & 7u) << 6));
  return lut->table[idx & 0x1FFu];
}

/* Map an already-expanded ARGB8888 pixel (as produced by md_colour_to_argb,
 * i.e. each channel is one of the 8 levels {0,36,72,...,252}) back through the
 * LUT. Used for the present-time pass over s_framebuf where only ARGB is left.
 * Recovers the 3-bit channel by dividing the 8-bit value by 36. */
static inline uint32_t color_lut_map_argb(const ColorLut* lut, uint32_t argb) {
  uint32_t r = ((argb >> 16) & 0xFFu) / 36u;
  uint32_t g = ((argb >>  8) & 0xFFu) / 36u;
  uint32_t b = ( argb        & 0xFFu) / 36u;
  if (r > 7u) r = 7u;
  if (g > 7u) g = 7u;
  if (b > 7u) b = 7u;
  uint32_t idx = r | (g << 3) | (b << 6);
  return lut->table[idx & 0x1FFu];
}

/* Transform a width*height ARGB8888 frame through the LUT into `dst`
 * (present-time copy; never the verified s_framebuf). `stride_px` is the row
 * stride in pixels (MAX_SCREEN_WIDTH for s_framebuf). */
void color_lut_map_frame(const ColorLut* lut, const uint32_t* src,
                         uint32_t* dst, int width, int height, int stride_px);

#ifdef __cplusplus
}
#endif

#endif  /* VIDEO_COLOR_LUT_H */
