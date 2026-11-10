#include "load_hold.h"
#include "overlay_draw.h"
#include <string.h>

void genesis_load_hold_cancel(GenesisLoadHold *h)
{
    memset(h, 0, sizeof *h);
}

void genesis_load_hold_begin(GenesisLoadHold *h, int source, int slot,
                             uint64_t now_ms)
{
    if (h->source || (source != GENESIS_LOAD_HOLD_PAD &&
                      source != GENESIS_LOAD_HOLD_KEYBOARD) || slot < 1 || slot > 9)
        return;
    h->started_ms = now_ms;
    h->source = source;
    h->slot = slot;
    h->fired = 0;
}

unsigned genesis_load_hold_elapsed(const GenesisLoadHold *h, uint64_t now_ms)
{
    if (!h->source || now_ms < h->started_ms) return 0;
    uint64_t elapsed = now_ms - h->started_ms;
    return elapsed < GENESIS_LOAD_HOLD_MS ? (unsigned)elapsed : GENESIS_LOAD_HOLD_MS;
}

int genesis_load_hold_update(GenesisLoadHold *h, uint64_t now_ms,
                             int held, int allowed)
{
    if (!h->source) return 0;
    if (!held || !allowed || now_ms < h->started_ms) {
        genesis_load_hold_cancel(h);
        return 0;
    }
    if (!h->fired && genesis_load_hold_elapsed(h, now_ms) == GENESIS_LOAD_HOLD_MS) {
        h->fired = 1;
        return h->slot;
    }
    return 0;
}

int genesis_load_hold_visible(const GenesisLoadHold *h)
{
    return h->source && !h->fired;
}

void genesis_load_hold_draw(const GenesisLoadHold *h, uint64_t now_ms,
                            uint32_t *pixels)
{
    const int w = GENESIS_LOAD_HOLD_WIDTH, height = GENESIS_LOAD_HOLD_HEIGHT;
    memset(pixels, 0, w * height * sizeof *pixels);
    if (!genesis_load_hold_visible(h)) return;
    gen_ovl_fill_rect(pixels, w, height, 0, 0, w, height, 0xEC101A31u);
    gen_ovl_stroke_rect(pixels, w, height, 0, 0, w, height, 0xFFF6C66Bu);
    gen_ovl_draw_text(pixels, w, height, 12, 8, "Loading State...", 0xFFF2F1EAu, 1);
    gen_ovl_fill_rect(pixels, w, height, 12, 24, w - 24, 6, 0xFF23324Cu);
    int filled = (int)((w - 24) * genesis_load_hold_elapsed(h, now_ms) / GENESIS_LOAD_HOLD_MS);
    gen_ovl_fill_rect(pixels, w, height, 12, 24, filled, 6, 0xFFF6C66Bu);
}
