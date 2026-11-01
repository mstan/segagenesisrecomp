#ifndef SEGACD_BIOS_TABLES_H
#define SEGACD_BIOS_TABLES_H
#include <stdint.h>
#include "segacd.h"
#include "m68k_step.h"

/* Generate the complete BRMINIT protection-table operation. The qualified
 * firmware uses GF(64) and GF(256). Buffers have 1<<bits bytes and must not
 * overlap. Invalid/nonprimitive polynomial input leaves all outputs intact.
 * This pure kernel does not change guest CPU, memory ownership or timing;
 * the firmware-service adapter supplies that contract separately. */
int segacd_bios_protection_tables(unsigned bits, uint8_t polynomial,
    uint8_t *work, uint8_t *powers, uint8_t *logarithms);
/* Complete qualified firmware helper ABI, with all checks before mutation.
 * Returns0 for the caller's original guarded native operation. No instruction
 * decoder, firmware execution, device advance or pacing change is used here. */
int segacd_bios_protection_service(SegaCD *, M68kStepContext *);
#endif
