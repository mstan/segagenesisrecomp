#ifndef GENESIS_OVERLAY_DRAW_H
#define GENESIS_OVERLAY_DRAW_H

#include <stdint.h>

/* ARGB8888 software primitives shared by the Genesis save and rewind panels.
 * Ported from the SNES runner's overlay drawing helpers. The explicit stride
 * keeps the bitmap font independent of SDL and of any game viewport size. */
void gen_ovl_fill_rect(uint32_t *dst, int stride, int height,
                       int x, int y, int width, int rect_height, uint32_t color);
void gen_ovl_stroke_rect(uint32_t *dst, int stride, int height,
                         int x, int y, int width, int rect_height, uint32_t color);
void gen_ovl_draw_char(uint32_t *dst, int stride, int height,
                       int x, int y, char c, uint32_t color, int scale);
void gen_ovl_draw_text(uint32_t *dst, int stride, int height,
                       int x, int y, const char *text, uint32_t color, int scale);

#endif
