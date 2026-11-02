/* Serialize directly into rbengine's unpublished, reusable capture allocation.
 * The ring alone publishes it after success; failed saves cannot alter history. */
#include "rb_state.h"
#include <stdlib.h>

int genesis_rb_capture(void *ctx, uint32_t tick, uint8_t **data,
                       size_t *capacity, size_t *size)
{
    (void)ctx;
    (void)tick;
    size_t need = genesis_rb_bound();
    *size = 0;
    if (!need) return 0;
    /* Keep small serializer-size fluctuations reusable without retaining a
     * large former state after its live execution image has shrunk. */
    if (*capacity < need || (*capacity > need && *capacity - need > 4096u)) {
        uint8_t *next = (uint8_t *)realloc(*data, need);
        if (!next) return 0;
        *data = next;
        *capacity = need;
    }
    *size = genesis_rb_save(*data, *capacity);
    return *size != 0;
}
