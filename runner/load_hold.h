#ifndef GENESIS_LOAD_HOLD_H
#define GENESIS_LOAD_HOLD_H

#include <stdint.h>

/* Host input policy: wall-clock time, independent of guest speed or refresh. */
enum { GENESIS_LOAD_HOLD_MS = 1500, GENESIS_LOAD_HOLD_PAD = 1,
       GENESIS_LOAD_HOLD_KEYBOARD = 2,
       GENESIS_LOAD_HOLD_WIDTH = 240, GENESIS_LOAD_HOLD_HEIGHT = 40 };
typedef struct GenesisLoadHold {
    uint64_t started_ms;
    int source, slot, fired;
} GenesisLoadHold;

void genesis_load_hold_cancel(GenesisLoadHold *hold);
void genesis_load_hold_begin(GenesisLoadHold *hold, int source, int slot,
                             uint64_t now_ms);
/* Returns the requested slot exactly once, after a continuous allowed hold.
 * Cancellation consumes this press: a fresh begin is required to try again. */
int genesis_load_hold_update(GenesisLoadHold *hold, uint64_t now_ms,
                             int held, int allowed);
int genesis_load_hold_visible(const GenesisLoadHold *hold);
unsigned genesis_load_hold_elapsed(const GenesisLoadHold *hold, uint64_t now_ms);
/* Transparent ARGB8888 notice; caller supplies WIDTH*HEIGHT pixels. */
void genesis_load_hold_draw(const GenesisLoadHold *hold, uint64_t now_ms,
                            uint32_t *pixels);
#endif
